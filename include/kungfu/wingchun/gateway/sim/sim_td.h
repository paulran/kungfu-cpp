#pragma once

#include <kungfu/wingchun/broker/trader.h>
#include <kungfu/wingchun/gateway/sim/matching_engine.h>
#include <kungfu/longfist/types.h>
#include <vector>
#include <functional>

namespace kungfu::wingchun::gateway::sim {

class SimTrader : public broker::Trader {
public:
    using OrderCallback = std::function<void(const longfist::types::Order&)>;
    using TradeCallback = std::function<void(const longfist::types::Trade&)>;
    using WriteCallback = std::function<void(const longfist::types::Order&, const longfist::types::Trade*)>;

    SimTrader() = default;
    ~SimTrader() override = default;

    void on_start() override;
    void on_exit() override;
    longfist::enums::BrokerState get_state() const override { return state_; }

    bool insert_order(const longfist::types::OrderInput& input) override;
    bool cancel_order(uint64_t order_id) override;
    bool req_position() override;
    bool req_account() override;

    void on_quote(const longfist::types::Quote& quote, int64_t now);

    void set_initial_asset(const longfist::types::Asset& asset) { asset_ = asset; }
    void set_order_callback(OrderCallback cb) { order_cb_ = std::move(cb); }
    void set_trade_callback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    void set_write_callback(WriteCallback cb) { write_cb_ = std::move(cb); }

    const MatchingEngine& engine() const { return engine_; }

private:
    MatchingEngine engine_;
    longfist::enums::BrokerState state_ = longfist::enums::BrokerState::Idle;
    longfist::types::Asset asset_{};
    std::vector<longfist::types::Position> positions_;
    OrderCallback order_cb_;
    TradeCallback trade_cb_;
    WriteCallback write_cb_;
    int64_t current_time_ = 0;
};

} // namespace kungfu::wingchun::gateway::sim
