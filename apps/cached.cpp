#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/yijinjing/cache/cached.h>
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
using namespace kungfu::yijinjing::cache;

int main(int argc, char **argv) {
    std::string mode_str = "live";
    std::string group = "service";
    std::string name = "cached";
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

    SPDLOG_INFO("starting cached service with mode={}, group={}, name={}, low_latency={}",
                mode_str, group, name, low_latency);

    cached service(loc, m, low_latency);

    service.run();

    return 0;
}