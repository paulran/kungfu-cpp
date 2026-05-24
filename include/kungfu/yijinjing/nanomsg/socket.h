#pragma once

#include <string>
#include <memory>

namespace kungfu::yijinjing::nanomsg {

enum class protocol { PUSH, PULL, PUB, SUB };

class Socket {
public:
    explicit Socket(protocol proto);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    void bind(const std::string& url);
    void connect(const std::string& url);
    void close();

    bool send(const std::string& msg);
    bool recv(std::string& msg, int timeout_ms = -1);

    bool is_open() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kungfu::yijinjing::nanomsg
