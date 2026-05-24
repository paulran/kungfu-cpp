#include <kungfu/service/supervisor.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <queue>
#include <stdexcept>

#ifdef KUNGFU_WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#endif

namespace kungfu::service {

Supervisor::Supervisor(std::vector<common::ProcessConfig> configs)
    : configs_(std::move(configs)) {
    for (const auto& cfg : configs_) {
        ProcessInfo info;
        info.config = cfg;
        processes_[cfg.name] = info;
    }
}

int64_t Supervisor::now_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

RestartPolicy Supervisor::parse_policy(const std::string& policy) const {
    if (policy == "always") return RestartPolicy::Always;
    if (policy == "on_failure") return RestartPolicy::OnFailure;
    return RestartPolicy::Never;
}

std::vector<std::string> Supervisor::topological_sort() const {
    // Kahn's algorithm
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;

    for (const auto& cfg : configs_) {
        if (in_degree.find(cfg.name) == in_degree.end()) {
            in_degree[cfg.name] = 0;
        }
        for (const auto& dep : cfg.depends_on) {
            adjacency[dep].push_back(cfg.name);
            in_degree[cfg.name]++;
            if (in_degree.find(dep) == in_degree.end()) {
                in_degree[dep] = 0;
            }
        }
    }

    std::queue<std::string> queue;
    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) {
            queue.push(name);
        }
    }

    std::vector<std::string> sorted;
    while (!queue.empty()) {
        auto node = queue.front();
        queue.pop();
        sorted.push_back(node);

        if (adjacency.find(node) != adjacency.end()) {
            for (const auto& neighbor : adjacency[node]) {
                in_degree[neighbor]--;
                if (in_degree[neighbor] == 0) {
                    queue.push(neighbor);
                }
            }
        }
    }

    if (sorted.size() != in_degree.size()) {
        spdlog::error("Supervisor: circular dependency detected in process configs");
    }

    return sorted;
}

int64_t Supervisor::spawn(const common::ProcessConfig& cfg) {
#ifdef KUNGFU_WIN32
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Build command line
    std::wstring cmdline;
    {
        // Convert executable to wide string
        std::wstring wexe(cfg.executable.begin(), cfg.executable.end());
        cmdline = L"\"" + wexe + L"\"";
        for (const auto& arg : cfg.args) {
            std::wstring warg(arg.begin(), arg.end());
            cmdline += L" " + warg;
        }
    }

    std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        cmdline_buf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr,
        &si, &pi
    );

    if (!ok) {
        spdlog::error("Supervisor: failed to spawn process '{}', error={}", cfg.name, GetLastError());
        return -1;
    }

    CloseHandle(pi.hThread);

    // Store handle in ProcessInfo
    auto& info = processes_[cfg.name];
    info.process_handle = pi.hProcess;

    return static_cast<int64_t>(pi.dwProcessId);
#else
    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("Supervisor: fork failed for process '{}': {}", cfg.name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // Child process
        std::vector<const char*> argv;
        argv.push_back(cfg.executable.c_str());
        for (const auto& arg : cfg.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        execvp(cfg.executable.c_str(), const_cast<char* const*>(argv.data()));
        // If execvp returns, it failed
        _exit(127);
    }
    return static_cast<int64_t>(pid);
#endif
}

bool Supervisor::is_alive(const ProcessInfo& info) const {
    if (info.pid < 0) return false;

#ifdef KUNGFU_WIN32
    if (info.process_handle == nullptr) return false;
    DWORD result = WaitForSingleObject(info.process_handle, 0);
    return (result == WAIT_TIMEOUT);
#else
    int status = 0;
    pid_t result = waitpid(static_cast<pid_t>(info.pid), &status, WNOHANG);
    return (result == 0); // 0 means still running
#endif
}

void Supervisor::kill_process(ProcessInfo& info, bool force) {
    if (info.pid < 0) return;

#ifdef KUNGFU_WIN32
    if (info.process_handle != nullptr) {
        if (force) {
            TerminateProcess(info.process_handle, 1);
        } else {
            // Graceful: send CTRL_BREAK_EVENT to process group
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(info.pid));
        }
    }
#else
    if (force) {
        kill(static_cast<pid_t>(info.pid), SIGKILL);
    } else {
        kill(static_cast<pid_t>(info.pid), SIGTERM);
    }
#endif
}

bool Supervisor::should_restart(const ProcessInfo& info) const {
    auto policy = parse_policy(info.config.restart_policy);

    if (policy == RestartPolicy::Never) return false;
    if (policy == RestartPolicy::OnFailure && info.last_exit_code == 0) return false;

    // Check restart window
    int64_t current = const_cast<Supervisor*>(this)->now_ms();
    int64_t window_start = current - (info.config.restart_window_seconds * 1000LL);

    if (info.restart_count >= info.config.max_restart_count &&
        info.last_exit_time > window_start) {
        return false; // Exceeded max restarts within window
    }

    return true;
}

void Supervisor::start_all() {
    auto order = topological_sort();
    for (const auto& name : order) {
        if (processes_.find(name) != processes_.end()) {
            start_process(name);
        }
    }
}

void Supervisor::stop_all(int timeout_ms) {
    auto order = topological_sort();
    std::reverse(order.begin(), order.end());

    for (const auto& name : order) {
        if (processes_.find(name) != processes_.end()) {
            stop_process(name, timeout_ms);
        }
    }
}

void Supervisor::start_process(const std::string& name) {
    auto it = processes_.find(name);
    if (it == processes_.end()) {
        spdlog::error("Supervisor: unknown process '{}'", name);
        return;
    }

    auto& info = it->second;
    if (info.state == ProcessState::Running) {
        spdlog::warn("Supervisor: process '{}' already running", name);
        return;
    }

    info.state = ProcessState::Starting;
    int64_t pid = spawn(info.config);

    if (pid > 0) {
        info.pid = pid;
        info.state = ProcessState::Running;
        info.start_time = now_ms();
        spdlog::info("Supervisor: started process '{}' with pid={}", name, pid);
    } else {
        info.state = ProcessState::Failed;
        spdlog::error("Supervisor: failed to start process '{}'", name);
    }
}

void Supervisor::stop_process(const std::string& name, int timeout_ms) {
    auto it = processes_.find(name);
    if (it == processes_.end()) return;

    auto& info = it->second;
    if (info.state != ProcessState::Running) return;

    info.state = ProcessState::Stopping;

    // Graceful stop
    kill_process(info, false);

    // Wait up to timeout
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline && is_alive(info)) {
        // Spin-wait with small sleep could be added but for non-blocking we just check
#ifdef KUNGFU_WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
    }

    // Force kill if still alive
    if (is_alive(info)) {
        kill_process(info, true);
        spdlog::warn("Supervisor: force-killed process '{}'", name);
    }

#ifdef KUNGFU_WIN32
    if (info.process_handle != nullptr) {
        DWORD exit_code = 0;
        GetExitCodeProcess(info.process_handle, &exit_code);
        info.last_exit_code = static_cast<int>(exit_code);
        CloseHandle(info.process_handle);
        info.process_handle = nullptr;
    }
#else
    int status = 0;
    waitpid(static_cast<pid_t>(info.pid), &status, 0);
    if (WIFEXITED(status)) {
        info.last_exit_code = WEXITSTATUS(status);
    } else {
        info.last_exit_code = -1;
    }
#endif

    info.state = ProcessState::Stopped;
    info.last_exit_time = now_ms();
    info.pid = -1;
    spdlog::info("Supervisor: stopped process '{}'", name);
}

void Supervisor::monitor() {
    for (auto& [name, info] : processes_) {
        if (info.state != ProcessState::Running) continue;

        if (!is_alive(info)) {
            // Process has exited
#ifdef KUNGFU_WIN32
            if (info.process_handle != nullptr) {
                DWORD exit_code = 0;
                GetExitCodeProcess(info.process_handle, &exit_code);
                info.last_exit_code = static_cast<int>(exit_code);
                CloseHandle(info.process_handle);
                info.process_handle = nullptr;
            }
#else
            int status = 0;
            waitpid(static_cast<pid_t>(info.pid), &status, WNOHANG);
            if (WIFEXITED(status)) {
                info.last_exit_code = WEXITSTATUS(status);
            } else {
                info.last_exit_code = -1;
            }
#endif
            info.last_exit_time = now_ms();
            info.pid = -1;
            spdlog::warn("Supervisor: process '{}' exited with code {}", name, info.last_exit_code);

            if (should_restart(info)) {
                // Check if enough time has passed since last restart (delay)
                int64_t time_since_exit = now_ms() - info.last_exit_time;
                if (time_since_exit >= info.config.restart_delay_ms) {
                    info.restart_count++;
                    spdlog::info("Supervisor: restarting process '{}' (attempt {})", name, info.restart_count);
                    int64_t pid = spawn(info.config);
                    if (pid > 0) {
                        info.pid = pid;
                        info.state = ProcessState::Running;
                        info.start_time = now_ms();
                    } else {
                        info.state = ProcessState::Failed;
                    }
                } else {
                    info.state = ProcessState::Exited; // Will be retried next monitor call
                }
            } else {
                // Check if exceeded max restarts
                int64_t window_start = now_ms() - (info.config.restart_window_seconds * 1000LL);
                if (info.restart_count >= info.config.max_restart_count &&
                    info.last_exit_time > window_start) {
                    info.state = ProcessState::Failed;
                    spdlog::error("Supervisor: process '{}' exceeded max restarts, marking Failed", name);
                } else {
                    info.state = ProcessState::Exited;
                }
            }
        }
    }

    // Handle processes in Exited state waiting for restart delay
    for (auto& [name, info] : processes_) {
        if (info.state != ProcessState::Exited) continue;

        if (should_restart(info)) {
            int64_t time_since_exit = now_ms() - info.last_exit_time;
            if (time_since_exit >= info.config.restart_delay_ms) {
                info.restart_count++;
                spdlog::info("Supervisor: restarting process '{}' (attempt {})", name, info.restart_count);
                int64_t pid = spawn(info.config);
                if (pid > 0) {
                    info.pid = pid;
                    info.state = ProcessState::Running;
                    info.start_time = now_ms();
                } else {
                    info.state = ProcessState::Failed;
                }
            }
        }
    }
}

ProcessInfo Supervisor::get_info(const std::string& name) const {
    auto it = processes_.find(name);
    if (it == processes_.end()) {
        throw std::runtime_error("Supervisor: unknown process '" + name + "'");
    }
    return it->second;
}

std::vector<ProcessInfo> Supervisor::get_all_info() const {
    std::vector<ProcessInfo> result;
    result.reserve(processes_.size());
    for (const auto& [name, info] : processes_) {
        result.push_back(info);
    }
    return result;
}

} // namespace kungfu::service
