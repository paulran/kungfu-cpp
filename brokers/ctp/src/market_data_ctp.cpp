#include "market_data_ctp.h"

#include <algorithm>
#include <filesystem>

#include <kungfu/wingchun/common.h>
#include <kungfu/yijinjing/time.h>
#include <spdlog/spdlog.h>

#include "ctp_md_spi.h"

namespace kungfu::wingchun::ctp {

using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;

namespace {
/// CTP front may leave ExchangeID empty on some versions; infer from the
/// instrument code for futures (e.g. rb2410 -> SHFE).
std::string resolve_exchange_id(const CThostFtdcDepthMarketDataField &depth) {
  std::string exchange_id = depth.ExchangeID;
  if (!exchange_id.empty()) {
    return exchange_id;
  }
  std::string instrument_id = depth.InstrumentID;
  std::string inferred = get_exchange_id_from_future_instrument_id(instrument_id);
  return inferred.empty() ? "FUTURE" : inferred;
}
} // namespace

MarketDataCtp::MarketDataCtp(broker::MarketDataVendor &vendor) : MarketData(vendor) {
  SPDLOG_INFO("CTP MD constructor");
}

MarketDataCtp::~MarketDataCtp() {
  SPDLOG_INFO("CTP MD destructor");
  if (api_ != nullptr) {
    api_->Release();
    api_ = nullptr;
  }
}

void MarketDataCtp::on_start() {
  config_ = parse_config(get_config());
  if (config_.front_uri.empty()) {
    SPDLOG_ERROR("CTP MD front_uri is empty, cannot connect");
    return;
  }

  std::filesystem::path flow_path = std::filesystem::path(get_runtime_folder()) / "ctp_md_flow";
  std::filesystem::create_directories(flow_path);

  api_ = CThostFtdcMdApi::CreateFtdcMdApi(flow_path.string().c_str());
  spi_ = new CtpMdSpi(api_, this, config_);
  api_->RegisterSpi(spi_);
  api_->RegisterFront(config_.front_uri.data());
  api_->Init();

  // Drain CTP callbacks enqueued by the SPI onto the event loop thread.
  add_time_interval(1000000, [&](const event_ptr &) { drain_tasks(); });

  update_broker_state(BrokerState::Idle);
  SPDLOG_INFO("CTP MD starting, front {}", config_.front_uri);
}

void MarketDataCtp::on_exit() {
  if (api_ != nullptr) {
    api_->Release();
    api_ = nullptr;
  }
}

void MarketDataCtp::enqueue(Task task) {
  {
    std::lock_guard<std::mutex> guard(tasks_mutex_);
    tasks_.emplace_back(std::move(task));
  }
}

void MarketDataCtp::drain_tasks() {
  std::deque<Task> pending;
  {
    std::lock_guard<std::mutex> guard(tasks_mutex_);
    pending.swap(tasks_);
  }
  while (!pending.empty()) {
    auto task = std::move(pending.front());
    pending.pop_front();
    task();
  }
}

bool MarketDataCtp::do_subscribe(const std::vector<std::string> &instrument_ids) {
  std::vector<char *> instrument_id_pointers;
  instrument_id_pointers.reserve(instrument_ids.size());
  for (const auto &instrument_id : instrument_ids) {
    instrument_id_pointers.push_back(const_cast<char *>(instrument_id.c_str()));
  }
  int ret = api_->SubscribeMarketData(instrument_id_pointers.data(), static_cast<int>(instrument_id_pointers.size()));
  if (ret != 0) {
    SPDLOG_ERROR("CTP MD SubscribeMarketData failed, error {}", ret);
    return false;
  }
  return true;
}

bool MarketDataCtp::subscribe(const std::vector<InstrumentKey> &instrument_keys) {
  std::vector<std::string> instrument_ids;
  instrument_ids.reserve(instrument_keys.size());
  for (const auto &key : instrument_keys) {
    std::string instrument_id = key.instrument_id.to_string();
    if (instrument_id.empty()) {
      // Ignore invalid keys (e.g. legacy empty frames replayed from journal).
      SPDLOG_WARN("CTP MD ignoring invalid subscribe key {}", key.key);
      continue;
    }
    if (subscribed_.insert(instrument_id).second) {
      instrument_ids.push_back(instrument_id);
    }
  }
  if (instrument_ids.empty()) {
    return true;
  }
  if (!logged_in_) {
    SPDLOG_INFO("CTP MD not logged in, {} instruments queued", instrument_ids.size());
    pending_subscribes_.insert(pending_subscribes_.end(), instrument_ids.begin(), instrument_ids.end());
    return true;
  }
  SPDLOG_INFO("CTP MD subscribing {} instruments", instrument_ids.size());
  return do_subscribe(instrument_ids);
}

bool MarketDataCtp::unsubscribe(const std::vector<InstrumentKey> &instrument_keys) {
  std::vector<char *> instrument_id_pointers;
  for (const auto &key : instrument_keys) {
    std::string instrument_id = key.instrument_id.to_string();
    if (instrument_id.empty()) {
      SPDLOG_WARN("CTP MD ignoring invalid unsubscribe key {}", key.key);
      continue;
    }
    subscribed_.erase(instrument_id);
    // Drop queued subscriptions that have not been flushed to CTP yet,
    // otherwise a login would still subscribe the canceled instrument.
    pending_subscribes_.erase(std::remove(pending_subscribes_.begin(), pending_subscribes_.end(), instrument_id),
                              pending_subscribes_.end());
    if (logged_in_) {
      instrument_id_pointers.push_back(const_cast<char *>(instrument_id.c_str()));
    }
  }
  if (instrument_id_pointers.empty()) {
    return true;
  }
  SPDLOG_INFO("CTP MD unsubscribing {} instruments", instrument_id_pointers.size());
  int ret = api_->UnSubscribeMarketData(instrument_id_pointers.data(), static_cast<int>(instrument_id_pointers.size()));
  if (ret != 0) {
    SPDLOG_ERROR("CTP MD UnSubscribeMarketData failed, error {}", ret);
    return false;
  }
  return true;
}

void MarketDataCtp::on_login_success(const CThostFtdcRspUserLoginField &login) {
  enqueue([this, login]() {
    logged_in_ = true;
    SPDLOG_INFO("CTP MD logged in, trading day {}", login.TradingDay);
    update_broker_state(BrokerState::LoggedIn);
    // CTP MD can push quotes as soon as it is logged in; report Ready so the
    // api's AutoClient records this MD as ready and renew() replays active
    // subscriptions after an MD restart. Without Ready, api never triggers
    // renew and a restarted MD never receives any InstrumentKey again.
    update_broker_state(BrokerState::Ready);
    if (!pending_subscribes_.empty()) {
      SPDLOG_INFO("CTP MD flushing {} queued subscriptions", pending_subscribes_.size());
      do_subscribe(pending_subscribes_);
      pending_subscribes_.clear();
    } else if (!subscribed_.empty()) {
      // re-login after reconnect: resubscribe everything
      do_subscribe({subscribed_.begin(), subscribed_.end()});
    }
  });
}

void MarketDataCtp::on_login_failed(const CThostFtdcRspInfoField &error) {
  enqueue([this, error]() {
    logged_in_ = false;
    update_broker_state(BrokerState::LoginFailed);
    SPDLOG_ERROR("CTP MD login failed, error {}: {}", error.ErrorID, error.ErrorMsg);
  });
}

void MarketDataCtp::on_front_disconnected(int reason) {
  enqueue([this, reason]() {
    logged_in_ = false;
    update_broker_state(BrokerState::DisConnected);
    SPDLOG_WARN("CTP MD front disconnected ({}), waiting for auto reconnect", reason);
  });
}

void MarketDataCtp::on_depth_market_data(const CThostFtdcDepthMarketDataField &depth) {
  Quote quote = {};

  std::string instrument_id = depth.InstrumentID;
  std::string exchange_id = resolve_exchange_id(depth);

  strncpy(quote.trading_day, depth.TradingDay, DATE_LEN);
  strncpy(quote.instrument_id, instrument_id.c_str(), INSTRUMENT_ID_LEN);
  strncpy(quote.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
  quote.instrument_type = get_instrument_type(exchange_id, instrument_id);
  quote.data_time = nano_from_ctp_time(depth.ActionDay, depth.UpdateTime, depth.UpdateMillisec);

  quote.pre_close_price = ctp_price(depth.PreClosePrice);
  quote.pre_settlement_price = ctp_price(depth.PreSettlementPrice);

  quote.last_price = ctp_price(depth.LastPrice);
  quote.volume = depth.Volume;
  quote.turnover = depth.Turnover;

  quote.pre_open_interest = depth.PreOpenInterest;
  quote.open_interest = depth.OpenInterest;

  quote.open_price = ctp_price(depth.OpenPrice);
  quote.high_price = ctp_price(depth.HighestPrice);
  quote.low_price = ctp_price(depth.LowestPrice);

  quote.upper_limit_price = ctp_price(depth.UpperLimitPrice);
  quote.lower_limit_price = ctp_price(depth.LowerLimitPrice);

  quote.close_price = ctp_price(depth.ClosePrice);
  quote.settlement_price = ctp_price(depth.SettlementPrice);

  const double *bid_prices[] = {&depth.BidPrice1, &depth.BidPrice2, &depth.BidPrice3, &depth.BidPrice4,
                                &depth.BidPrice5};
  const int *bid_volumes[] = {&depth.BidVolume1, &depth.BidVolume2, &depth.BidVolume3, &depth.BidVolume4,
                              &depth.BidVolume5};
  const double *ask_prices[] = {&depth.AskPrice1, &depth.AskPrice2, &depth.AskPrice3, &depth.AskPrice4,
                                &depth.AskPrice5};
  const int *ask_volumes[] = {&depth.AskVolume1, &depth.AskVolume2, &depth.AskVolume3, &depth.AskVolume4,
                              &depth.AskVolume5};

  for (int i = 0; i < 5; ++i) {
    quote.bid_price[i] = ctp_price(*bid_prices[i]);
    quote.bid_volume[i] = *bid_volumes[i];
    quote.ask_price[i] = ctp_price(*ask_prices[i]);
    quote.ask_volume[i] = *ask_volumes[i];
  }
  for (int i = 5; i < 10; ++i) {
    quote.bid_price[i] = 0.0;
    quote.bid_volume[i] = 0;
    quote.ask_price[i] = 0.0;
    quote.ask_volume[i] = 0;
  }

  enqueue([this, quote]() {
    if (auto writer = get_writer(yijinjing::data::location::PUBLIC)) {
      writer->write(yijinjing::time::now_in_nano(), quote);
    }
  });
}

} // namespace kungfu::wingchun::ctp
