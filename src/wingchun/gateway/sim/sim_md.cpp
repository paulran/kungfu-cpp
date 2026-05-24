#include <kungfu/wingchun/gateway/sim/sim_md.h>

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
    subscribed_.insert(instrument_id + "@" + exchange_id);
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

} // namespace kungfu::wingchun::gateway::sim
