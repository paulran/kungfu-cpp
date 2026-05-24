#include <kungfu/wingchun/strategy/sim_context.h>
#include <kungfu/wingchun/utils/trading_utils.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::strategy {

SimContext::SimContext() {
    sim_md_.set_quote_callback([this](const longfist::types::Quote& quote) {
        current_nano_ = quote.data_time;
        auto key = std::string(quote.instrument_id.data) + "@" + std::string(quote.exchange_id.data);
        quote_cache_[key] = quote;
        book_keeper_.on_quote_for_pnl(quote, book_uid_);

        // Match existing pending orders first
        sim_td_.on_quote(quote, current_nano_);

        // Then let strategy react (may place new orders)
        if (strategy_) {
            strategy_->on_quote(*this, quote);
        }

        // Match any new orders placed during on_quote against this same tick
        sim_td_.on_quote(quote, current_nano_);

        check_timers();
    });

    sim_td_.set_order_callback([this](const longfist::types::Order& order) {
        on_order(order);
    });

    sim_td_.set_trade_callback([this](const longfist::types::Trade& trade) {
        on_trade(trade);
    });
}

int64_t SimContext::now() const {
    return current_nano_;
}

std::string SimContext::trading_day() const {
    return trading_day_;
}

void SimContext::subscribe(const std::string& exchange_id,
                          const std::string& instrument_id,
                          longfist::enums::InstrumentType type) {
    sim_md_.subscribe(instrument_id, exchange_id, type);
    spdlog::info("SimContext: subscribed {} @ {}", instrument_id, exchange_id);
}

uint64_t SimContext::insert_order(const std::string& instrument_id,
                                 const std::string& exchange_id,
                                 double price, int64_t volume,
                                 longfist::enums::Side side,
                                 longfist::enums::Offset offset,
                                 longfist::enums::PriceType price_type) {
    uint64_t order_id = next_order_id_.fetch_add(1);

    longfist::types::OrderInput input{};
    input.order_id = order_id;
    input.instrument_id = longfist::instrument_id_t(instrument_id.c_str());
    input.exchange_id = longfist::exchange_id_t(exchange_id.c_str());
    input.limit_price = price;
    input.volume = volume;
    input.side = side;
    input.offset = offset;
    input.price_type = price_type;

    sim_td_.insert_order(input);
    return order_id;
}

void SimContext::cancel_order(uint64_t order_id) {
    sim_td_.cancel_order(order_id);
}

void SimContext::add_account(const std::string& group, const std::string& name) {
    spdlog::info("SimContext: add_account {}/{} (sim mode, no-op)", group, name);
}

void SimContext::add_md(const std::string& group, const std::string& name) {
    spdlog::info("SimContext: add_md {}/{} (sim mode, no-op)", group, name);
}

int32_t SimContext::add_timer(int64_t nano_after) {
    int32_t id = next_timer_id_++;
    TimerEntry entry;
    entry.trigger_time = current_nano_ + nano_after;
    entry.timer_id = id;
    entry.interval = 0;
    timers_.push_back(entry);
    return id;
}

int32_t SimContext::add_time_interval(int64_t duration_ns) {
    int32_t id = next_timer_id_++;
    TimerEntry entry;
    entry.trigger_time = current_nano_ + duration_ns;
    entry.timer_id = id;
    entry.interval = duration_ns;
    timers_.push_back(entry);
    return id;
}

const Book& SimContext::get_book() const {
    return const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_);
}

std::optional<longfist::types::Position> SimContext::get_position(
    const std::string& instrument_id, const std::string& exchange_id,
    longfist::enums::Direction direction) const {
    auto& book = const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_);
    auto key = instrument_id + "@" + exchange_id;
    const auto& positions = (direction == longfist::enums::Direction::Long)
                                ? book.long_positions : book.short_positions;
    auto it = positions.find(key);
    if (it != positions.end()) return it->second;
    return std::nullopt;
}

const longfist::types::Asset& SimContext::get_asset() const {
    return const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_).asset;
}

std::optional<longfist::types::Quote> SimContext::get_last_quote(
    const std::string& instrument_id, const std::string& exchange_id) const {
    auto key = instrument_id + "@" + exchange_id;
    auto it = quote_cache_.find(key);
    if (it != quote_cache_.end()) return it->second;
    return std::nullopt;
}

void SimContext::set_initial_asset(double available, double margin) {
    auto& book = book_keeper_.get_book(book_uid_);
    book.asset.available = available;
    book.asset.initial_equity = available;
    book.asset.static_equity = available;
    book.asset.dynamic_equity = available;
    book.asset.margin = margin;
}

void SimContext::feed_quote(const longfist::types::Quote& quote) {
    sim_md_.feed_quote(quote);
}

void SimContext::run_replay() {
    while (sim_md_.replay_next()) {
        // Each replay_next triggers feed_quote → on_quote callback chain
    }
}

void SimContext::on_order(const longfist::types::Order& order) {
    book_keeper_.on_order(order, book_uid_);
    if (strategy_) {
        strategy_->on_order(*this, order);
    }
}

void SimContext::on_trade(const longfist::types::Trade& trade) {
    book_keeper_.on_trade(trade, book_uid_);
    if (strategy_) {
        strategy_->on_trade(*this, trade);
    }
}

void SimContext::check_timers() {
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (current_nano_ >= it->trigger_time) {
            int32_t timer_id = it->timer_id;
            int64_t interval = it->interval;
            if (interval > 0) {
                it->trigger_time = current_nano_ + interval;
                ++it;
            } else {
                it = timers_.erase(it);
            }
            if (strategy_) {
                strategy_->on_timer(*this, current_nano_, timer_id);
            }
        } else {
            ++it;
        }
    }
}

} // namespace kungfu::wingchun::strategy
