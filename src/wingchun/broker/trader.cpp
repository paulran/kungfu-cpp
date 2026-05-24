#include <kungfu/wingchun/broker/trader.h>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::broker {

void TraderVendor::react() {
    // In a full implementation, this would read OrderInput/OrderAction frames
    // from the journal and dispatch to td_service()
    // For now, the SimTD test interface drives it directly
}

} // namespace kungfu::wingchun::broker
