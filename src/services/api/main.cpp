#include <kungfu/service/api_gateway.h>
#include <kungfu/common/config.h>
#include <kungfu/yijinjing/io/locator.h>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char** argv) {
    std::string config_path = argc > 1 ? argv[1] : "kungfu.toml";
    bool low_latency = false;

    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--low-latency") {
            low_latency = true;
        }
    }

    try {
        auto config = kungfu::common::KungfuConfig::load(config_path);
        spdlog::info("kf_api: starting with home={} http={}:{}",
                     config.system.home, config.api.host, config.api.port);

        kungfu::yijinjing::io::Locator locator(config.system.home);

        auto home_loc = kungfu::yijinjing::io::location::make(
            kungfu::yijinjing::io::category::SYSTEM,
            "service",
            "api",
            kungfu::yijinjing::io::mode::LIVE
        );

        kungfu::service::ApiGateway gateway(home_loc, locator, config.api, low_latency);
        gateway.run();
    } catch (const std::exception& e) {
        spdlog::error("kf_api: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
