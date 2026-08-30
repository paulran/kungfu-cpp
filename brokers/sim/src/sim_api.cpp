#include <kungfu/wingchun/extension.h>

#include "market_data_sim.h"
#include "trader_sim.h"

KF_EXTENSION_DEFINE_TRADER_FACTORY(kungfu::wingchun::sim::TraderSim)

KF_EXTENSION_DEFINE_MARKET_DATA_FACTORY(kungfu::wingchun::sim::MarketDataSim)
