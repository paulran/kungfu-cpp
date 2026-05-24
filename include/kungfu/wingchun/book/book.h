#pragma once

#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace kungfu::wingchun {

struct BookConfig {
    int32_t contract_multiplier = 1;
    double commission_rate = 0.0;
};

struct Book {
    uint32_t owner_uid = 0;
    BookConfig config;
    std::unordered_map<std::string, longfist::types::Position> long_positions;
    std::unordered_map<std::string, longfist::types::Position> short_positions;
    longfist::types::Asset asset{};
};

class BookKeeper {
public:
    void on_trade(const longfist::types::Trade& trade, uint32_t book_uid);
    void on_order(const longfist::types::Order& order, uint32_t book_uid);
    void on_position(const longfist::types::Position& pos, uint32_t book_uid);
    void on_asset(const longfist::types::Asset& asset, uint32_t book_uid);
    void on_quote_for_pnl(const longfist::types::Quote& quote, uint32_t book_uid);

    void set_instrument_info(const longfist::types::Instrument& inst);

    Book& get_book(uint32_t uid);
    bool has_book(uint32_t uid) const;

private:
    std::string make_position_key(const char* instrument_id, const char* exchange_id) const;
    int32_t get_multiplier(const char* instrument_id, const char* exchange_id) const;

    std::unordered_map<uint32_t, Book> books_;
    std::unordered_map<std::string, longfist::types::Instrument> instruments_;
};

} // namespace kungfu::wingchun
