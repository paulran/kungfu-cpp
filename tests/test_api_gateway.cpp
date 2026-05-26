#include <gtest/gtest.h>
#include <kungfu/common/config.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/serialize.h>
#include <kungfu/service/api_gateway.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <ctime>

// Include JWT implementation directly for unit testing
#include "../src/services/api/jwt.h"

using namespace kungfu;
using namespace kungfu::service;
using namespace kungfu::longfist;

// ====================== JWT Tests ======================

class JwtTest : public ::testing::Test {};

TEST_F(JwtTest, CreateAndVerify) {
    std::string secret = "test-secret-key";
    int64_t expire = std::time(nullptr) + 3600;
    std::string token = jwt::create_token(secret, "admin", expire);

    EXPECT_FALSE(token.empty());
    EXPECT_TRUE(jwt::verify_token(secret, token));
}

TEST_F(JwtTest, WrongSecret) {
    std::string secret = "correct-secret";
    int64_t expire = std::time(nullptr) + 3600;
    std::string token = jwt::create_token(secret, "admin", expire);

    EXPECT_FALSE(jwt::verify_token("wrong-secret", token));
}

TEST_F(JwtTest, ExpiredToken) {
    std::string secret = "test-secret";
    int64_t expire = std::time(nullptr) - 100; // already expired
    std::string token = jwt::create_token(secret, "admin", expire);

    EXPECT_FALSE(jwt::verify_token(secret, token));
}

TEST_F(JwtTest, GetUsername) {
    std::string secret = "test-secret";
    int64_t expire = std::time(nullptr) + 3600;
    std::string token = jwt::create_token(secret, "testuser", expire);

    EXPECT_EQ(jwt::get_username(token), "testuser");
}

TEST_F(JwtTest, GetExpiry) {
    std::string secret = "test-secret";
    int64_t expire = std::time(nullptr) + 7200;
    std::string token = jwt::create_token(secret, "admin", expire);

    EXPECT_EQ(jwt::get_expiry(token), expire);
}

TEST_F(JwtTest, InvalidToken) {
    EXPECT_FALSE(jwt::verify_token("secret", "invalid.token"));
    EXPECT_FALSE(jwt::verify_token("secret", ""));
    EXPECT_FALSE(jwt::verify_token("secret", "no-dots-here"));
    EXPECT_FALSE(jwt::verify_token("secret", "a.b"));
}

TEST_F(JwtTest, TokenFormat) {
    std::string secret = "test";
    int64_t expire = std::time(nullptr) + 3600;
    std::string token = jwt::create_token(secret, "user", expire);

    // JWT has exactly 2 dots
    int dot_count = 0;
    for (char c : token) {
        if (c == '.') dot_count++;
    }
    EXPECT_EQ(dot_count, 2);
}

// ====================== Config Tests (API section) ======================

class ApiConfigTest : public ::testing::Test {
protected:
    std::string config_path;

    void SetUp() override {
        config_path = (std::filesystem::temp_directory_path() / "kf_test_api_config.toml").string();
    }

    void TearDown() override {
        std::filesystem::remove(config_path);
    }

    void write_config(const std::string& content) {
        std::ofstream f(config_path);
        f << content;
    }
};

TEST_F(ApiConfigTest, ParseApiSection) {
    write_config(R"(
[system]
home = "/tmp/kf"

[api]
host = "0.0.0.0"
port = 9090
ws_port = 9091
jwt_secret = "my-super-secret"
jwt_expire_hours = 48
admin_user = "root"
admin_password = "p@ssw0rd"
)");

    auto config = common::KungfuConfig::load(config_path);
    EXPECT_EQ(config.api.host, "0.0.0.0");
    EXPECT_EQ(config.api.port, 9090);
    EXPECT_EQ(config.api.ws_port, 9091);
    EXPECT_EQ(config.api.jwt_secret, "my-super-secret");
    EXPECT_EQ(config.api.jwt_expire_hours, 48);
    EXPECT_EQ(config.api.admin_user, "root");
    EXPECT_EQ(config.api.admin_password, "p@ssw0rd");
}

TEST_F(ApiConfigTest, ApiDefaults) {
    write_config(R"(
[system]
home = "/tmp/kf"
)");

    auto config = common::KungfuConfig::load(config_path);
    EXPECT_EQ(config.api.host, "127.0.0.1");
    EXPECT_EQ(config.api.port, 8080);
    EXPECT_EQ(config.api.ws_port, 8081);
    EXPECT_EQ(config.api.jwt_secret, "kungfu-default-secret");
    EXPECT_EQ(config.api.jwt_expire_hours, 24);
    EXPECT_EQ(config.api.admin_user, "admin");
    EXPECT_EQ(config.api.admin_password, "admin");
}

// ====================== Serialization Tests ======================

class ApiSerializationTest : public ::testing::Test {};

TEST_F(ApiSerializationTest, QuoteToJson) {
    types::Quote quote{};
    std::strncpy(quote.instrument_id.data, "600000", 31);
    std::strncpy(quote.exchange_id.data, "SSE", 15);
    quote.last_price = 10.5;
    quote.volume = 1000;
    quote.bid_price_0 = 10.4;
    quote.ask_price_0 = 10.6;

    auto j = to_json(quote);
    EXPECT_EQ(j["instrument_id"], "600000");
    EXPECT_EQ(j["exchange_id"], "SSE");
    EXPECT_DOUBLE_EQ(j["last_price"].get<double>(), 10.5);
    EXPECT_EQ(j["volume"].get<int64_t>(), 1000);
    EXPECT_DOUBLE_EQ(j["bid_price_0"].get<double>(), 10.4);
    EXPECT_DOUBLE_EQ(j["ask_price_0"].get<double>(), 10.6);
}

TEST_F(ApiSerializationTest, OrderInputFromJson) {
    nlohmann::json j = {
        {"order_id", 12345},
        {"instrument_id", "000001"},
        {"exchange_id", "SZSE"},
        {"limit_price", 15.5},
        {"volume", 200},
        {"side", static_cast<int>(enums::Side::Buy)},
        {"offset", static_cast<int>(enums::Offset::Open)},
        {"price_type", static_cast<int>(enums::PriceType::Limit)}
    };

    types::OrderInput input{};
    from_json(j, input);

    EXPECT_EQ(input.order_id, 12345u);
    EXPECT_STREQ(input.instrument_id.data, "000001");
    EXPECT_STREQ(input.exchange_id.data, "SZSE");
    EXPECT_DOUBLE_EQ(input.limit_price, 15.5);
    EXPECT_EQ(input.volume, 200);
    EXPECT_EQ(input.side, enums::Side::Buy);
    EXPECT_EQ(input.offset, enums::Offset::Open);
    EXPECT_EQ(input.price_type, enums::PriceType::Limit);
}

TEST_F(ApiSerializationTest, OrderToJson) {
    types::Order order{};
    order.order_id = 999;
    std::strncpy(order.instrument_id.data, "IF2312", 31);
    std::strncpy(order.exchange_id.data, "CFFEX", 15);
    order.limit_price = 4000.0;
    order.volume = 10;
    order.volume_traded = 5;
    order.volume_left = 5;
    order.status = enums::OrderStatus::PartialFilledActive;
    order.side = enums::Side::Buy;

    auto j = to_json(order);
    EXPECT_EQ(j["order_id"].get<uint64_t>(), 999u);
    EXPECT_EQ(j["instrument_id"], "IF2312");
    EXPECT_EQ(j["exchange_id"], "CFFEX");
    EXPECT_EQ(j["volume_traded"].get<int64_t>(), 5);
    EXPECT_EQ(j["status"].get<int>(), static_cast<int>(enums::OrderStatus::PartialFilledActive));
}

TEST_F(ApiSerializationTest, PositionToJson) {
    types::Position pos{};
    std::strncpy(pos.instrument_id.data, "600000", 31);
    std::strncpy(pos.exchange_id.data, "SSE", 15);
    pos.direction = enums::Direction::Long;
    pos.volume = 500;
    pos.yesterday_volume = 300;
    pos.avg_open_price = 10.2;
    pos.unrealized_pnl = 150.0;
    pos.realized_pnl = 50.0;

    auto j = to_json(pos);
    EXPECT_EQ(j["instrument_id"], "600000");
    EXPECT_EQ(j["volume"].get<int64_t>(), 500);
    EXPECT_EQ(j["direction"].get<int>(), static_cast<int>(enums::Direction::Long));
    EXPECT_DOUBLE_EQ(j["unrealized_pnl"].get<double>(), 150.0);
}

TEST_F(ApiSerializationTest, AssetToJson) {
    types::Asset asset{};
    std::strncpy(asset.account_id.data, "sim001", 31);
    asset.initial_equity = 1000000.0;
    asset.dynamic_equity = 1050000.0;
    asset.available = 800000.0;
    asset.realized_pnl = 50000.0;

    auto j = to_json(asset);
    EXPECT_EQ(j["account_id"], "sim001");
    EXPECT_DOUBLE_EQ(j["initial_equity"].get<double>(), 1000000.0);
    EXPECT_DOUBLE_EQ(j["dynamic_equity"].get<double>(), 1050000.0);
    EXPECT_DOUBLE_EQ(j["available"].get<double>(), 800000.0);
}

// ====================== API Gateway Integration Tests ======================
// These tests create a real ApiGateway instance and test the HTTP server

class ApiGatewayTest : public ::testing::Test {
protected:
    std::string home_path;

    void SetUp() override {
        home_path = (std::filesystem::temp_directory_path() / "kf_api_test").string();
        std::filesystem::create_directories(home_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(home_path);
    }
};

TEST_F(ApiGatewayTest, Authenticate) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.host = "127.0.0.1";
    config.port = 18080; // use non-standard port to avoid conflicts
    config.ws_port = 18081;
    config.admin_user = "testadmin";
    config.admin_password = "testpass";
    config.jwt_secret = "test-jwt-secret";
    config.jwt_expire_hours = 1;

    ApiGateway gateway(home_loc, locator, config, false);

    // Valid credentials
    auto result = gateway.authenticate("testadmin", "testpass");
    EXPECT_TRUE(result.contains("token"));
    EXPECT_FALSE(result.contains("error"));
    EXPECT_EQ(result["token_type"], "Bearer");

    std::string token = result["token"];
    EXPECT_TRUE(gateway.verify_token(token));

    // Invalid credentials
    auto fail_result = gateway.authenticate("wrong", "creds");
    EXPECT_TRUE(fail_result.contains("error"));
    EXPECT_FALSE(fail_result.contains("token"));
}

TEST_F(ApiGatewayTest, SystemStatusEmpty) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18082;
    config.ws_port = 18083;

    ApiGateway gateway(home_loc, locator, config, false);

    auto status = gateway.get_system_status();
    EXPECT_TRUE(status.is_array());
    EXPECT_TRUE(status.empty());
}

TEST_F(ApiGatewayTest, OrdersEmpty) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18084;
    config.ws_port = 18085;

    ApiGateway gateway(home_loc, locator, config, false);

    auto orders = gateway.get_orders();
    EXPECT_TRUE(orders.is_array());
    EXPECT_TRUE(orders.empty());
}

TEST_F(ApiGatewayTest, InstrumentsEmpty) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18086;
    config.ws_port = 18087;

    ApiGateway gateway(home_loc, locator, config, false);

    auto instruments = gateway.get_instruments();
    EXPECT_TRUE(instruments.is_array());
    EXPECT_TRUE(instruments.empty());
}

TEST_F(ApiGatewayTest, PlaceOrderNoTD) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18088;
    config.ws_port = 18089;

    ApiGateway gateway(home_loc, locator, config, false);

    nlohmann::json order_req = {
        {"instrument_id", "600000"},
        {"exchange_id", "SSE"},
        {"limit_price", 10.5},
        {"volume", 100},
        {"side", static_cast<int>(enums::Side::Buy)},
        {"offset", static_cast<int>(enums::Offset::Open)},
        {"price_type", static_cast<int>(enums::PriceType::Limit)}
    };

    auto result = gateway.place_order(order_req);
    EXPECT_TRUE(result.contains("error"));
    EXPECT_EQ(result["error"], "No TD service available");
}

TEST_F(ApiGatewayTest, CancelOrderNoTD) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18090;
    config.ws_port = 18091;

    ApiGateway gateway(home_loc, locator, config, false);

    auto result = gateway.cancel_order(12345);
    EXPECT_TRUE(result.contains("error"));
    EXPECT_EQ(result["error"], "No TD service available");
}

TEST_F(ApiGatewayTest, SubscribeNoMD) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.port = 18092;
    config.ws_port = 18093;

    ApiGateway gateway(home_loc, locator, config, false);

    nlohmann::json req = {
        {"instrument_id", "600000"},
        {"exchange_id", "SSE"}
    };

    auto result = gateway.subscribe_market(req);
    EXPECT_TRUE(result.contains("error"));
    EXPECT_EQ(result["error"], "No MD service available");
}

// ====================== HTTP Server Lifecycle Test ======================

class ApiHttpTest : public ::testing::Test {
protected:
    std::string home_path;

    void SetUp() override {
        home_path = (std::filesystem::temp_directory_path() / "kf_api_http_test").string();
        std::filesystem::create_directories(home_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(home_path);
    }
};

TEST_F(ApiHttpTest, ServerStartStop) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.host = "127.0.0.1";
    config.port = 18200;
    config.ws_port = 18201;
    config.admin_user = "admin";
    config.admin_password = "admin";
    config.jwt_secret = "lifecycle-test-secret";

    ApiGateway gateway(home_loc, locator, config, false);
    gateway.on_start();

    // Verify the gateway is functional after start
    auto status = gateway.get_system_status();
    EXPECT_TRUE(status.is_array());

    auto auth_result = gateway.authenticate("admin", "admin");
    EXPECT_TRUE(auth_result.contains("token"));

    gateway.on_exit();
}

TEST_F(ApiHttpTest, MultipleEndpointsAfterAuth) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.host = "127.0.0.1";
    config.port = 18202;
    config.ws_port = 18203;
    config.admin_user = "admin";
    config.admin_password = "admin";
    config.jwt_secret = "multi-endpoint-secret";

    ApiGateway gateway(home_loc, locator, config, false);

    // Authenticate
    auto auth = gateway.authenticate("admin", "admin");
    EXPECT_TRUE(auth.contains("token"));
    std::string token = auth["token"];
    EXPECT_TRUE(gateway.verify_token(token));

    // Test all query endpoints return valid JSON
    EXPECT_TRUE(gateway.get_system_status().is_array());
    EXPECT_TRUE(gateway.get_orders().is_array());
    EXPECT_TRUE(gateway.get_instruments().is_array());
    EXPECT_TRUE(gateway.get_strategies().is_array());
    EXPECT_TRUE(gateway.get_accounts().is_array());
}

TEST_F(ApiHttpTest, TokenLifecycle) {
    yijinjing::io::Locator locator(home_path);
    auto home_loc = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "service", "api", yijinjing::io::mode::LIVE);

    common::ApiConfig config;
    config.host = "127.0.0.1";
    config.port = 18204;
    config.ws_port = 18205;
    config.admin_user = "admin";
    config.admin_password = "secret123";
    config.jwt_secret = "token-lifecycle-secret";
    config.jwt_expire_hours = 1;

    ApiGateway gateway(home_loc, locator, config, false);

    // Failed auth
    auto fail = gateway.authenticate("admin", "wrong");
    EXPECT_TRUE(fail.contains("error"));
    EXPECT_FALSE(fail.contains("token"));

    // Successful auth
    auto success = gateway.authenticate("admin", "secret123");
    EXPECT_TRUE(success.contains("token"));
    EXPECT_EQ(success["token_type"], "Bearer");

    std::string token = success["token"];
    EXPECT_TRUE(gateway.verify_token(token));
    EXPECT_FALSE(gateway.verify_token("garbage-token"));
    EXPECT_FALSE(gateway.verify_token(""));
}
