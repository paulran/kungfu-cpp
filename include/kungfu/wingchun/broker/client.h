#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <unordered_map>
#include <atomic>
#include <string>
#include <cstdint>

namespace kungfu::wingchun::broker {

class BrokerClient {
public:
    explicit BrokerClient(yijinjing::practice::apprentice& host);

    void subscribe(uint32_t md_uid, const std::string& instrument_id,
                  const std::string& exchange_id,
                  longfist::enums::InstrumentType type);
    void unsubscribe(uint32_t md_uid, const std::string& instrument_id,
                    const std::string& exchange_id);

    uint64_t insert_order(uint32_t td_uid,
                         const std::string& instrument_id,
                         const std::string& exchange_id,
                         double price, int64_t volume,
                         longfist::enums::Side side,
                         longfist::enums::Offset offset,
                         longfist::enums::PriceType price_type);
    void cancel_order(uint32_t td_uid, uint64_t order_id);

    void on_broker_state(uint32_t source_uid, longfist::enums::BrokerState state);
    longfist::enums::BrokerState get_state(uint32_t uid) const;

    void add_md(uint32_t md_uid);
    void add_td(uint32_t td_uid);

    uint32_t default_md_uid() const { return default_md_uid_; }
    uint32_t default_td_uid() const { return default_td_uid_; }

private:
    yijinjing::practice::apprentice& host_;
    std::unordered_map<uint32_t, longfist::enums::BrokerState> broker_states_;
    std::atomic<uint64_t> next_order_id_{1};
    uint32_t default_md_uid_ = 0;
    uint32_t default_td_uid_ = 0;
};

} // namespace kungfu::wingchun::broker
