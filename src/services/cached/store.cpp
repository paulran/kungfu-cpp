#include <kungfu/yijinjing/cache/store.h>
#include <sqlite_orm/sqlite_orm.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <mutex>

namespace kungfu::yijinjing::cache {

using namespace sqlite_orm;

// Helper: convert between packed types and row types
static OrderRow order_to_row(const longfist::types::Order& o) {
    OrderRow r;
    r.order_id = o.order_id;
    r.instrument_id = std::string(o.instrument_id.data);
    r.exchange_id = std::string(o.exchange_id.data);
    r.limit_price = o.limit_price;
    r.frozen_price = o.frozen_price;
    r.volume = o.volume;
    r.volume_traded = o.volume_traded;
    r.volume_left = o.volume_left;
    r.status = static_cast<int>(o.status);
    r.side = static_cast<int>(o.side);
    r.offset = static_cast<int>(o.offset);
    r.insert_time = o.insert_time;
    r.update_time = o.update_time;
    return r;
}

static longfist::types::Order row_to_order(const OrderRow& r) {
    longfist::types::Order o{};
    o.order_id = r.order_id;
    o.instrument_id = longfist::instrument_id_t(r.instrument_id.c_str());
    o.exchange_id = longfist::exchange_id_t(r.exchange_id.c_str());
    o.limit_price = r.limit_price;
    o.frozen_price = r.frozen_price;
    o.volume = r.volume;
    o.volume_traded = r.volume_traded;
    o.volume_left = r.volume_left;
    o.status = static_cast<longfist::enums::OrderStatus>(r.status);
    o.side = static_cast<longfist::enums::Side>(r.side);
    o.offset = static_cast<longfist::enums::Offset>(r.offset);
    o.insert_time = r.insert_time;
    o.update_time = r.update_time;
    return o;
}

static TradeRow trade_to_row(const longfist::types::Trade& t) {
    TradeRow r;
    r.trade_id = t.trade_id;
    r.order_id = t.order_id;
    r.instrument_id = std::string(t.instrument_id.data);
    r.exchange_id = std::string(t.exchange_id.data);
    r.price = t.price;
    r.volume = t.volume;
    r.side = static_cast<int>(t.side);
    r.offset = static_cast<int>(t.offset);
    r.trade_time = t.trade_time;
    return r;
}

static longfist::types::Trade row_to_trade(const TradeRow& r) {
    longfist::types::Trade t{};
    t.trade_id = r.trade_id;
    t.order_id = r.order_id;
    t.instrument_id = longfist::instrument_id_t(r.instrument_id.c_str());
    t.exchange_id = longfist::exchange_id_t(r.exchange_id.c_str());
    t.price = r.price;
    t.volume = r.volume;
    t.side = static_cast<longfist::enums::Side>(r.side);
    t.offset = static_cast<longfist::enums::Offset>(r.offset);
    t.trade_time = r.trade_time;
    return t;
}

static PositionRow position_to_row(const longfist::types::Position& p) {
    PositionRow r;
    r.id = 0; // auto-increment or upsert key
    r.instrument_id = std::string(p.instrument_id.data);
    r.exchange_id = std::string(p.exchange_id.data);
    r.direction = static_cast<int>(p.direction);
    r.volume = p.volume;
    r.yesterday_volume = p.yesterday_volume;
    r.avg_open_price = p.avg_open_price;
    r.position_cost = p.position_cost;
    r.unrealized_pnl = p.unrealized_pnl;
    r.realized_pnl = p.realized_pnl;
    return r;
}

static longfist::types::Position row_to_position(const PositionRow& r) {
    longfist::types::Position p{};
    p.instrument_id = longfist::instrument_id_t(r.instrument_id.c_str());
    p.exchange_id = longfist::exchange_id_t(r.exchange_id.c_str());
    p.direction = static_cast<longfist::enums::Direction>(r.direction);
    p.volume = r.volume;
    p.yesterday_volume = r.yesterday_volume;
    p.avg_open_price = r.avg_open_price;
    p.position_cost = r.position_cost;
    p.unrealized_pnl = r.unrealized_pnl;
    p.realized_pnl = r.realized_pnl;
    return p;
}

static AssetRow asset_to_row(const longfist::types::Asset& a) {
    AssetRow r;
    r.account_id = std::string(a.account_id.data);
    r.initial_equity = a.initial_equity;
    r.static_equity = a.static_equity;
    r.dynamic_equity = a.dynamic_equity;
    r.available = a.available;
    r.margin = a.margin;
    r.frozen_cash = a.frozen_cash;
    r.frozen_margin = a.frozen_margin;
    r.frozen_fee = a.frozen_fee;
    r.realized_pnl = a.realized_pnl;
    r.unrealized_pnl = a.unrealized_pnl;
    return r;
}

static longfist::types::Asset row_to_asset(const AssetRow& r) {
    longfist::types::Asset a{};
    a.account_id = longfist::account_id_t(r.account_id.c_str());
    a.initial_equity = r.initial_equity;
    a.static_equity = r.static_equity;
    a.dynamic_equity = r.dynamic_equity;
    a.available = r.available;
    a.margin = r.margin;
    a.frozen_cash = r.frozen_cash;
    a.frozen_margin = r.frozen_margin;
    a.frozen_fee = r.frozen_fee;
    a.realized_pnl = r.realized_pnl;
    a.unrealized_pnl = r.unrealized_pnl;
    return a;
}

// Define the storage schema
inline auto make_storage(const std::string& path) {
    return sqlite_orm::make_storage(path,
        make_table("orders",
            make_column("order_id", &OrderRow::order_id, primary_key()),
            make_column("instrument_id", &OrderRow::instrument_id),
            make_column("exchange_id", &OrderRow::exchange_id),
            make_column("limit_price", &OrderRow::limit_price),
            make_column("frozen_price", &OrderRow::frozen_price),
            make_column("volume", &OrderRow::volume),
            make_column("volume_traded", &OrderRow::volume_traded),
            make_column("volume_left", &OrderRow::volume_left),
            make_column("status", &OrderRow::status),
            make_column("side", &OrderRow::side),
            make_column("offset", &OrderRow::offset),
            make_column("insert_time", &OrderRow::insert_time),
            make_column("update_time", &OrderRow::update_time)
        ),
        make_table("trades",
            make_column("trade_id", &TradeRow::trade_id, primary_key()),
            make_column("order_id", &TradeRow::order_id),
            make_column("instrument_id", &TradeRow::instrument_id),
            make_column("exchange_id", &TradeRow::exchange_id),
            make_column("price", &TradeRow::price),
            make_column("volume", &TradeRow::volume),
            make_column("side", &TradeRow::side),
            make_column("offset", &TradeRow::offset),
            make_column("trade_time", &TradeRow::trade_time)
        ),
        make_table("positions",
            make_column("id", &PositionRow::id, primary_key().autoincrement()),
            make_column("instrument_id", &PositionRow::instrument_id),
            make_column("exchange_id", &PositionRow::exchange_id),
            make_column("direction", &PositionRow::direction),
            make_column("volume", &PositionRow::volume),
            make_column("yesterday_volume", &PositionRow::yesterday_volume),
            make_column("avg_open_price", &PositionRow::avg_open_price),
            make_column("position_cost", &PositionRow::position_cost),
            make_column("unrealized_pnl", &PositionRow::unrealized_pnl),
            make_column("realized_pnl", &PositionRow::realized_pnl)
        ),
        make_table("assets",
            make_column("account_id", &AssetRow::account_id, primary_key()),
            make_column("initial_equity", &AssetRow::initial_equity),
            make_column("static_equity", &AssetRow::static_equity),
            make_column("dynamic_equity", &AssetRow::dynamic_equity),
            make_column("available", &AssetRow::available),
            make_column("margin", &AssetRow::margin),
            make_column("frozen_cash", &AssetRow::frozen_cash),
            make_column("frozen_margin", &AssetRow::frozen_margin),
            make_column("frozen_fee", &AssetRow::frozen_fee),
            make_column("realized_pnl", &AssetRow::realized_pnl),
            make_column("unrealized_pnl", &AssetRow::unrealized_pnl)
        )
    );
}

using Storage = decltype(make_storage(""));

struct StateStore::Impl {
    Storage storage;
    std::mutex mtx;

    explicit Impl(const std::string& path)
        : storage(make_storage(path)) {
        storage.sync_schema();
    }
};

StateStore::StateStore(const std::string& db_path)
    : db_path_(db_path) {
    // Ensure parent directory exists
    auto parent = std::filesystem::path(db_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    impl_ = std::make_unique<Impl>(db_path);
    spdlog::info("StateStore: opened database at {}", db_path);
}

StateStore::~StateStore() = default;

void StateStore::upsert_order(const longfist::types::Order& order) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto row = order_to_row(order);
    impl_->storage.replace(row);
}

std::vector<longfist::types::Order> StateStore::get_all_orders() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto rows = impl_->storage.get_all<OrderRow>();
    std::vector<longfist::types::Order> result;
    result.reserve(rows.size());
    for (const auto& r : rows) {
        result.push_back(row_to_order(r));
    }
    return result;
}

void StateStore::insert_trade(const longfist::types::Trade& trade) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto row = trade_to_row(trade);
    impl_->storage.replace(row);
}

std::vector<longfist::types::Trade> StateStore::get_all_trades() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto rows = impl_->storage.get_all<TradeRow>();
    std::vector<longfist::types::Trade> result;
    result.reserve(rows.size());
    for (const auto& r : rows) {
        result.push_back(row_to_trade(r));
    }
    return result;
}

void StateStore::upsert_position(const longfist::types::Position& pos) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto row = position_to_row(pos);

    // Upsert by instrument_id + exchange_id + direction
    auto existing = impl_->storage.get_all<PositionRow>(
        where(
            c(&PositionRow::instrument_id) == row.instrument_id and
            c(&PositionRow::exchange_id) == row.exchange_id and
            c(&PositionRow::direction) == row.direction
        )
    );

    if (existing.empty()) {
        impl_->storage.insert(row);
    } else {
        row.id = existing[0].id;
        impl_->storage.update(row);
    }
}

std::vector<longfist::types::Position> StateStore::get_all_positions() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto rows = impl_->storage.get_all<PositionRow>();
    std::vector<longfist::types::Position> result;
    result.reserve(rows.size());
    for (const auto& r : rows) {
        result.push_back(row_to_position(r));
    }
    return result;
}

void StateStore::upsert_asset(const longfist::types::Asset& asset) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto row = asset_to_row(asset);
    impl_->storage.replace(row);
}

std::vector<longfist::types::Asset> StateStore::get_all_assets() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto rows = impl_->storage.get_all<AssetRow>();
    std::vector<longfist::types::Asset> result;
    result.reserve(rows.size());
    for (const auto& r : rows) {
        result.push_back(row_to_asset(r));
    }
    return result;
}

void StateStore::clear_all() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->storage.remove_all<OrderRow>();
    impl_->storage.remove_all<TradeRow>();
    impl_->storage.remove_all<PositionRow>();
    impl_->storage.remove_all<AssetRow>();
}

} // namespace kungfu::yijinjing::cache
