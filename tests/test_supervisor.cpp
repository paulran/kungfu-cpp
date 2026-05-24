#include <gtest/gtest.h>
#include <kungfu/service/supervisor.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace kungfu::service;
using namespace kungfu::common;

class SupervisorTest : public ::testing::Test {
protected:
    std::vector<ProcessConfig> make_simple_configs() {
        ProcessConfig a;
        a.name = "service_a";
        a.executable = "echo";
        a.args = {"hello"};
        a.restart_policy = "never";

        ProcessConfig b;
        b.name = "service_b";
        b.executable = "echo";
        b.args = {"world"};
        b.restart_policy = "on_failure";
        b.depends_on = {"service_a"};

        return {a, b};
    }
};

TEST_F(SupervisorTest, Construction) {
    auto configs = make_simple_configs();
    Supervisor sv(configs);
    auto info = sv.get_info("service_a");
    EXPECT_EQ(info.state, ProcessState::Stopped);
    EXPECT_EQ(info.config.name, "service_a");
}

TEST_F(SupervisorTest, GetAllInfo) {
    auto configs = make_simple_configs();
    Supervisor sv(configs);
    auto all = sv.get_all_info();
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(SupervisorTest, UnknownProcessThrows) {
    auto configs = make_simple_configs();
    Supervisor sv(configs);
    EXPECT_THROW(sv.get_info("nonexistent"), std::runtime_error);
}

TEST_F(SupervisorTest, TopologicalSortOrder) {
    // A depends on nothing, B depends on A, C depends on B
    ProcessConfig a;
    a.name = "a";
    a.executable = "echo";
    a.restart_policy = "never";

    ProcessConfig b;
    b.name = "b";
    b.executable = "echo";
    b.restart_policy = "never";
    b.depends_on = {"a"};

    ProcessConfig c;
    c.name = "c";
    c.executable = "echo";
    c.restart_policy = "never";
    c.depends_on = {"b"};

    Supervisor sv({a, b, c});

    // start_all should start in order a, b, c
    // We can verify by starting and checking states were attempted
    sv.start_all();

    // All should have been attempted (echo exits immediately, so may be Exited/Running briefly)
    auto ia = sv.get_info("a");
    auto ib = sv.get_info("b");
    auto ic = sv.get_info("c");
    // At minimum they should not be Stopped (they were started)
    EXPECT_NE(ia.state, ProcessState::Stopped);
    EXPECT_NE(ib.state, ProcessState::Stopped);
    EXPECT_NE(ic.state, ProcessState::Stopped);

    sv.stop_all(1000);
}

TEST_F(SupervisorTest, StartProcess) {
    ProcessConfig cfg;
    cfg.name = "sleeper";
#ifdef _WIN32
    cfg.executable = "cmd.exe";
    cfg.args = {"/c", "timeout", "/t", "5", "/nobreak"};
#else
    cfg.executable = "sleep";
    cfg.args = {"5"};
#endif
    cfg.restart_policy = "never";

    Supervisor sv({cfg});
    sv.start_process("sleeper");

    auto info = sv.get_info("sleeper");
    EXPECT_EQ(info.state, ProcessState::Running);
    EXPECT_GT(info.pid, 0);

    sv.stop_process("sleeper", 2000);
    info = sv.get_info("sleeper");
    EXPECT_EQ(info.state, ProcessState::Stopped);
}

TEST_F(SupervisorTest, RestartPolicyNever) {
    ProcessConfig cfg;
    cfg.name = "fast_exit";
#ifdef _WIN32
    cfg.executable = "cmd.exe";
    cfg.args = {"/c", "exit", "1"};
#else
    cfg.executable = "false"; // exits with code 1
#endif
    cfg.restart_policy = "never";

    Supervisor sv({cfg});
    sv.start_process("fast_exit");

    // Wait for process to exit
#ifdef _WIN32
    Sleep(500);
#else
    usleep(500000);
#endif

    sv.monitor();
    auto info = sv.get_info("fast_exit");
    // Should NOT restart since policy is never
    EXPECT_NE(info.state, ProcessState::Running);
}
