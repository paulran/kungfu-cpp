#include <kungfu/wingchun/broker/trader.h>
#include <kungfu/wingchun/gateway/sim/sim_td.h>
#include <kungfu/longfist/types.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

void TraderVendor::on_start() {
    apprentice::on_start();
    get_writer(home_, 0);
    if (service_) {
        static_cast<Trader*>(service_.get())->set_vendor(this);
        service_->on_start();
        state_ = service_->get_state();
        publish_state(state_);
    }
    spdlog::info("TraderVendor: started, uid={}", home_uid());
}

void TraderVendor::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            switch (event->msg_type()) {
                case longfist::types::OrderInput::tag: {
                    const auto& input = event->data<longfist::types::OrderInput>();
                    spdlog::info("TraderVendor: received OrderInput id={} {} @ {}",
                                 input.order_id, input.instrument_id.data, input.exchange_id.data);
                    if (td_service()) {
                        td_service()->insert_order(input);
                    }
                    break;
                }
                case longfist::types::OrderAction::tag: {
                    const auto& action = event->data<longfist::types::OrderAction>();
                    spdlog::info("TraderVendor: received OrderAction id={}", action.order_id);
                    if (td_service()) {
                        td_service()->cancel_order(action.order_id);
                    }
                    break;
                }
                case longfist::types::Quote::tag: {
                    const auto& quote = event->data<longfist::types::Quote>();
                    auto* sim_td = dynamic_cast<gateway::sim::SimTrader*>(td_service());
                    if (sim_td) {
                        sim_td->on_quote(quote, now_ns());
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
                spdlog::error("TraderVendor: event error: {}", e.what());
            }
        }
    );
}

void TraderVendor::on_active() {
    apprentice::on_active();
}

void TraderVendor::on_channel(uint32_t source_uid, uint32_t dest_uid) {
    if (dest_uid == home_uid()) {
        auto it = locations_.find(source_uid);
        if (it != locations_.end()) {
            reader_.join(it->second, home_uid());
            spdlog::info("TraderVendor: joined reader for source_uid={} dest={}", source_uid, home_uid());
        } else {
            spdlog::warn("TraderVendor: channel source_uid={} unknown, cannot join reader", source_uid);
        }
    }
}

} // namespace kungfu::wingchun::broker
