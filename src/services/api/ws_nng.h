#pragma once

#include <nng/nng.h>
#include <nlohmann/json.hpp>
#include <string>
#include <atomic>

namespace kungfu::service {

// NNG pub/sub based WebSocket publisher for internal C++ clients
// Clients connect as sub0 sockets over ws://host:port/
class WsNngPublisher {
public:
    WsNngPublisher() = default;
    ~WsNngPublisher();

    bool start(const std::string& host, uint16_t port);
    void stop();

    // Publish message with topic prefix (NNG sub filtering uses topic prefix match)
    void publish(const std::string& topic, const nlohmann::json& data);

    bool is_active() const { return active_.load(); }

private:
    nng_socket socket_ = NNG_SOCKET_INITIALIZER;
    std::atomic<bool> active_{false};
};

} // namespace kungfu::service
