#pragma once

#include <kungfu/longfist/types.h>
#include <kungfu/wingchun/broker/marketdata.h>
#include <kungfu/wingchun/sim/order_book.h>
#include <memory>
#include <unordered_map>

namespace kungfu::wingchun::sim {

class MarketDataSim : public broker::MarketData {
public:
  explicit MarketDataSim(broker::MarketDataVendor &vendor);

  ~MarketDataSim() override = default;

  void on_start() override;

  bool subscribe(const std::vector<longfist::types::InstrumentKey> &instrument_keys) override;

  bool unsubscribe(const std::vector<longfist::types::InstrumentKey> &instrument_keys) override;

private:
  MakerConfig config_;
  std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> orderbooks_;

  void update_orderbooks();

  longfist::types::Quote quote_from_orderbook(const OrderBook &book) const;

  void init_order_book(const std::string &instrument_id, const std::string &exchange_id);
};

} // namespace kungfu::wingchun::sim