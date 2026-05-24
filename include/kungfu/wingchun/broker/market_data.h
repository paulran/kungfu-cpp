#pragma once

#include <kungfu/wingchun/broker/broker.h>
#include <kungfu/longfist/types.h>
#include <string>

namespace kungfu::wingchun::broker {

class MarketData : public BrokerService {
public:
    ~MarketData() override = default;

    virtual bool subscribe(const std::string& instrument_id,
                          const std::string& exchange_id,
                          longfist::enums::InstrumentType type) = 0;
    virtual bool unsubscribe(const std::string& instrument_id,
                            const std::string& exchange_id) = 0;
    virtual bool subscribe_all(const std::string& exchange_id) { return false; }

    void set_vendor(BrokerVendor* vendor) { vendor_ = vendor; }
    BrokerVendor* vendor() { return vendor_; }

protected:
    BrokerVendor* vendor_ = nullptr;
};

class MarketDataVendor : public BrokerVendor {
public:
    using BrokerVendor::BrokerVendor;
    void react() override;
    MarketData* md_service() { return static_cast<MarketData*>(service_.get()); }
};

} // namespace kungfu::wingchun::broker
