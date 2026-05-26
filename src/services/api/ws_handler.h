#pragma once

#include <nng/nng.h>
#include <nng/http.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace kungfu::service {

class ApiGateway;

// Standard RFC 6455 WebSocket session (for browser/external clients)
class WsSessionImpl {
public:
    WsSessionImpl(nng_http* conn, ApiGateway* gateway);
    ~WsSessionImpl();

    void start();
    void stop();
    void send(const std::string& message);

    bool is_active() const { return active_.load(); }
    const std::vector<std::string>& subscriptions() const { return subscriptions_; }

private:
    void read_loop();
    void handle_message(const std::string& msg);
    bool do_handshake();

    // WebSocket frame helpers
    bool send_frame(uint8_t opcode, const uint8_t* data, size_t len);
    bool read_frame(std::vector<uint8_t>& payload, uint8_t& opcode);

    nng_http* conn_;
    ApiGateway* gateway_;
    std::atomic<bool> active_{false};
    std::thread read_thread_;
    std::vector<std::string> subscriptions_;
    std::mutex send_mutex_;
};

// WebSocket upgrade handler - hijacks HTTP connection
void ws_upgrade_handler(nng_http* conn, void* arg, nng_aio* aio);

// Manager for all active WebSocket sessions
class WsManager {
public:
    explicit WsManager(ApiGateway* gateway);
    ~WsManager();

    void add_session(nng_http* conn);
    void remove_closed_sessions();
    void broadcast(const std::string& channel, const nlohmann::json& data);
    void stop_all();

    size_t session_count() const;

private:
    ApiGateway* gateway_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<WsSessionImpl>> sessions_;
};

} // namespace kungfu::service
