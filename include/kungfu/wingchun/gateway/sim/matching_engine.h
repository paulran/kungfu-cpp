#pragma once

#include <kungfu/longfist/types.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace kungfu::wingchun::gateway::sim {

struct PendingOrder {
    longfist::types::OrderInput input;
    int64_t insert_time = 0;
    int64_t volume_left = 0;
};

struct Fill {
    uint64_t order_id;
    uint64_t trade_id;
    double price;
    int64_t volume;
    int64_t trade_time;
};

class MatchingEngine {
public:
    void insert_order(const longfist::types::OrderInput& input, int64_t now);
    bool cancel_order(uint64_t order_id);
    std::vector<Fill> on_quote(const longfist::types::Quote& quote, int64_t now);

    const std::unordered_map<uint64_t, PendingOrder>& pending_orders() const { return pending_orders_; }
    bool has_pending_order(uint64_t order_id) const;

private:
    bool try_match(PendingOrder& order, const longfist::types::Quote& quote,
                   std::vector<Fill>& fills, int64_t now);

    std::unordered_map<uint64_t, PendingOrder> pending_orders_;
    uint64_t next_trade_id_ = 1;
};

} // namespace kungfu::wingchun::gateway::sim
