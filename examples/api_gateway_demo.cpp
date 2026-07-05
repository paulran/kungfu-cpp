// API Gateway REST/WebSocket 客户端使用示例
// 演示通过 HTTP REST + WebSocket 接入 kungfu-cpp 交易系统的完整流程：
//   1. 登录认证 (JWT)
//   2. 查询系统状态 & 账户列表
//   3. 订阅行情 (REST + WebSocket 接收)
//   4. 下单
//   5. 查询账户资产/持仓
//   6. 撤单
//
// 编译需要: nlohmann/json, nng (已在 3rdparty 中)
// 运行前提: kf_master, kf_md_sim, kf_td_sim, kf_api 均已启动
#include "nng_http_client.h"

#include <nng/nng.h>
#include <nng/http.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <stdexcept>

using json = nlohmann::json;

// ============================================================
// 简易 HTTP 客户端 (基于 NNG v2 HTTP API)
// ============================================================

// struct HttpResponse {
//     int status_code = 0;
//     std::string body;
// };

// class SimpleHttpClient {
// public:
//     explicit SimpleHttpClient(const std::string& base_url) : base_url_(base_url) {}

//     void set_token(const std::string& token) { token_ = token; }

//     HttpResponse get(const std::string& path) {
//         return request("GET", path, "");
//     }

//     HttpResponse post(const std::string& path, const json& body) {
//         return request("POST", path, body.dump());
//     }

//     HttpResponse del(const std::string& path) {
//         return request("DELETE", path, "");
//     }

// private:
//     HttpResponse request(const std::string& method, const std::string& path,
//                          const std::string& req_body) {
//         HttpResponse resp;
//         nng_http_client* client = nullptr;
//         nng_http* conn = nullptr;
//         nng_aio* aio = nullptr;
//         nng_url* url = nullptr;

//         std::string full_url = base_url_ + path;

//         nng_err rv = nng_url_parse(&url, base_url_.c_str());
//         if (rv != NNG_OK) {
//             throw std::runtime_error("Failed to parse URL: " + base_url_);
//         }

//         rv = nng_http_client_alloc(&client, url);
//         if (rv != NNG_OK) {
//             nng_url_free(url);
//             throw std::runtime_error("Failed to alloc HTTP client");
//         }

//         int irv = nng_aio_alloc(&aio, nullptr, nullptr);
//         if (irv != 0) {
//             nng_http_client_free(client);
//             nng_url_free(url);
//             throw std::runtime_error("Failed to alloc aio");
//         }
//         nng_aio_set_timeout(aio, 5000);

//         // Connect to server
//         nng_http_client_connect(client, aio);
//         nng_aio_wait(aio);
//         if (nng_aio_result(aio) != 0) {
//             nng_aio_free(aio);
//             nng_http_client_free(client);
//             nng_url_free(url);
//             resp.status_code = -1;
//             resp.body = "Connection failed";
//             return resp;
//         }

//         conn = static_cast<nng_http*>(nng_aio_get_output(aio, 0));

//         // Set request properties
//         nng_http_set_method(conn, method.c_str());
//         nng_http_set_uri(conn, path.c_str(), nullptr);
//         nng_http_set_header(conn, "Content-Type", "application/json");

//         if (!token_.empty()) {
//             std::string auth = "Bearer " + token_;
//             nng_http_set_header(conn, "Authorization", auth.c_str());
//         }

//         if (!req_body.empty()) {
//             nng_http_copy_body(conn, req_body.c_str(), req_body.size());
//         }

//         // Send request
//         nng_http_write_request(conn, aio);
//         nng_aio_wait(aio);

//         if (nng_aio_result(aio) != 0) {
//             nng_http_close(conn);
//             nng_aio_free(aio);
//             nng_http_client_free(client);
//             nng_url_free(url);
//             resp.status_code = -1;
//             resp.body = "Write request failed";
//             return resp;
//         }

//         // Read response
//         nng_http_read_response(conn, aio);
//         nng_aio_wait(aio);

//         if (nng_aio_result(aio) == 0) {
//             resp.status_code = static_cast<int>(nng_http_get_status(conn));
//             void* data = nullptr;
//             size_t len = 0;
//             nng_http_get_body(conn, &data, &len);
//             if (data && len > 0) {
//                 resp.body.assign(static_cast<char*>(data), len);
//             }
//         } else {
//             resp.status_code = -1;
//             resp.body = "Read response failed";
//         }

//         nng_http_close(conn);
//         nng_aio_free(aio);
//         nng_http_client_free(client);
//         nng_url_free(url);

//         return resp;
//     }

//     std::string base_url_;
//     std::string token_;
// };

// ============================================================
// WebSocket 订阅客户端 (基于 NNG sub0)
// 用于接收实时行情、订单、成交推送
// ============================================================

class WsSubscriber {
public:
    explicit WsSubscriber(const std::string& ws_url) : ws_url_(ws_url) {}

    ~WsSubscriber() { stop(); }

    void subscribe(const std::string& channel) {
        channels_.push_back(channel);
    }

    void start(std::function<void(const json&)> on_message) {
        on_message_ = std::move(on_message);
        running_ = true;
        thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void run() {
        nng_socket sock;
        int rv = nng_sub0_open(&sock);
        if (rv != 0) {
            spdlog::error("[WS] Failed to open sub socket: {}", nng_strerror(static_cast<nng_err>(rv)));
            return;
        }

        for (auto& ch : channels_) {
            nng_sub0_socket_subscribe(sock, ch.c_str(), ch.size());
        }

        rv = nng_dial(sock, ws_url_.c_str(), nullptr, 0);
        if (rv != 0) {
            spdlog::error("[WS] Failed to connect to {}: {}", ws_url_, nng_strerror(static_cast<nng_err>(rv)));
            nng_socket_close(sock);
            return;
        }

        spdlog::info("[WS] Connected to {}", ws_url_);

        while (running_) {
            nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, 500);
            nng_msg* msg = nullptr;
            rv = nng_recvmsg(sock, &msg, 0);
            if (rv == 0 && msg) {
                size_t len = nng_msg_len(msg);
                char* data = static_cast<char*>(nng_msg_body(msg));
                std::string payload(data, len);
                nng_msg_free(msg);

                try {
                    auto j = json::parse(payload);
                    if (on_message_) {
                        on_message_(j);
                    }
                } catch (...) {
                    spdlog::warn("[WS] Failed to parse message: {}", payload.substr(0, 100));
                }
            }
        }

        nng_socket_close(sock);
    }

    std::string ws_url_;
    std::vector<std::string> channels_;
    std::function<void(const json&)> on_message_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// ============================================================
// Demo 主流程
// ============================================================

static void print_separator(const std::string& title) {
    spdlog::info("");
    spdlog::info("========== {} ==========", title);
}

int main(int argc, char** argv) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    std::string host = "127.0.0.1";
    int port = 8080;
    int ws_port = 8081;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) ws_port = std::stoi(argv[3]);

    std::string base_url = "http://" + host + ":" + std::to_string(port);
    std::string ws_url = "ws://" + host + ":" + std::to_string(ws_port) + "/";

    spdlog::info("=== KungFu API Gateway Demo ===");
    spdlog::info("REST endpoint: {}", base_url);
    spdlog::info("WebSocket endpoint: {}", ws_url);

    NngHttpClient client;
    if (!client.connect(base_url)) {
        spdlog::info("Failed to connect to {}", base_url);
        return 1;
    }

    // -------------------------------------------------------
    // Step 1: 登录认证
    // -------------------------------------------------------
    print_separator("Step 1: 登录认证");
    HttpRequest http_request;
    http_request.path = "/api/v1/auth/login";
    http_request.body = "{\"username\":\"admin\",\"password\":\"admin\"}";
    http_request.content_type = "application/json";
    
    HttpResponse http_response;
    if (client.post(http_request, http_response)) {
        if (http_response.is_success()) {
            auto login_resp = json::parse(http_response.body);
            std::string token = login_resp["token"].get<std::string>();
            client.set_token(token);
            spdlog::info("[Login] Got JWT token (expires_in={}s)", login_resp["expires_in"].get<int>());
        } else {
            spdlog::error("Login failed, post http error: {}", http_response.error_message);
            return 1;
        }
    } else {
        spdlog::error("Login failed, request error: {}", http_response.error_message);
        return 1;
    }


    // -------------------------------------------------------
    // Step 2: 查询系统状态 & 账户列表
    // -------------------------------------------------------
    print_separator("Step 2: 查询系统状态");
    http_request.path = "/api/v1/system/status";
    http_request.body = "";
    if (!client.get(http_request, http_response) || !http_response.is_success()) {
        spdlog::error("Failed to query system status, error: {}, {}", http_response.status_code, http_response.error_message);
        return 1;
    }
    auto system_status_resp = json::parse(http_response.body);
    spdlog::info("System] status response: {}", http_response.body);
    auto processes = json::parse(http_response.body);
    for (auto& p : processes) {
        spdlog::info("  Process: uid={} category={} group={} name={} broker_state={}",
                        p["uid"].get<uint32_t>(),
                        p["category"].get<int>(),
                        p.value("group", ""),
                        p.value("name", ""),
                        p.value("broker_state", -1));
    }

    print_separator("Step 2b: 查询账户列表");
    http_request.path = "/api/v1/accounts";
    http_request.body = "";
    if (!client.get(http_request, http_response) || !http_response.is_success()) {
        spdlog::error("Failed to query accounts, error: {}, {}", http_response.status_code, http_response.error_message);
        return 1;
    }
    spdlog::info("Queried accounts response: {}", http_response.body);
    std::string account_id;
    auto accounts = json::parse(http_response.body);
    for (auto& a : accounts) {
        spdlog::info("  Account: source={} id={} state={}",
                        a.value("source", ""),
                        a.value("account_id", ""),
                        a.value("state", 0));
        if (account_id.empty()) {
            account_id = a.value("account_id", "");
        }
    }
    if (account_id.empty()) {
        account_id = "sim";
        spdlog::warn("[Accounts] No account found, using default '{}'", account_id);
    }

    // -------------------------------------------------------
    // Step 3: 启动 WebSocket 订阅（行情 + 交易推送）
    // -------------------------------------------------------
    print_separator("Step 3: WebSocket 订阅行情 & 交易");

    WsSubscriber ws(ws_url);
    ws.subscribe("quote.");
    ws.subscribe("order.");
    ws.subscribe("trade.");

    std::atomic<int> quote_count{0};
    std::atomic<int> order_count{0};
    std::atomic<int> trade_count{0};

    ws.start([&](const json& msg) {
        std::string channel = msg.value("channel", "");
        auto data = msg.value("data", json::object());

        if (channel.find("quote.") == 0) {
            quote_count++;
            if (quote_count <= 3) {
                spdlog::info("[WS Quote] {} last={:.2f} bid={:.2f} ask={:.2f}",
                             data.value("instrument_id", ""),
                             data.value("last_price", 0.0),
                             data.value("bid_price_0", 0.0),
                             data.value("ask_price_0", 0.0));
            }
        } else if (channel.find("order.") == 0) {
            order_count++;
            spdlog::info("[WS Order] id={} status={} traded={}/{}",
                         data.value("order_id", 0ULL),
                         data.value("status", 0),
                         data.value("volume_traded", 0),
                         data.value("volume", 0));
        } else if (channel.find("trade.") == 0) {
            trade_count++;
            spdlog::info("[WS Trade] order_id={} price={:.2f} vol={}",
                         data.value("order_id", 0ULL),
                         data.value("price", 0.0),
                         data.value("volume", 0));
        }
    });

    // -------------------------------------------------------
    // Step 4: 订阅行情 (REST 接口触发 MD 订阅)
    // -------------------------------------------------------
    print_separator("Step 4: 订阅行情");
    json sub_req = {
        {"instrument_id", "600000"},
        {"exchange_id", "SSE"},
        {"instrument_type", 1}
    };
    http_request.path = "/api/v1/market/subscribe";
    http_request.body = sub_req.dump();
    if (!client.post(http_request, http_response) | !http_response.is_success()) {
        spdlog::error("Failed to subscribe market, error: {}, {}", http_response.status_code, http_response.error_message);
        return 1;
    }
    spdlog::info("Subscribe market response: {}", http_response.body);
    spdlog::info("Waiting 5s for market data...");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    spdlog::info("[WS] Received {} quotes so far", quote_count.load());

    // // -------------------------------------------------------
    // // Step 5: 下单
    // // -------------------------------------------------------
    // print_separator("Step 5: 下单");

    // json order_req = {
    //     {"instrument_id", "600000"},
    //     {"exchange_id", "SSE"},
    //     {"limit_price", 10.50},
    //     {"volume", 100},
    //     {"side", 0},       // Buy
    //     {"offset", 0},     // Open
    //     {"price_type", 0}  // Limit
    // };
    // resp = client.post("/api/v1/orders", order_req);
    // spdlog::info("[PlaceOrder] BUY 600000@SSE price=10.50 vol=100");
    // spdlog::info("[PlaceOrder] status={} body={}", resp.status_code, resp.body);

    // uint64_t order_id = 0;
    // if (resp.status_code == 201 || resp.status_code == 200) {
    //     auto order_resp = json::parse(resp.body);
    //     order_id = order_resp.value("order_id", 0ULL);
    //     spdlog::info("[PlaceOrder] order_id={}", order_id);
    // }

    // spdlog::info("Waiting 1s for order/trade callbacks...");
    // std::this_thread::sleep_for(std::chrono::seconds(1));

    // // -------------------------------------------------------
    // // Step 6: 查询账户资产 & 持仓
    // // -------------------------------------------------------
    // print_separator("Step 6: 查询账户信息");

    // resp = client.get("/api/v1/accounts/" + account_id + "/assets");
    // spdlog::info("[Assets] status={}", resp.status_code);
    // if (resp.status_code == 200) {
    //     auto asset = json::parse(resp.body);
    //     spdlog::info("  dynamic_equity={:.2f} available={:.2f} margin={:.2f}",
    //                  asset.value("dynamic_equity", 0.0),
    //                  asset.value("available", 0.0),
    //                  asset.value("margin", 0.0));
    //     spdlog::info("  realized_pnl={:.2f} unrealized_pnl={:.2f}",
    //                  asset.value("realized_pnl", 0.0),
    //                  asset.value("unrealized_pnl", 0.0));
    // }

    // resp = client.get("/api/v1/accounts/" + account_id + "/positions");
    // spdlog::info("[Positions] status={}", resp.status_code);
    // if (resp.status_code == 200) {
    //     auto positions = json::parse(resp.body);
    //     for (auto& pos : positions) {
    //         spdlog::info("  {}@{} dir={} vol={} avg_price={:.2f} unrealized_pnl={:.2f}",
    //                      pos.value("instrument_id", ""),
    //                      pos.value("exchange_id", ""),
    //                      pos.value("direction", 0),
    //                      pos.value("volume", 0),
    //                      pos.value("avg_open_price", 0.0),
    //                      pos.value("unrealized_pnl", 0.0));
    //     }
    // }

    // // -------------------------------------------------------
    // // Step 7: 查询订单列表 & 撤单
    // // -------------------------------------------------------
    // print_separator("Step 7: 查询订单 & 撤单");

    // resp = client.get("/api/v1/orders");
    // spdlog::info("[Orders] status={}", resp.status_code);
    // if (resp.status_code == 200) {
    //     auto orders = json::parse(resp.body);
    //     spdlog::info("[Orders] total={}", orders.size());
    //     for (auto& o : orders) {
    //         spdlog::info("  id={} {} {}@{} price={:.2f} vol={} status={}",
    //                      o.value("order_id", 0ULL),
    //                      o.value("side", 0) == 0 ? "BUY" : "SELL",
    //                      o.value("instrument_id", ""),
    //                      o.value("exchange_id", ""),
    //                      o.value("limit_price", 0.0),
    //                      o.value("volume", 0),
    //                      o.value("status", 0));
    //     }
    // }

    // if (order_id > 0) {
    //     spdlog::info("[Cancel] Cancelling order_id={}", order_id);
    //     resp = client.del("/api/v1/orders/" + std::to_string(order_id));
    //     spdlog::info("[Cancel] status={} body={}", resp.status_code, resp.body);
    // }

    // // -------------------------------------------------------
    // // Step 8: 取消行情订阅
    // // -------------------------------------------------------
    // print_separator("Step 8: 取消行情订阅");

    // json unsub_req = {{"instrument_id", "600000"}, {"exchange_id", "SSE"}};
    // resp = client.post("/api/v1/market/unsubscribe", unsub_req);
    // spdlog::info("[Unsubscribe] 600000@SSE status={} body={}", resp.status_code, resp.body);

    // // -------------------------------------------------------
    // // 清理
    // // -------------------------------------------------------
    // print_separator("Summary");
    // spdlog::info("WebSocket received: quotes={} orders={} trades={}",
    //              quote_count.load(), order_count.load(), trade_count.load());

    ws.stop();
    spdlog::info("=== Demo Complete ===");
    return 0;
}
