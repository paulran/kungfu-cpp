#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/yijinjing/io.h>
#include <kungfu/yijinjing/log.h>
#include "strategies/strategy101.h"

using namespace kungfu;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::practice;
using namespace kungfu::wingchun;
using namespace kungfu::wingchun::strategy;

class StrategyFactory {
public:
    static Strategy_ptr create(const std::string &type) {
        if (type == "kungfu_strategy_101") {
            return std::make_shared<KungfuStrategy101>();
        }
        return nullptr;
    }
};

int main(int argc, char **argv) {
    std::string mode_str = "live";
    std::string group = "sim";
    std::string name = "sim";
    std::string strategy_type = "kungfu_strategy_101";
    std::string arguments = "";
    bool low_latency = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
        } else if (arg == "--group" && i + 1 < argc) {
            group = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if (arg == "--strategy" && i + 1 < argc) {
            strategy_type = argv[++i];
        } else if (arg == "--args" && i + 1 < argc) {
            arguments = argv[++i];
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

    SPDLOG_INFO("starting strategy service with mode={}, group={}, name={}, strategy={}, low_latency={}, args={}",
                mode_str, group, name, strategy_type, low_latency, arguments);

    Runner runner(loc, group, name, m, low_latency, arguments);

    auto strategy = StrategyFactory::create(strategy_type);
    if (!strategy) {
        SPDLOG_ERROR("unsupported strategy type: {}", strategy_type);
        return -1;
    }
    runner.add_strategy(strategy);

    SPDLOG_INFO("runner added strategy: {}", strategy_type);

    runner.run();

    return 0;
}