#include <gtest/gtest.h>
#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <kungfu/wingchun/gateway/sim/sim_td.h>

using namespace kungfu::wingchun::gateway::sim;
using namespace kungfu::longfist;

TEST(SimMDTest, SubscribeUnsubscribe) {
    SimMarketData md;
    md.on_start();
    EXPECT_EQ(md.get_state(), enums::BrokerState::Ready);

    EXPECT_TRUE(md.subscribe("IF2401", "CFFEX", enums::InstrumentType::Future));
    EXPECT_TRUE(md.is_subscribed("IF2401", "CFFEX"));
    EXPECT_FALSE(md.is_subscribed("rb2405", "SHFE"));

    EXPECT_TRUE(md.unsubscribe("IF2401", "CFFEX"));
    EXPECT_FALSE(md.is_subscribed("IF2401", "CFFEX"));
}

TEST(SimMDTest, FeedQuoteCallback) {
    SimMarketData md;
    md.on_start();
    md.subscribe("IF2401", "CFFEX", enums::InstrumentType::Future);

    int callback_count = 0;
    types::Quote received{};
    md.set_quote_callback([&](const types::Quote& q) {
        received = q;
        callback_count++;
    });

    types::Quote quote{};
    quote.instrument_id = instrument_id_t("IF2401");
    quote.exchange_id = exchange_id_t("CFFEX");
    quote.last_price = 4500.0;
    md.feed_quote(quote);

    EXPECT_EQ(callback_count, 1);
    EXPECT_DOUBLE_EQ(received.last_price, 4500.0);
}

TEST(SimMDTest, ReplayData) {
    SimMarketData md;
    md.on_start();

    std::vector<types::Quote> data;
    for (int i = 0; i < 5; i++) {
        types::Quote q{};
        q.instrument_id = instrument_id_t("IF2401");
        q.exchange_id = exchange_id_t("CFFEX");
        q.last_price = 4500.0 + i;
        data.push_back(q);
    }
    md.set_data(std::move(data));

    int count = 0;
    md.set_quote_callback([&](const types::Quote&) { count++; });

    EXPECT_FALSE(md.replay_done());
    while (md.replay_next()) {}
    EXPECT_TRUE(md.replay_done());
    EXPECT_EQ(count, 5);
}

TEST(SimTDTest, InsertAndMatch) {
    SimTrader td;
    td.on_start();
    EXPECT_EQ(td.get_state(), enums::BrokerState::Ready);

    std::vector<types::Order> orders;
    std::vector<types::Trade> trades;
    td.set_order_callback([&](const types::Order& o) { orders.push_back(o); });
    td.set_trade_callback([&](const types::Trade& t) { trades.push_back(t); });

    types::OrderInput input{};
    input.order_id = 1;
    input.instrument_id = instrument_id_t("IF2401");
    input.exchange_id = exchange_id_t("CFFEX");
    input.limit_price = 4500.0;
    input.volume = 2;
    input.side = enums::Side::Buy;
    input.offset = enums::Offset::Open;
    input.price_type = enums::PriceType::Limit;

    EXPECT_TRUE(td.insert_order(input));
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].status, enums::OrderStatus::Submitted);

    // Feed matching quote
    types::Quote quote{};
    quote.instrument_id = instrument_id_t("IF2401");
    quote.exchange_id = exchange_id_t("CFFEX");
    quote.bid_price_0 = 4499.0;
    quote.bid_volume_0 = 10;
    quote.ask_price_0 = 4500.0;
    quote.ask_volume_0 = 10;
    quote.last_price = 4499.5;

    td.on_quote(quote, 2000);

    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].order_id, 1u);
    EXPECT_DOUBLE_EQ(trades[0].price, 4500.0);
    EXPECT_EQ(trades[0].volume, 2);

    // Order update: filled
    ASSERT_GE(orders.size(), 2u);
    EXPECT_EQ(orders.back().status, enums::OrderStatus::Filled);
}

TEST(SimTDTest, CancelOrder) {
    SimTrader td;
    td.on_start();

    std::vector<types::Order> orders;
    td.set_order_callback([&](const types::Order& o) { orders.push_back(o); });

    types::OrderInput input{};
    input.order_id = 1;
    input.instrument_id = instrument_id_t("IF2401");
    input.exchange_id = exchange_id_t("CFFEX");
    input.limit_price = 4400.0; // won't match
    input.volume = 5;
    input.side = enums::Side::Buy;
    input.offset = enums::Offset::Open;
    input.price_type = enums::PriceType::Limit;

    td.insert_order(input);
    EXPECT_TRUE(td.cancel_order(1));
    ASSERT_GE(orders.size(), 2u);
    EXPECT_EQ(orders.back().status, enums::OrderStatus::Cancelled);
}
