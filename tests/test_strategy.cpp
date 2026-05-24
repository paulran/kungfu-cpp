#include <gtest/gtest.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/strategy/context.h>
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

class TestStrategy : public Strategy {
public:
    int quote_count = 0;
    int order_count = 0;
    int trade_count = 0;
    bool started = false;
    types::Quote last_quote{};
    types::Trade last_trade{};

    void post_start(Context& ctx) override { started = true; }
    void on_quote(Context& ctx, const types::Quote& quote) override {
        last_quote = quote;
        quote_count++;
    }
    void on_order(Context& ctx, const types::Order& order) override { order_count++; }
    void on_trade(Context& ctx, const types::Trade& trade) override {
        last_trade = trade;
        trade_count++;
    }
};

class StrategyTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::unique_ptr<Locator> locator;
    location_ptr strat_loc;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_strategy").string();
        std::filesystem::remove_all(test_dir);
        locator = std::make_unique<Locator>(test_dir);
        strat_loc = location::make(category::STRATEGY, "test", "strat01", mode::LIVE);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(StrategyTest, RunnerSetup) {
    Runner runner(strat_loc, *locator, false);
    auto strategy = std::make_shared<TestStrategy>();
    runner.set_strategy(strategy);

    auto ctx = std::make_unique<RuntimeContext>(runner, *locator);
    runner.set_context(std::move(ctx));

    EXPECT_EQ(runner.strategy(), strategy.get());
    EXPECT_NE(runner.context(), nullptr);
}

TEST_F(StrategyTest, RuntimeContextQuoteCache) {
    Runner runner(strat_loc, *locator, false);
    auto strategy = std::make_shared<TestStrategy>();
    runner.set_strategy(strategy);

    auto ctx_ptr = std::make_unique<RuntimeContext>(runner, *locator);
    auto* ctx = ctx_ptr.get();
    runner.set_context(std::move(ctx_ptr));

    // No quote yet
    auto q = ctx->get_last_quote("IF2401", "CFFEX");
    EXPECT_FALSE(q.has_value());

    // Feed a quote
    types::Quote quote{};
    quote.instrument_id = instrument_id_t("IF2401");
    quote.exchange_id = exchange_id_t("CFFEX");
    quote.last_price = 4500.0;
    quote.data_time = 1000000;
    ctx->on_quote(quote);

    q = ctx->get_last_quote("IF2401", "CFFEX");
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->last_price, 4500.0);
    EXPECT_EQ(ctx->now(), 1000000);
}

TEST_F(StrategyTest, RuntimeContextAddMdTd) {
    Runner runner(strat_loc, *locator, false);
    auto ctx = std::make_unique<RuntimeContext>(runner, *locator);

    ctx->add_md("sim", "sim_md");
    ctx->add_account("sim", "sim_td");

    EXPECT_NE(ctx->broker_client().default_md_uid(), 0u);
    EXPECT_NE(ctx->broker_client().default_td_uid(), 0u);
}

TEST_F(StrategyTest, RuntimeContextTimer) {
    Runner runner(strat_loc, *locator, false);
    auto ctx = std::make_unique<RuntimeContext>(runner, *locator);

    int32_t id1 = ctx->add_timer(1000000000LL); // 1 second
    int32_t id2 = ctx->add_time_interval(500000000LL); // 0.5 seconds
    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
}

TEST_F(StrategyTest, BookKeeperIntegration) {
    Runner runner(strat_loc, *locator, false);
    auto ctx_ptr = std::make_unique<RuntimeContext>(runner, *locator);
    auto* ctx = ctx_ptr.get();
    runner.set_context(std::move(ctx_ptr));

    // Set initial asset
    types::Asset asset{};
    asset.account_id = account_id_t("test");
    asset.available = 1000000.0;
    asset.static_equity = 1000000.0;
    ctx->book_keeper().on_asset(asset, runner.home_uid());

    // Apply a trade
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

    auto pos = ctx->get_position("IF2401", "CFFEX", enums::Direction::Long);
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->volume, 2);
}
