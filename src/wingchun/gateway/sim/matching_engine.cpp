#include <kungfu/wingchun/gateway/sim/matching_engine.h>
#include <algorithm>

namespace kungfu::wingchun::gateway::sim {

void MatchingEngine::insert_order(const longfist::types::OrderInput& input, int64_t now) {
    PendingOrder po;
    po.input = input;
    po.insert_time = now;
    po.volume_left = input.volume;
    pending_orders_[input.order_id] = po;
}

bool MatchingEngine::cancel_order(uint64_t order_id) {
    auto it = pending_orders_.find(order_id);
    if (it == pending_orders_.end()) return false;
    pending_orders_.erase(it);
    return true;
}

bool MatchingEngine::has_pending_order(uint64_t order_id) const {
    return pending_orders_.find(order_id) != pending_orders_.end();
}

std::vector<Fill> MatchingEngine::on_quote(const longfist::types::Quote& quote, int64_t now) {
    std::vector<Fill> fills;
    std::vector<uint64_t> to_remove;

    for (auto& [order_id, po] : pending_orders_) {
        // Only match orders for this instrument
        if (std::string(po.input.instrument_id.data) != std::string(quote.instrument_id.data) ||
            std::string(po.input.exchange_id.data) != std::string(quote.exchange_id.data)) {
            continue;
        }
        if (try_match(po, quote, fills, now)) {
            to_remove.push_back(order_id);
        }
    }

    for (auto id : to_remove) {
        pending_orders_.erase(id);
    }

    return fills;
}

bool MatchingEngine::try_match(PendingOrder& order, const longfist::types::Quote& quote,
                               std::vector<Fill>& fills, int64_t now) {
    bool is_buy = (order.input.side == longfist::enums::Side::Buy);

    if (order.input.price_type == longfist::enums::PriceType::Market) {
        // Market order fills at best available price
        double fill_price = is_buy ? quote.ask_price_0 : quote.bid_price_0;
        if (fill_price <= 0.0) return false;

        int64_t available_vol = is_buy ? quote.ask_volume_0 : quote.bid_volume_0;
        int64_t fill_vol = std::min(order.volume_left, available_vol > 0 ? available_vol : order.volume_left);

        Fill f;
        f.order_id = order.input.order_id;
        f.trade_id = next_trade_id_++;
        f.price = fill_price;
        f.volume = fill_vol;
        f.trade_time = now;
        f.side = order.input.side;
        f.offset = order.input.offset;
        fills.push_back(f);

        order.volume_left -= fill_vol;
        return order.volume_left <= 0;
    }

    // Limit order matching
    double limit_price = order.input.limit_price;

    if (is_buy) {
        // Buy limit: fills if ask_price <= limit_price
        if (quote.ask_price_0 <= 0.0 || quote.ask_price_0 > limit_price) return false;

        int64_t available_vol = quote.ask_volume_0 > 0 ? quote.ask_volume_0 : order.volume_left;
        int64_t fill_vol = std::min(order.volume_left, available_vol);

        Fill f;
        f.order_id = order.input.order_id;
        f.trade_id = next_trade_id_++;
        f.price = quote.ask_price_0;
        f.volume = fill_vol;
        f.trade_time = now;
        f.side = order.input.side;
        f.offset = order.input.offset;
        fills.push_back(f);

        order.volume_left -= fill_vol;
        return order.volume_left <= 0;
    } else {
        // Sell limit: fills if bid_price >= limit_price
        if (quote.bid_price_0 <= 0.0 || quote.bid_price_0 < limit_price) return false;

        int64_t available_vol = quote.bid_volume_0 > 0 ? quote.bid_volume_0 : order.volume_left;
        int64_t fill_vol = std::min(order.volume_left, available_vol);

        Fill f;
        f.order_id = order.input.order_id;
        f.trade_id = next_trade_id_++;
        f.price = quote.bid_price_0;
        f.volume = fill_vol;
        f.trade_time = now;
        f.side = order.input.side;
        f.offset = order.input.offset;
        fills.push_back(f);

        order.volume_left -= fill_vol;
        return order.volume_left <= 0;
    }
}

} // namespace kungfu::wingchun::gateway::sim
