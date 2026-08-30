#include "trader_sim.h"
#include <kungfu/wingchun/common.h>
#include <kungfu/yijinjing/time.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::sim {

using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;

TraderSim::TraderSim(broker::TraderVendor &vendor) : Trader(vendor) {}

AccountType TraderSim::get_account_type() const { return AccountType::Stock; }

void TraderSim::on_start() {
  try {
    nlohmann::json config = nlohmann::json::parse(get_config());
    if (config.contains("match_mode")) {
      match_mode_ = match_mode_from_string(config["match_mode"].get<std::string>());
    }
  } catch (const std::exception &e) {
    SPDLOG_WARN("Failed to parse config, using default match_mode: {}", e.what());
  }

  enable_self_detect();
  update_broker_state(BrokerState::Ready);
}

int64_t TraderSim::get_min_volume(InstrumentType instrument_type) const {
  return (instrument_type == InstrumentType::Stock) ? 100 : 1;
}

bool TraderSim::insert_order(const event_ptr &event) {
  const OrderInput &order_input = event->data<OrderInput>();
  return insert_order_(event, order_input);
}

bool TraderSim::insert_batch_orders(const event_ptr &event) {
  auto &order_inputs = get_order_inputs();
  auto it = order_inputs.find(event->source());
  if (it == order_inputs.end()) {
    return true;
  }

  for (const auto &order_input : it->second) {
    insert_order_(event, order_input);
  }

  clear_order_inputs(event->source());
  return true;
}

bool TraderSim::insert_order_(const event_ptr &event, const OrderInput &order_input) {
  int64_t volume_traded = 0;
  auto writer = get_writer(event->source());

  Order order{};
  order_from_input(order_input, order);
  uint64_t order_id_copy = order.order_id;
  order.external_order_id = fmt::format("{}", order_id_copy).c_str();
  order.insert_time = event->gen_time();
  order.update_time = event->gen_time();

  std::string trading_day_str = yijinjing::time::strfnow("%Y%m%d");
  strncpy(order.trading_day, trading_day_str.c_str(), DATE_LEN);

  InstrumentType instrument_type =
      get_instrument_type(order_input.exchange_id.to_string(), order_input.instrument_id.to_string());
  order.instrument_type = instrument_type;

  if (instrument_type == InstrumentType::Repo && order.side == Side::Buy) {
    order.status = OrderStatus::Error;
    strncpy(order.error_msg, "repo can not buy", ERROR_MSG_LEN);
    writer->write(event->gen_time(), order);
    return false;
  }

  int64_t min_vol = get_min_volume(instrument_type);
  if (order_input.volume < min_vol) {
    order.status = OrderStatus::Error;
    strncpy(order.error_msg, "volume less than minimum", ERROR_MSG_LEN);
    writer->write(event->gen_time(), order);
    return false;
  }

  switch (match_mode_) {
  case MatchMode::Reject:
    order.status = OrderStatus::Error;
    strncpy(order.error_msg, "rejected by sim", ERROR_MSG_LEN);
    break;
  case MatchMode::Pend:
    order.status = OrderStatus::Pending;
    break;
  case MatchMode::Cancel:
    order.status = OrderStatus::Cancelled;
    break;
  case MatchMode::PartialFillAndCancel:
    volume_traded = min_vol;
    order.status = (volume_traded == order.volume) ? OrderStatus::Filled : OrderStatus::PartialFilledNotActive;
    break;
  case MatchMode::PartialFill:
    volume_traded = min_vol;
    order.status = (volume_traded == order.volume) ? OrderStatus::Filled : OrderStatus::PartialFilledActive;
    break;
  case MatchMode::Fill:
    volume_traded = order_input.volume;
    order.status = OrderStatus::Filled;
    break;
  case MatchMode::Multiple:
    volume_traded = order_input.volume;
    order.status = OrderStatus::Filled;
    break;
  default:
    throw std::runtime_error(fmt::format("invalid match mode {}", static_cast<int>(match_mode_)));
  }

  order.volume_left = order.volume - volume_traded;

  if (order_input.block_id != 0) {
    auto it = block_messages_.find(order_input.block_id);
    if (it == block_messages_.end()) {
      SPDLOG_ERROR("invalid block_id: {}", order_input.block_id);
      order.status = OrderStatus::Error;
      strncpy(order.error_msg, "No Block Message", ERROR_MSG_LEN);
      writer->write(event->gen_time(), order);
      return false;
    }
  }

  writer->write(event->gen_time(), order);
  orders_[order.order_id] = order;

  if (volume_traded > 0 && match_mode_ != MatchMode::Multiple) {
    Trade trade{};
    trade.trade_id = writer->current_frame_uid();
    strcpy(trade.external_order_id, order.external_order_id);
    uint64_t trade_id_copy = trade.trade_id;
    trade.external_trade_id = fmt::format("{}", trade_id_copy).c_str();
    trade.order_id = order.order_id;
    trade.volume = volume_traded;
    trade.price = order.limit_price;
    trade.side = order.side;
    trade.offset = order.offset;
    strcpy(trade.instrument_id, order.instrument_id);
    strcpy(trade.exchange_id, order.exchange_id);
    trade.instrument_type = order.instrument_type;
    trade.trade_time = yijinjing::time::now_in_nano();
    strncpy(trade.trading_day, trading_day_str.c_str(), DATE_LEN);

    writer->write(event->gen_time(), trade);
  } else if (volume_traded > 0 && match_mode_ == MatchMode::Multiple) {
    int64_t remaining = volume_traded;
    while (remaining > 0) {
      Trade trade{};
      trade.trade_id = writer->current_frame_uid();
      strcpy(trade.external_order_id, order.external_order_id);
      uint64_t trade_id_copy = trade.trade_id;
      trade.external_trade_id = fmt::format("{}", trade_id_copy).c_str();
      trade.order_id = order.order_id;
      trade.volume = std::min(min_vol, remaining);
      trade.price = order.limit_price;
      trade.side = order.side;
      trade.offset = order.offset;
      strcpy(trade.instrument_id, order.instrument_id);
      strcpy(trade.exchange_id, order.exchange_id);
      trade.instrument_type = order.instrument_type;
      trade.trade_time = yijinjing::time::now_in_nano();
      strncpy(trade.trading_day, trading_day_str.c_str(), DATE_LEN);

      writer->write(event->gen_time(), trade);
      remaining -= trade.volume;
    }
  }

  return true;
}

bool TraderSim::cancel_order(const event_ptr &event) {
  auto writer = get_writer(event->source());
  const OrderAction &order_action = event->data<OrderAction>();

  auto it = orders_.find(order_action.order_id);
  if (it == orders_.end()) {
    return true;
  }

  Order order = it->second;
  orders_.erase(it);

  if (order.volume_left == 0) {
    return true;
  }

  order.update_time = yijinjing::time::now_in_nano();
  if (order.volume - order.volume_left == 0) {
    order.status = OrderStatus::Cancelled;
  } else {
    order.status = OrderStatus::PartialFilledNotActive;
  }

  writer->write(event->gen_time(), order);
  return true;
}

bool TraderSim::req_position() { return false; }

bool TraderSim::req_account() { return false; }

bool TraderSim::req_order_trade() { return false; }

} // namespace kungfu::wingchun::sim
