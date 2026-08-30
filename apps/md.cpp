#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/broker/marketdata.h>
#include <kungfu/wingchun/extension.h>
#include <kungfu/wingchun/plugin.h>
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

class MarketDataFactory {
public:
    static MarketData_ptr create(const std::string &type, MarketDataVendor &vendor) {
        // Loads the extension library kf_<type>.dll / libkf_<type>.so (e.g.
        // kf_sim) and keeps it loaded for the whole process lifetime; the
        // service object is owned by the returned shared_ptr.
        auto library = plugin::Library::load("kf_" + type);
        if (!library) {
            return nullptr;
        }
        auto create_market_data = library->symbol_as<extension::CreateMarketDataFn>(
            extension::kMarketDataFactorySymbol);
        auto destroy_market_data = library->symbol_as<extension::DestroyMarketDataFn>(
            extension::kDestroyMarketDataSymbol);
        if (!create_market_data || !destroy_market_data) {
            SPDLOG_ERROR("market data factory symbols not found in extension kf_{} (expected {} / {})",
                         type, extension::kMarketDataFactorySymbol, extension::kDestroyMarketDataSymbol);
            return nullptr;
        }
        // The destroy function becomes the shared_ptr deleter so the object
        // is destroyed in the extension module that allocated it.
        return MarketData_ptr(create_market_data(&vendor), destroy_market_data);
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