#pragma once

#include <kungfu/common.h>
#include <set>
#include <string>
#include <vector>

namespace kungfu::wingchun::sim {

enum class Side { Buy = 1, Sell = 2 };

struct OrderBookLevel {
  double price = 0.0;
  int64_t qty = 0;
  int64_t order_count = 0;
};

struct Order {
  std::string secid;
  Side side;
  double price;
  int64_t qty;
};

struct Trade {
  double price = 0.0;
  int64_t qty = 0;
  Side aggressor;
};

struct MakerConfig {
  double base = 200.0;
  int32_t bound = 1000;
  int32_t samples = 1000;
  int32_t variation = 4;
  int32_t randseed = 6;
};

class OrderBook {
public:
  static constexpr double MIN_TICK = 0.01;
  static constexpr int MAX_DEPTH = 20;
  static constexpr int MAX_QTY = 100;
  static constexpr int DECIMALS = 2;

  explicit OrderBook(const std::string &security);

  ~OrderBook() = default;

  [[nodiscard]] const std::string &get_security() const { return security_; }

  [[nodiscard]] double mid() const;

  [[nodiscard]] double spread() const;

  [[nodiscard]] int depth_bids() const { return bids_.size(); }

  [[nodiscard]] int depth_offers() const { return offers_.size(); }

  [[nodiscard]] double bid_price(int at_level) const;

  [[nodiscard]] int64_t bid_qty(int at_level) const;

  [[nodiscard]] double offer_price(int at_level) const;

  [[nodiscard]] int64_t offer_qty(int at_level) const;

  [[nodiscard]] double best_bid() const;

  [[nodiscard]] double best_offer() const;

  std::vector<Trade> order(const Order &order);

  [[nodiscard]] int64_t aggregate_bid_qty(double trade_price) const;

  [[nodiscard]] int64_t aggregate_offer_qty(double trade_price) const;

  std::vector<std::pair<std::vector<Order>, double>> gen_orders(const MakerConfig &config);

private:
  struct LevelCompare {
    bool operator()(const OrderBookLevel &a, const OrderBookLevel &b) const {
      return a.price < b.price;
    }
  };

  std::set<OrderBookLevel, LevelCompare> bids_;
  std::set<OrderBookLevel, LevelCompare> offers_;
  std::string security_;
  double last_price_ = 0.0;

  std::vector<Trade> match(Side aggressor_side);

  void compact(std::set<OrderBookLevel, LevelCompare> &levels, double price);

  static double round_price(double price);

  static std::vector<Order> pad_book(const OrderBook &book, int depth, double price, Side side);
};

} // namespace kungfu::wingchun::sim