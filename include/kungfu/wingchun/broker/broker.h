#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/longfist/enums.h>
#include <memory>

namespace kungfu::wingchun::broker {

class BrokerService {
public:
    virtual ~BrokerService() = default;
    virtual void on_start() = 0;
    virtual void on_exit() = 0;
    virtual longfist::enums::BrokerState get_state() const = 0;
};

class BrokerVendor : public yijinjing::practice::apprentice {
public:
    BrokerVendor(const yijinjing::io::location_ptr& home,
                 yijinjing::io::Locator& locator, bool low_latency);

    void set_service(std::unique_ptr<BrokerService> service);
    BrokerService* service() { return service_.get(); }

    void react() override;
    void on_start() override;
    void on_exit() override;

    void publish_state(longfist::enums::BrokerState state);

    longfist::enums::BrokerState state() const { return state_; }

protected:
    std::unique_ptr<BrokerService> service_;
    longfist::enums::BrokerState state_ = longfist::enums::BrokerState::Idle;
};

} // namespace kungfu::wingchun::broker
