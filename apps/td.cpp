#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/broker/trader.h>
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

class TraderFactory {
public:
    static Trader_ptr create(const std::string &type, TraderVendor &vendor) {
        // Loads the extension library kf_<type>.dll / libkf_<type>.so (e.g.
        // kf_sim) and keeps it loaded for the whole process lifetime; the
        // service object is owned by the returned shared_ptr.
        auto library = plugin::Library::load("kf_" + type);
        if (!library) {
            return nullptr;
        }
        auto create_trader =
            library->symbol_as<extension::CreateTraderFn>(extension::kTraderFactorySymbol);
        auto destroy_trader =
            library->symbol_as<extension::DestroyTraderFn>(extension::kDestroyTraderSymbol);
        if (!create_trader || !destroy_trader) {
            SPDLOG_ERROR("trader factory symbols not found in extension kf_{} (expected {} / {})",
                         type, extension::kTraderFactorySymbol, extension::kDestroyTraderSymbol);
            return nullptr;
        }
        // The destroy function becomes the shared_ptr deleter so the object
        // is destroyed in the extension module that allocated it.
        return Trader_ptr(create_trader(&vendor), destroy_trader);
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