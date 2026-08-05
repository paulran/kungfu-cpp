#pragma once

#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/yijinjing/journal/assemble.h>

using namespace kungfu;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;
using namespace kungfu::yijinjing::data;

int i = 0;


class KungfuStrategy101 : public Strategy {
public:
  KungfuStrategy101() = default;
  ~KungfuStrategy101() = default;

  void pre_start(Context_ptr &context) override {
    SPDLOG_INFO("preparing strategy");
    SPDLOG_INFO("arguments: {}", context->arguments());
    context->add_account("sim", "sim");
    context->subscribe("sim", {"600000"}, {"SSE"});
    // context->subscribe_operator("bar", "my-bar");
    SPDLOG_INFO("is_bypass_accounting: {}", context->is_bypass_accounting());
    //    context->bypass_accounting();
    SPDLOG_INFO("is_bypass_accounting: {}", context->is_bypass_accounting());
  }

  void post_start(Context_ptr &context) override {
    SPDLOG_INFO("strategy started");
    auto &runtime = dynamic_cast<RuntimeContext &>(*context);
    auto &bookkeeper = runtime.get_bookkeeper();
    const auto &books = bookkeeper.get_books();
    SPDLOG_INFO("books.size(): {}", books.size());
    for (const auto &book_pair : books) {
      const auto &book = book_pair.second;
      SPDLOG_INFO("book asset: {}", book->asset.to_string());
      SPDLOG_INFO("long_positions.size(): {}", book->long_positions.size());
      for (const auto &position_pair : book->long_positions) {
        auto &position = position_pair.second;
        SPDLOG_INFO("Position: {}", position.to_string());
      }
    }

    auto l_ptr = location::make_shared(mode::LIVE, category::MD, "sim", "sim", std::make_shared<locator>());
    kungfu::yijinjing::journal::assemble asb(l_ptr, location::PUBLIC, AssembleMode::All);
    auto headers = asb.read_headers(Location{});
    for (const auto &head : headers) {
      SPDLOG_INFO("head: {}", head.to_string());
    }
    kungfu::yijinjing::journal::assemble asb2(l_ptr, location::PUBLIC, AssembleMode::All);
    auto locations = asb2.read_bytes<Location>();
    SPDLOG_INFO("locations.length: {}", locations.size());
    for (const auto &loc : locations) {
      SPDLOG_INFO("locaton byte: {}", std::string(loc.second.begin(), loc.second.end()));
    }
    kungfu::yijinjing::journal::assemble asb3(l_ptr, location::PUBLIC, AssembleMode::All);
    auto l3 = asb3.read_all<Location>();
    SPDLOG_INFO("locations.length: {}", l3.size());
    for (const auto &loc : l3) {
      SPDLOG_INFO("l3 : {}", loc.to_string());
    }

    //    auto fn = [&](int i) {
    //      int count = 0;
    //      std::this_thread::sleep_for(std::chrono::seconds(1));
    //      SPDLOG_INFO("thread");
    //      while (count++ < 10000) {
    //        context->insert_order("000001", "SZE", "sim", "fill", i, i * 100, PriceType::Limit, Side::Buy,
    //        Offset::Open);
    //      }
    //    };
    //
    //    static std::vector<std::thread> threads{};
    //    for (int idx = 0; idx < 32; ++idx) {
    //      threads.push_back(std::move(std::thread(fn, idx)));
    //    }
    //
    //    for (auto &t : threads) {
    //      t.join();
    //    }
  }

  void on_quote(Context_ptr &context, const longfist::types::Quote &quote,
                const kungfu::yijinjing::data::location_ptr &location) override {
    SPDLOG_INFO("on quote: {} i {} location->uid {}", quote.last_price, i, location->location_uid);
    i++;
    // if (i == 5) {
    //   std::shared_ptr<kungfu::yijinjing::journal::assemble> p_assemble =
    //       std::make_shared<kungfu::yijinjing::journal::assemble>(std::vector<locator_ptr>{});
    //   std::shared_ptr<kungfu::yijinjing::journal::frame_reader> r = p_assemble->get_reader(location);
    //   auto f = r->current_frame();
    //   SPDLOG_INFO("f source {} dest {} data {}", f->source(), f->dest(), f->data_as_string());
    //   while (true) {
    //     auto f = r->next_frame();
    //     if (!f) {
    //       SPDLOG_INFO("f null");
    //       break;
    //     }
    //     SPDLOG_INFO("f source {} dest {} data {}", f->source(), f->dest(), f->data_as_string());
    //   }
    // }
    if (i % 10 == 0) {
      context->insert_order("600000", "SSE", "sim", "sim", quote.last_price, 100, PriceType::Limit, Side::Buy, Offset::Open);
    }
  }

  void on_broker_state_change(Context_ptr &context,
                              const longfist::types::BrokerStateUpdate &broker_state_update,
                              const kungfu::yijinjing::data::location_ptr &location) override {
    SPDLOG_INFO("on broker state changed: {}", broker_state_update.to_string());
  };

  void on_tree(Context_ptr &context, const longfist::types::Tree &tree,
               const kungfu::yijinjing::data::location_ptr &location) override {
    SPDLOG_INFO("on tree: {}", tree.to_string());
  }

  void on_order(Context_ptr &context, const longfist::types::Order &order,
                const kungfu::yijinjing::data::location_ptr &location) override {
    SPDLOG_INFO("on order: {}", order.to_string());
  }

  void on_position_sync_reset(Context_ptr &context, const kungfu::wingchun::book::Book &old_book,
                              const kungfu::wingchun::book::Book &new_book) override {
    SPDLOG_INFO("on position sync reset, long_positions.size(): {}, short_positions.size(): {}", 
      new_book.long_positions.size(), new_book.short_positions.size());
    for (const auto &position_pair : new_book.long_positions) {
      auto &position = position_pair.second;
      SPDLOG_INFO("Position: {}", position.to_string());
    }
    for (const auto &position_pair : new_book.short_positions) {
      auto &position = position_pair.second;
      SPDLOG_INFO("Position: {}", position.to_string());
    }
  }

  void on_asset_sync_reset(Context_ptr &context, const kungfu::longfist::types::Asset &old_asset,
                           const kungfu::longfist::types::Asset &new_asset) override {
    SPDLOG_INFO("on asset sync reset: {}", new_asset.to_string());
  }

  void on_asset_margin_sync_reset(Context_ptr &context,
                                  const kungfu::longfist::types::AssetMargin &old_asset_margin,
                                  const kungfu::longfist::types::AssetMargin &new_asset_margin) override {
    SPDLOG_INFO("on asset margin sync reset: {}", new_asset_margin.to_string());
  }
};
