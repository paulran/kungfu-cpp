#include <kungfu/wingchun/strategy/sim_context.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstring>
#include <cmath>
#include <vector>

using namespace kungfu::longfist;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;

class DemoStrategy : public SimStrategy {
public:
    void pre_start(SimContext& ctx) override {
        ctx.add_account("sim", "sim");
        ctx.subscribe("SSE", "600000");
        spdlog::info("[Strategy] pre_start: subscribed 600000@SSE");
    }

    void post_start(SimContext& ctx) override {
        double avail = ctx.get_asset().avail;
        spdlog::info("[Strategy] post_start: ready to trade, asset available={:.2f}", avail);
    }

    void on_quote(SimContext& ctx, const Quote& quote) override {
        quote_count_++;
        spdlog::info("[Strategy] on_quote #{}: {} last={:.2f} bid={:.2f} ask={:.2f}",
                     quote_count_, std::string(quote.instrument_id),
                     quote.last_price, quote.bid_price[0], quote.ask_price[0]);

        if (!order_placed_ && quote.ask_price[0] > 0) {
            double price = quote.ask_price[0];
            int64_t volume = 100;
            auto order_id = ctx.insert_order(
                std::string(quote.instrument_id),
                std::string(quote.exchange_id),
                price, volume, Side::Buy, Offset::Open, PriceType::Limit);
            spdlog::info("[Strategy] placed BUY order: id={} price={:.2f} vol={}",
                         order_id, price, volume);
            order_placed_ = true;
        }

        if (order_filled_ && !close_placed_ && quote.bid_price[0] > entry_price_ * 1.02) {
            double price = quote.bid_price[0];
            auto order_id = ctx.insert_order(
                std::string(quote.instrument_id),
                std::string(quote.exchange_id),
                price, 100, Side::Sell, Offset::Close, PriceType::Limit);
            spdlog::info("[Strategy] placed SELL (close) order: id={} price={:.2f}", order_id, price);
            close_placed_ = true;
        }
    }

    void on_order(SimContext& ctx, const Order& order) override {
        int64_t filled = order.volume - order.volume_left;
        spdlog::info("[Strategy] on_order: id={} status={} filled={}/{}",
                     order.order_id, static_cast<int>(order.status),
                     filled, order.volume);
    }

    void on_trade(SimContext& ctx, const Trade& trade) override {
        spdlog::info("[Strategy] on_trade: id={} price={:.2f} vol={}",
                     trade.order_id, trade.price, trade.volume);
        if (!order_filled_) {
            order_filled_ = true;
            entry_price_ = trade.price;
        }

        auto pos = ctx.get_position(
            std::string(trade.instrument_id),
            std::string(trade.exchange_id), Direction::Long);
        if (pos) {
            int64_t pos_vol = pos->volume;
            double pos_avg = pos->avg_open_price;
            spdlog::info("[Strategy] position: vol={} avg_price={:.2f}", pos_vol, pos_avg);
        }
    }

    void pre_stop(SimContext& ctx) override {
        spdlog::info("[Strategy] pre_stop: quotes={} entry={:.2f} filled={}",
                     quote_count_, entry_price_, order_filled_);
    }

private:
    int quote_count_ = 0;
    bool order_placed_ = false;
    bool order_filled_ = false;
    bool close_placed_ = false;
    double entry_price_ = 0;
};

static std::vector<Quote> generate_sim_quotes() {
    std::vector<Quote> quotes;
    double base_price = 10.00;
    int64_t time_ns = 1000000000LL;

    for (int i = 0; i < 20; i++) {
        Quote q{};
        std::strncpy(q.instrument_id, "600000", sizeof(q.instrument_id));
        std::strncpy(q.exchange_id, "SSE", sizeof(q.exchange_id));
        q.data_time = time_ns + i * 500000000LL;

        double drift = 0.05 * std::sin(i * 0.5);
        q.last_price = base_price + drift + i * 0.02;
        q.bid_price[0] = q.last_price - 0.01;
        q.ask_price[0] = q.last_price + 0.01;
        q.bid_volume[0] = 1000;
        q.ask_volume[0] = 800;
        q.volume = 50000 + i * 1000;
        q.turnover = q.volume * q.last_price;

        quotes.push_back(q);
    }
    return quotes;
}

int main() {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::info("=== kungfu-cpp SIM Strategy Demo ===");
    spdlog::info("");

    SimContext ctx;
    DemoStrategy strategy;

    ctx.set_strategy(&strategy);
    ctx.set_trading_day("20260524");
    ctx.set_initial_asset(1000000.0);

    spdlog::info("--- Phase 1: Strategy Initialization ---");
    strategy.pre_start(ctx);
    strategy.post_start(ctx);

    spdlog::info("");
    spdlog::info("--- Phase 2: Market Data Replay (20 ticks) ---");
    auto quotes = generate_sim_quotes();
    ctx.sim_md().set_data(std::move(quotes));
    ctx.run_replay();

    spdlog::info("");
    spdlog::info("--- Phase 3: Final State ---");
    strategy.pre_stop(ctx);

    auto& asset = ctx.get_asset();
    double asset_avail = asset.avail;
    double asset_margin = asset.margin;
    double asset_frozen = asset.frozen_cash;
    spdlog::info("[Result] Asset: available={:.2f} margin={:.2f} frozen={:.2f}",
                 asset_avail, asset_margin, asset_frozen);

    auto& book = ctx.get_book();
    spdlog::info("[Result] Long positions: {}", book.long_positions.size());
    for (auto& [key, pos] : book.long_positions) {
        int64_t pos_vol = pos.volume;
        double pos_avg_price = pos.avg_open_price;
        double pos_unrealized = pos.unrealized_pnl;
        spdlog::info("[Result]   {}: vol={} avg_price={:.2f} unrealized_pnl={:.2f}",
                     key, pos_vol, pos_avg_price, pos_unrealized);
    }
    spdlog::info("[Result] Short positions: {}", book.short_positions.size());

    spdlog::info("");
    spdlog::info("=== Demo Complete ===");
    return 0;
}