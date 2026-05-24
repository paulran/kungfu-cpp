#include <kungfu/wingchun/broker/broker.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

BrokerVendor::BrokerVendor(const yijinjing::io::location_ptr& home,
                           yijinjing::io::Locator& locator, bool low_latency)
    : apprentice(home, locator, low_latency) {}

void BrokerVendor::set_service(std::unique_ptr<BrokerService> service) {
    service_ = std::move(service);
}

void BrokerVendor::react() {
    // Base react - subclasses (MarketDataVendor, TraderVendor) override
}

void BrokerVendor::on_start() {
    if (service_) {
        service_->on_start();
        state_ = service_->get_state();
        publish_state(state_);
    }
}

void BrokerVendor::on_exit() {
    if (service_) {
        service_->on_exit();
    }
}

void BrokerVendor::publish_state(longfist::enums::BrokerState state) {
    state_ = state;
    spdlog::info("BrokerVendor: state changed to {}", static_cast<int>(state));
}

} // namespace kungfu::wingchun::broker
