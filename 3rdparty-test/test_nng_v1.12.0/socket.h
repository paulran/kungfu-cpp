#pragma once

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nng/compat/nanomsg/nn.h>
#include <nng/compat/nanomsg/pipeline.h>
#include <nng/compat/nanomsg/pubsub.h>
#include <nng/compat/nanomsg/reqrep.h>

// Forward declaration for url_factory
namespace data {
struct location {};
typedef std::shared_ptr<location> location_ptr;
}


#define MAX_MSG_LENGTH (16 * 1024)


enum class protocol : int {
  UNKNOWN = -1,
  REPLY = NN_REP,
  REQUEST = NN_REQ,
  PUSH = NN_PUSH,
  PULL = NN_PULL,
  PUBLISH = NN_PUB,
  SUBSCRIBE = NN_SUB
};

inline std::string get_protocol_name(protocol p) {
  switch (p) {
  case protocol::REPLY:
    return "rep";
  case protocol::REQUEST:
    return "req";
  case protocol::PUSH:
    return "push";
  case protocol::PULL:
    return "pull";
  case protocol::PUBLISH:
    return "pub";
  case protocol::SUBSCRIBE:
    return "sub";
  default:
    return "unknown";
  }
}

[[maybe_unused]] inline protocol get_opposite_protol(protocol p) {
  switch (p) {
  case protocol::REPLY:
    return protocol::REQUEST;
  case protocol::REQUEST:
    return protocol::REPLY;
  case protocol::PUSH:
    return protocol::PULL;
  case protocol::PULL:
    return protocol::PUSH;
  case protocol::PUBLISH:
    return protocol::SUBSCRIBE;
  case protocol::SUBSCRIBE:
    return protocol::PUBLISH;
  default:
    return protocol::UNKNOWN;
  }
}

class url_factory {
public:
  [[nodiscard]] virtual std::string make_path_bind(data::location_ptr location, protocol p) const = 0;

  [[nodiscard]] virtual std::string make_path_connect(data::location_ptr location, protocol p) const = 0;
};


class nn_exception : public std::exception {
public:
  nn_exception() : errno_(nn_errno()) {}

  [[nodiscard]] const char *what() const noexcept override;

  [[maybe_unused]] [[nodiscard]] int num() const;

private:
  int errno_;
};


class socket {
public:
  explicit socket(protocol p) : socket(AF_SP, p, MAX_MSG_LENGTH){};

  socket(int domain, protocol p) : socket(domain, p, MAX_MSG_LENGTH){};

  socket(int domain, protocol p, int buffer_size);

  ~socket();

  void setsockopt(int level, int option, const void *optval, size_t optvallen) const;

  void setsockopt_str(int level, int option, const std::string &value) const;

  void setsockopt_int(int level, int option, int value) const;

  void getsockopt(int level, int option, void *optval, size_t *optvallen) const;

  [[maybe_unused]] [[nodiscard]] int getsockopt_int(int level, int option) const;

  int bind(const std::string &path);

  int connect(const std::string &path);

  [[maybe_unused]] void shutdown(int how = 0) const;

  void close() const;

  int send(const std::string &msg, int flags = NN_DONTWAIT) const;

  int recv(int flags = NN_DONTWAIT);

  const std::string &recv_msg(int flags = NN_DONTWAIT);

  [[maybe_unused]] [[nodiscard]] int send_json(const nlohmann::json &msg, int flags = NN_DONTWAIT) const;

  [[maybe_unused]] nlohmann::json recv_json(int flags = 0);

  const std::string &request(const std::string &json_message);

  [[maybe_unused]] [[nodiscard]] protocol get_protocol() const { return protocol_; };

  [[nodiscard]] const std::string &get_url() const { return url_; };

  [[nodiscard]] const std::string &last_message() const { return message_; };

private:
  int sock_;
  protocol protocol_;
  std::string url_;
  std::vector<char> buf_;
  std::string message_;

  /*  Prevent making copies of the socket by accident. */
  socket(const socket &);

  void operator=(const socket &);
};
