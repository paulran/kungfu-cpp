#include <kungfu/service/archive.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>

namespace kungfu::service {

Archive::Archive(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator,
                 bool low_latency, int archive_days)
    : apprentice(home, locator, low_latency), archive_days_(archive_days) {
}

void Archive::react() {
    events_.subscribe(
        lifetime_,
        [](yijinjing::practice::event_ptr) {
            // Archive does not process individual events;
            // it only performs periodic cleanup in on_active()
        },
        [](std::exception_ptr ep) {
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                spdlog::error("Archive: event error: {}", e.what());
            }
        }
    );
}

void Archive::on_active() {
    int64_t current = now_ns() / 1000000; // ms
    constexpr int64_t cleanup_interval_ms = 3600 * 1000; // 1 hour

    if ((current - last_cleanup_time_) >= cleanup_interval_ms) {
        cleanup_old_journals();
        last_cleanup_time_ = current;
    }
}

void Archive::cleanup_old_journals() {
    namespace fs = std::filesystem;

    std::string journal_dir = locator_.root() + "/journal";

    if (!fs::exists(journal_dir)) {
        spdlog::debug("Archive: journal directory does not exist: {}", journal_dir);
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(archive_days_ * 24);
    auto cutoff_time = std::chrono::system_clock::to_time_t(cutoff);

    int removed_count = 0;
    std::error_code ec;

    for (auto it = fs::recursive_directory_iterator(journal_dir, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            spdlog::warn("Archive: directory iteration error: {}", ec.message());
            break;
        }

        if (!it->is_regular_file()) continue;

        auto path = it->path();
        if (path.extension() != ".journal") continue;

        auto file_time = fs::last_write_time(path, ec);
        if (ec) continue;

        // Convert file_time to system_clock time_point
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        auto file_time_t = std::chrono::system_clock::to_time_t(sctp);

        if (file_time_t < cutoff_time) {
            fs::remove(path, ec);
            if (!ec) {
                removed_count++;
                spdlog::debug("Archive: removed old journal: {}", path.string());
            } else {
                spdlog::warn("Archive: failed to remove {}: {}", path.string(), ec.message());
            }
        }
    }

    if (removed_count > 0) {
        spdlog::info("Archive: cleaned up {} old journal files", removed_count);
    }
}

} // namespace kungfu::service
