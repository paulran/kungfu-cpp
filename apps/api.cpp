// SPDX-License-Identifier: Apache-2.0

// API service for kungfu-cpp: listens on a TCP port and accepts multiple
// concurrent QT client connections. Each client sends JSON requests
// (issue_order, cancel_order, request_market_data, ...) and receives real-time
// pushes (quotes, orders, trades, broker states, ...) over the same connection.
//
// Reliable message protocol over TCP (length-prefixed framing):
//
//   Each message: [4 bytes big-endian length][payload]
//   where length = payload size (not including the 4-byte header).
//
//   [JSON]  — request / response, starts with '{' (0x7B)
//     Request:  {"request_id": N, "method": "...", "data": {...}}
//     Response: {"msg_type": "response", "request_id": N, "data": {...}, "error": null}
//
//   [Binary] — push data, starts with 0x42 ('B')
//     | 0x42 | frame_header (36B packed) | raw struct data |
//       frame_header fields: length, header_length, gen_time, trigger_time,
//       msg_type (longfist type id), source, dest
//     The client distinguishes JSON from binary by the first byte:
//       '{' → JSON,  'B' → binary frame

#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/wingchun/book/bookkeeper.h>
#include <kungfu/wingchun/broker/client.h>
#include <kungfu/wingchun/common.h>
#include <kungfu/yijinjing/io.h>
#include <kungfu/yijinjing/log.h>
#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/yijinjing/time.h>

#include <type_traits>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = SSIZE_T;
using socklen_t = int;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// ---- Cross-platform socket helpers ----
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
inline int close_socket(socket_t s) { return ::closesocket(s); }
inline int shutdown_socket(socket_t s, int how) { return ::shutdown(s, how); }
#define SOCKET_SHUT_RDWR SD_BOTH
#define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
inline int socket_errno() { return ::WSAGetLastError(); }
inline const char *socket_strerror(int e) {
  static char msg[256];
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg, sizeof(msg), NULL);
  return msg;
}
#else
using socket_t = int;
constexpr socket_t INVALID_SOCKET_FD = -1;
inline int close_socket(socket_t s) { return ::close(s); }
inline int shutdown_socket(socket_t s, int how) { return ::shutdown(s, how); }
#define SOCKET_SHUT_RDWR SHUT_RDWR
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
inline int socket_errno() { return errno; }
inline const char *socket_strerror(int e) { return strerror(e); }
#endif

using namespace kungfu;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::practice;
using namespace kungfu::wingchun;
using namespace kungfu::wingchun::broker;
using namespace kungfu::wingchun::book;

namespace {

constexpr char BINARY_FRAME_MARKER = 0x42; // 'B' — distinguishes from JSON '{'
constexpr uint32_t MAX_MSG_SIZE = 64 * 1024; // max framed message size

uint32_t parse_uid_hex(const std::string &s) {
  uint32_t uid = 0;
  std::stringstream ss;
  ss << std::hex << s;
  ss >> uid;
  return uid;
}

nlohmann::json location_to_json(const location_ptr &loc) {
  nlohmann::json j;
  j["uid"] = fmt::format("{:08x}", loc->uid);
  j["category"] = get_category_name(loc->category);
  j["group"] = loc->group;
  j["name"] = loc->name;
  j["mode"] = get_mode_name(loc->mode);
  j["uname"] = loc->uname;
  return j;
}

constexpr uint64_t ID_TRANC = 0x00000000FFFFFFFF;
constexpr uint32_t PAGE_ID_MASK = 0x80000000;

// TCP framing helpers: [4 bytes big-endian length][payload]
void write_frame(socket_t fd, const char *data, uint32_t len) {
  uint32_t net_len = htonl(len);
#ifdef _WIN32
  ::send(fd, reinterpret_cast<const char *>(&net_len), 4, 0);
  ::send(fd, data, len, 0);
#else
  ::send(fd, reinterpret_cast<const char *>(&net_len), 4, MSG_NOSIGNAL);
  ::send(fd, data, len, MSG_NOSIGNAL);
#endif
}

// Read a complete framed message from fd. Returns false on connection close.
// Retries on EAGAIN/EWOULDBLOCK (recv timeout) so the caller's stop flag can be checked.
bool read_frame(socket_t fd, std::string &out, const std::atomic<bool> &stop) {
  uint32_t net_len = 0;
  char *p = reinterpret_cast<char *>(&net_len);
  int total = 0;
  while (total < 4 && !stop.load()) {
    int n = ::recv(fd, p + total, 4 - total, 0);
    if (n <= 0) {
      int e = socket_errno();
      if (n < 0 && (e == SOCKET_EWOULDBLOCK || e == EAGAIN)) continue; // timeout, retry
      return false;                                                       // actual error / closed
    }
    total += n;
  }
  if (stop.load()) return false;

  uint32_t len = ntohl(net_len);
  if (len > MAX_MSG_SIZE) return false;

  out.resize(len);
  total = 0;
  while (total < len && !stop.load()) {
    int n = ::recv(fd, out.data() + total, len - total, 0);
    if (n <= 0) {
      int e = socket_errno();
      if (n < 0 && (e == SOCKET_EWOULDBLOCK || e == EAGAIN)) continue;
      return false;
    }
    total += n;
  }
  return !stop.load();
}

} // namespace

/// Per-client session: tracks the TCP fd and this client's subscriptions.
struct ClientSession {
  socket_t fd = INVALID_SOCKET_FD;
  uint32_t id = 0;
  /// instrument hash → true, for filtering Quote pushes
  std::unordered_set<uint32_t> subscribed_keys;
  /// md_uid → set of keys, for unsubscribing from MD on disconnect
  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> md_subscribed_keys;
  /// strategy_uid → true, for filtering Order/Trade pushes
  std::unordered_set<uint32_t> subscribed_strategies;
  /// Send queue for outbound messages (main thread writes, per-client send thread drains)
  std::mutex send_mutex;
  std::deque<std::string> send_queue;
};

class ApiService : public apprentice {
public:
  explicit ApiService(locator_ptr locator, mode m, std::string host, int port, bool low_latency = false)
      : apprentice(location::make_shared(m, category::SYSTEM, "service", "api", std::move(locator)), low_latency),
        broker_client_(*this), bookkeeper_(*this, broker_client_, false), host_(std::move(host)), port_(port) {
    broker_client_.enroll_system(get_ledger_home_location());
  }

  ~ApiService() override { stop_server(); }

  void on_start() override {
    broker_client_.on_start(events_);
    bookkeeper_.on_start(events_);
    bookkeeper_.guard_positions();

    events_ | is(Quote::tag) | $$(push_quote(event));
    events_ | is(Order::tag) | $$(push_strategy_event<Order>(event));
    events_ | is(Trade::tag) | $$(push_strategy_event<Trade>(event));
    events_ | is(Position::tag) | $$(broadcast_binary<Position>(event));
    events_ | is(Asset::tag) | $$(broadcast_binary<Asset>(event));
    events_ | is(AssetMargin::tag) | $$(broadcast_binary<AssetMargin>(event));
    events_ | is(BrokerStateUpdate::tag) | $$(broadcast_binary<BrokerStateUpdate>(event));
    events_ | is(Instrument::tag) | $$(broadcast_binary<Instrument>(event));
    events_ | is(Channel::tag) | $$(broadcast_binary<Channel>(event));
    events_ | is(Register::tag) | $$(broadcast_binary<Register>(event));
    events_ | is(Deregister::tag) | $$(broadcast_binary<Deregister>(event));

    start_server();
  }

  void on_active() override {
    process_disconnect_queue();
    drain_requests();
    drain_send_queues();
  }

  void on_exit() override { stop_server(); }

private:
  AutoClient broker_client_;
  Bookkeeper bookkeeper_;
  std::string host_;
  int port_;

  socket_t listen_fd_ = INVALID_SOCKET_FD;
  std::atomic<bool> stop_server_{false};
  std::thread accept_thread_;

  /// order_id → session_id, for routing manual orders (client-issued, not strategy-owned).
  /// TD writes Order/Trade acks back with dest = api home uid, which never matches a
  /// subscribed strategy uid, so these need their own routing table.
  std::unordered_map<uint64_t, uint32_t> manual_order_sessions_;

  // ---- session management ----
  std::mutex sessions_mutex_;
  std::unordered_map<uint32_t, ClientSession> sessions_; // id → session
  uint32_t next_session_id_ = 1;

  // ---- disconnect queue (accept/recv threads → main thread) ----
  std::mutex disconnect_mutex_;
  std::queue<uint32_t> disconnect_queue_;

  // ---- request queue (recv threads → main thread) ----
  std::mutex queue_mutex_;
  std::queue<std::tuple<uint32_t, uint64_t, nlohmann::json>> request_queue_;

  // ---- instruction UID generation ----
  uint64_t make_instruction_uid(const journal::writer_ptr &writer, uint32_t dest, uint32_t client_id = 0) {
    uint64_t id_left = (uint64_t)(client_id xor dest) << 32u;
    uint64_t id_right = (ID_TRANC & writer->current_frame_uid()) | PAGE_ID_MASK;
    return id_left | id_right;
  }

  // ---- TCP server lifecycle ----
  void start_server() {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      SPDLOG_ERROR("WSAStartup failed: {}", socket_strerror(socket_errno()));
      return;
    }
#else
    // Reap child processes (strategies launched via start_strategy) automatically
    // to prevent zombies. SIG_IGN causes the kernel to auto-reap exited children.
    ::signal(SIGCHLD, SIG_IGN);
#endif

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == INVALID_SOCKET_FD) {
      SPDLOG_ERROR("failed to create socket: {}", socket_strerror(socket_errno()));
      return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (host_ == "0.0.0.0" || host_.empty()) {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
      addr.sin_addr.s_addr = inet_addr(host_.c_str());
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
      SPDLOG_ERROR("failed to bind on {}:{}: {}", host_, port_, socket_strerror(socket_errno()));
      close_socket(listen_fd_);
      listen_fd_ = INVALID_SOCKET_FD;
      return;
    }

    if (::listen(listen_fd_, 16) == SOCKET_ERROR) {
      SPDLOG_ERROR("failed to listen: {}", socket_strerror(socket_errno()));
      close_socket(listen_fd_);
      listen_fd_ = INVALID_SOCKET_FD;
      return;
    }

    std::string url = fmt::format("tcp://{}:{}", host_, port_);
    SPDLOG_INFO("api server listening on {}", url);

    accept_thread_ = std::thread([this] { accept_loop(); });
  }

  void stop_server() {
    stop_server_ = true;
    if (listen_fd_ != INVALID_SOCKET_FD) {
      shutdown_socket(listen_fd_, SOCKET_SHUT_RDWR);
      close_socket(listen_fd_);
      listen_fd_ = INVALID_SOCKET_FD;
    }
    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }

    // Close all client connections
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &[_, session] : sessions_) {
      if (session.fd != INVALID_SOCKET_FD) {
        shutdown_socket(session.fd, SOCKET_SHUT_RDWR);
        close_socket(session.fd);
      }
    }
    sessions_.clear();

#ifdef _WIN32
    WSACleanup();
#endif
  }

  // Accept loop: spawns a handler thread per client connection
  void accept_loop() {
    while (!stop_server_) {
      struct sockaddr_in client_addr;
      socklen_t addr_len = sizeof(client_addr);
      socket_t client_fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &addr_len);
      if (client_fd == INVALID_SOCKET_FD) {
        if (stop_server_) break;
        int e = socket_errno();
#ifdef _WIN32
        if (e == WSAEINTR) continue;
#else
        if (e == EINTR) continue;
#endif
        SPDLOG_WARN("accept failed: {}", socket_strerror(e));
        continue;
      }

      // Set a receive timeout so the recv thread can check stop flags
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 200 * 1000; // 200ms
      ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));

      uint32_t session_id = 0;
      {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        session_id = next_session_id_++;
        auto &session = sessions_[session_id];
        session.fd = client_fd;
        session.id = session_id;
      }

      SPDLOG_INFO("client connected (fd {}, id {}, total {})", static_cast<int>(client_fd), session_id, session_count());

      std::thread([this, client_fd, session_id] { client_recv_loop(client_fd, session_id); }).detach();
    }
  }

  size_t session_count() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
  }

  // ---- per-client receive loop (one thread per client) ----
  void client_recv_loop(socket_t fd, uint32_t session_id) {
    while (!stop_server_) {
      std::string body;
      if (!read_frame(fd, body, stop_server_)) {
        break; // connection closed or error
      }
      try {
        auto j = nlohmann::json::parse(body);
        uint64_t request_id = j.value("request_id", 0ULL);
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          request_queue_.push({session_id, request_id, std::move(j)});
        }
      } catch (const std::exception &e) {
        SPDLOG_WARN("failed to parse client message from session {}: {}", session_id, e.what());
      }
    }

    // Schedule cleanup on main thread
    {
      std::lock_guard<std::mutex> lock(disconnect_mutex_);
      disconnect_queue_.push(session_id);
    }
    SPDLOG_INFO("client disconnected (session {})", session_id);
  }

  // ---- disconnect cleanup (main thread) ----
  void process_disconnect_queue() {
    std::queue<uint32_t> local;
    {
      std::lock_guard<std::mutex> lock(disconnect_mutex_);
      std::swap(local, disconnect_queue_);
    }
    while (!local.empty()) {
      uint32_t session_id = local.front();
      local.pop();
      cleanup_session(session_id);
    }
  }

  void cleanup_session(uint32_t session_id) {
    socket_t fd = INVALID_SOCKET_FD;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> md_subscribed_keys;
    std::unordered_set<uint32_t> subscribed_strategies;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto it = sessions_.find(session_id);
      if (it == sessions_.end()) return;
      fd = it->second.fd;
      md_subscribed_keys = std::move(it->second.md_subscribed_keys);
      subscribed_strategies = std::move(it->second.subscribed_strategies);
      sessions_.erase(it);
      for (auto manual_it = manual_order_sessions_.begin(); manual_it != manual_order_sessions_.end();) {
        if (manual_it->second == session_id) {
          manual_it = manual_order_sessions_.erase(manual_it);
        } else {
          ++manual_it;
        }
      }
    }

    // Close the socket
    if (fd != INVALID_SOCKET_FD) {
      shutdown_socket(fd, SOCKET_SHUT_RDWR);
      close_socket(fd);
    }

    // Clean up MD subscriptions
    if (!md_subscribed_keys.empty()) {
      SPDLOG_INFO("session {} cleanup: unsubscribing from {} MD sources", session_id,
                  md_subscribed_keys.size());
      for (auto &[md_uid, keys] : md_subscribed_keys) {
        if (has_writer(md_uid)) {
          bool others_still_subscribed = false;
          {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto &[_, other] : sessions_) {
              if (other.md_subscribed_keys.count(md_uid)) {
                others_still_subscribed = true;
                break;
              }
            }
          }
          if (!others_still_subscribed) {
            InstrumentKey empty_key = {};
            empty_key.key = 0;
            get_writer(md_uid)->write(now(), empty_key);
            SPDLOG_INFO("unsubscribed from MD {:08x} (no other clients need it)", md_uid);
          }
        }
      }
    }

    // Clean up strategy subscriptions
    for (uint32_t strategy_uid : subscribed_strategies) {
      bool others_need = false;
      {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto &[_, other] : sessions_) {
          if (other.subscribed_strategies.count(strategy_uid)) {
            others_need = true;
            break;
          }
        }
      }
      if (!others_need) {
        for (const auto &[uid, loc] : get_locations()) {
          if (loc->category == category::TD) {
            reader_->disjoin_channel(uid, strategy_uid);
          }
        }
        SPDLOG_INFO("session {} cleanup: disjoined strategy {:08x} TD channels",
                    session_id, strategy_uid);
      }
    }

    if (md_subscribed_keys.empty() && subscribed_strategies.empty()) {
      SPDLOG_INFO("session {} cleanup: no subscriptions to cancel", session_id);
    }
  }

  // ---- main-thread request processing ----
  void drain_requests() {
    std::queue<std::tuple<uint32_t, uint64_t, nlohmann::json>> local;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      std::swap(local, request_queue_);
    }
    while (!local.empty()) {
      auto [session_id, request_id, req] = local.front();
      local.pop();
      handle_request(session_id, request_id, req);
    }
  }

  // Drain send queues: for each session that has pending sends, write to fd
  void drain_send_queues() {
    std::vector<std::pair<uint32_t, std::deque<std::string>>> to_send;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      for (auto &[id, session] : sessions_) {
        std::lock_guard<std::mutex> slock(session.send_mutex);
        if (!session.send_queue.empty()) {
          to_send.emplace_back(id, std::move(session.send_queue));
          session.send_queue.clear();
        }
      }
    }
    for (auto &[id, msgs] : to_send) {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto it = sessions_.find(id);
      if (it == sessions_.end()) continue;
      int fd = it->second.fd;
      for (auto &msg : msgs) {
        write_frame(fd, msg.data(), static_cast<uint32_t>(msg.size()));
      }
    }
  }

  void handle_request(uint32_t session_id, uint64_t request_id, const nlohmann::json &req) {
    std::string method = req.value("method", "");
    const auto &data = req.value("data", nlohmann::json::object());

    nlohmann::json response;
    response["msg_type"] = "response";
    response["request_id"] = request_id;

    try {
      if (method == "issue_order") {
        response["data"] = handle_issue_order(session_id, data);
      } else if (method == "cancel_order") {
        response["data"] = handle_cancel_order(data);
      } else if (method == "request_market_data") {
        response["data"] = handle_request_market_data(session_id, data);
      } else if (method == "cancel_market_data") {
        response["data"] = handle_cancel_market_data(session_id, data);
      } else if (method == "request_position") {
        response["data"] = handle_request_position();
      } else if (method == "get_locations") {
        response["data"] = handle_get_locations();
      } else if (method == "get_strategies") {
        response["data"] = handle_get_strategies();
      } else if (method == "is_ready_to_interact") {
        response["data"] = handle_is_ready_to_interact(data);
      } else if (method == "get_trading_day") {
        response["data"] = handle_get_trading_day();
      } else if (method == "now") {
        response["data"] = now();
      } else if (method == "get_subscriptions") {
        response["data"] = handle_get_subscriptions(session_id);
      } else if (method == "subscribe_strategy") {
        response["data"] = handle_subscribe_strategy(session_id, data);
      } else if (method == "unsubscribe_strategy") {
        response["data"] = handle_unsubscribe_strategy(session_id, data);
      } else if (method == "start_strategy") {
        response["data"] = handle_start_strategy(data);
      } else if (method == "stop_strategy") {
        response["data"] = handle_stop_strategy(data);
      } else {
        response["error"] = fmt::format("unknown method: {}", method);
      }
    } catch (const std::exception &e) {
      response["error"] = e.what();
      SPDLOG_ERROR("handle_request error: {}, method: {}, data: {}", e.what(), method, data.dump());
    }

    send_json(session_id, response);
  }

  // ---- request handlers ----

  nlohmann::json handle_issue_order(uint32_t session_id, const nlohmann::json &data) {
    auto account = resolve_location(data);
    if (!account) throw std::runtime_error("invalid account location");
    if (!is_location_live(account->uid) || !has_writer(account->uid))
      throw std::runtime_error(fmt::format("account {} not ready", account->uname));

    auto writer = get_writer(account->uid);
    OrderInput input = {};
    nlohmann::json order_data = data;
    order_data.erase("location");
    if (!order_data.contains("instrument_type") && order_data.contains("exchange_id") &&
        order_data.contains("instrument_id")) {
      order_data["instrument_type"] = static_cast<int>(get_instrument_type(order_data["exchange_id"].get<std::string>(),
                                                           order_data["instrument_id"].get<std::string>()));
    }
    // Ensure all numeric/bool fields have defaults so parse() doesn't hit null
    if (!order_data.contains("order_id")) order_data["order_id"] = 0;
    if (!order_data.contains("parent_id")) order_data["parent_id"] = 0;
    if (!order_data.contains("frozen_price")) order_data["frozen_price"] = 0.0;
    if (!order_data.contains("is_swap")) order_data["is_swap"] = false;
    if (!order_data.contains("block_id")) order_data["block_id"] = 0;
    if (!order_data.contains("insert_time")) order_data["insert_time"] = 0;
    std::string s = order_data.dump();
    input.parse(s.c_str(), static_cast<uint32_t>(s.size()));

    input.order_id = make_instruction_uid(writer, account->uid, get_home_uid());
    input.insert_time = now();

    writer->write_as(now(), input, get_home_uid(), account->uid);
    bookkeeper_.on_order_input(now(), get_home_uid(), account->uid, input);
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      manual_order_sessions_[input.order_id] = session_id;
    }

    auto volume = input.volume;
    auto side = static_cast<int>(input.side);
    auto order_id = input.order_id;
    SPDLOG_INFO("issue_order instrument {}@{} volume {} side {} -> order_id {}",
                input.instrument_id.to_string(), input.exchange_id.to_string(), volume, side, order_id);

    nlohmann::json result;
    result["order_id"] = order_id;
    return result;
  }

  nlohmann::json handle_cancel_order(const nlohmann::json &data) {
    auto account = resolve_location(data);
    if (!account) throw std::runtime_error("invalid account location");
    if (!is_location_live(account->uid) || !has_writer(account->uid))
      throw std::runtime_error(fmt::format("account {} not ready", account->uname));

    auto writer = get_writer(account->uid);
    OrderAction action = {};
    nlohmann::json action_data = data;
    action_data.erase("location");
    // Ensure all numeric/enum fields have defaults so parse() doesn't hit null
    if (!action_data.contains("order_action_id")) action_data["order_action_id"] = 0;
    if (!action_data.contains("action_flag")) action_data["action_flag"] = 0;
    if (!action_data.contains("price")) action_data["price"] = 0.0;
    if (!action_data.contains("volume")) action_data["volume"] = 0;
    if (!action_data.contains("insert_time")) action_data["insert_time"] = 0;
    std::string s = action_data.dump();
    action.parse(s.c_str(), static_cast<uint32_t>(s.size()));

    action.order_action_id = make_instruction_uid(writer, account->uid, get_home_uid());
    action.insert_time = now();
    writer->write_as(now(), action, get_home_uid(), account->uid);

    auto order_id = action.order_id;
    auto order_action_id = action.order_action_id;
    SPDLOG_INFO("cancel_order order_id {} -> action_id {}", order_id, order_action_id);

    nlohmann::json result;
    result["order_action_id"] = order_action_id;
    return result;
  }

  nlohmann::json handle_request_market_data(uint32_t session_id, const nlohmann::json &data) {
    auto md = resolve_location(data);
    if (!md) throw std::runtime_error("invalid md location");
    if (!has_writer(md->uid)) throw std::runtime_error(fmt::format("md {} writer not ready", md->uname));

    std::string exchange_id = data.value("exchange_id", "");
    std::string instrument_id = data.value("instrument_id", "");
    if (exchange_id.empty() || instrument_id.empty())
      throw std::runtime_error("exchange_id and instrument_id are required");

    uint32_t key = hash_instrument(exchange_id.c_str(), instrument_id.c_str());

    bool need_send_to_md = true;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      for (auto &[_, session] : sessions_) {
        if (session.subscribed_keys.count(key)) {
          need_send_to_md = false;
          break;
        }
      }
    }
    if (need_send_to_md) {
      InstrumentKey instrument_key = {};
      instrument_key.key = key;
      strcpy(instrument_key.instrument_id, instrument_id.c_str());
      strcpy(instrument_key.exchange_id, exchange_id.c_str());
      instrument_key.instrument_type = get_instrument_type(exchange_id, instrument_id);
      get_writer(md->uid)->write(now(), instrument_key);
    }

    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto &session = sessions_[session_id];
      session.subscribed_keys.insert(key);
      session.md_subscribed_keys[md->uid].insert(key);
    }

    SPDLOG_INFO("request_market_data {}@{} from MD {} (session {})", exchange_id, instrument_id, md->uname, session_id);

    nlohmann::json result;
    result["success"] = true;
    result["key"] = key;
    return result;
  }

  nlohmann::json handle_cancel_market_data(uint32_t session_id, const nlohmann::json &data) {
    auto md = resolve_location(data);
    if (!md) throw std::runtime_error("invalid md location");

    std::string exchange_id = data.value("exchange_id", "");
    std::string instrument_id = data.value("instrument_id", "");
    uint32_t key = hash_instrument(exchange_id.c_str(), instrument_id.c_str());

    bool still_needed_by_others = false;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto &session = sessions_[session_id];
      session.subscribed_keys.erase(key);
      auto it = session.md_subscribed_keys.find(md->uid);
      if (it != session.md_subscribed_keys.end()) {
        it->second.erase(key);
        if (it->second.empty()) session.md_subscribed_keys.erase(it);
      }
      for (auto &[pid, other] : sessions_) {
        if (pid != session_id && other.subscribed_keys.count(key)) {
          still_needed_by_others = true;
          break;
        }
      }
    }

    if (!still_needed_by_others && has_writer(md->uid)) {
      InstrumentKey empty_key = {};
      empty_key.key = 0;
      get_writer(md->uid)->write(now(), empty_key);
    }

    SPDLOG_INFO("cancel_market_data {}@{} from MD {} (session {})", exchange_id, instrument_id, md->uname, session_id);

    nlohmann::json result;
    result["success"] = true;
    result["key"] = key;
    return result;
  }

  nlohmann::json handle_request_position() {
    if (has_writer(ledger_home_location_->uid)) {
      get_writer(ledger_home_location_->uid)->mark(now(), PositionRequest::tag);
    }
    nlohmann::json result;
    result["success"] = true;
    return result;
  }

  nlohmann::json handle_get_locations() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &[uid, loc] : get_locations()) {
      nlohmann::json entry = location_to_json(loc);
      entry["live"] = is_location_live(uid);
      arr.push_back(std::move(entry));
    }
    return arr;
  }

  nlohmann::json handle_get_strategies() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &[uid, loc] : get_locations()) {
      if (loc->category == category::STRATEGY) {
        nlohmann::json entry = location_to_json(loc);
        entry["live"] = is_location_live(uid);
        arr.push_back(std::move(entry));
      }
    }
    return arr;
  }

  nlohmann::json handle_is_ready_to_interact(const nlohmann::json &data) {
    auto loc = resolve_location(data);
    nlohmann::json result;
    result["ready"] = loc && is_location_live(loc->uid) && has_writer(loc->uid);
    return result;
  }

  nlohmann::json handle_get_trading_day() {
    return time::strftime(get_trading_day(), KUNGFU_TRADING_DAY_FORMAT);
  }

  nlohmann::json handle_get_subscriptions(uint32_t session_id) {
    nlohmann::json arr = nlohmann::json::array();
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      for (uint32_t key : it->second.subscribed_keys) {
        arr.push_back(fmt::format("{:08x}", key));
      }
    }
    return arr;
  }

  // ---- strategy subscription (Order/Trade monitoring) ----

  /// Subscribe to a strategy's Order/Trade stream.
  /// Joins (TD_location, strategy_uid) for every live TD so the reader picks up
  /// Order/Trade frames TD writes to that strategy's private channel.
  nlohmann::json handle_subscribe_strategy(uint32_t session_id, const nlohmann::json &data) {
    auto strat = resolve_location(data);
    if (!strat) throw std::runtime_error("invalid strategy location");
    if (strat->category != category::STRATEGY)
      throw std::runtime_error(fmt::format("{} is not a strategy", strat->uname));

    uint32_t strategy_uid = strat->uid;
    bool already = false;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto it = sessions_.find(session_id);
      if (it == sessions_.end()) throw std::runtime_error("session not found");
      already = !it->second.subscribed_strategies.insert(strategy_uid).second;
    }

    if (!already) {
      // Join (td, strategy_uid) for every known TD so we can read Order/Trade
      int td_count = 0;
      for (const auto &[uid, loc] : get_locations()) {
        if (loc->category == category::TD && is_location_live(uid)) {
          reader_->join(loc, strategy_uid, now());
          ++td_count;
        }
      }
      SPDLOG_INFO("session {} subscribed to strategy {:08x} (joined {} TD channels)",
                  session_id, strategy_uid, td_count);
    }

    nlohmann::json result;
    result["success"] = true;
    result["strategy_uid"] = fmt::format("{:08x}", strategy_uid);
    return result;
  }

  /// Unsubscribe from a strategy's Order/Trade stream.
  /// Disjoins (TD, strategy_uid) only if no other session still needs it.
  nlohmann::json handle_unsubscribe_strategy(uint32_t session_id, const nlohmann::json &data) {
    auto strat = resolve_location(data);
    if (!strat) throw std::runtime_error("invalid strategy location");
    if (strat->category != category::STRATEGY)
      throw std::runtime_error(fmt::format("{} is not a strategy", strat->uname));

    uint32_t strategy_uid = strat->uid;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto it = sessions_.find(session_id);
      if (it == sessions_.end()) throw std::runtime_error("session not found");
      it->second.subscribed_strategies.erase(strategy_uid);
    }

    // Check if any other session still subscribes to this strategy
    bool others_need = false;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      for (const auto &[id, session] : sessions_) {
        if (session.subscribed_strategies.count(strategy_uid)) {
          others_need = true;
          break;
        }
      }
    }

    if (!others_need) {
      for (const auto &[uid, loc] : get_locations()) {
        if (loc->category == category::TD) {
          reader_->disjoin_channel(uid, strategy_uid);
        }
      }
      SPDLOG_INFO("session {} unsubscribed from strategy {:08x} (disjoined TD channels)",
                  session_id, strategy_uid);
    } else {
      SPDLOG_INFO("session {} unsubscribed from strategy {:08x} (other sessions still need it)",
                  session_id, strategy_uid);
    }

    nlohmann::json result;
    result["success"] = true;
    return result;
  }

  /// Get the set of strategy uids this session is subscribed to.
  nlohmann::json handle_get_strategy_subscriptions(uint32_t session_id) {
    nlohmann::json arr = nlohmann::json::array();
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      for (uint32_t uid : it->second.subscribed_strategies) {
        arr.push_back(fmt::format("{:08x}", uid));
      }
    }
    return arr;
  }

  // ---- strategy lifecycle management ----

  /// Start (launch) a strategy process.
  /// On Linux: forks and execs the kf_strategy binary.
  /// On Windows: uses CreateProcess to launch the strategy binary.
  nlohmann::json handle_start_strategy(const nlohmann::json &data) {
    std::string mode_str = data.value("mode", "live");
    std::string group = data.value("group", "sim");
    std::string name = data.value("name", "sim");
    std::string strategy_type = data.value("strategy", "kungfu_strategy_101");
    std::string arguments = data.value("args", "");
    // Resolve kf_strategy next to the kf_api executable (same directory)
    std::string exe_path;
    {
      char self_path[4096] = {0};
#ifdef _WIN32
      GetModuleFileNameA(NULL, self_path, sizeof(self_path));
#else
      ssize_t n = ::readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
      if (n > 0) self_path[n] = '\0';
#endif
      std::filesystem::path p(self_path);
#ifdef _WIN32
      exe_path = (p.parent_path() / "kf_strategy.exe").string();
#else
      exe_path = (p.parent_path() / "kf_strategy").string();
#endif
    }
    bool low_latency = data.value("low_latency", false);

    // Check if a strategy with the same (mode, group, name) is already running
    auto m = get_mode_by_name(mode_str);
    for (const auto &[uid, loc] : get_locations()) {
      if (loc->category == category::STRATEGY && loc->group == group && loc->name == name &&
          loc->mode == m && is_location_live(uid)) {
        throw std::runtime_error(
            fmt::format("strategy {}/{}/{} is already running", mode_str, group, name));
      }
    }

    // Build argv / command line
    std::vector<std::string> args_storage;
    args_storage.push_back(exe_path);
    args_storage.push_back("--mode");
    args_storage.push_back(mode_str);
    args_storage.push_back("--group");
    args_storage.push_back(group);
    args_storage.push_back("--name");
    args_storage.push_back(name);
    args_storage.push_back("--strategy");
    args_storage.push_back(strategy_type);
    if (!arguments.empty()) {
      args_storage.push_back("--args");
      args_storage.push_back(arguments);
    }
    if (low_latency) {
      args_storage.push_back("--low-latency");
    }

    int pid = 0;

#ifdef _WIN32
    // Build a single command-line string (needs proper quoting for args with spaces)
    std::string cmdline;
    for (size_t i = 0; i < args_storage.size(); ++i) {
      if (i > 0) cmdline += " ";
      const std::string &a = args_storage[i];
      bool need_quote = a.find(' ') != std::string::npos || a.find('"') != std::string::npos;
      if (need_quote) {
        cmdline += "\"";
        for (char c : a) {
          if (c == '"') cmdline += "\\\"";
          else cmdline += c;
        }
        cmdline += "\"";
      } else {
        cmdline += a;
      }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Open NUL and redirect stdin/stdout/stderr to it (detached console output)
    HANDLE hNull = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hNull != INVALID_HANDLE_VALUE) {
      si.hStdInput = hNull;
      si.hStdOutput = hNull;
      si.hStdError = hNull;
    } else {
      si.hStdInput = INVALID_HANDLE_VALUE;
      si.hStdOutput = INVALID_HANDLE_VALUE;
      si.hStdError = INVALID_HANDLE_VALUE;
    }

    // CreateProcess needs a mutable buffer for the command line
    std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back('\0');

    BOOL ok = CreateProcessA(
        NULL,                        // use command line to resolve exe
        cmdline_buf.data(),          // command line (mutable)
        NULL, NULL,                  // process/thread security attrs
        TRUE,                        // inherit handles (so std handles are inherited)
        CREATE_NO_WINDOW,            // creation flags
        NULL,                        // environment
        NULL,                        // current dir
        &si, &pi);

    if (!ok) {
      DWORD err = GetLastError();
      if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
      char msg[256];
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, msg, sizeof(msg), NULL);
      throw std::runtime_error(fmt::format("CreateProcess failed for {}: {}", exe_path, msg));
    }
    pid = static_cast<int>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
#else
    // Convert to char* array (must remain valid until execvp)
    std::vector<char *> argv;
    for (auto &a : args_storage) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t child_pid = fork();
    if (child_pid < 0) {
      throw std::runtime_error(fmt::format("fork failed: {}", strerror(errno)));
    }
    if (child_pid == 0) {
      // Child: detach from parent's process group, redirect stdio to /dev/null, then exec
      setsid();
      int devnull = ::open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        ::close(devnull);
      }
      execvp(exe_path.c_str(), argv.data());
      // execvp only returns on failure
      SPDLOG_ERROR("execvp failed for {}: {}", exe_path, strerror(errno));
      _exit(127);
    }
    pid = static_cast<int>(child_pid);
#endif

    SPDLOG_INFO("started strategy {} (mode={}, group={}, name={}, pid={})",
                strategy_type, mode_str, group, name, pid);

    nlohmann::json result;
    result["success"] = true;
    result["pid"] = pid;
    return result;
  }

  /// Stop a running strategy gracefully by sending RequestStop.
  /// The strategy's apprentice catches RequestStop and calls signal_stop().
  /// Note: this returns immediately after sending the mark. The strategy exits
  /// asynchronously and the master's health check deregisters it (~1s).
  /// SIGCHLD is set to SIG_IGN so child processes are auto-reaped (no zombies).
  nlohmann::json handle_stop_strategy(const nlohmann::json &data) {
    auto strat = resolve_location(data);
    if (!strat) throw std::runtime_error("invalid strategy location");
    if (strat->category != category::STRATEGY)
      throw std::runtime_error(fmt::format("{} is not a strategy", strat->uname));
    if (!is_location_live(strat->uid))
      throw std::runtime_error(fmt::format("strategy {} is not running", strat->uname));

    if (!has_writer(strat->uid))
      throw std::runtime_error(fmt::format("no writer to strategy {}", strat->uname));

    // Write RequestStop mark to (API, strategy_uid) journal.
    // The strategy reads this via AutoClient's request_read_from(api_uid).
    get_writer(strat->uid)->mark(now(), RequestStop::tag);
    SPDLOG_INFO("sent RequestStop to strategy {:08x} ({}), will exit asynchronously",
                strat->uid, strat->uname);

    nlohmann::json result;
    result["success"] = true;
    result["strategy_uid"] = fmt::format("{:08x}", strat->uid);
    result["note"] = "strategy will exit asynchronously; wait ~1s before restarting";
    return result;
  }

  // ---- location resolution ----
  location_ptr resolve_location(const nlohmann::json &data) {
    if (!data.contains("location")) return nullptr;
    const auto &loc = data["location"];

    if (loc.contains("uid")) {
      uint32_t uid = parse_uid_hex(loc["uid"].get<std::string>());
      if (has_location(uid)) return get_location(uid);
    }

    if (loc.contains("category") && loc.contains("group") && loc.contains("name")) {
      auto cat = get_category_by_name(loc["category"].get<std::string>());
      auto m = loc.contains("mode") ? get_mode_by_name(loc["mode"].get<std::string>())
                                    : get_io_device()->get_home()->mode;
      auto group = loc["group"].get<std::string>();
      auto name = loc["name"].get<std::string>();
      return location::make_shared(m, cat, group, name, get_locator());
    }
    return nullptr;
  }

  // ---- push helpers ----

  /// Build a binary frame: [0x42][frame_header][raw struct data]
  template <typename T> static std::string build_binary_frame(const event_ptr &event) {
    constexpr size_t header_size = sizeof(frame_header);
    constexpr size_t data_size = sizeof(T);
    const size_t total = 1 + header_size + data_size;

    std::string msg(total, '\0');
    char *buf = msg.data();

    buf[0] = BINARY_FRAME_MARKER;

    auto *hdr = reinterpret_cast<frame_header *>(buf + 1);
    hdr->length = static_cast<uint32_t>(header_size + data_size);
    hdr->header_length = static_cast<uint32_t>(header_size);
    hdr->gen_time = event->gen_time();
    hdr->trigger_time = event->trigger_time();
    hdr->msg_type = T::tag;
    hdr->source = event->source();
    hdr->dest = event->dest();

    const T &data = event->template data<T>();
    memcpy(buf + 1 + header_size, &data, data_size);
    return msg;
  }

  /// Queue a message for a specific session's send buffer.
  void queue_send(uint32_t session_id, std::string msg) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    std::lock_guard<std::mutex> slock(it->second.send_mutex);
    it->second.send_queue.push_back(std::move(msg));
  }

  /// Broadcast a binary frame to ALL connected clients.
  template <typename T> void broadcast_binary(const event_ptr &event) {
    std::string msg = build_binary_frame<T>(event);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &[id, session] : sessions_) {
      std::lock_guard<std::mutex> slock(session.send_mutex);
      session.send_queue.push_back(msg);
    }
  }

  /// Push a Quote only to clients that subscribed to that instrument.
  void push_quote(const event_ptr &event) {
    const auto &quote = event->data<Quote>();
    uint32_t key = hash_instrument(quote.exchange_id, quote.instrument_id);

    std::string msg = build_binary_frame<Quote>(event);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &[id, session] : sessions_) {
      if (session.subscribed_keys.count(key)) {
        std::lock_guard<std::mutex> slock(session.send_mutex);
        session.send_queue.push_back(msg);
      }
    }
  }

  /// Push Order/Trade only to clients that subscribed to the strategy (event->dest()).
  /// Manual orders issued by clients via issue_order are routed by manual_order_sessions_
  /// instead, since TD acks them with dest = api home uid, not a strategy uid.
  template <typename T> void push_strategy_event(const event_ptr &event) {
    std::string msg = build_binary_frame<T>(event);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    if constexpr (std::is_same_v<T, Order> || std::is_same_v<T, Trade>) {
      auto manual_it = manual_order_sessions_.find(event->data<T>().order_id);
      if (manual_it != manual_order_sessions_.end()) {
        auto session_it = sessions_.find(manual_it->second);
        if (session_it != sessions_.end()) {
          std::lock_guard<std::mutex> slock(session_it->second.send_mutex);
          session_it->second.send_queue.push_back(msg);
        }
        return;
      }
    }
    uint32_t strategy_uid = event->dest();
    for (auto &[id, session] : sessions_) {
      if (session.subscribed_strategies.count(strategy_uid)) {
        std::lock_guard<std::mutex> slock(session.send_mutex);
        session.send_queue.push_back(msg);
      }
    }
  }

  /// Send a JSON response to a specific client.
  void send_json(uint32_t session_id, const nlohmann::json &j) {
    std::string s = j.dump();
    queue_send(session_id, std::move(s));
  }
};

int main(int argc, char **argv) {
  std::string mode_str = "live";
  std::string host = "0.0.0.0";
  int port = 7788;
  bool low_latency = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--mode" && i + 1 < argc) {
      mode_str = argv[++i];
    } else if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--low-latency") {
      low_latency = true;
    }
  }

  mode m = mode::LIVE;
  if (mode_str == "sim") {
    m = mode::DATA;
  } else if (mode_str == "replay") {
    m = mode::REPLAY;
  }

  auto loc = std::make_shared<locator>(m);

  SPDLOG_INFO("starting api service with mode={}, host={}, port={}, low_latency={}", mode_str, host, port,
              low_latency);

  ApiService service(loc, m, host, port, low_latency);
  service.run();

  return 0;
}
