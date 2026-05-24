#include <kungfu/service/master.h>
#include <kungfu/common/config.h>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char** argv) {
    std::string config_path = argc > 1 ? argv[1] : "kungfu.toml";

    try {
        auto cfg = kungfu::common::KungfuConfig::load(config_path);

        spdlog::set_level(spdlog::level::from_str(cfg.system.log_level));
        spdlog::info("Master: starting with home={}", cfg.system.home);

        kungfu::yijinjing::io::Locator locator(cfg.system.home);
        kungfu::service::Master master(locator, cfg.system.low_latency);

        master.run();
    } catch (const std::exception& e) {
        spdlog::error("Master: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
