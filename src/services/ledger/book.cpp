#include <kungfu/wingchun/book/book.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace kungfu::wingchun {

std::string BookKeeper::make_position_key(const char* instrument_id, const char* exchange_id) const {
    return std::string(instrument_id) + "@" + std::string(exchange_id);
}

Book& BookKeeper::get_book(uint32_t uid) {
    auto it = books_.find(uid);
    if (it == books_.end()) {
        Book book;
        book.owner_uid = uid;
        books_[uid] = book;
        return books_[uid];
    }
    return it->second;
}

bool BookKeeper::has_book(uint32_t uid) const {
    return books_.find(uid) != books_.end();
}

void BookKeeper::on_trade(const longfist::types::Trade& trade, uint32_t book_uid) {
    auto& book = get_book(book_uid);
    std::string key = make_position_key(trade.instrument_id.data, trade.exchange_id.data);

    // Determine which position map to use based on side/direction
    auto& positions = (trade.side == longfist::enums::Side::Buy)
                          ? book.long_positions
                          : book.short_positions;

    auto it = positions.find(key);
    if (it == positions.end()) {
        // Create new position
        longfist::types::Position pos{};
        pos.instrument_id = trade.instrument_id;
        pos.exchange_id = trade.exchange_id;
        pos.direction = (trade.side == longfist::enums::Side::Buy)
                            ? longfist::enums::Direction::Long
                            : longfist::enums::Direction::Short;
        positions[key] = pos;
        it = positions.find(key);
    }

    auto& pos = it->second;

    if (trade.offset == longfist::enums::Offset::Open) {
        // Opening position
        double cost = trade.price * static_cast<double>(trade.volume);
        double total_cost = pos.avg_open_price * static_cast<double>(pos.volume) + cost;
        pos.volume += trade.volume;
        if (pos.volume > 0) {
            pos.avg_open_price = total_cost / static_cast<double>(pos.volume);
        }
        pos.position_cost = pos.avg_open_price * static_cast<double>(pos.volume);

        // Update asset: freeze cash for buy
        if (trade.side == longfist::enums::Side::Buy) {
            book.asset.available -= cost;
        } else {
            book.asset.available -= cost; // for short opening, margin deducted
        }
    } else {
        // Closing position (Close, CloseToday, CloseYesterday)
        // For close, we reduce volume from the opposite side
        auto& close_positions = (trade.side == longfist::enums::Side::Sell)
                                    ? book.long_positions
                                    : book.short_positions;

        auto close_it = close_positions.find(key);
        if (close_it != close_positions.end()) {
            auto& close_pos = close_it->second;
            double pnl = 0.0;

            if (trade.side == longfist::enums::Side::Sell) {
                // Selling to close long
                pnl = (trade.price - close_pos.avg_open_price) * static_cast<double>(trade.volume);
                book.asset.available += trade.price * static_cast<double>(trade.volume);
            } else {
                // Buying to close short
                pnl = (close_pos.avg_open_price - trade.price) * static_cast<double>(trade.volume);
                book.asset.available += close_pos.avg_open_price * static_cast<double>(trade.volume);
            }

            close_pos.volume -= trade.volume;
            close_pos.realized_pnl += pnl;
            book.asset.realized_pnl += pnl;

            if (close_pos.volume <= 0) {
                close_pos.volume = 0;
                close_pos.avg_open_price = 0.0;
                close_pos.position_cost = 0.0;
            } else {
                close_pos.position_cost = close_pos.avg_open_price * static_cast<double>(close_pos.volume);
            }
        } else {
            spdlog::warn("BookKeeper: close trade but no matching position for {}", key);
        }
    }
}

void BookKeeper::on_order(const longfist::types::Order& order, uint32_t book_uid) {
    auto& book = get_book(book_uid);

    // When an order is submitted, freeze the cash
    if (order.status == longfist::enums::OrderStatus::Submitted ||
        order.status == longfist::enums::OrderStatus::Pending) {
        // Cash freeze is handled on order submission
        if (order.side == longfist::enums::Side::Buy && order.offset == longfist::enums::Offset::Open) {
            double frozen = order.frozen_price * static_cast<double>(order.volume_left);
            book.asset.frozen_cash += frozen;
        }
    }

    // When order is cancelled or fully filled, unfreeze
    if (order.status == longfist::enums::OrderStatus::Cancelled ||
        order.status == longfist::enums::OrderStatus::Error ||
        order.status == longfist::enums::OrderStatus::Filled ||
        order.status == longfist::enums::OrderStatus::PartialFilledNotActive) {
        // Unfreeze remaining
        if (order.side == longfist::enums::Side::Buy && order.offset == longfist::enums::Offset::Open) {
            double unfreeze = order.frozen_price * static_cast<double>(order.volume_left);
            book.asset.frozen_cash -= unfreeze;
            if (book.asset.frozen_cash < 0.0) book.asset.frozen_cash = 0.0;
        }
    }
}

void BookKeeper::on_position(const longfist::types::Position& pos, uint32_t book_uid) {
    auto& book = get_book(book_uid);
    std::string key = make_position_key(pos.instrument_id.data, pos.exchange_id.data);

    if (pos.direction == longfist::enums::Direction::Long) {
        book.long_positions[key] = pos;
    } else {
        book.short_positions[key] = pos;
    }
}

void BookKeeper::on_asset(const longfist::types::Asset& asset, uint32_t book_uid) {
    auto& book = get_book(book_uid);
    book.asset = asset;
}

} // namespace kungfu::wingchun
