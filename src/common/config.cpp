#include <kungfu/common/config.h>
#include <toml++/toml.hpp>
#include <stdexcept>

namespace kungfu::common {

KungfuConfig KungfuConfig::load(const std::string& path) {
    KungfuConfig config;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        throw std::runtime_error(std::string("Failed to parse config: ") + err.what());
    }

    // Parse [system] section
    if (auto sys = tbl["system"].as_table()) {
        if (auto v = (*sys)["home"].value<std::string>()) config.system.home = *v;
        if (auto v = (*sys)["log_level"].value<std::string>()) config.system.log_level = *v;
        if (auto v = (*sys)["low_latency"].value<bool>()) config.system.low_latency = *v;
        if (auto v = (*sys)["page_size"].value<int64_t>()) config.system.page_size = static_cast<uint32_t>(*v);
        if (auto v = (*sys)["archive_days"].value<int64_t>()) config.system.archive_days = static_cast<int>(*v);
    }

    // Parse [[services]] array
    if (auto services = tbl["services"].as_array()) {
        for (const auto& elem : *services) {
            if (auto svc = elem.as_table()) {
                ProcessConfig pc;
                if (auto v = (*svc)["name"].value<std::string>()) pc.name = *v;
                if (auto v = (*svc)["executable"].value<std::string>()) pc.executable = *v;
                if (auto v = (*svc)["restart_policy"].value<std::string>()) pc.restart_policy = *v;
                if (auto v = (*svc)["max_restart_count"].value<int64_t>()) pc.max_restart_count = static_cast<int>(*v);
                if (auto v = (*svc)["restart_window_seconds"].value<int64_t>()) pc.restart_window_seconds = static_cast<int>(*v);
                if (auto v = (*svc)["restart_delay_ms"].value<int64_t>()) pc.restart_delay_ms = static_cast<int>(*v);
                if (auto v = (*svc)["priority"].value<int64_t>()) pc.priority = static_cast<int>(*v);

                if (auto args_arr = (*svc)["args"].as_array()) {
                    for (const auto& arg : *args_arr) {
                        if (auto s = arg.value<std::string>()) {
                            pc.args.push_back(*s);
                        }
                    }
                }

                if (auto deps_arr = (*svc)["depends_on"].as_array()) {
                    for (const auto& dep : *deps_arr) {
                        if (auto s = dep.value<std::string>()) {
                            pc.depends_on.push_back(*s);
                        }
                    }
                }

                config.services.push_back(std::move(pc));
            }
        }
    }

    return config;
}

} // namespace kungfu::common
