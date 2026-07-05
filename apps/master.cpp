#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/yijinjing/practice/master.h>
#include <kungfu/yijinjing/practice/hero.h>
#include <kungfu/yijinjing/time.h>

using namespace kungfu;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::practice;

class master_app : public master {
public:
    explicit master_app(location_ptr home, bool low_latency = false) : master(home, low_latency) {}

    void on_register(const event_ptr &event, const Register &register_data) override {
        SPDLOG_INFO("app registered: {}", register_data.name);
    }

    void on_interval_check(int64_t nanotime) override {
        SPDLOG_TRACE("interval check at {}", time::strftime(nanotime));
    }

    int64_t acquire_trading_day() override {
        return time::now_in_nano();
    }
};

int main(int argc, char **argv) {
    std::string mode_str = "live";
    bool low_latency = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
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
    auto home = location::make_shared(m, category::SYSTEM, "master", "master", loc);

    SPDLOG_INFO("starting master with mode={}, low_latency={}", mode_str, low_latency);
    SPDLOG_INFO("master home: {}", home->uname);

    master_app app(home, low_latency);
    app.run();

    return 0;
}