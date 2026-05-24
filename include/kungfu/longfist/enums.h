#pragma once

#include <cstdint>

namespace kungfu::longfist::enums {

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class Offset : uint8_t { Open = 0, Close = 1, CloseToday = 2, CloseYesterday = 3 };
enum class Direction : uint8_t { Long = 0, Short = 1 };
enum class PriceType : uint8_t { Limit = 0, Market = 1, BestPrice = 2 };
enum class OrderStatus : uint8_t {
    Unknown = 0, Submitted = 1, Pending = 2, Cancelled = 3,
    Error = 4, Filled = 5, PartialFilledNotActive = 6, PartialFilledActive = 7
};
enum class InstrumentType : uint8_t {
    Unknown = 0, Stock = 1, Future = 2, Bond = 3,
    StockOption = 4, Fund = 5, Index = 6, Repo = 7, Crypto = 8
};
enum class BrokerState : uint8_t {
    Unknown = 0, Idle = 1, DisConnected = 2, Connected = 3,
    LoggedIn = 4, Ready = 5, LoginFailed = 6
};
enum class VolumeCondition : uint8_t { Any = 0, Min = 1, All = 2 };
enum class TimeCondition : uint8_t { GFD = 0, IOC = 1, GTC = 2 };
enum class HistoryOrderAction : uint8_t { None = 0, Cancel = 1, Modify = 2 };

} // namespace kungfu::longfist::enums
