// SPDX-License-Identifier: Apache-2.0

// Interactive console client that talks to apps/api.cpp (kf_api).
//
// Usage:
//   kf_api_demo [--host 127.0.0.1] [--port 7788]
//
// It opens a raw TCP socket with length-prefixed framing, connects to the
// api service, then presents a menu covering every request method the
// server understands:
//   get_locations, get_strategies, get_trading_day, now, get_subscriptions,
//   is_ready_to_interact, request_market_data, cancel_market_data,
//   request_position, issue_order, cancel_order
//
// A background thread prints JSON responses and decodes the binary push
// frames (quotes, orders, trades, positions, assets, broker states, ...).
//
// Wire protocol (mirrors apps/api.cpp):
//   Each message: [4 bytes big-endian length][payload]
//   JSON    starts with '{'  -> {"request_id":N,"method":"...","data":{...}}
//   Binary  starts with 'B' (0x42) -> [0x42][frame_header][raw struct]
//
// The terminal is split into two regions:
//   TOP REGION   — server responses, binary push frames, log output (auto-scroll)
//   BOTTOM REGION — menu banner, prompts, user input line (fixed)
//
// ANSI escape sequences (compatible with linux / WSL / xterm / tmux):
//   \e[1;Nr    DECSTBM  — set vertical scroll region (lines 1..N inclusive)
//   \e[s / \e[u         — save / restore cursor position
//   \e[n;mH              — move cursor to (row=n, col=m)
//   \e[2K                — erase entire line
//   \e[0m                — reset SGR attributes
//   \e[1;36m             — bold cyan
//   \e[2J                — erase whole display

#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>

#include <nlohmann/json.hpp>
#include <fmt/format.h>

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
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = SSIZE_T;
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

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
using namespace kungfu::longfist::types;

namespace {

constexpr char BINARY_FRAME_MARKER = 0x42;
constexpr uint32_t MAX_MSG_SIZE = 64 * 1024;

// Layout: keep last 3 rows for input banner + prompt line
constexpr int INPUT_ZONE_HEIGHT = 3;
constexpr int INPUT_PROMPT_LINE = 2; // offset from bottom row

std::mutex g_cout_mutex;
std::atomic<bool> g_stop{false};
socket_t g_sock_fd = INVALID_SOCKET_FD;

int g_term_rows = 24;
int g_term_cols = 80;
bool g_use_split = true; // will be set to false if terminal can't be detected / not a TTY

int top_rows() { return std::max(1, g_term_rows - INPUT_ZONE_HEIGHT); }
int separator_row() { return top_rows() + 1; }
int banner_row() { return separator_row() + 1; }
int prompt_row() { return banner_row() + 1; }

// ---- low-level ANSI helpers (assume cout lock held by caller) ----
void ansi(const char *seq) { std::cout << seq; }
void set_scroll_region(int top, int bottom) {
  std::cout << fmt::format("\e[{};{}r", top, bottom);
}
void goto_row_col(int row, int col = 1) {
  std::cout << fmt::format("\e[{};{}H", row, col);
}
void erase_line() { std::cout << "\e[2K"; }
void save_cursor() { std::cout << "\e[s"; }
void restore_cursor() { std::cout << "\e[u"; }
void flush_io() { std::cout.flush(); }

// ---- split screen init / teardown ----
void detect_term_size() {
#ifdef _WIN32
  // Use Windows console API to get terminal size
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) {
    g_use_split = false;
    return;
  }
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
    g_use_split = false;
    return;
  }
  int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  if (rows > 4) {
    g_term_rows = rows;
    g_term_cols = cols;
    g_use_split = true;
  } else {
    g_use_split = false;
  }
#else
  struct winsize ws;
  if (isatty(STDOUT_FILENO) == 1 && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 4) {
    g_term_rows = static_cast<int>(ws.ws_row);
    g_term_cols = static_cast<int>(ws.ws_col);
    g_use_split = true;
  } else {
    g_use_split = false;
  }
#endif
}

void init_split_screen() {
  detect_term_size();
  if (!g_use_split) return;
  std::lock_guard<std::mutex> lock(g_cout_mutex);
  ansi("\e[2J");            // clear display
  goto_row_col(1, 1);      // cursor to home
  set_scroll_region(1, top_rows());
  ansi("\e[H");
  flush_io();
}

void redraw_separator_and_banner_locked() {
  if (!g_use_split) return;
  std::string sep(static_cast<size_t>(g_term_cols), '-');
  goto_row_col(separator_row(), 1);
  erase_line();
  std::cout << "\e[1;36m" << sep << "\e[0m";
  goto_row_col(banner_row(), 1);
  erase_line();
  std::cout << "\e[1;36m[input]\e[0m press 'h' for menu, '0' to quit.  Pending output scrolls above.";
  goto_row_col(prompt_row(), 1);
  erase_line();
  flush_io();
}

void exit_split_screen() {
  if (!g_use_split) return;
  std::lock_guard<std::mutex> lock(g_cout_mutex);
  // reset scroll region to whole screen, move cursor below separator
  set_scroll_region(1, g_term_rows);
  goto_row_col(prompt_row() + 1, 1);
  ansi("\e[0m");
  flush_io();
}

// ---- logging (TOP REGION) ----
void log(const std::string &line) {
  std::lock_guard<std::mutex> lock(g_cout_mutex);
  if (!g_use_split) {
    std::cout << line << std::endl;
    return;
  }
  // Save cursor (which may be sitting at input prompt), output into the
  // top scroll region, then restore cursor so input typing looks undisturbed.
  save_cursor();
  goto_row_col(top_rows(), 1);
  std::cout << line << "\n";
  flush_io();
  restore_cursor();
  flush_io();
}

// ---- input prompt helpers (BOTTOM REGION) ----
std::string read_line(const std::string &prompt) {
  if (!g_use_split) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    return line;
  }

  std::string line;
  {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    goto_row_col(prompt_row(), 1);
    erase_line();
    std::cout << prompt;
    flush_io();
  }
  // Read from stdin without holding the lock (so logs can still appear).
  if (!std::getline(std::cin, line)) {
    // re-enter lock to clear prompt line / preserve scroll state
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    goto_row_col(prompt_row(), 1);
    erase_line();
    flush_io();
    return {};
  }
  {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    // Echo the submitted command into the top log (so the user has a record)
    save_cursor();
    goto_row_col(top_rows(), 1);
    std::cout << "\e[33m[user]\e[0m " << prompt << line << "\n";
    flush_io();
    restore_cursor();
    erase_line();
    flush_io();
  }
  return line;
}

std::string read_line_default(const std::string &prompt, const std::string &def) {
  std::string s = read_line(prompt);
  if (s.empty()) return def;
  return s;
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
      if (n < 0 && (e == SOCKET_EWOULDBLOCK || e == EAGAIN)) continue;
      return false;
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

// Send a framed message: [4 bytes big-endian length][payload]
bool write_frame(socket_t fd, const std::string &body) {
  uint32_t net_len = htonl(static_cast<uint32_t>(body.size()));
#ifdef _WIN32
  if (::send(fd, reinterpret_cast<const char *>(&net_len), 4, 0) != 4) return false;
  if (::send(fd, body.c_str(), static_cast<int>(body.size()), 0) != static_cast<int>(body.size())) return false;
#else
  if (::send(fd, reinterpret_cast<const char *>(&net_len), 4, MSG_NOSIGNAL) != 4) return false;
  if (::send(fd, body.c_str(), body.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(body.size())) return false;
#endif
  return true;
}

nlohmann::json read_location(const std::string &what) {
  log(fmt::format("-- {} location (respond at prompt below) --", what));
  std::string mode = read_line_default("  by (u)id or (n)ame? [n]: ", "n");
  nlohmann::json loc = nlohmann::json::object();
  if (mode == "u") {
    loc["uid"] = read_line("  uid (hex, e.g. 0a1b2c3d): ");
  } else {
    loc["category"] = read_line_default("  category [td]: ", "td");
    loc["group"] = read_line("  group (e.g. simnow): ");
    loc["name"] = read_line("  name (e.g. 12345678): ");
    loc["mode"] = read_line_default("  mode [live]: ", "live");
  }
  return loc;
}

void decode_binary_frame(const char *buf, size_t len) {
  constexpr size_t header_size = sizeof(frame_header);
  if (len < 1 + header_size) {
    log(fmt::format("  [binary frame too short: {} bytes]", len));
    return;
  }

  const auto *hdr = reinterpret_cast<const frame_header *>(buf + 1);
  const char *body = buf + 1 + header_size;
  size_t body_len = len - 1 - header_size;

  auto msg_type = hdr->msg_type;
  auto gen_time = hdr->gen_time;
  auto trig_time = hdr->trigger_time;
  auto src = hdr->source;
  auto dst = hdr->dest;

  std::string header = fmt::format("gen_time={} trig_time={} type={} src={:08x} dst={:08x}",
                                   gen_time, trig_time, msg_type, src, dst);

#define DECODE_PACK(TYPE)                                                                              \
  case TYPE::tag: {                                                                                    \
    if (body_len >= sizeof(TYPE)) {                                                                    \
      TYPE d{};                                                                                        \
      std::memcpy(&d, body, sizeof(TYPE));                                                             \
      log(fmt::format("[{}] {} | {}", #TYPE, header, d.to_string()));                                  \
    } else {                                                                                           \
      log(fmt::format("[{}] {} | body too short ({} < {})", #TYPE, header, body_len, sizeof(TYPE)));   \
    }                                                                                                  \
    break;                                                                                             \
  }

  switch (msg_type) {
    DECODE_PACK(Quote)
    DECODE_PACK(Order)
    DECODE_PACK(Trade)
    DECODE_PACK(Position)
    DECODE_PACK(Asset)
    DECODE_PACK(AssetMargin)
    DECODE_PACK(BrokerStateUpdate)
    DECODE_PACK(Instrument)
    DECODE_PACK(Channel)
    case Register::tag:
      log(fmt::format("[Register] {} (variable-length payload, header-only)", header));
      break;
    case Deregister::tag:
      log(fmt::format("[Deregister] {} (variable-length payload, header-only)", header));
      break;
    default:
      log(fmt::format("[unknown msg_type {}] {}", msg_type, header));
      break;
  }
#undef DECODE_PACK
}

void receiver_loop() {
  while (!g_stop.load()) {
    std::string body;
    if (!read_frame(g_sock_fd, body, g_stop)) {
      if (!g_stop.load()) {
        log("[receiver] connection closed");
      }
      break;
    }
    if (body.empty()) continue;

    if (body[0] == BINARY_FRAME_MARKER) {
      decode_binary_frame(body.data(), body.size());
    } else if (body[0] == '{') {
      try {
        auto j = nlohmann::json::parse(body);
        std::string kind = j.value("msg_type", "json");
        if (kind == "response") {
          uint64_t rid = j.value("request_id", 0ULL);
          if (j.contains("error") && !j["error"].is_null()) {
            log(fmt::format("[RESPONSE id={}] ERROR: {}", rid, j["error"].dump()));
          } else {
            log(fmt::format("[RESPONSE id={}] {}", rid, j.value("data", nlohmann::json()).dump()));
          }
        } else {
          log(fmt::format("[JSON] {}", j.dump()));
        }
      } catch (const std::exception &e) {
        log(fmt::format("[json parse error] {}: {}", e.what(), body));
      }
    }
  }
}

void send_request(uint64_t request_id, const std::string &method, const nlohmann::json &data) {
  nlohmann::json req = {{"request_id", request_id}, {"method", method}, {"data", data}};
  std::string s = req.dump();
  if (!write_frame(g_sock_fd, s)) {
    log(fmt::format("[send] failed: {}", socket_strerror(socket_errno())));
    return;
  }
  log(fmt::format(">> {} (id={}) sent", method, request_id));
}

void menu() {
  if (!g_use_split) {
    log("\n================ kungfu api demo ================");
    log("  1  get_locations          9  request_position");
    log("  2  get_strategies        10  issue_order");
    log("  3  get_trading_day       11  cancel_order");
    log("  4  now                   12  subscribe_strategy");
    log("  5  get_subscriptions     13  unsubscribe_strategy");
    log("  6  is_ready_to_interact  14  start_strategy");
    log("  7  request_market_data   15  stop_strategy");
    log("  8  cancel_market_data     0  quit");
    log("=================================================");
    return;
  }

  // In split-screen mode, render the menu into the top log region as a block.
  const char *lines[] = {
      "================ kungfu api demo ================",
      "  1  get_locations          9  request_position",
      "  2  get_strategies        10  issue_order",
      "  3  get_trading_day       11  cancel_order",
      "  4  now                   12  subscribe_strategy",
      "  5  get_subscriptions     13  unsubscribe_strategy",
      "  6  is_ready_to_interact  14  start_strategy",
      "  7  request_market_data   15  stop_strategy",
      "  8  cancel_market_data     0  quit",
      "=================================================",
      nullptr,
  };
  for (int i = 0; lines[i]; ++i) log(lines[i]);
}

} // namespace

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  int port = 7788;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--no-split") {
      g_use_split = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: kf_api_demo [--host HOST] [--port PORT] [--no-split]\n";
      return 0;
    }
  }

  std::signal(SIGINT, [](int) {
    g_stop.store(true);
    exit_split_screen();
#ifdef _WIN32
    ExitProcess(0);
#else
    std::_Exit(0);
#endif
  });

#ifdef _WIN32
  // Initialize Winsock
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed: " << socket_strerror(socket_errno()) << std::endl;
    return 1;
  }
#endif

  init_split_screen();

  g_sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (g_sock_fd == INVALID_SOCKET_FD) {
    exit_split_screen();
    std::cerr << "failed to create socket: " << socket_strerror(socket_errno()) << std::endl;
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    exit_split_screen();
    std::cerr << "invalid host: " << host << std::endl;
    close_socket(g_sock_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  if (::connect(g_sock_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    exit_split_screen();
    std::cerr << "failed to connect to " << host << ":" << port << ": " << socket_strerror(socket_errno()) << std::endl;
    close_socket(g_sock_fd);
    g_sock_fd = INVALID_SOCKET_FD;
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  // Set receive timeout
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 200 * 1000;
  ::setsockopt(g_sock_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));

  // After logging banner messages, draw the input separator & banner
  log(fmt::format("connected to {}:{} (TCP). server pushes scroll above this line.", host, port));
  log("tip: start kf_api first, e.g.  kf_api --mode live --host 0.0.0.0 --port 7788");
  if (g_use_split) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    redraw_separator_and_banner_locked();
  }

  std::thread rx(receiver_loop);

  uint64_t request_id = 1;
  menu();

  while (!g_stop.load()) {
    std::string choice = read_line("\n> ");
    if (std::cin.eof() || choice == "0" || choice == "q") {
      break;
    }
    if (choice == "h" || choice == "help") {
      menu();
      continue;
    }
    if (choice.empty()) {
      continue;
    }

    try {
      if (choice == "1") {
        send_request(request_id++, "get_locations", nlohmann::json::object());
      } else if (choice == "2") {
        send_request(request_id++, "get_strategies", nlohmann::json::object());
      } else if (choice == "3") {
        send_request(request_id++, "get_trading_day", nlohmann::json::object());
      } else if (choice == "4") {
        send_request(request_id++, "now", nlohmann::json::object());
      } else if (choice == "5") {
        send_request(request_id++, "get_subscriptions", nlohmann::json::object());
      } else if (choice == "6") {
        nlohmann::json data;
        data["location"] = read_location("interact");
        send_request(request_id++, "is_ready_to_interact", data);
      } else if (choice == "7") {
        nlohmann::json data;
        data["location"] = read_location("md");
        data["exchange_id"] = read_line_default("  exchange_id [SSE]: ", "SSE");
        data["instrument_id"] = read_line("  instrument_id (e.g. 600000): ");
        send_request(request_id++, "request_market_data", data);
      } else if (choice == "8") {
        nlohmann::json data;
        data["location"] = read_location("md");
        data["exchange_id"] = read_line_default("  exchange_id [SSE]: ", "SSE");
        data["instrument_id"] = read_line("  instrument_id (e.g. 600000): ");
        send_request(request_id++, "cancel_market_data", data);
      } else if (choice == "9") {
        send_request(request_id++, "request_position", nlohmann::json::object());
      } else if (choice == "10") {
        nlohmann::json data;
        data["location"] = read_location("account/td");
        data["exchange_id"] = read_line_default("  exchange_id [SSE]: ", "SSE");
        data["instrument_id"] = read_line("  instrument_id (e.g. 600000): ");
        data["limit_price"] = std::stod(read_line_default("  limit_price [10.00]: ", "10.00"));
        data["volume"] = std::stoll(read_line_default("  volume [100]: ", "100"));
        data["side"] = read_line_default("  side [Buy/Sell] [Buy]: ", "Buy");
        data["offset"] = read_line_default("  offset [Open/Close] [Open]: ", "Open");
        data["price_type"] = read_line_default("  price_type [Limit] [Limit]: ", "Limit");
        data["hedge_flag"] = read_line_default("  hedge_flag [Speculation] [Speculation]: ", "Speculation");
        data["volume_condition"] = read_line_default("  volume_condition [Any] [Any]: ", "Any");
        data["time_condition"] = read_line_default("  time_condition [GFD] [GFD]: ", "GFD");
        send_request(request_id++, "issue_order", data);
      } else if (choice == "11") {
        nlohmann::json data;
        data["location"] = read_location("account/td");
        std::string order_id_str = read_line("  order_id (decimal, from issue_order response): ");
        data["order_id"] = std::stoull(order_id_str);
        send_request(request_id++, "cancel_order", data);
      } else if (choice == "12") {
        nlohmann::json data;
        log("Subscribe to a strategy's Order/Trade stream.");
        log("  Use get_strategies (menu 2) first to find the strategy uid.");
        data["location"] = read_location("strategy");
        send_request(request_id++, "subscribe_strategy", data);
      } else if (choice == "13") {
        nlohmann::json data;
        data["location"] = read_location("strategy");
        send_request(request_id++, "unsubscribe_strategy", data);
      } else if (choice == "14") {
        nlohmann::json data;
        log("Start (launch) a strategy process.");
        data["mode"] = read_line_default("  mode [live]: ", "live");
        data["group"] = read_line_default("  group [sim]: ", "sim");
        data["name"] = read_line("  name (e.g. sim): ");
        data["strategy"] = read_line_default("  strategy [kungfu_strategy_101]: ", "kungfu_strategy_101");
        data["exe"] = read_line_default("  exe path [./Release/kf_strategy]: ", "./Release/kf_strategy");
        data["args"] = read_line_default("  args (optional, Enter to skip): ", "");
        send_request(request_id++, "start_strategy", data);
      } else if (choice == "15") {
        nlohmann::json data;
        log("Stop a running strategy gracefully (sends RequestStop).");
        data["location"] = read_location("strategy");
        send_request(request_id++, "stop_strategy", data);
      } else {
        log("unknown choice, press 'h' for help");
      }
    } catch (const std::exception &e) {
      log(fmt::format("input error: {}", e.what()));
    }
  }

  g_stop.store(true);
  log("shutting down...");
  if (rx.joinable()) {
    rx.join();
  }
  if (g_sock_fd != INVALID_SOCKET_FD) {
    shutdown_socket(g_sock_fd, SOCKET_SHUT_RDWR);
    close_socket(g_sock_fd);
    g_sock_fd = INVALID_SOCKET_FD;
  }
  exit_split_screen();
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
