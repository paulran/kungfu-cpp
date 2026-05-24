#pragma once

#include <kungfu/wingchun/broker/broker.h>
#include <kungfu/longfist/types.h>
#include <string>
#include <cstdint>

namespace kungfu::wingchun::broker {

class Trader : public BrokerService {
public:
    ~Trader() override = default;

    virtual bool insert_order(const longfist::types::OrderInput& input) = 0;
    virtual bool cancel_order(uint64_t order_id) = 0;
    virtual bool req_position() = 0;
    virtual bool req_account() = 0;

    void set_vendor(BrokerVendor* vendor) { vendor_ = vendor; }
    BrokerVendor* vendor() { return vendor_; }

protected:
    BrokerVendor* vendor_ = nullptr;
};

class TraderVendor : public BrokerVendor {
public:
    using BrokerVendor::BrokerVendor;
    void react() override;
    Trader* td_service() { return static_cast<Trader*>(service_.get()); }
};

} // namespace kungfu::wingchun::broker
