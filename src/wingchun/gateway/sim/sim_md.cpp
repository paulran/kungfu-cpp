#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <cstring>
#include <cmath>

namespace kungfu::wingchun::gateway::sim {

void SimMarketData::on_start() {
    state_ = longfist::enums::BrokerState::Ready;
}

void SimMarketData::on_exit() {
    state_ = longfist::enums::BrokerState::Idle;
}

bool SimMarketData::subscribe(const std::string& instrument_id,
                             const std::string& exchange_id,
                             longfist::enums::InstrumentType) {
    auto key = instrument_id + "@" + exchange_id;
    subscribed_.insert(key);
    if (last_prices_.find(key) == last_prices_.end()) {
        last_prices_[key] = 10.00;
    }
    return true;
}

bool SimMarketData::unsubscribe(const std::string& instrument_id,
                               const std::string& exchange_id) {
    subscribed_.erase(instrument_id + "@" + exchange_id);
    return true;
}

bool SimMarketData::subscribe_all(const std::string&) {
    return true;
}

bool SimMarketData::is_subscribed(const std::string& instrument_id,
                                  const std::string& exchange_id) const {
    return subscribed_.count(instrument_id + "@" + exchange_id) > 0;
}

void SimMarketData::feed_quote(const longfist::types::Quote& quote) {
    auto key = std::string(quote.instrument_id.data) + "@" + std::string(quote.exchange_id.data);
    if (subscribed_.empty() || subscribed_.count(key)) {
        if (quote_cb_) {
            quote_cb_(quote);
        }
    }
}

void SimMarketData::set_data(std::vector<longfist::types::Quote> quotes) {
    replay_data_ = std::move(quotes);
    replay_index_ = 0;
}

bool SimMarketData::replay_next() {
    if (replay_index_ >= replay_data_.size()) return false;
    feed_quote(replay_data_[replay_index_]);
    replay_index_++;
    return true;
}

bool SimMarketData::replay_done() const {
    return replay_index_ >= replay_data_.size();
}

bool SimMarketData::should_generate(int64_t now_ns) const {
    return (now_ns - last_generate_time_) >= GENERATE_INTERVAL_NS;
}

void SimMarketData::generate_quotes(int64_t now_ns) {
    last_generate_time_ = now_ns;
    std::uniform_real_distribution<double> dist(-0.03, 0.03);

    for (const auto& key : subscribed_) {
        auto at_pos = key.find('@');
        if (at_pos == std::string::npos) continue;

        std::string instrument_id = key.substr(0, at_pos);
        std::string exchange_id = key.substr(at_pos + 1);

        double& last_price = last_prices_[key];
        last_price += dist(rng_);
        if (last_price < 1.0) last_price = 1.0;

        longfist::types::Quote q{};
        std::strncpy(q.instrument_id.data, instrument_id.c_str(), sizeof(q.instrument_id.data) - 1);
        std::strncpy(q.exchange_id.data, exchange_id.c_str(), sizeof(q.exchange_id.data) - 1);
        q.data_time = now_ns;
        q.last_price = last_price;
        q.bid_price_0 = last_price - 0.01;
        q.ask_price_0 = last_price + 0.01;
        q.bid_price_1 = last_price - 0.02;
        q.ask_price_1 = last_price + 0.02;
        q.bid_volume_0 = 1000;
        q.ask_volume_0 = 800;
        q.bid_volume_1 = 2000;
        q.ask_volume_1 = 1500;
        q.volume = 50000;
        q.turnover = q.volume * last_price;

        if (write_cb_) write_cb_(q);
        if (quote_cb_) quote_cb_(q);
    }
}

} // namespace kungfu::wingchun::gateway::sim
