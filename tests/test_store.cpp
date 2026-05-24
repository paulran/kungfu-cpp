#include <gtest/gtest.h>
#include <kungfu/yijinjing/cache/store.h>
#include <filesystem>

using namespace kungfu::yijinjing::cache;
using namespace kungfu::longfist;

class StoreTest : public ::testing::Test {
protected:
    std::string db_path;
    std::unique_ptr<StateStore> store;

    void SetUp() override {
        db_path = (std::filesystem::temp_directory_path() / "kf_test_store.db").string();
        std::filesystem::remove(db_path);
        store = std::make_unique<StateStore>(db_path);
    }

    void TearDown() override {
        store.reset();
        std::filesystem::remove(db_path);
    }
};

TEST_F(StoreTest, OrderUpsert) {
    types::Order order{};
    order.order_id = 12345;
    order.instrument_id = instrument_id_t("IF2401");
    order.exchange_id = exchange_id_t("CFFEX");
    order.limit_price = 4500.0;
    order.volume = 10;
    order.volume_traded = 5;
    order.volume_left = 5;
    order.status = enums::OrderStatus::PartialFilledActive;
    order.side = enums::Side::Buy;
    order.offset = enums::Offset::Open;
    order.insert_time = 1000000;

    store->upsert_order(order);

    auto orders = store->get_all_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].order_id, 12345u);
    EXPECT_STREQ(orders[0].instrument_id.data, "IF2401");
    EXPECT_STREQ(orders[0].exchange_id.data, "CFFEX");
    EXPECT_DOUBLE_EQ(orders[0].limit_price, 4500.0);
    EXPECT_EQ(orders[0].volume, 10);
    EXPECT_EQ(orders[0].volume_traded, 5);

    // Upsert: update the same order
    order.volume_traded = 10;
    order.volume_left = 0;
    order.status = enums::OrderStatus::Filled;
    store->upsert_order(order);

    orders = store->get_all_orders();
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].volume_traded, 10);
    EXPECT_EQ(orders[0].status, enums::OrderStatus::Filled);
}

TEST_F(StoreTest, TradeInsert) {
    types::Trade trade{};
    trade.trade_id = 99001;
    trade.order_id = 12345;
    trade.instrument_id = instrument_id_t("rb2405");
    trade.exchange_id = exchange_id_t("SHFE");
    trade.price = 3800.0;
    trade.volume = 5;
    trade.side = enums::Side::Buy;
    trade.offset = enums::Offset::Open;
    trade.trade_time = 2000000;

    store->insert_trade(trade);

    auto trades = store->get_all_trades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].trade_id, 99001u);
    EXPECT_STREQ(trades[0].instrument_id.data, "rb2405");
    EXPECT_DOUBLE_EQ(trades[0].price, 3800.0);
}

TEST_F(StoreTest, PositionUpsert) {
    types::Position pos{};
    pos.instrument_id = instrument_id_t("IF2401");
    pos.exchange_id = exchange_id_t("CFFEX");
    pos.direction = enums::Direction::Long;
    pos.volume = 10;
    pos.yesterday_volume = 5;
    pos.avg_open_price = 4500.0;
    pos.position_cost = 45000.0;

    store->upsert_position(pos);

    auto positions = store->get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].volume, 10);

    // Update volume
    pos.volume = 15;
    store->upsert_position(pos);

    positions = store->get_all_positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].volume, 15);
}

TEST_F(StoreTest, AssetUpsert) {
    types::Asset asset{};
    asset.account_id = account_id_t("test_account");
    asset.initial_equity = 1000000.0;
    asset.static_equity = 1000000.0;
    asset.dynamic_equity = 1050000.0;
    asset.available = 800000.0;
    asset.margin = 200000.0;

    store->upsert_asset(asset);

    auto assets = store->get_all_assets();
    ASSERT_EQ(assets.size(), 1u);
    EXPECT_STREQ(assets[0].account_id.data, "test_account");
    EXPECT_DOUBLE_EQ(assets[0].initial_equity, 1000000.0);
    EXPECT_DOUBLE_EQ(assets[0].available, 800000.0);
}

TEST_F(StoreTest, ClearAll) {
    types::Order order{};
    order.order_id = 1;
    order.instrument_id = instrument_id_t("test");
    store->upsert_order(order);

    types::Trade trade{};
    trade.trade_id = 1;
    trade.instrument_id = instrument_id_t("test");
    store->insert_trade(trade);

    store->clear_all();

    EXPECT_TRUE(store->get_all_orders().empty());
    EXPECT_TRUE(store->get_all_trades().empty());
    EXPECT_TRUE(store->get_all_positions().empty());
    EXPECT_TRUE(store->get_all_assets().empty());
}

TEST_F(StoreTest, MultipleOrdersAndTrades) {
    for (int i = 0; i < 10; i++) {
        types::Order order{};
        order.order_id = static_cast<uint64_t>(i + 1);
        order.instrument_id = instrument_id_t("IF2401");
        order.exchange_id = exchange_id_t("CFFEX");
        order.volume = i * 10;
        store->upsert_order(order);
    }

    auto orders = store->get_all_orders();
    EXPECT_EQ(orders.size(), 10u);
}
