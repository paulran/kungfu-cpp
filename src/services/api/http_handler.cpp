#include "http_handler.h"
#include "jwt.h"
#include <kungfu/service/api_gateway.h>
#include <kungfu/longfist/serialize.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <string_view>

namespace kungfu::service {

void send_json_response(nng_http* conn, nng_aio* aio, int status, const std::string& body) {
    nng_http_set_status(conn, static_cast<nng_http_status>(status), nullptr);
    nng_http_set_header(conn, "Content-Type", "application/json");
    nng_http_set_header(conn, "Access-Control-Allow-Origin", "*");
    nng_http_set_header(conn, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    nng_http_set_header(conn, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    nng_http_copy_body(conn, body.data(), body.size());
    nng_aio_finish(aio, NNG_OK);
}

void send_error(nng_http* conn, nng_aio* aio, int status, const std::string& message) {
    nlohmann::json err = {{"error", message}, {"status", status}};
    send_json_response(conn, aio, status, err.dump());
}

std::string extract_path_param(const std::string& uri, const std::string& prefix) {
    if (uri.size() <= prefix.size()) return "";
    std::string rest = uri.substr(prefix.size());
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        return rest.substr(0, slash);
    }
    return rest;
}

std::string extract_bearer_token(nng_http* conn) {
    const char* auth = nng_http_get_header(conn, "Authorization");
    if (!auth) return "";
    std::string_view sv(auth);
    if (sv.substr(0, 7) == "Bearer ") {
        return std::string(sv.substr(7));
    }
    return "";
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

void api_http_handler(nng_http* conn, void* arg, nng_aio* aio) {
    auto* gateway = static_cast<ApiGateway*>(arg);
    const char* uri_raw = nng_http_get_uri(conn);
    const char* method_raw = nng_http_get_method(conn);

    if (!uri_raw || !method_raw) {
        send_error(conn, aio, 400, "Bad request");
        return;
    }

    std::string uri(uri_raw);
    std::string method(method_raw);

    // Handle CORS preflight
    if (method == "OPTIONS") {
        send_json_response(conn, aio, 204, "");
        return;
    }

    // Auth endpoint (no token required)
    if (uri == "/api/v1/auth/login" && method == "POST") {
        void* body_data = nullptr;
        size_t body_len = 0;
        nng_http_get_body(conn, &body_data, &body_len);

        try {
            std::string body_str(static_cast<char*>(body_data), body_len);
            auto req = nlohmann::json::parse(body_str);
            std::string username = req.value("username", "");
            std::string password = req.value("password", "");
            auto result = gateway->authenticate(username, password);
            int status = result.contains("error") ? 401 : 200;
            send_json_response(conn, aio, status, result.dump());
        } catch (const std::exception& e) {
            send_error(conn, aio, 400, "Invalid request body");
        }
        return;
    }

    // All other endpoints require JWT authentication
    std::string token = extract_bearer_token(conn);
    if (token.empty() || !gateway->verify_token(token)) {
        send_error(conn, aio, 401, "Unauthorized");
        return;
    }

    // Route dispatch
    try {
        // System endpoints
        if (uri == "/api/v1/system/status" && method == "GET") {
            auto result = gateway->get_system_status();
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        // Account endpoints
        if (uri == "/api/v1/accounts" && method == "GET") {
            auto result = gateway->get_accounts();
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        if (starts_with(uri, "/api/v1/accounts/") && method == "GET") {
            std::string account_rest = uri.substr(17); // after "/api/v1/accounts/"
            auto slash_pos = account_rest.find('/');
            if (slash_pos == std::string::npos) {
                // /api/v1/accounts/{id} - account detail (same as assets for now)
                auto result = gateway->get_account_assets(account_rest);
                send_json_response(conn, aio, 200, result.dump());
            } else {
                std::string account_id = account_rest.substr(0, slash_pos);
                std::string sub_path = account_rest.substr(slash_pos + 1);
                if (sub_path == "assets") {
                    auto result = gateway->get_account_assets(account_id);
                    send_json_response(conn, aio, 200, result.dump());
                } else if (sub_path == "positions") {
                    auto result = gateway->get_account_positions(account_id);
                    send_json_response(conn, aio, 200, result.dump());
                } else {
                    send_error(conn, aio, 404, "Not found");
                }
            }
            return;
        }

        // Order endpoints
        if (uri == "/api/v1/orders" && method == "GET") {
            auto result = gateway->get_orders();
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        if (uri == "/api/v1/orders" && method == "POST") {
            void* body_data = nullptr;
            size_t body_len = 0;
            nng_http_get_body(conn, &body_data, &body_len);
            std::string body_str(static_cast<char*>(body_data), body_len);
            auto req = nlohmann::json::parse(body_str);
            auto result = gateway->place_order(req);
            int status = result.contains("error") ? 400 : 201;
            send_json_response(conn, aio, status, result.dump());
            return;
        }

        if (starts_with(uri, "/api/v1/orders/") && method == "GET") {
            std::string id_str = extract_path_param(uri, "/api/v1/orders/");
            uint64_t order_id = std::stoull(id_str);
            auto result = gateway->get_order(order_id);
            int status = result.contains("error") ? 404 : 200;
            send_json_response(conn, aio, status, result.dump());
            return;
        }

        if (starts_with(uri, "/api/v1/orders/") && method == "DELETE") {
            std::string id_str = extract_path_param(uri, "/api/v1/orders/");
            uint64_t order_id = std::stoull(id_str);
            auto result = gateway->cancel_order(order_id);
            int status = result.contains("error") ? 400 : 200;
            send_json_response(conn, aio, status, result.dump());
            return;
        }

        // Market endpoints
        if (uri == "/api/v1/market/instruments" && method == "GET") {
            auto result = gateway->get_instruments();
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        if (uri == "/api/v1/market/subscribe" && method == "POST") {
            void* body_data = nullptr;
            size_t body_len = 0;
            nng_http_get_body(conn, &body_data, &body_len);
            std::string body_str(static_cast<char*>(body_data), body_len);
            auto req = nlohmann::json::parse(body_str);
            auto result = gateway->subscribe_market(req);
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        if (uri == "/api/v1/market/unsubscribe" && method == "POST") {
            void* body_data = nullptr;
            size_t body_len = 0;
            nng_http_get_body(conn, &body_data, &body_len);
            std::string body_str(static_cast<char*>(body_data), body_len);
            auto req = nlohmann::json::parse(body_str);
            auto result = gateway->unsubscribe_market(req);
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        // Strategy endpoints
        if (uri == "/api/v1/strategies" && method == "GET") {
            auto result = gateway->get_strategies();
            send_json_response(conn, aio, 200, result.dump());
            return;
        }

        send_error(conn, aio, 404, "Not found");

    } catch (const std::exception& e) {
        spdlog::error("API handler error: {}", e.what());
        send_error(conn, aio, 500, "Internal server error");
    }
}

} // namespace kungfu::service
