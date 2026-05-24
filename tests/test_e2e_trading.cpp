#include <gtest/gtest.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime_context.h>
#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <kungfu/wingchun/gateway/sim/sim_td.h>
#include <kungfu/yijinjing/io/locator.h>
#include <filesystem>

using namespace kungfu::wingchun;
using namespace kungfu::wingchun::strategy;
using namespace kungfu::wingchun::gateway::sim;
using namespace kungfu::longfist;
using namespace kungfu::yijinjing::io;

class BuyOnQuoteStrategy : public Strategy {
public:
    bool ordered = false;
    uint64_t last_order_id = 0;
    int trade_count = 0;
    double realized_pnl = 0.0;

    void on_quote(Context& ctx, const types::Quote& quote) override {
        if (!ordered && quote.last_price > 0) {
            last_order_id = ctx.insert_order(
                std::string(quote.instrument_id.data),
                std::string(quote.exchange_id.data),
                quote.ask_price_0, 2,
                enums::Side::Buy, enums::Offset::Open);
            ordered = true;
        }
    }

    void on_trade(Context& ctx, const types::Trade& trade) override {
        trade_count++;
    }
};

TEST(E2ETrading, SimMdToStrategyToSimTd) {
    // Setup
    auto test_dir = (std::filesystem::temp_directory_path() / "kf_test_e2e").string();
    std::filesystem::remove_all(test_dir);
    Locator locator(test_dir);
    auto strat_loc = location::make(category::STRATEGY, "test", "e2e", mode::LIVE);

    // Create components
    SimMarketData sim_md;
    SimTrader sim_td;
    sim_md.on_start();
    sim_td.on_start();

    // Create strategy runner
    Runner runner(strat_loc, locator, false);
    auto strategy = std::make_shared<BuyOnQuoteStrategy>();
    runner.set_strategy(strategy);

    auto ctx_ptr = std::make_unique<RuntimeContext>(runner, locator);
    auto* ctx = ctx_ptr.get();
    ctx->add_md("sim", "md01");
    ctx->add_account("sim", "td01");
    runner.set_context(std::move(ctx_ptr));

    // Wire callbacks: SimMD -> Strategy -> SimTD -> Strategy
    sim_md.set_quote_callback([&](const types::Quote& quote) {
        ctx->on_quote(quote);
        // Dispatch to strategy
        strategy->on_quote(*ctx, quote);

        // Also feed quote to SimTD for matching
        sim_td.on_quote(quote, quote.data_time);
    });

    sim_td.set_order_callback([&](const types::Order& order) {
        ctx->on_order(order);
        strategy->on_order(*ctx, order);
    });

    sim_td.set_trade_callback([&](const types::Trade& trade) {
        ctx->on_trade(trade);
        strategy->on_trade(*ctx, trade);
    });

    // The strategy subscribes
    sim_md.subscribe("IF2401", "CFFEX", enums::InstrumentType::Future);

    // Simulate: strategy will use ctx.insert_order which goes through BrokerClient
    // For this test, we intercept by directly inserting to SimTD when strategy orders

    // Feed a quote - this should trigger the strategy to order
    types::Quote quote{};
    quote.instrument_id = instrument_id_t("IF2401");
    quote.exchange_id = exchange_id_t("CFFEX");
    quote.last_price = 4500.0;
    quote.bid_price_0 = 4499.0;
    quote.bid_volume_0 = 100;
    quote.ask_price_0 = 4500.0;
    quote.ask_volume_0 = 100;
    quote.data_time = 1000000;

    sim_md.feed_quote(quote);

    // Strategy should have ordered
    EXPECT_TRUE(strategy->ordered);
    EXPECT_NE(strategy->last_order_id, 0u);

    // Now manually route the order from strategy to SimTD
    // (In production, the journal IPC handles this)
    types::OrderInput input{};
    input.order_id = strategy->last_order_id;
    input.instrument_id = instrument_id_t("IF2401");
    input.exchange_id = exchange_id_t("CFFEX");
    input.limit_price = 4500.0;
    input.volume = 2;
    input.side = enums::Side::Buy;
    input.offset = enums::Offset::Open;
    input.price_type = enums::PriceType::Limit;

    sim_td.insert_order(input);

    // Feed another quote to trigger matching
    sim_td.on_quote(quote, 2000000);

    // Verify trade callback fired
    EXPECT_GE(strategy->trade_count, 1);

    // Verify BookKeeper has the position
    auto pos = ctx->get_position("IF2401", "CFFEX", enums::Direction::Long);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->volume, 2);

    std::filesystem::remove_all(test_dir);
}

TEST(E2ETrading, QuoteUpdatesPnL) {
    auto test_dir = (std::filesystem::temp_directory_path() / "kf_test_e2e_pnl").string();
    std::filesystem::remove_all(test_dir);
    Locator locator(test_dir);
    auto strat_loc = location::make(category::STRATEGY, "test", "pnl", mode::LIVE);

    Runner runner(strat_loc, locator, false);
    auto strategy = std::make_shared<BuyOnQuoteStrategy>();
    runner.set_strategy(strategy);

    auto ctx_ptr = std::make_unique<RuntimeContext>(runner, locator);
    auto* ctx = ctx_ptr.get();
    runner.set_context(std::move(ctx_ptr));

    // Manually set up a position via trade
    types::Trade trade{};
    trade.trade_id = 1;
    trade.order_id = 1;
    trade.instrument_id = instrument_id_t("IF2401");
    trade.exchange_id = exchange_id_t("CFFEX");
    trade.price = 4500.0;
    trade.volume = 2;
    trade.side = enums::Side::Buy;
    trade.offset = enums::Offset::Open;
    ctx->on_trade(trade);

    // Now feed a quote with higher price to see unrealized PnL
    types::Quote quote{};
    quote.instrument_id = instrument_id_t("IF2401");
    quote.exchange_id = exchange_id_t("CFFEX");
    quote.last_price = 4510.0;
    quote.data_time = 3000000;
    ctx->on_quote(quote);

    auto pos = ctx->get_position("IF2401", "CFFEX", enums::Direction::Long);
    ASSERT_TRUE(pos.has_value());
    // Unrealized PnL = (4510 - 4500) * 2 * multiplier(1) = 20
    EXPECT_NEAR(pos->unrealized_pnl, 20.0, 0.01);

    std::filesystem::remove_all(test_dir);
}

TEST(E2ETrading, SimTdPartialFillFlow) {
    SimTrader td;
    td.on_start();

    std::vector<types::Order> orders;
    std::vector<types::Trade> trades;
    td.set_order_callback([&](const types::Order& o) { orders.push_back(o); });
    td.set_trade_callback([&](const types::Trade& t) { trades.push_back(t); });

    types::OrderInput input{};
    input.order_id = 1;
    input.instrument_id = instrument_id_t("IF2401");
    input.exchange_id = exchange_id_t("CFFEX");
    input.limit_price = 4500.0;
    input.volume = 10;
    input.side = enums::Side::Buy;
    input.offset = enums::Offset::Open;
    input.price_type = enums::PriceType::Limit;

    td.insert_order(input);
    EXPECT_EQ(orders.size(), 1u); // Submitted

    // Partial fill: only 3 available
    types::Quote q1{};
    q1.instrument_id = instrument_id_t("IF2401");
    q1.exchange_id = exchange_id_t("CFFEX");
    q1.ask_price_0 = 4500.0;
    q1.ask_volume_0 = 3;
    q1.bid_price_0 = 4499.0;
    q1.bid_volume_0 = 10;

    td.on_quote(q1, 2000);
    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].volume, 3);
    ASSERT_GE(orders.size(), 2u);
    EXPECT_EQ(orders.back().status, enums::OrderStatus::PartialFilledActive);

    // Fill remaining
    types::Quote q2{};
    q2.instrument_id = instrument_id_t("IF2401");
    q2.exchange_id = exchange_id_t("CFFEX");
    q2.ask_price_0 = 4500.0;
    q2.ask_volume_0 = 20;
    q2.bid_price_0 = 4499.0;
    q2.bid_volume_0 = 10;

    td.on_quote(q2, 3000);
    EXPECT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[1].volume, 7);
    EXPECT_EQ(orders.back().status, enums::OrderStatus::Filled);
}
