#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kungfu::common {

struct SystemConfig {
    std::string home;
    std::string log_level = "info";
    bool low_latency = false;
    uint32_t page_size = 1048576;
    int archive_days = 7;
};

struct ProcessConfig {
    std::string name;
    std::string executable;
    std::vector<std::string> args;
    std::string restart_policy = "on_failure"; // always / on_failure / never
    int max_restart_count = 3;
    int restart_window_seconds = 60;
    int restart_delay_ms = 2000;
    std::vector<std::string> depends_on;
    int priority = 5;
};

struct ApiConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    uint16_t ws_port = 8081;
    std::string jwt_secret = "kungfu-default-secret";
    int jwt_expire_hours = 24;
    std::string admin_user = "admin";
    std::string admin_password = "admin";
};

struct KungfuConfig {
    SystemConfig system;
    ApiConfig api;
    std::vector<ProcessConfig> services;

    static KungfuConfig load(const std::string& path);
};

} // namespace kungfu::common
