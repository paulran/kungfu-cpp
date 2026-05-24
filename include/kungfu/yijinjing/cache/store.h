#pragma once

#include <kungfu/longfist/types.h>
#include <string>
#include <vector>
#include <memory>

namespace kungfu::yijinjing::cache {

// Intermediate row types for sqlite_orm (strings instead of array_t)
struct OrderRow {
    uint64_t order_id = 0;
    std::string instrument_id;
    std::string exchange_id;
    double limit_price = 0.0;
    double frozen_price = 0.0;
    int64_t volume = 0;
    int64_t volume_traded = 0;
    int64_t volume_left = 0;
    int status = 0;
    int side = 0;
    int offset = 0;
    int64_t insert_time = 0;
    int64_t update_time = 0;
};

struct TradeRow {
    uint64_t trade_id = 0;
    uint64_t order_id = 0;
    std::string instrument_id;
    std::string exchange_id;
    double price = 0.0;
    int64_t volume = 0;
    int side = 0;
    int offset = 0;
    int64_t trade_time = 0;
};

struct PositionRow {
    int64_t id = 0; // synthetic primary key
    std::string instrument_id;
    std::string exchange_id;
    int direction = 0;
    int64_t volume = 0;
    int64_t yesterday_volume = 0;
    double avg_open_price = 0.0;
    double position_cost = 0.0;
    double unrealized_pnl = 0.0;
    double realized_pnl = 0.0;
};

struct AssetRow {
    std::string account_id;
    double initial_equity = 0.0;
    double static_equity = 0.0;
    double dynamic_equity = 0.0;
    double available = 0.0;
    double margin = 0.0;
    double frozen_cash = 0.0;
    double frozen_margin = 0.0;
    double frozen_fee = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
};

class StateStore {
public:
    explicit StateStore(const std::string& db_path);
    ~StateStore();

    // Orders
    void upsert_order(const longfist::types::Order& order);
    std::vector<longfist::types::Order> get_all_orders();

    // Trades
    void insert_trade(const longfist::types::Trade& trade);
    std::vector<longfist::types::Trade> get_all_trades();

    // Positions
    void upsert_position(const longfist::types::Position& pos);
    std::vector<longfist::types::Position> get_all_positions();

    // Assets
    void upsert_asset(const longfist::types::Asset& asset);
    std::vector<longfist::types::Asset> get_all_assets();

    void clear_all();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string db_path_;
};

} // namespace kungfu::yijinjing::cache
