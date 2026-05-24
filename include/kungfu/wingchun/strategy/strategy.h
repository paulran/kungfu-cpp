#pragma once

#include <kungfu/longfist/types.h>
#include <string>

namespace kungfu::wingchun::strategy {

class Context;

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void pre_start(Context& ctx) {}
    virtual void post_start(Context& ctx) {}
    virtual void pre_stop(Context& ctx) {}
    virtual void post_stop(Context& ctx) {}

    virtual void on_quote(Context& ctx, const longfist::types::Quote& quote) {}
    virtual void on_bar(Context& ctx, const longfist::types::Bar& bar) {}
    virtual void on_order(Context& ctx, const longfist::types::Order& order) {}
    virtual void on_trade(Context& ctx, const longfist::types::Trade& trade) {}
    virtual void on_position(Context& ctx, const longfist::types::Position& pos) {}
    virtual void on_asset(Context& ctx, const longfist::types::Asset& asset) {}
    virtual void on_timer(Context& ctx, int64_t nano, int32_t timer_id) {}
    virtual void on_trading_day(Context& ctx, const std::string& trading_day) {}
};

} // namespace kungfu::wingchun::strategy
