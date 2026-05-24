#include <kungfu/service/cached.h>
#include <kungfu/yijinjing/io/locator.h>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char** argv) {
    std::string home_path = argc > 1 ? argv[1] : ".";
    bool low_latency = false;

    if (argc > 2 && std::string(argv[2]) == "--low-latency") {
        low_latency = true;
    }

    try {
        kungfu::yijinjing::io::Locator locator(home_path);

        auto home_loc = kungfu::yijinjing::io::location::make(
            kungfu::yijinjing::io::category::SYSTEM,
            "service",
            "cached",
            kungfu::yijinjing::io::mode::LIVE
        );

        kungfu::service::Cached cached(home_loc, locator, low_latency);
        cached.run();
    } catch (const std::exception& e) {
        spdlog::error("Cached: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
