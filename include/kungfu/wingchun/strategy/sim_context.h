#pragma once

#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <kungfu/wingchun/gateway/sim/sim_td.h>
#include <kungfu/wingchun/book/book.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <functional>
#include <cstdint>

namespace kungfu::wingchun::strategy {

class SimContext : public Context {
public:
    SimContext();

    int64_t now() const override;
    std::string trading_day() const override;

    void subscribe(const std::string& exchange_id,
                  const std::string& instrument_id,
                  longfist::enums::InstrumentType type) override;

    uint64_t insert_order(const std::string& instrument_id,
                         const std::string& exchange_id,
                         double price, int64_t volume,
                         longfist::enums::Side side,
                         longfist::enums::Offset offset,
                         longfist::enums::PriceType price_type) override;
    void cancel_order(uint64_t order_id) override;

    void add_account(const std::string& group, const std::string& name) override;
    void add_md(const std::string& group, const std::string& name) override;

    int32_t add_timer(int64_t nano_after) override;
    int32_t add_time_interval(int64_t duration_ns) override;

    const Book& get_book() const override;
    std::optional<longfist::types::Position> get_position(
        const std::string& instrument_id, const std::string& exchange_id,
        longfist::enums::Direction direction) const override;
    const longfist::types::Asset& get_asset() const override;

    std::optional<longfist::types::Quote> get_last_quote(
        const std::string& instrument_id, const std::string& exchange_id) const override;

    gateway::sim::SimMarketData& sim_md() { return sim_md_; }
    gateway::sim::SimTrader& sim_td() { return sim_td_; }
    BookKeeper& book_keeper() { return book_keeper_; }

    void set_strategy(Strategy* s) { strategy_ = s; }
    void set_trading_day(const std::string& td) { trading_day_ = td; }
    void set_initial_asset(double available, double margin = 0);

    void feed_quote(const longfist::types::Quote& quote);
    void run_replay();

private:
    Strategy* strategy_ = nullptr;
    gateway::sim::SimMarketData sim_md_;
    gateway::sim::SimTrader sim_td_;
    BookKeeper book_keeper_;
    uint32_t book_uid_ = 1;

    int64_t current_nano_ = 0;
    std::string trading_day_;
    std::unordered_map<std::string, longfist::types::Quote> quote_cache_;
    std::atomic<uint64_t> next_order_id_{1};

    struct TimerEntry { int64_t trigger_time; int32_t timer_id; int64_t interval; };
    std::vector<TimerEntry> timers_;
    int32_t next_timer_id_ = 1;

    void on_order(const longfist::types::Order& order);
    void on_trade(const longfist::types::Trade& trade);
    void check_timers();
};

} // namespace kungfu::wingchun::strategy
