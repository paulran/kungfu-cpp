#ifndef WINGCHUN_SIM_CONTEXT_H
#define WINGCHUN_SIM_CONTEXT_H

#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/book/book.h>
#include <vector>
#include <string>

namespace kungfu::wingchun::strategy {

class SimStrategy {
public:
    virtual ~SimStrategy() = default;
    virtual void pre_start(class SimContext& ctx) {}
    virtual void post_start(class SimContext& ctx) {}
    virtual void pre_stop(class SimContext& ctx) {}
    virtual void on_quote(class SimContext& ctx, const longfist::types::Quote& quote) {}
    virtual void on_order(class SimContext& ctx, const longfist::types::Order& order) {}
    virtual void on_trade(class SimContext& ctx, const longfist::types::Trade& trade) {}
};

class SimMD {
public:
    void set_data(std::vector<longfist::types::Quote> data) {
        quotes_ = std::move(data);
    }

    const std::vector<longfist::types::Quote>& get_quotes() const {
        return quotes_;
    }

private:
    std::vector<longfist::types::Quote> quotes_;
};

class SimContext {
public:
    SimContext() : book_(empty_commissions_, empty_instruments_) {}

    void set_strategy(SimStrategy* strategy) {
        strategy_ = strategy;
    }

    void set_trading_day(const std::string& day) {
        trading_day_ = std::stoll(day);
        std::strncpy(book_.asset.trading_day, day.c_str(), sizeof(book_.asset.trading_day));
    }

    void set_initial_asset(double available) {
        book_.asset.holder_uid = 1;  // non-zero sentinel for sim (avoids assertion in Book::get_position)
        book_.asset.avail = available;
        book_.asset.static_equity = available;
    }

    longfist::types::Asset& get_asset() {
        return book_.asset;
    }

    book::Book& get_book() {
        return book_;
    }

    std::optional<longfist::types::Position> get_position(const std::string& instrument_id,
                                                           const std::string& exchange_id,
                                                           longfist::enums::Direction direction) {
        try {
            auto& pos = book_.get_position(direction, exchange_id.c_str(), instrument_id.c_str());
            return pos;
        } catch (...) {
            return {};
        }
    }

    SimMD& sim_md() {
        return sim_md_;
    }

    void run_replay() {
        if (!strategy_) return;
        for (const auto& quote : sim_md_.get_quotes()) {
            strategy_->on_quote(*this, quote);
        }
    }

    void add_account(const std::string& source, const std::string& account) {}

    void subscribe(const std::string& exchange_id, const std::string& instrument_id) {
        subscriptions_.push_back({exchange_id, instrument_id});
    }

    uint64_t insert_order(const std::string& instrument_id, const std::string& exchange_id,
                          double price, int64_t volume, longfist::enums::Side side,
                          longfist::enums::Offset offset, longfist::enums::PriceType price_type) {
        uint64_t order_id = next_order_id_++;
        longfist::types::OrderInput input{};
        std::strncpy(input.instrument_id, instrument_id.c_str(), sizeof(input.instrument_id));
        std::strncpy(input.exchange_id, exchange_id.c_str(), sizeof(input.exchange_id));
        input.limit_price = price;
        input.volume = volume;
        input.side = side;
        input.offset = offset;
        input.price_type = price_type;
        book_.order_inputs[order_id] = input;

        longfist::types::Order order{};
        order.order_id = order_id;
        std::strncpy(order.instrument_id, instrument_id.c_str(), sizeof(order.instrument_id));
        std::strncpy(order.exchange_id, exchange_id.c_str(), sizeof(order.exchange_id));
        order.limit_price = price;
        order.volume = volume;
        order.volume_left = volume;
        order.status = longfist::enums::OrderStatus::Submitted;
        order.side = side;
        order.offset = offset;
        book_.orders[order_id] = order;

        if (strategy_) {
            strategy_->on_order(*this, order);
        }

        longfist::types::Trade trade{};
        trade.order_id = order_id;
        trade.price = price;
        trade.volume = volume;
        std::strncpy(trade.instrument_id, instrument_id.c_str(), sizeof(trade.instrument_id));
        std::strncpy(trade.exchange_id, exchange_id.c_str(), sizeof(trade.exchange_id));
        trade.side = side;
        trade.offset = offset;
        book_.trades[order_id] = trade;

        double cost = price * volume;
        if (side == longfist::enums::Side::Buy) {
            book_.asset.avail -= cost;
            book_.asset.frozen_cash += cost;
        }

        if (strategy_) {
            strategy_->on_trade(*this, trade);
        }

        order.status = longfist::enums::OrderStatus::Filled;
        order.volume_left = 0;
        book_.orders[order_id] = order;

        if (strategy_) {
            strategy_->on_order(*this, order);
        }

        return order_id;
    }

private:
    SimStrategy* strategy_ = nullptr;
    int64_t trading_day_ = 0;
    book::CommissionMap empty_commissions_;
    book::InstrumentMap empty_instruments_;
    book::Book book_;
    SimMD sim_md_;
    uint64_t next_order_id_ = 1;
    std::vector<std::pair<std::string, std::string>> subscriptions_;
};

} // namespace kungfu::wingchun::strategy

#endif // WINGCHUN_SIM_CONTEXT_H