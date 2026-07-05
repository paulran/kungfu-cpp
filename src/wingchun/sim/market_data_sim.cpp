#include <kungfu/wingchun/sim/market_data_sim.h>
#include <kungfu/wingchun/common.h>
#include <kungfu/yijinjing/time.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::sim {

using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;

MarketDataSim::MarketDataSim(broker::MarketDataVendor &vendor) : MarketData(vendor) {
  config_.base = 200.0;
  config_.bound = 1000;
  config_.samples = 1000;
  config_.variation = 4;
  config_.randseed = 6;
}

void MarketDataSim::on_start() {
  add_time_interval(500 * 1000 * 1000, [this](const event_ptr &) { update_orderbooks(); });
  update_broker_state(BrokerState::Ready);
}

void MarketDataSim::init_order_book(const std::string &instrument_id, const std::string &exchange_id) {
  std::string security = instrument_id + "." + exchange_id;
  uint32_t instrument_key = hash_instrument(exchange_id.c_str(), instrument_id.c_str());

  if (orderbooks_.find(instrument_key) != orderbooks_.end()) {
    return;
  }

  auto book = std::make_unique<OrderBook>(security);
  for (int i = 0; i < OrderBook::MAX_DEPTH; ++i) {
    double delta = (i + 1) * 1.0;
    book->order({security, Side::Buy, config_.base - delta, 1});
    book->order({security, Side::Sell, config_.base + delta, 1});
  }

  orderbooks_[instrument_key] = std::move(book);
}

Quote MarketDataSim::quote_from_orderbook(const OrderBook &book) const {
  Quote quote{};

  std::string security = book.get_security();
  size_t dot_pos = security.find('.');
  std::string instrument_id = security.substr(0, dot_pos);
  std::string exchange_id = security.substr(dot_pos + 1);

  strncpy(quote.instrument_id, instrument_id.c_str(), INSTRUMENT_ID_LEN);
  strncpy(quote.exchange_id, exchange_id.c_str(), EXCHANGE_ID_LEN);
  quote.instrument_type = get_instrument_type(exchange_id, instrument_id);
  quote.data_time = yijinjing::time::now_in_nano();

  int bid_depth = std::min(book.depth_bids(), 10);
  int ask_depth = std::min(book.depth_offers(), 10);

  for (int i = 0; i < bid_depth; ++i) {
    quote.bid_price[i] = book.bid_price(i);
    quote.bid_volume[i] = book.bid_qty(i);
  }
  for (int i = bid_depth; i < 10; ++i) {
    quote.bid_price[i] = 0.0;
    quote.bid_volume[i] = 0;
  }

  for (int i = 0; i < ask_depth; ++i) {
    quote.ask_price[i] = book.offer_price(i);
    quote.ask_volume[i] = book.offer_qty(i);
  }
  for (int i = ask_depth; i < 10; ++i) {
    quote.ask_price[i] = 0.0;
    quote.ask_volume[i] = 0;
  }

  if (bid_depth > 0 && ask_depth > 0) {
    quote.last_price = std::round((quote.ask_price[0] + quote.bid_price[0]) / 2.0 * 100) / 100;
  }

  return quote;
}

void MarketDataSim::update_orderbooks() {
  auto writer = get_writer(0);

  for (auto &[key, book] : orderbooks_) {
    auto orders_list = book->gen_orders(config_);
    for (auto &[orders, mid] : orders_list) {
      for (const auto &order : orders) {
        std::string instrument_id = order.secid.substr(0, order.secid.find('.'));
        std::string exchange_id = order.secid.substr(order.secid.find('.') + 1);
        InstrumentType instrument_type = get_instrument_type(exchange_id, instrument_id);

        int64_t qty = order.qty;
        if (instrument_type != InstrumentType::Future) {
          qty *= 100;
        }

        book->order({order.secid, order.side, order.price, qty});
      }
    }

    Quote quote = quote_from_orderbook(*book);
    writer->write(yijinjing::time::now_in_nano(), quote);
  }
}

bool MarketDataSim::subscribe(const std::vector<InstrumentKey> &instrument_keys) {
  SPDLOG_INFO("subscribe {} instruments", instrument_keys.size());

  for (const auto &key : instrument_keys) {
    std::string instrument_id(key.instrument_id.to_string());
    std::string exchange_id(key.exchange_id.to_string());

    uint32_t instrument_key = hash_instrument(exchange_id.c_str(), instrument_id.c_str());
    if (orderbooks_.find(instrument_key) == orderbooks_.end()) {
      init_order_book(instrument_id, exchange_id);
    }
  }

  return true;
}

bool MarketDataSim::unsubscribe(const std::vector<InstrumentKey> &instrument_keys) {
  return false;
}

} // namespace kungfu::wingchun::sim