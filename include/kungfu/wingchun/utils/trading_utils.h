#pragma once

#include <kungfu/longfist/enums.h>
#include <string>

namespace kungfu::wingchun::utils {

inline longfist::enums::Direction side_to_direction(longfist::enums::Side side) {
    return side == longfist::enums::Side::Buy
        ? longfist::enums::Direction::Long
        : longfist::enums::Direction::Short;
}

inline longfist::enums::Direction opposite_direction(longfist::enums::Direction dir) {
    return dir == longfist::enums::Direction::Long
        ? longfist::enums::Direction::Short
        : longfist::enums::Direction::Long;
}

inline longfist::enums::Side direction_to_close_side(longfist::enums::Direction dir) {
    return dir == longfist::enums::Direction::Long
        ? longfist::enums::Side::Sell
        : longfist::enums::Side::Buy;
}

inline bool is_final_status(longfist::enums::OrderStatus status) {
    return status == longfist::enums::OrderStatus::Filled ||
           status == longfist::enums::OrderStatus::Cancelled ||
           status == longfist::enums::OrderStatus::Error ||
           status == longfist::enums::OrderStatus::PartialFilledNotActive;
}

inline bool is_active_status(longfist::enums::OrderStatus status) {
    return status == longfist::enums::OrderStatus::Submitted ||
           status == longfist::enums::OrderStatus::Pending ||
           status == longfist::enums::OrderStatus::PartialFilledActive;
}

namespace exchange {
    constexpr const char* SSE   = "SSE";
    constexpr const char* SZSE  = "SZSE";
    constexpr const char* CFFEX = "CFFEX";
    constexpr const char* SHFE  = "SHFE";
    constexpr const char* DCE   = "DCE";
    constexpr const char* CZCE  = "CZCE";
    constexpr const char* INE   = "INE";
    constexpr const char* GFEX  = "GFEX";
    constexpr const char* BSE   = "BSE";
}

inline std::string make_instrument_key(const std::string& instrument_id,
                                       const std::string& exchange_id) {
    return instrument_id + "@" + exchange_id;
}

inline bool supports_close_today(const std::string& exchange_id) {
    return exchange_id == exchange::SHFE || exchange_id == exchange::INE;
}

inline bool is_equity_like(longfist::enums::InstrumentType type) {
    return type == longfist::enums::InstrumentType::Stock ||
           type == longfist::enums::InstrumentType::Fund ||
           type == longfist::enums::InstrumentType::Bond ||
           type == longfist::enums::InstrumentType::Repo;
}

} // namespace kungfu::wingchun::utils
