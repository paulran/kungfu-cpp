#include <kungfu/service/ledger.h>
#include <kungfu/yijinjing/io/locator.h>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char** argv) {
    std::string home_path = argc > 1 ? argv[1] : ".";
    bool low_latency = false;

    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--low-latency") {
            low_latency = true;
        }
    }

    try {
        spdlog::info("Ledger: starting with home={}", home_path);

        kungfu::yijinjing::io::Locator locator(home_path);

        auto home_loc = kungfu::yijinjing::io::location::make(
            kungfu::yijinjing::io::category::SYSTEM,
            "service",
            "ledger",
            kungfu::yijinjing::io::mode::LIVE
        );

        kungfu::service::Ledger ledger(home_loc, locator, low_latency);
        ledger.run();
    } catch (const std::exception& e) {
        spdlog::error("Ledger: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
