#include <kungfu/yijinjing/practice/hero.h>
#include <chrono>
#include <thread>

namespace kungfu::yijinjing::practice {

hero::hero(io::Locator& locator, io::mode m, bool low_latency)
    : locator_(locator), mode_(m), low_latency_(low_latency), reader_(locator) {}

void hero::run() {
    running_ = true;

    auto observable = rxcpp::observable<>::create<event_ptr>(
        [this](rxcpp::subscriber<event_ptr> subscriber) {
            produce(subscriber);
        }
    ).publish();

    events_ = observable;

    react();

    observable.connect();

    on_exit();
}

void hero::stop() {
    running_ = false;
}

void hero::produce(rxcpp::subscriber<event_ptr>& subscriber) {
    while (running_) {
        // Drain nng control messages
        if (pull_socket_) {
            std::string msg;
            while (pull_socket_->recv(msg, 0)) {
                // For Phase 1: nng messages are notification-only
                // Full JSON message parsing would go here in Phase 2
            }
        }

        // Drain journal frames
        while (reader_.data_available()) {
            auto frame = reader_.current_frame();
            auto event = std::make_shared<Event>();
            event->frame = frame;

            auto it = locations_.find(frame.source());
            if (it != locations_.end()) {
                event->source_location = it->second;
            }

            subscriber.on_next(event);
            reader_.next();
        }

        on_active();

        if (!low_latency_) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    subscriber.on_completed();
}

std::shared_ptr<journal::Writer> hero::get_writer(const io::location_ptr& source, uint32_t dest_uid) {
    auto it = writers_.find(dest_uid);
    if (it != writers_.end()) return it->second;

    auto writer = std::make_shared<journal::Writer>(source, dest_uid, locator_);
    writers_[dest_uid] = writer;
    return writer;
}

void hero::register_location(const io::location_ptr& loc) {
    locations_[loc->uid] = loc;
}

int64_t hero::now_ns() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace kungfu::yijinjing::practice
