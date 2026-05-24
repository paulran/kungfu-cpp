#include <gtest/gtest.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/serialize.h>
#include <cstring>

using namespace kungfu::longfist;
using namespace kungfu::longfist::types;

TEST(LongfistTest, QuoteIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<Quote>);
}

TEST(LongfistTest, OrderInputIsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderInput>);
}

TEST(LongfistTest, QuoteTag) {
    EXPECT_EQ(Quote::tag, 101);
}

TEST(LongfistTest, OrderInputTag) {
    EXPECT_EQ(OrderInput::tag, 201);
}

TEST(LongfistTest, ToJsonQuote) {
    Quote q{};
    std::strncpy(q.instrument_id, "600000", 31);
    std::strncpy(q.exchange_id, "SSE", 15);
    q.last_price = 15.5;
    q.volume = 5000;
    q.data_time = 1234567890;

    auto j = to_json(q);
    EXPECT_EQ(j["instrument_id"], "600000");
    EXPECT_EQ(j["exchange_id"], "SSE");
    EXPECT_DOUBLE_EQ(j["last_price"].get<double>(), 15.5);
    EXPECT_EQ(j["volume"].get<int64_t>(), 5000);
}

TEST(LongfistTest, FromJsonQuote) {
    nlohmann::json j;
    j["instrument_id"] = "000001";
    j["exchange_id"] = "SZSE";
    j["last_price"] = 20.3;
    j["volume"] = 3000;
    j["data_time"] = 999;

    Quote q{};
    from_json(j, q);

    EXPECT_STREQ(q.instrument_id, "000001");
    EXPECT_STREQ(q.exchange_id, "SZSE");
    EXPECT_DOUBLE_EQ(q.last_price, 20.3);
    EXPECT_EQ(q.volume, 3000);
    EXPECT_EQ(q.data_time, 999);
}

TEST(LongfistTest, RoundTripOrderInput) {
    OrderInput oi{};
    oi.order_id = 12345;
    std::strncpy(oi.instrument_id, "IF2403", 31);
    std::strncpy(oi.exchange_id, "CFFEX", 15);
    oi.limit_price = 4500.0;
    oi.volume = 2;
    oi.side = enums::Side::Buy;
    oi.offset = enums::Offset::Open;
    oi.price_type = enums::PriceType::Limit;

    auto json_str = to_string(oi);
    auto restored = from_string<OrderInput>(json_str);

    EXPECT_EQ(restored.order_id, 12345u);
    EXPECT_STREQ(restored.instrument_id, "IF2403");
    EXPECT_DOUBLE_EQ(restored.limit_price, 4500.0);
    EXPECT_EQ(restored.volume, 2);
    EXPECT_EQ(static_cast<int>(restored.side), static_cast<int>(enums::Side::Buy));
    EXPECT_EQ(static_cast<int>(restored.offset), static_cast<int>(enums::Offset::Open));
}

TEST(LongfistTest, EnumSerialization) {
    OrderInput oi{};
    oi.side = enums::Side::Sell;
    oi.offset = enums::Offset::CloseToday;

    auto j = to_json(oi);
    EXPECT_EQ(j["side"].get<int>(), 1);
    EXPECT_EQ(j["offset"].get<int>(), 2);
}
