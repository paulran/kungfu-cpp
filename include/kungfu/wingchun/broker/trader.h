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
    void on_start() override;
    void on_active() override;
    void on_channel(uint32_t source_uid, uint32_t dest_uid) override;
    Trader* td_service() { return static_cast<Trader*>(service_.get()); }

    using apprentice::get_writer;
    using apprentice::register_location;
    using apprentice::request_read_from;
    using apprentice::request_write_to;
};

} // namespace kungfu::wingchun::broker
