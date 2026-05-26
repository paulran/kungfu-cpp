#include "ws_handler.h"
#include <kungfu/service/api_gateway.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <array>
#include <algorithm>

namespace kungfu::service {

namespace {

// Minimal SHA-1 for WebSocket handshake (RFC 6455 requires it)
struct SHA1 {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t count = 0;
    uint8_t buffer[64]{};

    static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void transform(const uint8_t* block) {
        uint32_t W[80];
        for (int i = 0; i < 16; i++) {
            W[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
                   (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
        }
        for (int i = 16; i < 80; i++) {
            W[i] = rotl(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = rotl(a, 5) + f + e + k + W[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    }

    void update(const uint8_t* data, size_t len) {
        size_t offset = static_cast<size_t>(count % 64);
        count += len;
        for (size_t i = 0; i < len; i++) {
            buffer[offset++] = data[i];
            if (offset == 64) { transform(buffer); offset = 0; }
        }
    }

    std::array<uint8_t, 20> finalize() {
        uint64_t bits = count * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (count % 64 != 56) update(&pad, 1);
        uint8_t len_bytes[8];
        for (int i = 0; i < 8; i++) len_bytes[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        update(len_bytes, 8);

        std::array<uint8_t, 20> digest{};
        for (int i = 0; i < 5; i++) {
            digest[i*4+0] = static_cast<uint8_t>(state[i] >> 24);
            digest[i*4+1] = static_cast<uint8_t>(state[i] >> 16);
            digest[i*4+2] = static_cast<uint8_t>(state[i] >> 8);
            digest[i*4+3] = static_cast<uint8_t>(state[i]);
        }
        return digest;
    }
};

std::string base64_encode(const uint8_t* data, size_t len) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i+1]) << 8;
        if (i + 2 < len) n |= uint32_t(data[i+2]);
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    return result;
}

std::string compute_ws_accept(const std::string& key) {
    const std::string magic = "258EAFA5-E914-47DA-95CA-5AB5DC85B3C8";
    std::string concat = key + magic;
    SHA1 sha;
    sha.update(reinterpret_cast<const uint8_t*>(concat.data()), concat.size());
    auto digest = sha.finalize();
    return base64_encode(digest.data(), digest.size());
}

} // anonymous namespace

// WsSessionImpl

WsSessionImpl::WsSessionImpl(nng_http* conn, ApiGateway* gateway)
    : conn_(conn), gateway_(gateway) {
}

WsSessionImpl::~WsSessionImpl() {
    stop();
}

void WsSessionImpl::start() {
    if (!do_handshake()) {
        nng_http_close(conn_);
        return;
    }
    active_ = true;
    read_thread_ = std::thread([this] { read_loop(); });
}

void WsSessionImpl::stop() {
    active_ = false;
    if (read_thread_.joinable()) {
        nng_http_close(conn_);
        read_thread_.join();
    }
}

void WsSessionImpl::send(const std::string& message) {
    if (!active_) return;
    std::lock_guard<std::mutex> lock(send_mutex_);
    send_frame(0x01, reinterpret_cast<const uint8_t*>(message.data()), message.size());
}

bool WsSessionImpl::do_handshake() {
    const char* ws_key = nng_http_get_header(conn_, "Sec-WebSocket-Key");
    if (!ws_key) return false;

    std::string accept = compute_ws_accept(ws_key);

    // The handshake response is built manually since we hijacked the connection
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    nng_aio* aio;
    nng_aio_alloc(&aio, nullptr, nullptr);
    nng_iov iov;
    iov.iov_buf = const_cast<char*>(response.data());
    iov.iov_len = response.size();
    nng_aio_set_iov(aio, 1, &iov);
    nng_http_write_all(conn_, aio);
    nng_aio_wait(aio);
    nng_err rv = nng_aio_result(aio);
    nng_aio_free(aio);

    return rv == 0;
}

bool WsSessionImpl::send_frame(uint8_t opcode, const uint8_t* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode); // FIN + opcode

    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.insert(frame.end(), data, data + len);

    nng_aio* aio;
    nng_aio_alloc(&aio, nullptr, nullptr);
    nng_iov iov;
    iov.iov_buf = frame.data();
    iov.iov_len = frame.size();
    nng_aio_set_iov(aio, 1, &iov);
    nng_http_write_all(conn_, aio);
    nng_aio_wait(aio);
    nng_err rv2 = nng_aio_result(aio);
    nng_aio_free(aio);
    return rv2 == 0;
}

bool WsSessionImpl::read_frame(std::vector<uint8_t>& payload, uint8_t& opcode) {
    uint8_t header[2];
    nng_aio* aio;
    nng_aio_alloc(&aio, nullptr, nullptr);

    nng_iov iov;
    iov.iov_buf = header;
    iov.iov_len = 2;
    nng_aio_set_iov(aio, 1, &iov);
    nng_http_read_all(conn_, aio);
    nng_aio_wait(aio);
    if (nng_aio_result(aio) != 0) { nng_aio_free(aio); return false; }

    opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;

    if (length == 126) {
        uint8_t ext[2];
        iov.iov_buf = ext; iov.iov_len = 2;
        nng_aio_set_iov(aio, 1, &iov);
        nng_http_read_all(conn_, aio);
        nng_aio_wait(aio);
        if (nng_aio_result(aio) != 0) { nng_aio_free(aio); return false; }
        length = (uint64_t(ext[0]) << 8) | ext[1];
    } else if (length == 127) {
        uint8_t ext[8];
        iov.iov_buf = ext; iov.iov_len = 8;
        nng_aio_set_iov(aio, 1, &iov);
        nng_http_read_all(conn_, aio);
        nng_aio_wait(aio);
        if (nng_aio_result(aio) != 0) { nng_aio_free(aio); return false; }
        length = 0;
        for (int i = 0; i < 8; i++) length = (length << 8) | ext[i];
    }

    uint8_t mask_key[4] = {};
    if (masked) {
        iov.iov_buf = mask_key; iov.iov_len = 4;
        nng_aio_set_iov(aio, 1, &iov);
        nng_http_read_all(conn_, aio);
        nng_aio_wait(aio);
        if (nng_aio_result(aio) != 0) { nng_aio_free(aio); return false; }
    }

    payload.resize(static_cast<size_t>(length));
    if (length > 0) {
        iov.iov_buf = payload.data(); iov.iov_len = static_cast<size_t>(length);
        nng_aio_set_iov(aio, 1, &iov);
        nng_http_read_all(conn_, aio);
        nng_aio_wait(aio);
        if (nng_aio_result(aio) != 0) { nng_aio_free(aio); return false; }

        if (masked) {
            for (size_t i = 0; i < payload.size(); i++) {
                payload[i] ^= mask_key[i % 4];
            }
        }
    }

    nng_aio_free(aio);
    return true;
}

void WsSessionImpl::read_loop() {
    while (active_) {
        std::vector<uint8_t> payload;
        uint8_t opcode;
        if (!read_frame(payload, opcode)) {
            active_ = false;
            break;
        }

        switch (opcode) {
            case 0x01: // text
                handle_message(std::string(payload.begin(), payload.end()));
                break;
            case 0x08: // close
                send_frame(0x08, nullptr, 0);
                active_ = false;
                break;
            case 0x09: // ping
                send_frame(0x0A, payload.data(), payload.size()); // pong
                break;
            default:
                break;
        }
    }
}

void WsSessionImpl::handle_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);
        std::string action = j.value("action", "");

        if (action == "subscribe") {
            std::string channel = j.value("channel", "");
            if (!channel.empty()) {
                subscriptions_.push_back(channel);
                nlohmann::json resp = {{"action", "subscribed"}, {"channel", channel}};
                send(resp.dump());
            }
        } else if (action == "unsubscribe") {
            std::string channel = j.value("channel", "");
            subscriptions_.erase(
                std::remove(subscriptions_.begin(), subscriptions_.end(), channel),
                subscriptions_.end());
            nlohmann::json resp = {{"action", "unsubscribed"}, {"channel", channel}};
            send(resp.dump());
        }
    } catch (...) {
        // Ignore malformed messages
    }
}

// WebSocket upgrade handler
void ws_upgrade_handler(nng_http* conn, void* arg, nng_aio* aio) {
    auto* manager = static_cast<WsManager*>(arg);

    const char* upgrade = nng_http_get_header(conn, "Upgrade");
    if (!upgrade || std::string(upgrade) != "websocket") {
        nng_http_set_status(conn, NNG_HTTP_STATUS_BAD_REQUEST, nullptr);
        nng_aio_finish(aio, NNG_OK);
        return;
    }

    // Hijack the connection from the HTTP server
    nng_http_hijack(conn);

    // The connection is now ours - create a WS session
    manager->add_session(conn);

    // Don't finish the aio - we hijacked
    nng_aio_finish(aio, NNG_OK);
}

// WsManager

WsManager::WsManager(ApiGateway* gateway) : gateway_(gateway) {}

WsManager::~WsManager() {
    stop_all();
}

void WsManager::add_session(nng_http* conn) {
    auto session = std::make_shared<WsSessionImpl>(conn, gateway_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.push_back(session);
    }
    session->start();
    spdlog::info("API: WebSocket client connected, total={}", session_count());
}

void WsManager::remove_closed_sessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](const auto& s) { return !s->is_active(); }),
        sessions_.end());
}

void WsManager::broadcast(const std::string& channel, const nlohmann::json& data) {
    nlohmann::json msg = {{"channel", channel}, {"data", data}};
    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& session : sessions_) {
        if (!session->is_active()) continue;
        for (const auto& sub : session->subscriptions()) {
            if (sub == channel || sub == "*") {
                session->send(payload);
                break;
            }
        }
    }
}

void WsManager::stop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& session : sessions_) {
        session->stop();
    }
    sessions_.clear();
}

size_t WsManager::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

} // namespace kungfu::service
