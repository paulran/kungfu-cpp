#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace kf {

enum class Side : int { Buy = 0, Sell = 1 };
enum class Offset : int { Open = 0, Close = 1, CloseToday = 2, CloseYesterday = 3 };
enum class Direction : int { Long = 0, Short = 1 };
enum class PriceType : int { Limit = 0, Market = 1, BestPrice = 2 };
enum class OrderStatus : int {
    Unknown = 0,
    Submitted = 1,
    Pending = 2,
    Cancelled = 3,
    Error = 4,
    Filled = 5,
    PartialFilledNotActive = 6,
    PartialFilledActive = 7
};
enum class InstrumentType : int {
    Unknown = 0, Stock = 1, Future = 2, Bond = 3,
    StockOption = 4, Fund = 5, Index = 6, Repo = 7, Crypto = 8
};
enum class BrokerState : int {
    Unknown = 0, Idle = 1, DisConnected = 2, Connected = 3,
    LoggedIn = 4, Ready = 5, LoginFailed = 6
};
enum class Category : int { System = 0, TD = 1, MD = 2, Strategy = 3 };

struct Quote {
    QString instrument_id;
    QString exchange_id;
    int64_t data_time = 0;
    double last_price = 0;
    double pre_close_price = 0;
    double open_price = 0;
    double high_price = 0;
    double low_price = 0;
    int64_t volume = 0;
    double turnover = 0;
    double bid_price[5] = {};
    int64_t bid_volume[5] = {};
    double ask_price[5] = {};
    int64_t ask_volume[5] = {};
};

struct Order {
    uint64_t order_id = 0;
    QString instrument_id;
    QString exchange_id;
    double limit_price = 0;
    double frozen_price = 0;
    int64_t volume = 0;
    int64_t volume_traded = 0;
    int64_t volume_left = 0;
    OrderStatus status = OrderStatus::Unknown;
    Side side = Side::Buy;
    Offset offset = Offset::Open;
    int64_t insert_time = 0;
    int64_t update_time = 0;
};

struct Trade {
    uint64_t trade_id = 0;
    uint64_t order_id = 0;
    QString instrument_id;
    QString exchange_id;
    double price = 0;
    int64_t volume = 0;
    Side side = Side::Buy;
    Offset offset = Offset::Open;
    int64_t trade_time = 0;
};

struct Position {
    QString instrument_id;
    QString exchange_id;
    Direction direction = Direction::Long;
    int64_t volume = 0;
    int64_t yesterday_volume = 0;
    double avg_open_price = 0;
    double position_cost = 0;
    double unrealized_pnl = 0;
    double realized_pnl = 0;
};

struct Asset {
    QString account_id;
    double initial_equity = 0;
    double static_equity = 0;
    double dynamic_equity = 0;
    double available = 0;
    double margin = 0;
    double frozen_cash = 0;
    double frozen_margin = 0;
    double frozen_fee = 0;
    double realized_pnl = 0;
    double unrealized_pnl = 0;
};

struct Instrument {
    QString instrument_id;
    QString exchange_id;
    InstrumentType instrument_type = InstrumentType::Unknown;
    double price_tick = 0;
    int32_t delivery_year = 0;
    int32_t delivery_month = 0;
    int32_t contract_multiplier = 1;
    double long_margin_ratio = 1.0;
    double short_margin_ratio = 1.0;
};

struct ProcessInfo {
    uint32_t uid = 0;
    Category category = Category::System;
    QString group;
    QString name;
    int mode = 0;
    BrokerState broker_state = BrokerState::Unknown;
};

struct StrategyInfo {
    uint32_t uid = 0;
    QString group;
    QString name;
    BrokerState state = BrokerState::Unknown;
};

struct AccountInfo {
    uint32_t uid = 0;
    QString source;
    QString account_id;
    BrokerState state = BrokerState::Unknown;
};

struct OrderInput {
    QString instrument_id;
    QString exchange_id;
    double limit_price = 0;
    int64_t volume = 0;
    Side side = Side::Buy;
    Offset offset = Offset::Open;
    PriceType price_type = PriceType::Limit;
};

} // namespace kf
