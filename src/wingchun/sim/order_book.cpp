#include <kungfu/wingchun/sim/order_book.h>
#include <cmath>
#include <random>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::sim {

double OrderBook::round_price(double price) {
  double scale = std::pow(10.0, DECIMALS);
  return std::round(price * scale) / scale;
}

OrderBook::OrderBook(const std::string &security) : security_(security) {}

double OrderBook::mid() const {
  if (!bids_.empty() && !offers_.empty()) {
    return (best_bid() + best_offer()) / 2.0;
  }
  throw std::runtime_error("No bids/offers!");
}

double OrderBook::spread() const {
  if (!bids_.empty() && !offers_.empty()) {
    return best_offer() - best_bid();
  }
  return 0.0;
}

double OrderBook::bid_price(int at_level) const {
  if (at_level < 0 || at_level >= static_cast<int>(bids_.size())) {
    return 0.0;
  }
  auto it = bids_.rbegin();
  std::advance(it, at_level);
  return it->price;
}

int64_t OrderBook::bid_qty(int at_level) const {
  if (at_level < 0 || at_level >= static_cast<int>(bids_.size())) {
    return 0;
  }
  auto it = bids_.rbegin();
  std::advance(it, at_level);
  return it->qty;
}

double OrderBook::offer_price(int at_level) const {
  if (at_level < 0 || at_level >= static_cast<int>(offers_.size())) {
    return 0.0;
  }
  auto it = offers_.begin();
  std::advance(it, at_level);
  return it->price;
}

int64_t OrderBook::offer_qty(int at_level) const {
  if (at_level < 0 || at_level >= static_cast<int>(offers_.size())) {
    return 0;
  }
  auto it = offers_.begin();
  std::advance(it, at_level);
  return it->qty;
}

double OrderBook::best_bid() const {
  if (bids_.empty()) {
    return 0.0;
  }
  return bids_.rbegin()->price;
}

double OrderBook::best_offer() const {
  if (offers_.empty()) {
    return 0.0;
  }
  return offers_.begin()->price;
}

void OrderBook::compact(std::set<OrderBookLevel, LevelCompare> &levels, double price) {
  auto it = levels.find({price, 0, 0});
  if (it == levels.end()) {
    return;
  }

  auto prev_it = it;
  ++prev_it;
  while (prev_it != levels.end() && prev_it->price == price) {
    OrderBookLevel merged = *it;
    merged.qty += prev_it->qty;
    merged.order_count += prev_it->order_count;
    levels.erase(it);
    it = levels.insert(merged).first;
    prev_it = levels.erase(prev_it);
  }
}

std::vector<Trade> OrderBook::match(Side aggressor_side) {
  std::vector<Trade> trades;

  if (aggressor_side == Side::Buy) {
    while (!offers_.empty() && !bids_.empty()) {
      auto &offer = *offers_.begin();
      auto &bid = *bids_.rbegin();

      if (bid.price >= offer.price) {
        int64_t trade_qty = std::min(bid.qty, offer.qty);
        Trade trade{offer.price, trade_qty, Side::Buy};
        last_price_ = offer.price;
        trades.push_back(trade);

        if (bid.qty == trade_qty) {
          bids_.erase(std::prev(bids_.end()));
        } else {
          OrderBookLevel updated_bid = bid;
          updated_bid.qty -= trade_qty;
          bids_.erase(std::prev(bids_.end()));
          bids_.insert(updated_bid);
        }

        if (offer.qty == trade_qty) {
          offers_.erase(offers_.begin());
        } else {
          OrderBookLevel updated_offer = offer;
          updated_offer.qty -= trade_qty;
          offers_.erase(offers_.begin());
          offers_.insert(updated_offer);
        }
      } else {
        break;
      }
    }
  } else {
    while (!bids_.empty() && !offers_.empty()) {
      auto &bid = *bids_.rbegin();
      auto &offer = *offers_.begin();

      if (bid.price >= offer.price) {
        int64_t trade_qty = std::min(bid.qty, offer.qty);
        Trade trade{offer.price, trade_qty, Side::Sell};
        last_price_ = offer.price;
        trades.push_back(trade);

        if (bid.qty == trade_qty) {
          bids_.erase(std::prev(bids_.end()));
        } else {
          OrderBookLevel updated_bid = bid;
          updated_bid.qty -= trade_qty;
          bids_.erase(std::prev(bids_.end()));
          bids_.insert(updated_bid);
        }

        if (offer.qty == trade_qty) {
          offers_.erase(offers_.begin());
        } else {
          OrderBookLevel updated_offer = offer;
          updated_offer.qty -= trade_qty;
          offers_.erase(offers_.begin());
          offers_.insert(updated_offer);
        }
      } else {
        break;
      }
    }
  }

  return trades;
}

std::vector<Trade> OrderBook::order(const Order &order) {
  if (security_ != order.secid) {
    throw std::runtime_error(fmt::format("Cannot place order for security {} on book[{}]", order.secid, security_));
  }

  auto &levels = (order.side == Side::Buy) ? bids_ : offers_;
  OrderBookLevel new_level{round_price(order.price), order.qty, 1};

  auto [it, inserted] = levels.insert(new_level);
  if (!inserted) {
    OrderBookLevel merged = *it;
    merged.qty += order.qty;
    merged.order_count += 1;
    levels.erase(it);
    levels.insert(merged);
  }

  if (levels.size() > MAX_DEPTH) {
    if (order.side == Side::Sell) {
      levels.erase(std::prev(levels.end()));
    } else {
      levels.erase(levels.begin());
    }
  }

  return match(order.side);
}

int64_t OrderBook::aggregate_bid_qty(double trade_price) const {
  int64_t qty = 0;
  for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
    if (it->price >= trade_price) {
      qty += it->qty;
    } else {
      break;
    }
  }
  return qty;
}

int64_t OrderBook::aggregate_offer_qty(double trade_price) const {
  int64_t qty = 0;
  for (const auto &offer : offers_) {
    if (offer.price <= trade_price) {
      qty += offer.qty;
    } else {
      break;
    }
  }
  return qty;
}

std::vector<Order> OrderBook::pad_book(const OrderBook &book, int depth, double price, Side side) {
  std::vector<Order> orders;
  if (depth >= MAX_DEPTH) {
    return orders;
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(1, 10);
  std::uniform_int_distribution<> offset_dis(1, 3);

  double sign = (side == Side::Buy) ? -1.0 : 1.0;
  int num = MAX_DEPTH - depth;
  double best = price;
  int offset = 1;

  for (int i = 0; i < num; ++i) {
    orders.push_back({
        book.security_,
        side,
        round_price(best + sign * offset * MIN_TICK),
        dis(gen),
    });
    offset += offset_dis(gen);
  }

  return orders;
}

std::vector<std::pair<std::vector<Order>, double>> OrderBook::gen_orders(const MakerConfig &config) {
  std::vector<std::pair<std::vector<Order>, double>> result;

  std::random_device rd;
  std::mt19937 gen(config.randseed != 0 ? config.randseed : rd());
  std::uniform_int_distribution<> bound_dis(1 - config.bound, config.bound - 1);
  std::uniform_int_distribution<> tick_dis(1, 10);

  double upper_bound = config.base + config.bound * MIN_TICK;
  double lower_bound = config.base - config.bound * MIN_TICK;
  double mid_price = config.base + bound_dis(gen) * MIN_TICK;
  double direction = 1.0;

  for (int i = 0; i < config.samples; ++i) {
    if (i % config.variation == 0) {
      if (mid_price >= upper_bound) {
        direction = -1.0;
      } else if (mid_price <= lower_bound) {
        direction = 1.0;
      } else {
        direction = (std::uniform_int_distribution<>(0, 1)(gen) == 0) ? -1.0 : 1.0;
      }
    }

    mid_price += direction * tick_dis(gen) * MIN_TICK;

    std::vector<Order> orders;
    double sell_price = mid_price + MIN_TICK;
    double buy_price = mid_price - MIN_TICK;

    if (direction < 0) {
      int64_t qty = aggregate_bid_qty(sell_price);
      orders.push_back({security_, Side::Sell, round_price(sell_price), qty + 1});
      orders.push_back({security_, Side::Buy, round_price(buy_price), 1});
    } else {
      int64_t qty = aggregate_offer_qty(buy_price);
      orders.push_back({security_, Side::Buy, round_price(buy_price), qty + 1});
      orders.push_back({security_, Side::Sell, round_price(sell_price), 1});
    }

    orders.insert(orders.end(), pad_book(*this, depth_bids(), buy_price, Side::Buy).begin(),
                  pad_book(*this, depth_bids(), buy_price, Side::Buy).end());
    orders.insert(orders.end(), pad_book(*this, depth_offers(), sell_price, Side::Sell).begin(),
                  pad_book(*this, depth_offers(), sell_price, Side::Sell).end());

    result.emplace_back(std::move(orders), mid_price);
  }

  return result;
}

} // namespace kungfu::wingchun::sim