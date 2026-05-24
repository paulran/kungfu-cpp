#pragma once

#include <kungfu/common/config.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace kungfu::service {

enum class ProcessState : uint8_t {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
    Exited
};

enum class RestartPolicy : uint8_t {
    Always,
    OnFailure,
    Never
};

struct ProcessInfo {
    common::ProcessConfig config;
    ProcessState state = ProcessState::Stopped;
    int64_t pid = -1;
    int restart_count = 0;
    int64_t start_time = 0;
    int64_t last_exit_time = 0;
    int last_exit_code = 0;
#ifdef KUNGFU_WIN32
    void* process_handle = nullptr;
#endif
};

class Supervisor {
public:
    explicit Supervisor(std::vector<common::ProcessConfig> configs);

    void start_all();
    void stop_all(int timeout_ms = 5000);
    void start_process(const std::string& name);
    void stop_process(const std::string& name, int timeout_ms = 5000);

    void monitor(); // non-blocking check, call periodically

    ProcessInfo get_info(const std::string& name) const;
    std::vector<ProcessInfo> get_all_info() const;

private:
    int64_t spawn(const common::ProcessConfig& cfg);
    void kill_process(ProcessInfo& info, bool force);
    bool is_alive(const ProcessInfo& info) const;
    bool should_restart(const ProcessInfo& info) const;
    std::vector<std::string> topological_sort() const;
    RestartPolicy parse_policy(const std::string& policy) const;
    int64_t now_ms() const;

    std::vector<common::ProcessConfig> configs_;
    std::unordered_map<std::string, ProcessInfo> processes_;
};

} // namespace kungfu::service
