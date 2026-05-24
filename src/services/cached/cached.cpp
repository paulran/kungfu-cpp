#include <kungfu/service/cached.h>
#include <kungfu/longfist/types.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace kungfu::service {

Cached::Cached(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator, bool low_latency)
    : apprentice(home, locator, low_latency) {
    std::string db_path = locator.root() + "/db/cached.db";
    store_ = std::make_unique<yijinjing::cache::StateStore>(db_path);
}

void Cached::on_start() {
    spdlog::info("Cached: service started, uid={}", home_uid());
}

void Cached::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            int32_t msg_type = event->msg_type();

            switch (msg_type) {
                case longfist::types::Order::tag:
                case longfist::types::Trade::tag:
                case longfist::types::Position::tag:
                case longfist::types::Asset::tag: {
                    // Buffer the raw frame data for batch write
                    auto data_len = event->frame.data_length();
                    std::vector<char> buf(data_len);
                    std::memcpy(buf.data(), event->frame.data_address(), data_len);
                    pending_writes_.emplace_back(msg_type, std::move(buf));
                    break;
                }
                default:
                    break;
            }
        },
        [](std::exception_ptr ep) {
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                spdlog::error("Cached: event error: {}", e.what());
            }
        }
    );
}

void Cached::on_active() {
    int64_t current = now_ns() / 1000000; // ms
    if ((current - last_flush_time_) >= 1000 && !pending_writes_.empty()) {
        flush_pending();
        last_flush_time_ = current;
    }
}

void Cached::flush_pending() {
    for (const auto& [msg_type, data] : pending_writes_) {
        switch (msg_type) {
            case longfist::types::Order::tag: {
                if (data.size() >= sizeof(longfist::types::Order)) {
                    longfist::types::Order order;
                    std::memcpy(&order, data.data(), sizeof(order));
                    store_->upsert_order(order);
                }
                break;
            }
            case longfist::types::Trade::tag: {
                if (data.size() >= sizeof(longfist::types::Trade)) {
                    longfist::types::Trade trade;
                    std::memcpy(&trade, data.data(), sizeof(trade));
                    store_->insert_trade(trade);
                }
                break;
            }
            case longfist::types::Position::tag: {
                if (data.size() >= sizeof(longfist::types::Position)) {
                    longfist::types::Position pos;
                    std::memcpy(&pos, data.data(), sizeof(pos));
                    store_->upsert_position(pos);
                }
                break;
            }
            case longfist::types::Asset::tag: {
                if (data.size() >= sizeof(longfist::types::Asset)) {
                    longfist::types::Asset asset;
                    std::memcpy(&asset, data.data(), sizeof(asset));
                    store_->upsert_asset(asset);
                }
                break;
            }
            default:
                break;
        }
    }

    spdlog::debug("Cached: flushed {} records to SQLite", pending_writes_.size());
    pending_writes_.clear();
}

} // namespace kungfu::service
