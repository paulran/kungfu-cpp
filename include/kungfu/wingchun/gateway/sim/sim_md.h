#pragma once

#include <kungfu/wingchun/broker/market_data.h>
#include <kungfu/longfist/types.h>
#include <set>
#include <vector>
#include <string>
#include <functional>

namespace kungfu::wingchun::gateway::sim {

class SimMarketData : public broker::MarketData {
public:
    using QuoteCallback = std::function<void(const longfist::types::Quote&)>;

    SimMarketData() = default;
    ~SimMarketData() override = default;

    void on_start() override;
    void on_exit() override;
    longfist::enums::BrokerState get_state() const override { return state_; }

    bool subscribe(const std::string& instrument_id,
                  const std::string& exchange_id,
                  longfist::enums::InstrumentType type) override;
    bool unsubscribe(const std::string& instrument_id,
                    const std::string& exchange_id) override;
    bool subscribe_all(const std::string& exchange_id) override;

    bool is_subscribed(const std::string& instrument_id, const std::string& exchange_id) const;

    void feed_quote(const longfist::types::Quote& quote);
    void set_data(std::vector<longfist::types::Quote> quotes);
    bool replay_next();
    bool replay_done() const;

    void set_quote_callback(QuoteCallback cb) { quote_cb_ = std::move(cb); }

private:
    std::set<std::string> subscribed_;
    std::vector<longfist::types::Quote> replay_data_;
    size_t replay_index_ = 0;
    longfist::enums::BrokerState state_ = longfist::enums::BrokerState::Idle;
    QuoteCallback quote_cb_;
};

} // namespace kungfu::wingchun::gateway::sim
