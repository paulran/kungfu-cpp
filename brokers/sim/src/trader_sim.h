#pragma once

#include <kungfu/longfist/enums.h>
#include <kungfu/longfist/types.h>
#include <kungfu/wingchun/broker/trader.h>
#include "match_mode.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace kungfu::wingchun::sim {

class TraderSim : public broker::Trader {
public:
  explicit TraderSim(broker::TraderVendor &vendor);

  ~TraderSim() override = default;

  [[nodiscard]] longfist::enums::AccountType get_account_type() const override;

  void on_start() override;

  bool insert_order(const event_ptr &event) override;

  bool insert_batch_orders(const event_ptr &event) override;

  bool cancel_order(const event_ptr &event) override;

  bool req_position() override;

  bool req_account() override;

  bool req_order_trade() override;

private:
  MatchMode match_mode_ = MatchMode::Fill;
  std::unordered_map<uint64_t, longfist::types::Order> orders_;

  bool insert_order_(const event_ptr &event, const longfist::types::OrderInput &order_input);

  int64_t get_min_volume(longfist::enums::InstrumentType instrument_type) const;
};

} // namespace kungfu::wingchun::sim
