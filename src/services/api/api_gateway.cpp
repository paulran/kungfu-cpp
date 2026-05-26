#include <kungfu/service/api_gateway.h>
#include "http_handler.h"
#include "ws_handler.h"
#include "ws_nng.h"
#include "jwt.h"
#include <kungfu/longfist/serialize.h>
#include <spdlog/spdlog.h>
#include <ctime>

namespace kungfu::service {

static std::unique_ptr<WsManager> g_ws_manager;
static std::unique_ptr<WsNngPublisher> g_ws_nng_publisher;

ApiGateway::ApiGateway(const yijinjing::io::location_ptr& home,
                       yijinjing::io::Locator& locator,
                       const common::ApiConfig& config,
                       bool low_latency)
    : apprentice(home, locator, low_latency)
    , config_(config) {
}

ApiGateway::~ApiGateway() {
    stop_http_server();
    stop_ws_nng_publisher();
}

void ApiGateway::on_start() {
    apprentice::on_start();
    start_http_server();
    start_ws_nng_publisher();
    spdlog::info("API Gateway: started, uid={}, http={}:{}, ws_nng={}:{}",
                 home_uid(), config_.host, config_.port, config_.host, config_.ws_port);
}

void ApiGateway::on_exit() {
    stop_http_server();
    stop_ws_nng_publisher();
    if (g_ws_manager) g_ws_manager->stop_all();
    spdlog::info("API Gateway: stopped");
}

void ApiGateway::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            switch (event->msg_type()) {
                case longfist::types::Quote::tag: {
                    const auto& quote = event->data<longfist::types::Quote>();
                    cache_quote(quote);
                    auto j = longfist::to_json(quote);
                    std::string channel = "quote." + std::string(quote.instrument_id.data);
                    broadcast_ws(channel, j);
                    break;
                }
                case longfist::types::Order::tag: {
                    const auto& order = event->data<longfist::types::Order>();
                    cache_order(order);
                    auto j = longfist::to_json(order);
                    broadcast_ws("order", j);
                    break;
                }
                case longfist::types::Trade::tag: {
                    const auto& trade = event->data<longfist::types::Trade>();
                    cache_trade(trade);
                    auto j = longfist::to_json(trade);
                    broadcast_ws("trade", j);
                    break;
                }
                case longfist::types::Position::tag: {
                    const auto& pos = event->data<longfist::types::Position>();
                    cache_position(pos, event->source());
                    auto j = longfist::to_json(pos);
                    broadcast_ws("position", j);
                    break;
                }
                case longfist::types::Asset::tag: {
                    const auto& asset = event->data<longfist::types::Asset>();
                    cache_asset(asset);
                    auto j = longfist::to_json(asset);
                    broadcast_ws("asset", j);
                    break;
                }
                case longfist::types::Instrument::tag: {
                    const auto& inst = event->data<longfist::types::Instrument>();
                    cache_instrument(inst);
                    break;
                }
                case longfist::types::BrokerStateUpdate::tag: {
                    const auto& bsu = event->data<longfist::types::BrokerStateUpdate>();
                    cache_broker_state(bsu);
                    auto j = longfist::to_json(bsu);
                    broadcast_ws("system.status", j);
                    break;
                }
                case longfist::types::Register::tag: {
                    const auto& reg = event->data<longfist::types::Register>();
                    cache_register(reg, event->source_location);
                    break;
                }
                default:
                    break;
            }

            // Periodically clean up dead WS sessions
            if (g_ws_manager) {
                g_ws_manager->remove_closed_sessions();
            }
        },
        [](std::exception_ptr ep) {
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                spdlog::error("API Gateway: event error: {}", e.what());
            }
        }
    );
}

// HTTP Server management

void ApiGateway::start_http_server() {
    std::string url = "http://" + config_.host + ":" + std::to_string(config_.port);

    nng_url* nng_u = nullptr;
    nng_err rv = nng_url_parse(&nng_u, url.c_str());
    if (rv != 0) {
        spdlog::error("API: failed to parse URL {}: {}", url, nng_strerror(rv));
        return;
    }

    rv = nng_http_server_hold(&http_server_, nng_u);
    nng_url_free(nng_u);
    if (rv != 0) {
        spdlog::error("API: failed to hold HTTP server: {}", nng_strerror(rv));
        return;
    }

    // Register REST API tree handler
    rv = nng_http_handler_alloc(&api_handler_, "/api/v1", api_http_handler);
    if (rv != 0) {
        spdlog::error("API: failed to alloc handler: {}", nng_strerror(rv));
        return;
    }
    nng_http_handler_set_tree(api_handler_);
    nng_http_handler_set_method(api_handler_, nullptr); // Accept all methods
    nng_http_handler_set_data(api_handler_, this, nullptr);
    nng_http_handler_collect_body(api_handler_, true, 1024 * 1024); // 1MB max body

    rv = nng_http_server_add_handler(http_server_, api_handler_);
    if (rv != 0) {
        spdlog::error("API: failed to add REST handler: {}", nng_strerror(rv));
        return;
    }

    // Register WebSocket upgrade handler
    g_ws_manager = std::make_unique<WsManager>(this);

    rv = nng_http_handler_alloc(&ws_handler_, "/api/v1/ws", ws_upgrade_handler);
    if (rv != 0) {
        spdlog::error("API: failed to alloc WS handler: {}", nng_strerror(rv));
        return;
    }
    nng_http_handler_set_method(ws_handler_, "GET");
    nng_http_handler_set_data(ws_handler_, g_ws_manager.get(), nullptr);

    rv = nng_http_server_add_handler(http_server_, ws_handler_);
    if (rv != 0) {
        spdlog::error("API: failed to add WS handler: {}", nng_strerror(rv));
        return;
    }

    // Start the HTTP server
    rv = nng_http_server_start(http_server_);
    if (rv != 0) {
        spdlog::error("API: failed to start HTTP server: {}", nng_strerror(rv));
        return;
    }

    spdlog::info("API: HTTP server started on {}", url);
}

void ApiGateway::stop_http_server() {
    if (http_server_) {
        nng_http_server_stop(http_server_);
        nng_http_server_release(http_server_);
        http_server_ = nullptr;
    }
    g_ws_manager.reset();
}

void ApiGateway::start_ws_nng_publisher() {
    g_ws_nng_publisher = std::make_unique<WsNngPublisher>();
    g_ws_nng_publisher->start(config_.host, config_.ws_port);
}

void ApiGateway::stop_ws_nng_publisher() {
    if (g_ws_nng_publisher) {
        g_ws_nng_publisher->stop();
        g_ws_nng_publisher.reset();
    }
}

// WebSocket broadcast

void ApiGateway::broadcast_ws(const std::string& channel, const nlohmann::json& data) {
    // Broadcast to standard WebSocket clients
    if (g_ws_manager) {
        g_ws_manager->broadcast(channel, data);
    }
    // Broadcast to NNG pub/sub clients
    if (g_ws_nng_publisher && g_ws_nng_publisher->is_active()) {
        g_ws_nng_publisher->publish(channel, data);
    }
}

// State cache operations

void ApiGateway::cache_quote(const longfist::types::Quote& quote) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    quotes_[std::string(quote.instrument_id.data)] = quote;
}

void ApiGateway::cache_order(const longfist::types::Order& order) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    orders_[order.order_id] = order;
}

void ApiGateway::cache_trade(const longfist::types::Trade& trade) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    trades_[trade.trade_id] = trade;
}

void ApiGateway::cache_position(const longfist::types::Position& pos, uint32_t source_uid) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Use source_uid to determine account; for now use instrument as key
    std::string account = "default";
    if (auto it = registered_locations_.find(source_uid); it != registered_locations_.end()) {
        account = it->second->name;
    }
    auto& pos_vec = positions_[account];
    std::string key = std::string(pos.instrument_id.data) + "@" + std::string(pos.exchange_id.data);
    bool found = false;
    for (auto& p : pos_vec) {
        if (std::string(p.instrument_id.data) + "@" + std::string(p.exchange_id.data) == key &&
            p.direction == pos.direction) {
            p = pos;
            found = true;
            break;
        }
    }
    if (!found) pos_vec.push_back(pos);
}

void ApiGateway::cache_asset(const longfist::types::Asset& asset) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    assets_[std::string(asset.account_id.data)] = asset;
}

void ApiGateway::cache_instrument(const longfist::types::Instrument& inst) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    instruments_[std::string(inst.instrument_id.data)] = inst;
}

void ApiGateway::cache_broker_state(const longfist::types::BrokerStateUpdate& state) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    broker_states_[state.source_uid] = state.state;
}

void ApiGateway::cache_register(const longfist::types::Register& reg, const yijinjing::io::location_ptr& loc) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (loc) {
        registered_locations_[loc->uid] = loc;
    }
}

// REST API response generators

nlohmann::json ApiGateway::get_system_status() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [uid, loc] : registered_locations_) {
        nlohmann::json entry;
        entry["uid"] = uid;
        entry["category"] = static_cast<int>(loc->cat);
        entry["group"] = loc->group;
        entry["name"] = loc->name;
        entry["mode"] = static_cast<int>(loc->m);
        if (auto it = broker_states_.find(uid); it != broker_states_.end()) {
            entry["broker_state"] = it->second;
        } else {
            entry["broker_state"] = -1;
        }
        result.push_back(entry);
    }
    return result;
}

nlohmann::json ApiGateway::get_accounts() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [uid, loc] : registered_locations_) {
        if (loc->cat == yijinjing::io::category::TD) {
            nlohmann::json entry;
            entry["uid"] = uid;
            entry["source"] = loc->group;
            entry["account_id"] = loc->name;
            if (auto it = broker_states_.find(uid); it != broker_states_.end()) {
                entry["state"] = it->second;
            }
            result.push_back(entry);
        }
    }
    return result;
}

nlohmann::json ApiGateway::get_account_assets(const std::string& account_id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (auto it = assets_.find(account_id); it != assets_.end()) {
        return longfist::to_json(it->second);
    }
    return nlohmann::json{{"error", "Account not found"}};
}

nlohmann::json ApiGateway::get_account_positions(const std::string& account_id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    if (auto it = positions_.find(account_id); it != positions_.end()) {
        for (const auto& pos : it->second) {
            result.push_back(longfist::to_json(pos));
        }
    }
    return result;
}

nlohmann::json ApiGateway::get_orders() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [id, order] : orders_) {
        result.push_back(longfist::to_json(order));
    }
    return result;
}

nlohmann::json ApiGateway::get_order(uint64_t order_id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (auto it = orders_.find(order_id); it != orders_.end()) {
        return longfist::to_json(it->second);
    }
    return nlohmann::json{{"error", "Order not found"}};
}

nlohmann::json ApiGateway::get_instruments() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [id, inst] : instruments_) {
        result.push_back(longfist::to_json(inst));
    }
    return result;
}

nlohmann::json ApiGateway::get_strategies() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [uid, loc] : registered_locations_) {
        if (loc->cat == yijinjing::io::category::STRATEGY) {
            nlohmann::json entry;
            entry["uid"] = uid;
            entry["group"] = loc->group;
            entry["name"] = loc->name;
            if (auto it = broker_states_.find(uid); it != broker_states_.end()) {
                entry["state"] = it->second;
            }
            result.push_back(entry);
        }
    }
    return result;
}

// Trading actions

nlohmann::json ApiGateway::place_order(const nlohmann::json& order_request) {
    longfist::types::OrderInput input{};
    try {
        longfist::from_json(order_request, input);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("Invalid order: ") + e.what()}};
    }

    if (input.volume <= 0) {
        return nlohmann::json{{"error", "Volume must be positive"}};
    }
    if (input.limit_price < 0) {
        return nlohmann::json{{"error", "Price must be non-negative"}};
    }

    // Assign order_id if not set
    if (input.order_id == 0) {
        input.order_id = static_cast<uint64_t>(now_ns());
    }

    // Find the target TD by looking for registered TD locations
    uint32_t td_uid = 0;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& [uid, loc] : registered_locations_) {
            if (loc->cat == yijinjing::io::category::TD) {
                td_uid = uid;
                break;
            }
        }
    }

    if (td_uid == 0) {
        return nlohmann::json{{"error", "No TD service available"}};
    }

    // Write to journal targeting the TD
    auto writer = get_writer(home_, td_uid);
    if (writer) {
        writer->write(now_ns(), input);
        spdlog::info("API: placed order id={} {} {} vol={}",
                     input.order_id, input.instrument_id.data, input.exchange_id.data, input.volume);
        return nlohmann::json{{"order_id", input.order_id}, {"status", "submitted"}};
    }

    return nlohmann::json{{"error", "Failed to write order"}};
}

nlohmann::json ApiGateway::cancel_order(uint64_t order_id) {
    longfist::types::OrderAction action{};
    action.order_id = order_id;
    action.action = longfist::enums::HistoryOrderAction::Cancel;

    // Find target TD
    uint32_t td_uid = 0;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& [uid, loc] : registered_locations_) {
            if (loc->cat == yijinjing::io::category::TD) {
                td_uid = uid;
                break;
            }
        }
    }

    if (td_uid == 0) {
        return nlohmann::json{{"error", "No TD service available"}};
    }

    auto writer = get_writer(home_, td_uid);
    if (writer) {
        writer->write(now_ns(), action);
        spdlog::info("API: cancel order id={}", order_id);
        return nlohmann::json{{"order_id", order_id}, {"status", "cancel_submitted"}};
    }

    return nlohmann::json{{"error", "Failed to write cancel"}};
}

nlohmann::json ApiGateway::subscribe_market(const nlohmann::json& request) {
    longfist::types::Subscribe sub{};
    try {
        longfist::from_json(request, sub);
    } catch (...) {
        return nlohmann::json{{"error", "Invalid subscribe request"}};
    }

    // Find target MD
    uint32_t md_uid = 0;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& [uid, loc] : registered_locations_) {
            if (loc->cat == yijinjing::io::category::MD) {
                md_uid = uid;
                break;
            }
        }
    }

    if (md_uid == 0) {
        return nlohmann::json{{"error", "No MD service available"}};
    }

    auto writer = get_writer(home_, md_uid);
    if (writer) {
        writer->write(now_ns(), sub);
        return nlohmann::json{{"status", "subscribed"},
                              {"instrument_id", std::string(sub.instrument_id.data)},
                              {"exchange_id", std::string(sub.exchange_id.data)}};
    }

    return nlohmann::json{{"error", "Failed to write subscribe"}};
}

nlohmann::json ApiGateway::unsubscribe_market(const nlohmann::json& request) {
    longfist::types::Unsubscribe unsub{};
    try {
        longfist::from_json(request, unsub);
    } catch (...) {
        return nlohmann::json{{"error", "Invalid unsubscribe request"}};
    }

    uint32_t md_uid = 0;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto& [uid, loc] : registered_locations_) {
            if (loc->cat == yijinjing::io::category::MD) {
                md_uid = uid;
                break;
            }
        }
    }

    if (md_uid == 0) {
        return nlohmann::json{{"error", "No MD service available"}};
    }

    auto writer = get_writer(home_, md_uid);
    if (writer) {
        writer->write(now_ns(), unsub);
        return nlohmann::json{{"status", "unsubscribed"},
                              {"instrument_id", std::string(unsub.instrument_id.data)},
                              {"exchange_id", std::string(unsub.exchange_id.data)}};
    }

    return nlohmann::json{{"error", "Failed to write unsubscribe"}};
}

// Authentication

nlohmann::json ApiGateway::authenticate(const std::string& username, const std::string& password) {
    if (username != config_.admin_user || password != config_.admin_password) {
        return nlohmann::json{{"error", "Invalid credentials"}};
    }

    int64_t expire = std::time(nullptr) + config_.jwt_expire_hours * 3600;
    std::string token = jwt::create_token(config_.jwt_secret, username, expire);

    return nlohmann::json{
        {"token", token},
        {"expires_in", config_.jwt_expire_hours * 3600},
        {"token_type", "Bearer"}
    };
}

bool ApiGateway::verify_token(const std::string& token) const {
    return jwt::verify_token(config_.jwt_secret, token);
}

} // namespace kungfu::service
