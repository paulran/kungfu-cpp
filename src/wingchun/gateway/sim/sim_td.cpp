#include <kungfu/wingchun/gateway/sim/sim_td.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::gateway::sim {

void SimTrader::on_start() {
    state_ = longfist::enums::BrokerState::Ready;
}

void SimTrader::on_exit() {
    state_ = longfist::enums::BrokerState::Idle;
}

bool SimTrader::insert_order(const longfist::types::OrderInput& input) {
    engine_.insert_order(input, current_time_);

    longfist::types::Order order{};
    order.order_id = input.order_id;
    order.instrument_id = input.instrument_id;
    order.exchange_id = input.exchange_id;
    order.limit_price = input.limit_price;
    order.frozen_price = input.limit_price;
    order.volume = input.volume;
    order.volume_traded = 0;
    order.volume_left = input.volume;
    order.status = longfist::enums::OrderStatus::Submitted;
    order.side = input.side;
    order.offset = input.offset;
    order.insert_time = current_time_;
    order.update_time = current_time_;

    if (order_cb_) order_cb_(order);
    if (write_cb_) write_cb_(order, nullptr);
    return true;
}

bool SimTrader::cancel_order(uint64_t order_id) {
    auto it = engine_.pending_orders().find(order_id);
    if (it == engine_.pending_orders().end()) return false;

    auto& po = it->second;
    longfist::types::Order order{};
    order.order_id = order_id;
    order.instrument_id = po.input.instrument_id;
    order.exchange_id = po.input.exchange_id;
    order.limit_price = po.input.limit_price;
    order.frozen_price = po.input.limit_price;
    order.volume = po.input.volume;
    order.volume_traded = po.input.volume - po.volume_left;
    order.volume_left = po.volume_left;
    order.status = longfist::enums::OrderStatus::Cancelled;
    order.side = po.input.side;
    order.offset = po.input.offset;
    order.insert_time = po.insert_time;
    order.update_time = current_time_;

    engine_.cancel_order(order_id);
    if (order_cb_) order_cb_(order);
    if (write_cb_) write_cb_(order, nullptr);
    return true;
}

bool SimTrader::req_position() {
    return true;
}

bool SimTrader::req_account() {
    return true;
}

void SimTrader::on_quote(const longfist::types::Quote& quote, int64_t now) {
    current_time_ = now;
    auto fills = engine_.on_quote(quote, now);

    for (const auto& fill : fills) {
        longfist::types::Trade trade{};
        trade.trade_id = fill.trade_id;
        trade.order_id = fill.order_id;
        trade.instrument_id = quote.instrument_id;
        trade.exchange_id = quote.exchange_id;
        trade.price = fill.price;
        trade.volume = fill.volume;
        trade.trade_time = fill.trade_time;
        trade.side = fill.side;
        trade.offset = fill.offset;

        if (trade_cb_) trade_cb_(trade);

        longfist::types::Order order{};
        order.order_id = fill.order_id;
        order.instrument_id = quote.instrument_id;
        order.exchange_id = quote.exchange_id;
        order.side = fill.side;
        order.offset = fill.offset;
        order.update_time = now;

        if (engine_.has_pending_order(fill.order_id)) {
            auto& po = engine_.pending_orders().at(fill.order_id);
            order.limit_price = po.input.limit_price;
            order.frozen_price = po.input.limit_price;
            order.volume = po.input.volume;
            order.volume_traded = po.input.volume - po.volume_left;
            order.volume_left = po.volume_left;
            order.status = longfist::enums::OrderStatus::PartialFilledActive;
            order.insert_time = po.insert_time;
        } else {
            order.status = longfist::enums::OrderStatus::Filled;
            order.volume_left = 0;
        }

        if (order_cb_) order_cb_(order);
        if (write_cb_) write_cb_(order, &trade);
    }
}

} // namespace kungfu::wingchun::gateway::sim
