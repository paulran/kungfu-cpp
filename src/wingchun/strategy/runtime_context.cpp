#include <kungfu/wingchun/strategy/runtime_context.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/utils/trading_utils.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::strategy {

RuntimeContext::RuntimeContext(Runner& runner, yijinjing::io::Locator& locator)
    : runner_(runner), locator_(locator), broker_client_(runner) {
    book_uid_ = runner.home_uid();
}

int64_t RuntimeContext::now() const {
    return current_nano_;
}

std::string RuntimeContext::trading_day() const {
    return trading_day_;
}

void RuntimeContext::subscribe(const std::string& exchange_id,
                              const std::string& instrument_id,
                              longfist::enums::InstrumentType type) {
    uint32_t md_uid = broker_client_.default_md_uid();
    if (md_uid == 0) {
        spdlog::warn("RuntimeContext: no MD source registered, cannot subscribe");
        return;
    }
    broker_client_.subscribe(md_uid, instrument_id, exchange_id, type);
}

uint64_t RuntimeContext::insert_order(const std::string& instrument_id,
                                     const std::string& exchange_id,
                                     double price, int64_t volume,
                                     longfist::enums::Side side,
                                     longfist::enums::Offset offset,
                                     longfist::enums::PriceType price_type) {
    uint32_t td_uid = broker_client_.default_td_uid();
    if (td_uid == 0) {
        spdlog::warn("RuntimeContext: no TD source registered, cannot insert order");
        return 0;
    }
    return broker_client_.insert_order(td_uid, instrument_id, exchange_id,
                                       price, volume, side, offset, price_type);
}

void RuntimeContext::cancel_order(uint64_t order_id) {
    uint32_t td_uid = broker_client_.default_td_uid();
    if (td_uid == 0) return;
    broker_client_.cancel_order(td_uid, order_id);
}

void RuntimeContext::add_account(const std::string& group, const std::string& name) {
    auto loc = yijinjing::io::location::make(yijinjing::io::category::TD, group, name, yijinjing::io::mode::LIVE);
    broker_client_.add_td(loc->uid);
    spdlog::info("RuntimeContext: added account td/{}/{} uid={}", group, name, loc->uid);
}

void RuntimeContext::add_md(const std::string& group, const std::string& name) {
    auto loc = yijinjing::io::location::make(yijinjing::io::category::MD, group, name, yijinjing::io::mode::LIVE);
    broker_client_.add_md(loc->uid);
    spdlog::info("RuntimeContext: added md/{}/{} uid={}", group, name, loc->uid);
}

int32_t RuntimeContext::add_timer(int64_t nano_after) {
    int32_t id = next_timer_id_++;
    TimerEntry entry;
    entry.trigger_time = current_nano_ + nano_after;
    entry.timer_id = id;
    entry.interval = 0;
    timers_.push_back(entry);
    return id;
}

int32_t RuntimeContext::add_time_interval(int64_t duration_ns) {
    int32_t id = next_timer_id_++;
    TimerEntry entry;
    entry.trigger_time = current_nano_ + duration_ns;
    entry.timer_id = id;
    entry.interval = duration_ns;
    timers_.push_back(entry);
    return id;
}

const Book& RuntimeContext::get_book() const {
    return const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_);
}

std::optional<longfist::types::Position> RuntimeContext::get_position(
    const std::string& instrument_id, const std::string& exchange_id,
    longfist::enums::Direction direction) const {
    auto& book = const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_);
    auto key = instrument_id + "@" + exchange_id;

    const auto& positions = (direction == longfist::enums::Direction::Long)
                                ? book.long_positions : book.short_positions;
    auto it = positions.find(key);
    if (it != positions.end()) {
        return it->second;
    }
    return std::nullopt;
}

const longfist::types::Asset& RuntimeContext::get_asset() const {
    return const_cast<BookKeeper&>(book_keeper_).get_book(book_uid_).asset;
}

std::optional<longfist::types::Quote> RuntimeContext::get_last_quote(
    const std::string& instrument_id, const std::string& exchange_id) const {
    auto key = instrument_id + "@" + exchange_id;
    auto it = quote_cache_.find(key);
    if (it != quote_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void RuntimeContext::on_quote(const longfist::types::Quote& quote) {
    current_nano_ = quote.data_time;
    auto key = std::string(quote.instrument_id.data) + "@" + std::string(quote.exchange_id.data);
    quote_cache_[key] = quote;
    book_keeper_.on_quote_for_pnl(quote, book_uid_);
}

void RuntimeContext::on_order(const longfist::types::Order& order) {
    book_keeper_.on_order(order, book_uid_);
}

void RuntimeContext::on_trade(const longfist::types::Trade& trade) {
    book_keeper_.on_trade(trade, book_uid_);
}

} // namespace kungfu::wingchun::strategy
