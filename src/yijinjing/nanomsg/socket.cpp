#include <kungfu/yijinjing/nanomsg/socket.h>
#include <nng/nng.h>
#include <stdexcept>
#include <cstring>

namespace kungfu::yijinjing::nanomsg {

struct Socket::Impl {
    nng_socket sock = NNG_SOCKET_INITIALIZER;
    protocol proto;
    bool opened = false;

    void open(protocol p) {
        int rv = 0;
        proto = p;
        switch (p) {
            case protocol::PUB:  rv = nng_pub0_open(&sock); break;
            case protocol::SUB:  rv = nng_sub0_open(&sock); break;
            case protocol::PUSH: rv = nng_push0_open(&sock); break;
            case protocol::PULL: rv = nng_pull0_open(&sock); break;
        }
        if (rv != 0) throw std::runtime_error(std::string("nng open failed: ") + nng_strerror(static_cast<nng_err>(rv)));
        opened = true;

        if (p == protocol::SUB) {
            nng_sub0_socket_subscribe(sock, "", 0);
        }
    }
};

Socket::Socket(protocol proto) : impl_(std::make_unique<Impl>()) {
    impl_->open(proto);
}

Socket::~Socket() {
    if (impl_ && impl_->opened) {
        nng_socket_close(impl_->sock);
    }
}

Socket::Socket(Socket&& other) noexcept : impl_(std::move(other.impl_)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->opened) nng_socket_close(impl_->sock);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void Socket::bind(const std::string& url) {
    nng_listener listener;
    int rv = nng_listen(impl_->sock, url.c_str(), &listener, 0);
    if (rv != 0) throw std::runtime_error("nng bind failed on " + url + ": " + nng_strerror(static_cast<nng_err>(rv)));
}

void Socket::connect(const std::string& url) {
    nng_dialer dialer;
    int rv = nng_dial(impl_->sock, url.c_str(), &dialer, 0);
    if (rv != 0) throw std::runtime_error("nng connect failed on " + url + ": " + nng_strerror(static_cast<nng_err>(rv)));
}

void Socket::close() {
    if (impl_ && impl_->opened) {
        nng_socket_close(impl_->sock);
        impl_->opened = false;
    }
}

bool Socket::send(const std::string& msg) {
    int rv = nng_send(impl_->sock, const_cast<char*>(msg.data()),
                      msg.size(), NNG_FLAG_NONBLOCK);
    return rv == 0;
}

bool Socket::recv(std::string& msg, int timeout_ms) {
    if (timeout_ms >= 0) {
        nng_socket_set_ms(impl_->sock, NNG_OPT_RECVTIMEO, timeout_ms);
    }

    nng_msg* nmsg = nullptr;
    int rv = nng_recvmsg(impl_->sock, &nmsg, 0);
    if (rv != 0) return false;

    msg.assign(static_cast<char*>(nng_msg_body(nmsg)), nng_msg_len(nmsg));
    nng_msg_free(nmsg);
    return true;
}

bool Socket::is_open() const {
    return impl_ && impl_->opened;
}

} // namespace kungfu::yijinjing::nanomsg
