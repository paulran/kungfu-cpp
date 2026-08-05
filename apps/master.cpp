// NOMINMAX must be defined before any header pulls in windows.h, so that
// std::min/std::max keep working alongside win32 APIs below.
#ifdef _WIN32
#define NOMINMAX
#endif

#include <chrono>
#include <ctime>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <string.h>
#endif

#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/yijinjing/practice/master.h>
#include <kungfu/yijinjing/practice/hero.h>
#include <kungfu/yijinjing/time.h>

using namespace kungfu;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::practice;

namespace {
// Trading day rolls over at 18:00 local time and skips weekends, mirroring
// kungfu.wingchun.calendar.Calendar.trading_day_ns. The returned value is the
// midnight (00:00:00) nanosecond timestamp of the resolved trading day.
int64_t trading_day_ns() {
  int64_t now = time::now_in_nano();
  std::time_t now_t = static_cast<std::time_t>(now / time_unit::NANOSECONDS_PER_SECOND);
  std::tm lt = *std::localtime(&now_t);
  if (lt.tm_hour >= 18) {
    lt.tm_mday += 1;
    std::mktime(&lt); // re-normalize tm_wday / tm_mday
  }
  // tm_wday: 0 = Sunday, 6 = Saturday
  while (lt.tm_wday == 0 || lt.tm_wday == 6) {
    lt.tm_mday += 1;
    std::mktime(&lt);
  }
  lt.tm_hour = 0;
  lt.tm_min = 0;
  lt.tm_sec = 0;
  std::time_t day_t = std::mktime(&lt);
  return static_cast<int64_t>(day_t) * time_unit::NANOSECONDS_PER_SECOND;
}

// Best-effort OS process liveness check, equivalent to psutil.Process.is_running().
bool process_alive(int32_t pid) {
  if (pid <= 0) {
    return false;
  }
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (h == nullptr) {
    // ERROR_INVALID_PARAMETER is returned when the pid does not exist; anything
    // else (e.g. ERROR_ACCESS_DENIED) means the process is alive but restricted.
    return GetLastError() != ERROR_INVALID_PARAMETER;
  }
  DWORD code = 0;
  bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
#else
  if (kill(pid, 0) == 0) {
    return true;
  }
  return errno != ESRCH; // EPERM: alive but no permission, ESRCH: no such process
#endif
}

// Graceful termination, equivalent to psutil.Process.terminate().
void terminate_process(int32_t pid) {
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (h == nullptr) {
    SPDLOG_ERROR("failed to open apprentice pid {} for terminate: {}", pid, GetLastError());
    return;
  }
  if (!TerminateProcess(h, 1)) {
    SPDLOG_ERROR("failed to terminate apprentice pid {}: {}", pid, GetLastError());
  }
  CloseHandle(h);
#else
  if (kill(pid, SIGTERM) != 0) {
    SPDLOG_ERROR("failed to terminate apprentice pid {}: {}", pid, strerror(errno));
  }
#endif
}

// Forceful kill, equivalent to psutil.Process.kill().
void kill_process(int32_t pid) {
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (h == nullptr) {
    SPDLOG_ERROR("failed to open apprentice pid {} for kill: {}", pid, GetLastError());
    return;
  }
  if (!TerminateProcess(h, 1)) {
    SPDLOG_ERROR("failed to kill apprentice pid {}: {}", pid, GetLastError());
  }
  CloseHandle(h);
#else
  if (kill(pid, SIGKILL) != 0) {
    SPDLOG_ERROR("failed to kill apprentice pid {}: {}", pid, strerror(errno));
  }
#endif
}

std::string uname_of(const Register &reg) {
  return fmt::format("{}/{}/{}/{}", get_category_name(reg.category), reg.group, reg.name, get_mode_name(reg.mode));
}
} // namespace

class master_app : public master {
public:
  explicit master_app(location_ptr home, bool low_latency = false) : master(home, low_latency) {
    trading_day_ = trading_day_ns();

    // Load persisted commissions from the profile db, mirroring python master.__init__.
    // The in-memory map is kept for parity with the python runtime state.
    profile commissions_profile(get_locator());
    commissions_profile.setup();
    for (const auto &commission : commissions_profile.get_all(Commission{})) {
      commissions_[commission.product_id.to_string()] = commission;
    }
    SPDLOG_INFO("loaded {} commissions from profile", commissions_.size());

    // NOTE: python applies default_commissions here, but default_commissions.apply(..., 1)
    // filters rows by mode and matches none (string "QUANT" != int 1), so it is effectively
    // a no-op. The C++ CommissionRateMode / InstrumentType enums have no QUANT/OPTION members
    // either, so applying the defaults is intentionally skipped to stay consistent with the
    // python runtime behaviour.
  }

  void on_register(const event_ptr &event, const Register &register_data) override {
    auto uname = uname_of(register_data);
    SPDLOG_INFO("app {} {} checking in", register_data.pid, uname);

    if (apprentices_.find(register_data.pid) != apprentices_.end()) {
      return;
    }
    if (!process_alive(register_data.pid)) {
      SPDLOG_ERROR("app [{}] {} checkin failed: process not running", register_data.pid, uname);
      deregister_app(event->gen_time(), register_data.location_uid);
      return;
    }
    apprentices_[register_data.pid] = apprentice_info{register_data, uname};
  }

  void on_interval_check(int64_t nanotime) override {
    try {
      health_check();
      switch_trading_day();
    } catch (const std::exception &e) {
      SPDLOG_ERROR("task error: {}", e.what());
    }
  }

  int64_t acquire_trading_day() override { return trading_day_ns(); }

  void on_exit() override {
    // base master tears down sessions and broadcasts deregisters / session-end markers
    master::on_exit();

    auto apps = get_live_processes();
    for (const auto &app : apps) {
      SPDLOG_INFO("terminating apprentice {} pid {}", app.uname, app.register_data.pid);
      terminate_process(app.register_data.pid);
    }

    const int time_to_wait = 10;
    for (int count = 0; count < time_to_wait; ++count) {
      auto remaining = get_live_processes();
      if (remaining.empty()) {
        break;
      }
      std::string names;
      for (const auto &app : remaining) {
        names += app.uname + " ";
      }
      SPDLOG_INFO("terminating apprentices, remaining [{}], count down {}s", names, time_to_wait - count);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (const auto &app : get_live_processes()) {
      SPDLOG_WARN("killing apprentice {} pid {}", app.uname, app.register_data.pid);
      kill_process(app.register_data.pid);
    }

    SPDLOG_INFO("master cleaned up");
  }

private:
  struct apprentice_info {
    Register register_data;
    std::string uname;
  };

  int64_t trading_day_ = 0;
  std::unordered_map<std::string, Commission> commissions_ = {};
  std::unordered_map<int32_t, apprentice_info> apprentices_ = {};

  bool is_live_process(int32_t pid) const {
    auto it = apprentices_.find(pid);
    if (it == apprentices_.end()) {
      return false;
    }
    return process_alive(pid);
  }

  bool is_node_process(int32_t pid) const {
    auto it = apprentices_.find(pid);
    if (it == apprentices_.end()) {
      return false;
    }
    const auto &reg = it->second.register_data;
    return reg.category == category::SYSTEM && reg.group == "node";
  }

  std::vector<apprentice_info> get_live_processes() const {
    std::vector<apprentice_info> result;
    for (const auto &[pid, app] : apprentices_) {
      if (is_node_process(pid)) {
        continue;
      }
      if (!is_live_process(pid)) {
        continue;
      }
      result.push_back(app);
    }
    return result;
  }

  // periodic task: deregister apprentices whose OS process has gone away
  void health_check() {
    auto now = time::now_in_nano();
    std::vector<int32_t> stale;
    for (const auto &[pid, app] : apprentices_) {
      if (!process_alive(pid)) {
        SPDLOG_WARN("cleaning up stale app {} with pid {}", app.uname, pid);
        stale.push_back(pid);
      }
    }
    for (int32_t pid : stale) {
      auto it = apprentices_.find(pid);
      if (it != apprentices_.end()) {
        deregister_app(now, it->second.register_data.location_uid);
        apprentices_.erase(it);
      }
    }
  }

  // periodic task: publish trading day when it advances past 18:00 / over a weekend
  void switch_trading_day() {
    int64_t current = trading_day_ns();
    if (trading_day_ < current) {
      trading_day_ = current;
      SPDLOG_INFO("trading day switched to {}", time::strftime(current, KUNGFU_TRADING_DAY_FORMAT));
      publish_trading_day();
    }
  }
};

int main(int argc, char **argv) {
    std::string mode_str = "live";
    bool low_latency = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
        } else if (arg == "--low-latency") {
            low_latency = true;
        }
    }

    mode m = mode::LIVE;
    if (mode_str == "sim") {
        m = mode::DATA;
    } else if (mode_str == "replay") {
        m = mode::REPLAY;
    }

    auto loc = std::make_shared<locator>(m);
    auto home = location::make_shared(m, category::SYSTEM, "master", "master", loc);

    SPDLOG_INFO("starting master with mode={}, low_latency={}", mode_str, low_latency);
    SPDLOG_INFO("master home: {}", home->uname);

    master_app app(home, low_latency);
    app.run();

    return 0;
}
