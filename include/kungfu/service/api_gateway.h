#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/longfist/types.h>
#include <kungfu/common/config.h>
#include <nng/nng.h>
#include <nng/http.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>

namespace kungfu::service {

struct WsSession {
    nng_http* conn;
    std::vector<std::string> subscribed_channels;
    std::atomic<bool> active{true};
};

class ApiGateway : public yijinjing::practice::apprentice {
public:
    ApiGateway(const yijinjing::io::location_ptr& home,
               yijinjing::io::Locator& locator,
               const common::ApiConfig& config,
               bool low_latency);
    ~ApiGateway() override;

    void react() override;
    void on_start() override;
    void on_exit() override;

    // HTTP request dispatch (called by NNG handler callback)
    void handle_http_request(nng_http* conn, nng_aio* aio);

    // WebSocket session management
    void on_ws_connect(nng_http* conn);
    void broadcast_ws(const std::string& channel, const nlohmann::json& data);

    // State accessors (thread-safe)
    nlohmann::json get_system_status() const;
    nlohmann::json get_accounts() const;
    nlohmann::json get_account_assets(const std::string& account_id) const;
    nlohmann::json get_account_positions(const std::string& account_id) const;
    nlohmann::json get_orders() const;
    nlohmann::json get_order(uint64_t order_id) const;
    nlohmann::json get_instruments() const;
    nlohmann::json get_strategies() const;

    // Trading actions
    nlohmann::json place_order(const nlohmann::json& order_request);
    nlohmann::json cancel_order(uint64_t order_id);
    nlohmann::json subscribe_market(const nlohmann::json& request);
    nlohmann::json unsubscribe_market(const nlohmann::json& request);

    // Auth
    nlohmann::json authenticate(const std::string& username, const std::string& password);
    bool verify_token(const std::string& token) const;

    const common::ApiConfig& config() const { return config_; }

private:
    void start_http_server();
    void stop_http_server();
    void start_ws_nng_publisher();
    void stop_ws_nng_publisher();

    void cache_quote(const longfist::types::Quote& quote);
    void cache_order(const longfist::types::Order& order);
    void cache_trade(const longfist::types::Trade& trade);
    void cache_position(const longfist::types::Position& pos, uint32_t source_uid);
    void cache_asset(const longfist::types::Asset& asset);
    void cache_instrument(const longfist::types::Instrument& inst);
    void cache_broker_state(const longfist::types::BrokerStateUpdate& state);
    void cache_register(const longfist::types::Register& reg, const yijinjing::io::location_ptr& loc);

    common::ApiConfig config_;

    // NNG HTTP server
    nng_http_server* http_server_ = nullptr;
    nng_http_handler* api_handler_ = nullptr;
    nng_http_handler* ws_handler_ = nullptr;

    // NNG pub socket for internal WS broadcast
    nng_socket ws_pub_socket_;
    bool ws_pub_active_ = false;

    // Standard WebSocket sessions
    mutable std::mutex ws_mutex_;
    std::vector<std::shared_ptr<WsSession>> ws_sessions_;

    // State cache
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, longfist::types::Quote> quotes_;
    std::unordered_map<uint64_t, longfist::types::Order> orders_;
    std::unordered_map<uint64_t, longfist::types::Trade> trades_;
    std::unordered_map<std::string, std::vector<longfist::types::Position>> positions_; // keyed by account_id
    std::unordered_map<std::string, longfist::types::Asset> assets_; // keyed by account_id
    std::unordered_map<std::string, longfist::types::Instrument> instruments_;
    std::unordered_map<uint32_t, int32_t> broker_states_; // uid -> BrokerState
    std::unordered_map<uint32_t, yijinjing::io::location_ptr> registered_locations_;
};

} // namespace kungfu::service
