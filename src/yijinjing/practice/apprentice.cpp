#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/longfist/types.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace kungfu::yijinjing::practice {

apprentice::apprentice(const io::location_ptr& home, io::Locator& locator, bool low_latency)
    : hero(locator, home->m, low_latency), home_(home) {
    register_location(home_);

    master_location_ = io::location::make(
        io::category::SYSTEM, "master", "master", io::mode::LIVE);
    register_location(master_location_);
}

void apprentice::on_start() {
    register_to_master();
}

void apprentice::on_active() {
    if (!sub_socket_) return;

    std::string msg;
    while (sub_socket_->recv(msg, 0)) {
        try {
            auto j = nlohmann::json::parse(msg);
            int32_t msg_type = j.value("msg_type", 0);

            if (msg_type == longfist::types::Channel::tag) {
                uint32_t source_uid = j.value("source_uid", static_cast<uint32_t>(0));
                uint32_t dest_uid = j.value("dest_uid", static_cast<uint32_t>(0));

                if (j.contains("source_category") && locations_.find(source_uid) == locations_.end()) {
                    auto cat = static_cast<io::category>(j["source_category"].get<int32_t>());
                    auto group = j["source_group"].get<std::string>();
                    auto name = j["source_name"].get<std::string>();
                    auto mode = static_cast<io::mode>(j["source_mode"].get<int32_t>());
                    auto loc = io::location::make(cat, group, name, mode);
                    register_location(loc);
                }

                spdlog::info("apprentice: received channel source={} dest={}", source_uid, dest_uid);
                on_channel(source_uid, dest_uid);
            }
        } catch (const std::exception& e) {
            spdlog::warn("apprentice: failed to parse PUB message: {}", e.what());
        }
    }
}

void apprentice::register_to_master() {
    // Connect PUSH socket to Master's PULL
    push_socket_ = std::make_unique<nanomsg::Socket>(nanomsg::protocol::PUSH);
    auto pull_url = locator_.nn_path(master_location_, "pull");
    try {
        push_socket_->connect(pull_url);
    } catch (const std::exception& e) {
        spdlog::warn("apprentice: cannot connect to master PULL at {}: {}", pull_url, e.what());
        push_socket_.reset();
    }

    // Connect SUB socket to Master's PUB
    sub_socket_ = std::make_unique<nanomsg::Socket>(nanomsg::protocol::SUB);
    auto pub_url = locator_.nn_path(master_location_, "pub");
    try {
        sub_socket_->connect(pub_url);
    } catch (const std::exception& e) {
        spdlog::warn("apprentice: cannot connect to master PUB at {}: {}", pub_url, e.what());
        sub_socket_.reset();
    }

    // Send Register message
    if (push_socket_) {
#ifdef _WIN32
        int32_t pid = static_cast<int32_t>(GetCurrentProcessId());
#else
        int32_t pid = static_cast<int32_t>(getpid());
#endif
        auto msg = encode_register(home_, pid);
        push_socket_->send(msg);
        spdlog::info("apprentice: registered to master as {}, uid={}, pid={}",
                     home_->uname(), home_->uid, pid);
    }
}

void apprentice::request_write_to(uint32_t dest_uid) {
    if (push_socket_) {
        auto msg = encode_request_write_to(home_->uid, dest_uid);
        push_socket_->send(msg);
    }
    get_writer(home_, dest_uid);
}

void apprentice::request_read_from(const io::location_ptr& source, uint32_t dest_uid, int64_t from_time) {
    register_location(source);
    if (push_socket_) {
        auto msg = encode_request_read_from(source->uid, dest_uid, from_time);
        push_socket_->send(msg);
    }
    reader_.join(source, dest_uid, from_time);
}

} // namespace kungfu::yijinjing::practice
