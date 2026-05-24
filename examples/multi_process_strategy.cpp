#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime_context.h>
#include <kungfu/common/config.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <spdlog/spdlog.h>

using namespace kungfu::longfist;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;

class DemoStrategy : public Strategy {
public:
    void pre_start(Context& ctx) override {
        ctx.add_md("sim", "sim");
        ctx.add_account("sim", "sim");
        ctx.subscribe("SSE", "600000");
        spdlog::info("[Strategy] pre_start: subscribed 600000@SSE, waiting for quotes...");
    }

    void post_start(Context& ctx) override {
        spdlog::info("[Strategy] post_start: ready");
    }

    void on_quote(Context& ctx, const Quote& quote) override {
        quote_count_++;
        if (quote_count_ % 5 == 1) {
            spdlog::info("[Strategy] on_quote #{}: {} last={:.4f} bid={:.4f} ask={:.4f}",
                         quote_count_, quote.instrument_id.data,
                         quote.last_price, quote.bid_price_0, quote.ask_price_0);
        }

        if (!order_placed_ && quote.ask_price_0 > 0 && quote_count_ >= 3) {
            double price = quote.ask_price_0;
            auto order_id = ctx.insert_order(
                std::string(quote.instrument_id.data),
                std::string(quote.exchange_id.data),
                price, 100, Side::Buy, Offset::Open, PriceType::Limit);
            spdlog::info("[Strategy] placed BUY order: id={} price={:.4f} vol=100", order_id, price);
            order_placed_ = true;
        }
    }

    void on_order(Context& ctx, const Order& order) override {
        spdlog::info("[Strategy] on_order: id={} status={} traded={}/{}",
                     order.order_id, static_cast<int>(order.status),
                     order.volume_traded, order.volume);
    }

    void on_trade(Context& ctx, const Trade& trade) override {
        spdlog::info("[Strategy] on_trade: id={} price={:.4f} vol={}",
                     trade.order_id, trade.price, trade.volume);
        auto pos = ctx.get_position(
            std::string(trade.instrument_id.data),
            std::string(trade.exchange_id.data), Direction::Long);
        if (pos) {
            spdlog::info("[Strategy] position: vol={} avg_price={:.4f}",
                         pos->volume, pos->avg_open_price);
        }
    }

    void pre_stop(Context& ctx) override {
        spdlog::info("[Strategy] pre_stop: received {} quotes, order_placed={}",
                     quote_count_, order_placed_);
    }

private:
    int quote_count_ = 0;
    bool order_placed_ = false;
};

int main(int argc, char** argv) {
    std::string config_path = argc > 1 ? argv[1] : "kungfu.toml";

    try {
        auto cfg = kungfu::common::KungfuConfig::load(config_path);
        spdlog::set_level(spdlog::level::from_str(cfg.system.log_level));

        kungfu::yijinjing::io::Locator locator(cfg.system.home);
        auto location = kungfu::yijinjing::io::location::make(
            kungfu::yijinjing::io::category::STRATEGY, "default", "demo",
            kungfu::yijinjing::io::mode::LIVE);

        spdlog::info("kf_strategy: starting, uid={}", location->uid);

        Runner runner(location, locator, cfg.system.low_latency);
        auto ctx = std::make_unique<RuntimeContext>(runner, locator);
        auto strategy = std::make_shared<DemoStrategy>();

        runner.set_strategy(strategy);
        runner.set_context(std::move(ctx));
        runner.run();
    } catch (const std::exception& e) {
        spdlog::error("kf_strategy: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
