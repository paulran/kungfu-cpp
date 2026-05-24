#include <kungfu/wingchun/broker/client.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

BrokerClient::BrokerClient(yijinjing::practice::apprentice& host)
    : host_(host) {}

void BrokerClient::add_md(uint32_t md_uid) {
    broker_states_[md_uid] = longfist::enums::BrokerState::Idle;
    if (default_md_uid_ == 0) {
        default_md_uid_ = md_uid;
    }
}

void BrokerClient::add_td(uint32_t td_uid) {
    broker_states_[td_uid] = longfist::enums::BrokerState::Idle;
    if (default_td_uid_ == 0) {
        default_td_uid_ = td_uid;
    }
}

void BrokerClient::subscribe(uint32_t md_uid, const std::string& instrument_id,
                            const std::string& exchange_id,
                            longfist::enums::InstrumentType type) {
    longfist::types::Subscribe sub{};
    sub.instrument_id = longfist::instrument_id_t(instrument_id.c_str());
    sub.exchange_id = longfist::exchange_id_t(exchange_id.c_str());
    sub.instrument_type = type;
    // In full implementation: host_.get_writer(md_uid)->write(0, sub);
    spdlog::debug("BrokerClient: subscribe {} @ {} to md_uid={}", instrument_id, exchange_id, md_uid);
}

void BrokerClient::unsubscribe(uint32_t md_uid, const std::string& instrument_id,
                              const std::string& exchange_id) {
    longfist::types::Unsubscribe unsub{};
    unsub.instrument_id = longfist::instrument_id_t(instrument_id.c_str());
    unsub.exchange_id = longfist::exchange_id_t(exchange_id.c_str());
    spdlog::debug("BrokerClient: unsubscribe {} @ {} from md_uid={}", instrument_id, exchange_id, md_uid);
}

uint64_t BrokerClient::insert_order(uint32_t td_uid,
                                    const std::string& instrument_id,
                                    const std::string& exchange_id,
                                    double price, int64_t volume,
                                    longfist::enums::Side side,
                                    longfist::enums::Offset offset,
                                    longfist::enums::PriceType price_type) {
    uint64_t order_id = next_order_id_.fetch_add(1);

    longfist::types::OrderInput input{};
    input.order_id = order_id;
    input.instrument_id = longfist::instrument_id_t(instrument_id.c_str());
    input.exchange_id = longfist::exchange_id_t(exchange_id.c_str());
    input.limit_price = price;
    input.volume = volume;
    input.side = side;
    input.offset = offset;
    input.price_type = price_type;

    // In full implementation: host_.get_writer(td_uid)->write(0, input);
    spdlog::debug("BrokerClient: insert_order id={} {} {} @ {} vol={}",
                 order_id, instrument_id, exchange_id, price, volume);
    return order_id;
}

void BrokerClient::cancel_order(uint32_t td_uid, uint64_t order_id) {
    longfist::types::OrderAction action{};
    action.order_id = order_id;
    action.action = longfist::enums::HistoryOrderAction::Cancel;
    // In full implementation: host_.get_writer(td_uid)->write(0, action);
    spdlog::debug("BrokerClient: cancel_order id={} to td_uid={}", order_id, td_uid);
}

void BrokerClient::on_broker_state(uint32_t source_uid, longfist::enums::BrokerState state) {
    broker_states_[source_uid] = state;
}

longfist::enums::BrokerState BrokerClient::get_state(uint32_t uid) const {
    auto it = broker_states_.find(uid);
    if (it != broker_states_.end()) return it->second;
    return longfist::enums::BrokerState::Unknown;
}

} // namespace kungfu::wingchun::broker
