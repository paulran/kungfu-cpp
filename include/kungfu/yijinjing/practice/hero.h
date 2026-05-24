#pragma once

#include <kungfu/yijinjing/journal/reader.h>
#include <kungfu/yijinjing/journal/writer.h>
#include <kungfu/yijinjing/io/locator.h>
#include <kungfu/yijinjing/nanomsg/socket.h>
#include <rxcpp/rx.hpp>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <functional>

namespace kungfu::yijinjing::practice {

struct Event {
    journal::Frame frame;
    io::location_ptr source_location;

    int32_t msg_type() const { return frame.msg_type(); }
    int64_t gen_time() const { return frame.gen_time(); }
    uint32_t source() const { return frame.source(); }
    uint32_t dest() const { return frame.dest(); }

    template<typename T>
    const T& data() const { return frame.data<T>(); }
};

using event_ptr = std::shared_ptr<Event>;

class hero {
public:
    hero(io::Locator& locator, io::mode m, bool low_latency);
    virtual ~hero() = default;

    void run();
    void stop();

    virtual void react() = 0;
    virtual void on_active() {}
    virtual void on_exit() {}

protected:
    io::Locator& locator_;
    io::mode mode_;
    bool low_latency_;
    std::atomic<bool> running_{false};

    rxcpp::observable<event_ptr> events_;
    rxcpp::composite_subscription lifetime_;

    journal::Reader reader_;
    std::unordered_map<uint32_t, std::shared_ptr<journal::Writer>> writers_;
    std::unordered_map<uint32_t, io::location_ptr> locations_;

    std::shared_ptr<journal::Writer> get_writer(const io::location_ptr& source, uint32_t dest_uid);
    void register_location(const io::location_ptr& loc);

    int64_t now_ns() const;

    // nng sockets for master side
    std::unique_ptr<nanomsg::Socket> pub_socket_;
    std::unique_ptr<nanomsg::Socket> pull_socket_;

private:
    void produce(rxcpp::subscriber<event_ptr>& subscriber);
};

} // namespace kungfu::yijinjing::practice
