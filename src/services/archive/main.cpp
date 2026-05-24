#include <kungfu/service/archive.h>
#include <kungfu/yijinjing/io/locator.h>
#include <spdlog/spdlog.h>
#include <string>

int main(int argc, char** argv) {
    std::string home_path = argc > 1 ? argv[1] : ".";
    bool low_latency = false;
    int archive_days = 7;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--low-latency") {
            low_latency = true;
        } else if (arg == "--archive-days" && i + 1 < argc) {
            archive_days = std::stoi(argv[++i]);
        }
    }

    try {
        kungfu::yijinjing::io::Locator locator(home_path);

        auto home_loc = kungfu::yijinjing::io::location::make(
            kungfu::yijinjing::io::category::SYSTEM,
            "service",
            "archive",
            kungfu::yijinjing::io::mode::LIVE
        );

        kungfu::service::Archive archive(home_loc, locator, low_latency, archive_days);
        archive.run();
    } catch (const std::exception& e) {
        spdlog::error("Archive: fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
