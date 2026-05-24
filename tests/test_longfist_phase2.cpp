#include <gtest/gtest.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/serialize.h>

using namespace kungfu::longfist;

TEST(LongfistPhase2Types, DeregisterTag) {
    EXPECT_EQ(types::Deregister::tag, 10103);
    types::Deregister d{};
    d.uid = 42;
    d.pid = 1234;
    EXPECT_EQ(d.uid, 42u);
    EXPECT_EQ(d.pid, 1234);
}

TEST(LongfistPhase2Types, LocationType) {
    EXPECT_EQ(types::Location::tag, 10104);
    types::Location loc{};
    loc.uid = 100;
    loc.category = 1;
    loc.group = array_t<32>("xtp");
    loc.name = array_t<32>("md01");
    loc.mode = 0;
    EXPECT_EQ(loc.uid, 100u);
    EXPECT_STREQ(loc.group.data, "xtp");
    EXPECT_STREQ(loc.name.data, "md01");
}

TEST(LongfistPhase2Types, BrokerStateUpdate) {
    EXPECT_EQ(types::BrokerStateUpdate::tag, 10105);
    types::BrokerStateUpdate bsu{};
    bsu.source_uid = 7;
    bsu.state = 2;
    EXPECT_EQ(bsu.source_uid, 7u);
    EXPECT_EQ(bsu.state, 2);
}

TEST(LongfistPhase2Types, RequestCached) {
    EXPECT_EQ(types::RequestCached::tag, 10012);
    types::RequestCached rc{};
    rc.source_uid = 123;
    rc.from_time = 999;
    EXPECT_EQ(rc.source_uid, 123u);
    EXPECT_EQ(rc.from_time, 999);
}

TEST(LongfistPhase2Types, RequestCachedDone) {
    EXPECT_EQ(types::RequestCachedDone::tag, 10013);
    types::RequestCachedDone rcd{};
    rcd.dest_uid = 456;
    EXPECT_EQ(rcd.dest_uid, 456u);
}

TEST(LongfistPhase2Types, CacheReset) {
    EXPECT_EQ(types::CacheReset::tag, 10014);
    types::CacheReset cr{};
    cr.trigger_time = 12345678;
    EXPECT_EQ(cr.trigger_time, 12345678);
}

TEST(LongfistPhase2Types, TradingDay) {
    EXPECT_EQ(types::TradingDay::tag, 10015);
    types::TradingDay td{};
    td.trading_day = array_t<16>("20240101");
    td.timestamp = 1704067200;
    EXPECT_STREQ(td.trading_day.data, "20240101");
    EXPECT_EQ(td.timestamp, 1704067200);
}

TEST(LongfistPhase2Types, LocationSerialize) {
    types::Location loc{};
    loc.uid = 55;
    loc.category = 2;
    loc.group = array_t<32>("ctp");
    loc.name = array_t<32>("td01");
    loc.mode = 1;

    auto json = to_json(loc);
    EXPECT_EQ(json["uid"], 55u);
    EXPECT_EQ(json["category"], 2);
    EXPECT_EQ(json["group"], "ctp");
    EXPECT_EQ(json["name"], "td01");
    EXPECT_EQ(json["mode"], 1);

    types::Location loc2{};
    from_json(json, loc2);
    EXPECT_EQ(loc2.uid, 55u);
    EXPECT_STREQ(loc2.group.data, "ctp");
}

TEST(LongfistPhase2Types, TradingDaySerialize) {
    types::TradingDay td{};
    td.trading_day = array_t<16>("20240315");
    td.timestamp = 1710460800;

    auto json = to_json(td);
    EXPECT_EQ(json["trading_day"], "20240315");
    EXPECT_EQ(json["timestamp"], 1710460800);
}

TEST(LongfistPhase2Types, SizeFixed) {
    EXPECT_TRUE(types::size_fixed_v<types::Deregister>);
    EXPECT_TRUE(types::size_fixed_v<types::Location>);
    EXPECT_TRUE(types::size_fixed_v<types::BrokerStateUpdate>);
    EXPECT_TRUE(types::size_fixed_v<types::RequestCached>);
    EXPECT_TRUE(types::size_fixed_v<types::RequestCachedDone>);
    EXPECT_TRUE(types::size_fixed_v<types::CacheReset>);
    EXPECT_TRUE(types::size_fixed_v<types::TradingDay>);
}
