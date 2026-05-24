#include <gtest/gtest.h>
#include <kungfu/wingchun/book/book.h>

using namespace kungfu::wingchun;
using namespace kungfu::longfist;

class BookKeeperTest : public ::testing::Test {
protected:
    BookKeeper keeper;
    uint32_t book_uid = 1001;

    types::Trade make_trade(uint64_t id, const char* inst, const char* exch,
                            double price, int64_t volume,
                            enums::Side side, enums::Offset offset) {
        types::Trade t{};
        t.trade_id = id;
        t.order_id = id;
        t.instrument_id = instrument_id_t(inst);
        t.exchange_id = exchange_id_t(exch);
        t.price = price;
        t.volume = volume;
        t.side = side;
        t.offset = offset;
        t.trade_time = 1000000;
        return t;
    }
};

TEST_F(BookKeeperTest, GetBookCreatesNew) {
    EXPECT_FALSE(keeper.has_book(book_uid));
    auto& book = keeper.get_book(book_uid);
    EXPECT_TRUE(keeper.has_book(book_uid));
    EXPECT_EQ(book.owner_uid, book_uid);
}

TEST_F(BookKeeperTest, OpenLongPosition) {
    auto& book = keeper.get_book(book_uid);
    book.asset.available = 1000000.0;

    auto trade = make_trade(1, "IF2401", "CFFEX", 4500.0, 2,
                            enums::Side::Buy, enums::Offset::Open);
    keeper.on_trade(trade, book_uid);

    ASSERT_EQ(book.long_positions.size(), 1u);
    auto key = "IF2401@CFFEX";
    auto it = book.long_positions.find(key);
    ASSERT_NE(it, book.long_positions.end());

    auto& pos = it->second;
    EXPECT_EQ(pos.volume, 2);
    EXPECT_DOUBLE_EQ(pos.avg_open_price, 4500.0);
    EXPECT_EQ(pos.direction, enums::Direction::Long);
}

TEST_F(BookKeeperTest, OpenMultipleLong) {
    auto& book = keeper.get_book(book_uid);
    book.asset.available = 1000000.0;

    // First buy at 4500
    auto t1 = make_trade(1, "IF2401", "CFFEX", 4500.0, 2,
                         enums::Side::Buy, enums::Offset::Open);
    keeper.on_trade(t1, book_uid);

    // Second buy at 4600
    auto t2 = make_trade(2, "IF2401", "CFFEX", 4600.0, 3,
                         enums::Side::Buy, enums::Offset::Open);
    keeper.on_trade(t2, book_uid);

    auto key = "IF2401@CFFEX";
    auto& pos = book.long_positions[key];
    EXPECT_EQ(pos.volume, 5);
    // avg = (4500*2 + 4600*3) / 5 = (9000+13800)/5 = 4560
    EXPECT_NEAR(pos.avg_open_price, 4560.0, 0.01);
}

TEST_F(BookKeeperTest, CloseLongPosition) {
    auto& book = keeper.get_book(book_uid);
    book.asset.available = 1000000.0;

    // Open long
    auto t1 = make_trade(1, "IF2401", "CFFEX", 4500.0, 10,
                         enums::Side::Buy, enums::Offset::Open);
    keeper.on_trade(t1, book_uid);

    // Close partial at profit
    auto t2 = make_trade(2, "IF2401", "CFFEX", 4600.0, 3,
                         enums::Side::Sell, enums::Offset::Close);
    keeper.on_trade(t2, book_uid);

    auto key = "IF2401@CFFEX";
    auto& pos = book.long_positions[key];
    EXPECT_EQ(pos.volume, 7);
    // PnL = (4600 - 4500) * 3 = 300
    EXPECT_NEAR(pos.realized_pnl, 300.0, 0.01);
    EXPECT_NEAR(book.asset.realized_pnl, 300.0, 0.01);
}

TEST_F(BookKeeperTest, OnPositionDirect) {
    types::Position pos{};
    pos.instrument_id = instrument_id_t("rb2405");
    pos.exchange_id = exchange_id_t("SHFE");
    pos.direction = enums::Direction::Short;
    pos.volume = 5;

    keeper.on_position(pos, book_uid);

    auto& book = keeper.get_book(book_uid);
    auto key = "rb2405@SHFE";
    ASSERT_EQ(book.short_positions.count(key), 1u);
    EXPECT_EQ(book.short_positions[key].volume, 5);
}

TEST_F(BookKeeperTest, OnAsset) {
    types::Asset asset{};
    asset.account_id = account_id_t("account01");
    asset.initial_equity = 500000.0;
    asset.available = 450000.0;

    keeper.on_asset(asset, book_uid);

    auto& book = keeper.get_book(book_uid);
    EXPECT_DOUBLE_EQ(book.asset.initial_equity, 500000.0);
    EXPECT_DOUBLE_EQ(book.asset.available, 450000.0);
}

TEST_F(BookKeeperTest, OrderFreezeUnfreeze) {
    auto& book = keeper.get_book(book_uid);
    book.asset.available = 1000000.0;
    book.asset.frozen_cash = 0.0;

    // Submit order
    types::Order order{};
    order.order_id = 1;
    order.instrument_id = instrument_id_t("IF2401");
    order.exchange_id = exchange_id_t("CFFEX");
    order.side = enums::Side::Buy;
    order.offset = enums::Offset::Open;
    order.frozen_price = 4500.0;
    order.volume = 10;
    order.volume_left = 10;
    order.status = enums::OrderStatus::Submitted;

    keeper.on_order(order, book_uid);
    EXPECT_DOUBLE_EQ(book.asset.frozen_cash, 45000.0);

    // Cancel order
    order.status = enums::OrderStatus::Cancelled;
    keeper.on_order(order, book_uid);
    EXPECT_DOUBLE_EQ(book.asset.frozen_cash, 0.0);
}
