#pragma once

#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <kungfu/wingchun/book/book.h>
#include <string>
#include <optional>
#include <cstdint>

namespace kungfu::wingchun::strategy {

class Context {
public:
    virtual ~Context() = default;

    virtual int64_t now() const = 0;
    virtual std::string trading_day() const = 0;

    virtual void subscribe(const std::string& exchange_id,
                          const std::string& instrument_id,
                          longfist::enums::InstrumentType type =
                              longfist::enums::InstrumentType::Unknown) = 0;

    virtual uint64_t insert_order(const std::string& instrument_id,
                                  const std::string& exchange_id,
                                  double price, int64_t volume,
                                  longfist::enums::Side side,
                                  longfist::enums::Offset offset,
                                  longfist::enums::PriceType price_type =
                                      longfist::enums::PriceType::Limit) = 0;

    virtual void cancel_order(uint64_t order_id) = 0;

    virtual void add_account(const std::string& group, const std::string& name) = 0;
    virtual void add_md(const std::string& group, const std::string& name) = 0;

    virtual int32_t add_timer(int64_t nano_after) = 0;
    virtual int32_t add_time_interval(int64_t duration_ns) = 0;

    virtual const Book& get_book() const = 0;
    virtual std::optional<longfist::types::Position> get_position(
        const std::string& instrument_id, const std::string& exchange_id,
        longfist::enums::Direction direction = longfist::enums::Direction::Long) const = 0;
    virtual const longfist::types::Asset& get_asset() const = 0;

    virtual std::optional<longfist::types::Quote> get_last_quote(
        const std::string& instrument_id, const std::string& exchange_id) const = 0;
};

} // namespace kungfu::wingchun::strategy
