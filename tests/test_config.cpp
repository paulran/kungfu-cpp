#include <gtest/gtest.h>
#include <kungfu/common/config.h>
#include <filesystem>
#include <fstream>

using namespace kungfu::common;

class ConfigTest : public ::testing::Test {
protected:
    std::string config_path;

    void SetUp() override {
        config_path = (std::filesystem::temp_directory_path() / "kf_test_config.toml").string();
    }

    void TearDown() override {
        std::filesystem::remove(config_path);
    }

    void write_config(const std::string& content) {
        std::ofstream f(config_path);
        f << content;
    }
};

TEST_F(ConfigTest, ParseSystemSection) {
    write_config(R"(
[system]
home = "/opt/kungfu"
log_level = "debug"
low_latency = true
page_size = 2097152
archive_days = 14
)");

    auto config = KungfuConfig::load(config_path);
    EXPECT_EQ(config.system.home, "/opt/kungfu");
    EXPECT_EQ(config.system.log_level, "debug");
    EXPECT_TRUE(config.system.low_latency);
    EXPECT_EQ(config.system.page_size, 2097152u);
    EXPECT_EQ(config.system.archive_days, 14);
}

TEST_F(ConfigTest, ParseServices) {
    write_config(R"(
[system]
home = "/tmp/kf"

[[services]]
name = "cached"
executable = "kf_cached"
args = ["--home", "/tmp/kf"]
restart_policy = "always"
max_restart_count = 5
restart_window_seconds = 120
priority = 1

[[services]]
name = "ledger"
executable = "kf_ledger"
args = ["--home", "/tmp/kf"]
restart_policy = "on_failure"
depends_on = ["cached"]
priority = 2
)");

    auto config = KungfuConfig::load(config_path);
    ASSERT_EQ(config.services.size(), 2u);

    EXPECT_EQ(config.services[0].name, "cached");
    EXPECT_EQ(config.services[0].executable, "kf_cached");
    ASSERT_EQ(config.services[0].args.size(), 2u);
    EXPECT_EQ(config.services[0].args[0], "--home");
    EXPECT_EQ(config.services[0].args[1], "/tmp/kf");
    EXPECT_EQ(config.services[0].restart_policy, "always");
    EXPECT_EQ(config.services[0].max_restart_count, 5);
    EXPECT_EQ(config.services[0].priority, 1);

    EXPECT_EQ(config.services[1].name, "ledger");
    ASSERT_EQ(config.services[1].depends_on.size(), 1u);
    EXPECT_EQ(config.services[1].depends_on[0], "cached");
}

TEST_F(ConfigTest, Defaults) {
    write_config(R"(
[system]
home = "/tmp/kf"
)");

    auto config = KungfuConfig::load(config_path);
    EXPECT_EQ(config.system.log_level, "info");
    EXPECT_FALSE(config.system.low_latency);
    EXPECT_EQ(config.system.page_size, 1048576u);
    EXPECT_EQ(config.system.archive_days, 7);
    EXPECT_TRUE(config.services.empty());
}

TEST_F(ConfigTest, InvalidFile) {
    EXPECT_THROW(KungfuConfig::load("/nonexistent/path.toml"), std::runtime_error);
}
