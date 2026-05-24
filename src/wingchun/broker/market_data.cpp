#include <kungfu/wingchun/broker/market_data.h>
#include <kungfu/longfist/types.h>
#include <kungfu/wingchun/gateway/sim/sim_md.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

void MarketDataVendor::on_start() {
    apprentice::on_start();
    get_writer(home_, 0);
    if (service_) {
        static_cast<MarketData*>(service_.get())->set_vendor(this);
        service_->on_start();
        state_ = service_->get_state();
        publish_state(state_);
    }
    spdlog::info("MarketDataVendor: started, uid={}", home_uid());
}

void MarketDataVendor::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            switch (event->msg_type()) {
                case longfist::types::Subscribe::tag: {
                    const auto& sub = event->data<longfist::types::Subscribe>();
                    if (md_service()) {
                        md_service()->subscribe(
                            std::string(sub.instrument_id.data),
                            std::string(sub.exchange_id.data),
                            sub.instrument_type);
                        spdlog::info("MarketDataVendor: subscribed {} @ {}",
                                     sub.instrument_id.data, sub.exchange_id.data);
                    }
                    break;
                }
                case longfist::types::Unsubscribe::tag: {
                    const auto& unsub = event->data<longfist::types::Unsubscribe>();
                    if (md_service()) {
                        md_service()->unsubscribe(
                            std::string(unsub.instrument_id.data),
                            std::string(unsub.exchange_id.data));
                    }
                    break;
                }
                default:
                    break;
            }
        },
        [](std::exception_ptr ep) {
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) {
                spdlog::error("MarketDataVendor: event error: {}", e.what());
            }
        }
    );
}

void MarketDataVendor::on_active() {
    apprentice::on_active();

    auto* sim_md = dynamic_cast<gateway::sim::SimMarketData*>(md_service());
    if (sim_md && sim_md->should_generate(now_ns())) {
        sim_md->generate_quotes(now_ns());
    }
}

} // namespace kungfu::wingchun::broker
