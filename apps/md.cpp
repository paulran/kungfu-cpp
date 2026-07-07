#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/broker/marketdata.h>
#include <kungfu/wingchun/sim/market_data_sim.h>
#include <kungfu/yijinjing/io.h>
#include <kungfu/yijinjing/log.h>

using namespace kungfu;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::practice;
using namespace kungfu::wingchun;
using namespace kungfu::wingchun::broker;
using namespace kungfu::wingchun::sim;

class MarketDataFactory {
public:
    static MarketData_ptr create(const std::string &type, MarketDataVendor &vendor) {
        if (type == "sim") {
            return std::make_shared<MarketDataSim>(vendor);
        }
        return nullptr;
    }
};

int main(int argc, char **argv) {
    std::string mode_str = "live";
    std::string group = "sim";
    std::string name = "sim";
    bool low_latency = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
        } else if (arg == "--group" && i + 1 < argc) {
            group = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if (arg == "--low-latency") {
            low_latency = true;
        }
    }

    mode m = mode::LIVE;
    if (mode_str == "sim") {
        m = mode::DATA;
    } else if (mode_str == "replay") {
        m = mode::REPLAY;
    }

    auto loc = std::make_shared<locator>(m);

    SPDLOG_INFO("starting md service with mode={}, group={}, name={}, low_latency={}",
                mode_str, group, name, low_latency);

    MarketDataVendor vendor(loc, group, name, low_latency);

    auto service = MarketDataFactory::create(group, vendor);
    if (!service) {
        SPDLOG_ERROR("unsupported market data type: {}", group);
        return -1;
    }
    vendor.set_service(service);

    vendor.run();

    return 0;
}