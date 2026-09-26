#pragma once

#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/yijinjing/journal/assemble.h>
#include <nlohmann/json.hpp>

using namespace kungfu;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;
using namespace kungfu::yijinjing::data;

int i = 0;


class KungfuStrategy101 : public Strategy {
protected:
  std::string source_;
  std::string account_;
  std::vector<std::string> instrument_ids_;
  std::string exchange_ids_;
  int64_t volume_;

public:
  KungfuStrategy101() {
    SPDLOG_INFO("KungfuStrategy101 constructor");
   }

  ~KungfuStrategy101() = default;

  void pre_start(Context_ptr &context) override {
    SPDLOG_INFO("preparing strategy");
    std::string arguments = context->arguments();
    SPDLOG_INFO("arguments: {}", arguments);
    if (arguments.empty()) {
      SPDLOG_INFO("arguments is empty, using default values");
      arguments = R"({"source": "sim", "account": "sim", "instrument_ids": ["600000"], "exchange_ids": "SSE", "volume": 100})";
    }
    // Parse arguments as JSON
    // Example:
    // '{"source":"sim","account":"sim","instrument_ids":["600000"],"exchange_ids":"SSE","volume":100}'
    // '{"source":"ctp","account":"simnow","instrument_ids":["au2610"],"exchange_ids":"SHFE","volume":1}'
    try {
      auto arguments_j = nlohmann::json::parse(arguments);
      source_ = arguments_j["source"].get<std::string>();
      account_ = arguments_j["account"].get<std::string>();
      instrument_ids_ = arguments_j["instrument_ids"].get<std::vector<std::string>>();
      exchange_ids_ = arguments_j["exchange_ids"].get<std::string>();
      volume_ = arguments_j["volume"].get<int64_t>();
      SPDLOG_INFO("source: {}", source_);
      SPDLOG_INFO("account: {}", account_);
      SPDLOG_INFO("instrument_ids: {}, first: {}", instrument_ids_.size(), instrument_ids_[0]);
      SPDLOG_INFO("exchange_ids: {}", exchange_ids_);
      SPDLOG_INFO("volume: {}", volume_);
    } catch (const std::exception &e) {
      SPDLOG_ERROR("error parsing arguments: {}", e.what());
      throw e;
    }

    context->add_account(source_, account_);
    context->subscribe(source_, instrument_ids_, exchange_ids_);
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
    SPDLOG_INFO("headers.length: {}", headers.size());
    if (headers.size() > 0) {
      SPDLOG_INFO("last head: {}", headers.back().to_string());
    }
    // for (const auto &head : headers) {
    //   SPDLOG_INFO("head: {}", head.to_string());
    // }
    kungfu::yijinjing::journal::assemble asb2(l_ptr, location::PUBLIC, AssembleMode::All);
    auto locations = asb2.read_bytes<Location>();
    SPDLOG_INFO("locations.length: {}", locations.size());
    if (locations.size() > 0) {
      auto &last_bytes = locations.back().second;
      SPDLOG_INFO("last locaton byte: {}", std::string(last_bytes.begin(), last_bytes.end()));
    }
    // for (const auto &loc : locations) {
    //   SPDLOG_INFO("locaton byte: {}", std::string(loc.second.begin(), loc.second.end()));
    // }
    kungfu::yijinjing::journal::assemble asb3(l_ptr, location::PUBLIC, AssembleMode::All);
    auto l3 = asb3.read_all<Location>();
    SPDLOG_INFO("locations.length: {}", l3.size());
    if (l3.size() > 0) {
      SPDLOG_INFO("last l3 loc: {}", l3.back().to_string());
    }
    // for (const auto &loc : l3) {
    //   SPDLOG_INFO("l3 : {}", loc.to_string());
    // }

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
      context->insert_order(instrument_ids_[0], exchange_ids_, source_, account_, quote.last_price, volume_, PriceType::Limit, Side::Buy, Offset::Open);
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
