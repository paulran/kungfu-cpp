#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/broker/trader.h>
#include <kungfu/wingchun/sim/trader_sim.h>
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

class TraderFactory {
public:
    static Trader_ptr create(const std::string &type, TraderVendor &vendor) {
        if (type == "sim") {
            return std::make_shared<TraderSim>(vendor);
        }
        return nullptr;
    }
};

int main(int argc, char **argv) {
    std::string mode_str = "live";
    std::string group = "sim";
    std::string name = "sim";
    std::string source = "sim";
    bool low_latency = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
        } else if (arg == "--group" && i + 1 < argc) {
            group = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if (arg == "--source" && i + 1 < argc) {
            source = argv[++i];
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

    SPDLOG_INFO("starting td service with mode={}, group={}, name={}, source={}, low_latency={}",
                mode_str, group, name, source, low_latency);

    TraderVendor vendor(loc, group, name, low_latency);

    auto trader = TraderFactory::create(source, vendor);
    if (!trader) {
        SPDLOG_ERROR("unsupported trader type: {}", source);
        return -1;
    }
    vendor.set_service(trader);

    vendor.run();

    return 0;
}