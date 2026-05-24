#include <kungfu/service/ledger.h>
#include <kungfu/longfist/types.h>
#include <spdlog/spdlog.h>

namespace kungfu::service {

Ledger::Ledger(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator, bool low_latency)
    : apprentice(home, locator, low_latency) {
}

void Ledger::on_start() {
    apprentice::on_start();
    spdlog::info("Ledger: service started, uid={}", home_uid());
}

void Ledger::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            switch (event->msg_type()) {
                case longfist::types::Trade::tag: {
                    const auto& trade = event->data<longfist::types::Trade>();
                    uint32_t book_uid = event->source();
                    book_keeper_.on_trade(trade, book_uid);

                    // After trade, write updated position and asset to PUBLIC writer
                    auto& book = book_keeper_.get_book(book_uid);
                    auto public_writer = get_writer(home_, 0); // PUBLIC

                    // Find the affected position
                    std::string key = std::string(trade.instrument_id.data) + "@" +
                                      std::string(trade.exchange_id.data);

                    if (trade.side == longfist::enums::Side::Buy) {
                        auto it = book.long_positions.find(key);
                        if (it != book.long_positions.end()) {
                            public_writer->write(now_ns(), it->second);
                        }
                    } else {
                        auto it = book.short_positions.find(key);
                        if (it != book.short_positions.end()) {
                            public_writer->write(now_ns(), it->second);
                        }
                    }

                    // Write updated asset
                    public_writer->write(now_ns(), book.asset);
                    break;
                }
                case longfist::types::Order::tag: {
                    const auto& order = event->data<longfist::types::Order>();
                    uint32_t book_uid = event->source();
                    book_keeper_.on_order(order, book_uid);
                    break;
                }
                case longfist::types::Position::tag: {
                    const auto& pos = event->data<longfist::types::Position>();
                    uint32_t book_uid = event->source();
                    book_keeper_.on_position(pos, book_uid);
                    break;
                }
                case longfist::types::Asset::tag: {
                    const auto& asset = event->data<longfist::types::Asset>();
                    uint32_t book_uid = event->source();
                    book_keeper_.on_asset(asset, book_uid);
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
                spdlog::error("Ledger: event error: {}", e.what());
            }
        }
    );
}

} // namespace kungfu::service
