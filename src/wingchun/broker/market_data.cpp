#include <kungfu/wingchun/broker/market_data.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

void MarketDataVendor::react() {
    // In a full implementation, this would read Subscribe/Unsubscribe frames
    // from the journal and dispatch to md_service()
    // For now, the SimMD test interface drives it directly
}

} // namespace kungfu::wingchun::broker
