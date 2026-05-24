#include <gtest/gtest.h>
#include <kungfu/wingchun/gateway/sim/matching_engine.h>

using namespace kungfu::wingchun::gateway::sim;
using namespace kungfu::longfist;

class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;

    types::Quote make_quote(const char* inst, const char* exch,
                           double bid, int64_t bid_vol,
                           double ask, int64_t ask_vol) {
        types::Quote q{};
        q.instrument_id = instrument_id_t(inst);
        q.exchange_id = exchange_id_t(exch);
        q.bid_price_0 = bid;
        q.bid_volume_0 = bid_vol;
        q.ask_price_0 = ask;
        q.ask_volume_0 = ask_vol;
        q.last_price = (bid + ask) / 2.0;
        return q;
    }

    types::OrderInput make_order(uint64_t id, const char* inst, const char* exch,
                                 double price, int64_t volume,
                                 enums::Side side, enums::PriceType pt = enums::PriceType::Limit) {
        types::OrderInput input{};
        input.order_id = id;
        input.instrument_id = instrument_id_t(inst);
        input.exchange_id = exchange_id_t(exch);
        input.limit_price = price;
        input.volume = volume;
        input.side = side;
        input.offset = enums::Offset::Open;
        input.price_type = pt;
        return input;
    }
};

TEST_F(MatchingEngineTest, InsertOrder) {
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 5, enums::Side::Buy);
    engine.insert_order(input, 1000);
    EXPECT_TRUE(engine.has_pending_order(1));
    EXPECT_EQ(engine.pending_orders().size(), 1u);
}

TEST_F(MatchingEngineTest, CancelOrder) {
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 5, enums::Side::Buy);
    engine.insert_order(input, 1000);
    EXPECT_TRUE(engine.cancel_order(1));
    EXPECT_FALSE(engine.has_pending_order(1));
}

TEST_F(MatchingEngineTest, CancelNonexistent) {
    EXPECT_FALSE(engine.cancel_order(999));
}

TEST_F(MatchingEngineTest, BuyLimitMatch) {
    // Buy limit at 4500, ask at 4500 -> should match
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 3, enums::Side::Buy);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4500.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].order_id, 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 4500.0);
    EXPECT_EQ(fills[0].volume, 3);
    EXPECT_EQ(fills[0].trade_time, 2000);
    EXPECT_FALSE(engine.has_pending_order(1));
}

TEST_F(MatchingEngineTest, BuyLimitNoMatch) {
    // Buy limit at 4500, ask at 4501 -> no match
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 3, enums::Side::Buy);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4501.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(engine.has_pending_order(1));
}

TEST_F(MatchingEngineTest, SellLimitMatch) {
    // Sell limit at 4500, bid at 4500 -> should match
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 2, enums::Side::Sell);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4500.0, 10, 4501.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].order_id, 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 4500.0);
    EXPECT_EQ(fills[0].volume, 2);
}

TEST_F(MatchingEngineTest, SellLimitNoMatch) {
    // Sell limit at 4500, bid at 4499 -> no match
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 2, enums::Side::Sell);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4501.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    EXPECT_TRUE(fills.empty());
}

TEST_F(MatchingEngineTest, PartialFill) {
    // Buy 10 lots, but only 3 available at ask
    auto input = make_order(1, "IF2401", "CFFEX", 4500.0, 10, enums::Side::Buy);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4500.0, 3);
    auto fills = engine.on_quote(quote, 2000);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].volume, 3);
    // Order should still be pending with 7 remaining
    EXPECT_TRUE(engine.has_pending_order(1));
    EXPECT_EQ(engine.pending_orders().at(1).volume_left, 7);

    // Feed another quote to fill the rest
    auto quote2 = make_quote("IF2401", "CFFEX", 4499.0, 10, 4500.0, 20);
    auto fills2 = engine.on_quote(quote2, 3000);

    ASSERT_EQ(fills2.size(), 1u);
    EXPECT_EQ(fills2[0].volume, 7);
    EXPECT_FALSE(engine.has_pending_order(1));
}

TEST_F(MatchingEngineTest, MarketOrder) {
    auto input = make_order(1, "IF2401", "CFFEX", 0.0, 5, enums::Side::Buy, enums::PriceType::Market);
    engine.insert_order(input, 1000);

    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4501.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 4501.0); // fills at ask
    EXPECT_EQ(fills[0].volume, 5);
}

TEST_F(MatchingEngineTest, MultipleOrders) {
    auto input1 = make_order(1, "IF2401", "CFFEX", 4500.0, 2, enums::Side::Buy);
    auto input2 = make_order(2, "IF2401", "CFFEX", 4490.0, 3, enums::Side::Buy);
    engine.insert_order(input1, 1000);
    engine.insert_order(input2, 1001);

    // Ask at 4500 -> only order 1 matches
    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4500.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    EXPECT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].order_id, 1u);
    EXPECT_TRUE(engine.has_pending_order(2));
}

TEST_F(MatchingEngineTest, DifferentInstruments) {
    auto input1 = make_order(1, "IF2401", "CFFEX", 4500.0, 2, enums::Side::Buy);
    auto input2 = make_order(2, "rb2405", "SHFE", 3800.0, 5, enums::Side::Buy);
    engine.insert_order(input1, 1000);
    engine.insert_order(input2, 1001);

    // Quote for IF2401 only
    auto quote = make_quote("IF2401", "CFFEX", 4499.0, 10, 4500.0, 10);
    auto fills = engine.on_quote(quote, 2000);

    EXPECT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].order_id, 1u);
    EXPECT_TRUE(engine.has_pending_order(2)); // rb2405 still pending
}
