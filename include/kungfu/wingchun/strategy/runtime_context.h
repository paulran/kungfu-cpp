#pragma once

#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/broker/client.h>
#include <kungfu/wingchun/book/book.h>
#include <kungfu/yijinjing/io/locator.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace kungfu::wingchun::strategy {

class Runner;

class RuntimeContext : public Context {
public:
    RuntimeContext(Runner& runner, yijinjing::io::Locator& locator);

    int64_t now() const override;
    std::string trading_day() const override;

    void subscribe(const std::string& exchange_id,
                  const std::string& instrument_id,
                  longfist::enums::InstrumentType type) override;

    uint64_t insert_order(const std::string& instrument_id,
                         const std::string& exchange_id,
                         double price, int64_t volume,
                         longfist::enums::Side side, longfist::enums::Offset offset,
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

    // Event dispatch (called by Runner)
    void on_quote(const longfist::types::Quote& quote);
    void on_order(const longfist::types::Order& order);
    void on_trade(const longfist::types::Trade& trade);

    BookKeeper& book_keeper() { return book_keeper_; }
    broker::BrokerClient& broker_client() { return broker_client_; }

private:
    Runner& runner_;
    yijinjing::io::Locator& locator_;
    broker::BrokerClient broker_client_;
    BookKeeper book_keeper_;
    uint32_t book_uid_ = 0;

    std::string trading_day_;
    std::unordered_map<std::string, longfist::types::Quote> quote_cache_;

    struct TimerEntry { int64_t trigger_time; int32_t timer_id; int64_t interval; };
    std::vector<TimerEntry> timers_;
    int32_t next_timer_id_ = 1;
    int64_t current_nano_ = 0;
};

} // namespace kungfu::wingchun::strategy
