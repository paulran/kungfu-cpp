#pragma once

#include <kungfu/longfist/enums.h>
#include <kungfu/common/types.h>
#include <boost/hana/define_struct.hpp>
#include <boost/hana/for_each.hpp>
#include <boost/hana/accessors.hpp>
#include <boost/hana/first.hpp>
#include <boost/hana/second.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

// Workaround for MSVC C++20 compatibility with Boost.Hana
#if defined(_MSC_VER) && (_MSVC_LANG > 201703L)
namespace std {
template <typename T> struct is_literal_type : std::true_type {};
}
#endif

namespace kungfu::longfist {

// Fixed-size char array wrapper for use in BOOST_HANA_DEFINE_STRUCT
// (raw char[N] confuses the macro's preprocessor comma parsing)
template<std::size_t N>
struct array_t {
    char data[N]{};

    operator const char*() const { return data; }
    operator char*() { return data; }

    array_t() = default;
    array_t(const char* s) { std::strncpy(data, s, N - 1); data[N - 1] = '\0'; }

    bool operator==(const array_t& o) const { return std::strncmp(data, o.data, N) == 0; }
    bool operator!=(const array_t& o) const { return !(*this == o); }
};

using instrument_id_t = array_t<32>;
using exchange_id_t = array_t<16>;
using account_id_t = array_t<32>;

// Trait to detect array_t
template<typename T> struct is_array_t : std::false_type {};
template<std::size_t N> struct is_array_t<array_t<N>> : std::true_type {};
template<typename T> inline constexpr bool is_array_t_v = is_array_t<T>::value;

namespace types {

// Trait: is type fixed-size (can be memcpy'd into journal frame)?
template<typename T, typename = void>
struct is_size_fixed : std::is_trivially_copyable<T> {};

template<typename T>
inline constexpr bool size_fixed_v = is_size_fixed<T>::value;

// ============ System control types (mark-only, no data) ============

struct PageEnd {
    static constexpr int32_t tag = -1;
    static constexpr const char* type_name = "PageEnd";
};

struct SessionStart {
    static constexpr int32_t tag = 10000;
    static constexpr const char* type_name = "SessionStart";
};

struct TimeReset {
    static constexpr int32_t tag = 10001;
    static constexpr const char* type_name = "TimeReset";
};

struct RequestStart {
    static constexpr int32_t tag = 10002;
    static constexpr const char* type_name = "RequestStart";
};

struct RequestStop {
    static constexpr int32_t tag = 10003;
    static constexpr const char* type_name = "RequestStop";
};

struct RequestReadFrom {
    static constexpr int32_t tag = 10010;
    static constexpr const char* type_name = "RequestReadFrom";
};

struct RequestWriteTo {
    static constexpr int32_t tag = 10011;
    static constexpr const char* type_name = "RequestWriteTo";
};

// ============ Packed data types (fixed-size, zero-copy) ============

#pragma pack(push, 1)

struct Quote {
    BOOST_HANA_DEFINE_STRUCT(Quote,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (int64_t, data_time),
        (double, last_price),
        (double, pre_close_price),
        (double, open_price),
        (double, high_price),
        (double, low_price),
        (int64_t, volume),
        (double, turnover),
        (double, bid_price_0),
        (double, bid_price_1),
        (double, bid_price_2),
        (double, bid_price_3),
        (double, bid_price_4),
        (int64_t, bid_volume_0),
        (int64_t, bid_volume_1),
        (int64_t, bid_volume_2),
        (int64_t, bid_volume_3),
        (int64_t, bid_volume_4),
        (double, ask_price_0),
        (double, ask_price_1),
        (double, ask_price_2),
        (double, ask_price_3),
        (double, ask_price_4),
        (int64_t, ask_volume_0),
        (int64_t, ask_volume_1),
        (int64_t, ask_volume_2),
        (int64_t, ask_volume_3),
        (int64_t, ask_volume_4)
    );
    static constexpr int32_t tag = 101;
    static constexpr const char* type_name = "Quote";
};

struct Bar {
    BOOST_HANA_DEFINE_STRUCT(Bar,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (int64_t, start_time),
        (int64_t, end_time),
        (double, open),
        (double, high),
        (double, low),
        (double, close),
        (int64_t, volume),
        (double, turnover)
    );
    static constexpr int32_t tag = 102;
    static constexpr const char* type_name = "Bar";
};

struct OrderInput {
    BOOST_HANA_DEFINE_STRUCT(OrderInput,
        (uint64_t, order_id),
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (double, limit_price),
        (int64_t, volume),
        (enums::Side, side),
        (enums::Offset, offset),
        (enums::PriceType, price_type)
    );
    static constexpr int32_t tag = 201;
    static constexpr const char* type_name = "OrderInput";
};

struct Order {
    BOOST_HANA_DEFINE_STRUCT(Order,
        (uint64_t, order_id),
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (double, limit_price),
        (double, frozen_price),
        (int64_t, volume),
        (int64_t, volume_traded),
        (int64_t, volume_left),
        (enums::OrderStatus, status),
        (enums::Side, side),
        (enums::Offset, offset),
        (int64_t, insert_time),
        (int64_t, update_time)
    );
    static constexpr int32_t tag = 202;
    static constexpr const char* type_name = "Order";
};

struct Trade {
    BOOST_HANA_DEFINE_STRUCT(Trade,
        (uint64_t, trade_id),
        (uint64_t, order_id),
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (double, price),
        (int64_t, volume),
        (enums::Side, side),
        (enums::Offset, offset),
        (int64_t, trade_time)
    );
    static constexpr int32_t tag = 203;
    static constexpr const char* type_name = "Trade";
};

struct Position {
    BOOST_HANA_DEFINE_STRUCT(Position,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (enums::Direction, direction),
        (int64_t, volume),
        (int64_t, yesterday_volume),
        (double, avg_open_price),
        (double, position_cost),
        (double, unrealized_pnl),
        (double, realized_pnl)
    );
    static constexpr int32_t tag = 301;
    static constexpr const char* type_name = "Position";
};

struct Asset {
    BOOST_HANA_DEFINE_STRUCT(Asset,
        (account_id_t, account_id),
        (double, initial_equity),
        (double, static_equity),
        (double, dynamic_equity),
        (double, available),
        (double, margin),
        (double, frozen_cash),
        (double, frozen_margin),
        (double, frozen_fee),
        (double, realized_pnl),
        (double, unrealized_pnl)
    );
    static constexpr int32_t tag = 302;
    static constexpr const char* type_name = "Asset";
};

struct Instrument {
    BOOST_HANA_DEFINE_STRUCT(Instrument,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (enums::InstrumentType, instrument_type),
        (double, price_tick),
        (int32_t, delivery_year),
        (int32_t, delivery_month),
        (int32_t, contract_multiplier),
        (double, long_margin_ratio),
        (double, short_margin_ratio)
    );
    static constexpr int32_t tag = 103;
    static constexpr const char* type_name = "Instrument";
};

// Register message (used in nng checkin, variable-size via JSON)
struct Register {
    BOOST_HANA_DEFINE_STRUCT(Register,
        (int32_t, msg_type),
        (uint32_t, source),
        (uint32_t, dest),
        (int64_t, gen_time),
        (int64_t, trigger_time),
        (int32_t, pid)
    );
    static constexpr int32_t tag = 10100;
    static constexpr const char* type_name = "Register";
};

// Channel establishment
struct Channel {
    BOOST_HANA_DEFINE_STRUCT(Channel,
        (uint32_t, source_uid),
        (uint32_t, dest_uid)
    );
    static constexpr int32_t tag = 10101;
    static constexpr const char* type_name = "Channel";
};

// Time request for timers
struct TimeRequest {
    BOOST_HANA_DEFINE_STRUCT(TimeRequest,
        (int64_t, trigger_time),
        (int32_t, msg_type),
        (uint32_t, source)
    );
    static constexpr int32_t tag = 10102;
    static constexpr const char* type_name = "TimeRequest";
};

// Deregister: app leaving the system
struct Deregister {
    BOOST_HANA_DEFINE_STRUCT(Deregister,
        (uint32_t, uid),
        (int32_t, pid)
    );
    static constexpr int32_t tag = 10103;
    static constexpr const char* type_name = "Deregister";
};

// Location info published by master
struct Location {
    BOOST_HANA_DEFINE_STRUCT(Location,
        (uint32_t, uid),
        (int32_t, category),
        (array_t<32>, group),
        (array_t<32>, name),
        (int32_t, mode)
    );
    static constexpr int32_t tag = 10104;
    static constexpr const char* type_name = "Location";
};

// Broker state update (connected/disconnected/etc)
struct BrokerStateUpdate {
    BOOST_HANA_DEFINE_STRUCT(BrokerStateUpdate,
        (uint32_t, source_uid),
        (int32_t, state)
    );
    static constexpr int32_t tag = 10105;
    static constexpr const char* type_name = "BrokerStateUpdate";
};

// Request cached state recovery
struct RequestCached {
    BOOST_HANA_DEFINE_STRUCT(RequestCached,
        (uint32_t, source_uid),
        (int64_t, from_time)
    );
    static constexpr int32_t tag = 10012;
    static constexpr const char* type_name = "RequestCached";
};

// Cached state recovery complete
struct RequestCachedDone {
    BOOST_HANA_DEFINE_STRUCT(RequestCachedDone,
        (uint32_t, dest_uid)
    );
    static constexpr int32_t tag = 10013;
    static constexpr const char* type_name = "RequestCachedDone";
};

// Cache reset signal (clear all cached state)
struct CacheReset {
    BOOST_HANA_DEFINE_STRUCT(CacheReset,
        (int64_t, trigger_time)
    );
    static constexpr int32_t tag = 10014;
    static constexpr const char* type_name = "CacheReset";
};

// Trading day notification
struct TradingDay {
    BOOST_HANA_DEFINE_STRUCT(TradingDay,
        (array_t<16>, trading_day),
        (int64_t, timestamp)
    );
    static constexpr int32_t tag = 10015;
    static constexpr const char* type_name = "TradingDay";
};

// Order action (cancel/modify)
struct OrderAction {
    BOOST_HANA_DEFINE_STRUCT(OrderAction,
        (uint64_t, order_id),
        (uint64_t, order_action_id),
        (enums::HistoryOrderAction, action),
        (double, price),
        (int64_t, volume)
    );
    static constexpr int32_t tag = 204;
    static constexpr const char* type_name = "OrderAction";
};

// Subscribe market data
struct Subscribe {
    BOOST_HANA_DEFINE_STRUCT(Subscribe,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id),
        (enums::InstrumentType, instrument_type)
    );
    static constexpr int32_t tag = 401;
    static constexpr const char* type_name = "Subscribe";
};

// Unsubscribe market data
struct Unsubscribe {
    BOOST_HANA_DEFINE_STRUCT(Unsubscribe,
        (instrument_id_t, instrument_id),
        (exchange_id_t, exchange_id)
    );
    static constexpr int32_t tag = 402;
    static constexpr const char* type_name = "Unsubscribe";
};

// Request position query
struct RequestPosition {
    BOOST_HANA_DEFINE_STRUCT(RequestPosition,
        (account_id_t, account_id)
    );
    static constexpr int32_t tag = 403;
    static constexpr const char* type_name = "RequestPosition";
};

// Request account/asset query
struct RequestAccount {
    BOOST_HANA_DEFINE_STRUCT(RequestAccount,
        (account_id_t, account_id)
    );
    static constexpr int32_t tag = 404;
    static constexpr const char* type_name = "RequestAccount";
};

#pragma pack(pop)

} // namespace types
} // namespace kungfu::longfist
