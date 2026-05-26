#include "ws_nng.h"
#include <nng/nng.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace kungfu::service {

WsNngPublisher::~WsNngPublisher() {
    stop();
}

bool WsNngPublisher::start(const std::string& host, uint16_t port) {
    int rv = nng_pub0_open(&socket_);
    if (rv != 0) {
        spdlog::error("API: failed to open pub socket: {}", nng_strerror(static_cast<nng_err>(rv)));
        return false;
    }

    std::string url = "ws://" + host + ":" + std::to_string(port) + "/";
    rv = nng_listen(socket_, url.c_str(), nullptr, 0);
    if (rv != 0) {
        spdlog::error("API: failed to listen on {}: {}", url, nng_strerror(static_cast<nng_err>(rv)));
        nng_socket_close(socket_);
        return false;
    }

    active_ = true;
    spdlog::info("API: NNG pub/sub WebSocket listening on {}", url);
    return true;
}

void WsNngPublisher::stop() {
    if (active_.exchange(false)) {
        nng_socket_close(socket_);
    }
}

void WsNngPublisher::publish(const std::string& topic, const nlohmann::json& data) {
    if (!active_) return;

    // NNG sub topic filtering: prefix the message with "topic\0"
    nlohmann::json msg = {{"channel", topic}, {"data", data}};
    std::string payload = topic + '\0' + msg.dump();

    int rv = nng_send(socket_, const_cast<char*>(payload.data()), payload.size(), NNG_FLAG_NONBLOCK);
    if (rv != 0 && rv != static_cast<int>(NNG_EAGAIN)) {
        spdlog::warn("API: nng pub send failed: {}", nng_strerror(static_cast<nng_err>(rv)));
    }
}

} // namespace kungfu::service
