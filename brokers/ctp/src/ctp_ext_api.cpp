#include <kungfu/wingchun/extension.h>

#include "market_data_ctp.h"
#include "trader_ctp.h"

KF_EXTENSION_DEFINE_TRADER_FACTORY(kungfu::wingchun::ctp::TraderCtp)

KF_EXTENSION_DEFINE_MARKET_DATA_FACTORY(kungfu::wingchun::ctp::MarketDataCtp)
