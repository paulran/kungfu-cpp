#include <kungfu/service/master.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <chrono>

namespace kungfu::service {

Master::Master(yijinjing::io::Locator& locator, bool low_latency)
    : hero(locator, yijinjing::io::mode::LIVE, low_latency) {
    master_location_ = yijinjing::io::location::make(
        yijinjing::io::category::SYSTEM, "master", "master", yijinjing::io::mode::LIVE);
    register_location(master_location_);

    // Get writer for PUBLIC (dest_uid = 0)
    get_writer(master_location_, PUBLIC_UID);

    // Bind PUB socket for broadcasting to apprentices
    pub_socket_ = std::make_unique<yijinjing::nanomsg::Socket>(yijinjing::nanomsg::protocol::PUB);
    auto pub_url = locator_.nn_path(master_location_, "pub");
    pub_socket_->bind(pub_url);
    spdlog::info("Master: PUB bound to {}", pub_url);

    // Bind PULL socket for receiving messages from apprentices
    pull_socket_ = std::make_unique<yijinjing::nanomsg::Socket>(yijinjing::nanomsg::protocol::PULL);
    auto pull_url = locator_.nn_path(master_location_, "pull");
    pull_socket_->bind(pull_url);
    spdlog::info("Master: PULL bound to {}", pull_url);
}

void Master::react() {
    events_.subscribe(
        lifetime_,
        [this](yijinjing::practice::event_ptr event) {
            switch (event->msg_type()) {
                case longfist::types::Register::tag: {
                    // Register arrives via nng JSON, parsed in produce() → synthesized as event
                    const auto& reg = event->data<longfist::types::Register>();
                    yijinjing::practice::RegisterMessage msg;
                    msg.pid = reg.pid;
                    msg.uid = event->source();
                    // For events synthesized from nng, source location is pre-populated
                    if (event->source_location) {
                        msg.category = static_cast<int32_t>(event->source_location->cat);
                        msg.group = event->source_location->group;
                        msg.name = event->source_location->name;
                        msg.mode = static_cast<int32_t>(event->source_location->m);
                    }
                    register_app(msg);
                    break;
                }
                case longfist::types::RequestWriteTo::tag: {
                    on_request_write_to(event->source(), event->dest());
                    break;
                }
                case longfist::types::RequestReadFrom::tag: {
                    on_request_read_from(event->source(), event->dest());
                    break;
                }
                case longfist::types::RequestStop::tag: {
                    spdlog::info("Master: received stop request");
                    stop();
                    break;
                }
                default:
                    break;
            }
        },
        [](std::exception_ptr ep) {
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                spdlog::error("Master: event error: {}", e.what());
            }
        },
        []() {
            spdlog::info("Master: event stream completed");
        }
    );
}

void Master::on_active() {
    // Parse nng JSON messages from pull socket
    if (pull_socket_) {
        std::string msg;
        while (pull_socket_->recv(msg, 0)) {
            try {
                auto j = nlohmann::json::parse(msg);
                int32_t msg_type = j.value("msg_type", 0);

                if (msg_type == longfist::types::Register::tag) {
                    auto reg_msg = yijinjing::practice::decode_register(msg);
                    register_app(reg_msg);
                } else if (msg_type == longfist::types::RequestWriteTo::tag) {
                    uint32_t source_uid = j.value("source_uid", static_cast<uint32_t>(0));
                    uint32_t dest_uid = j.value("dest_uid", static_cast<uint32_t>(0));
                    on_request_write_to(source_uid, dest_uid);
                } else if (msg_type == longfist::types::RequestReadFrom::tag) {
                    uint32_t source_uid = j.value("source_uid", static_cast<uint32_t>(0));
                    uint32_t dest_uid = j.value("dest_uid", static_cast<uint32_t>(0));
                    on_request_read_from(source_uid, dest_uid);
                }
            } catch (const std::exception& e) {
                spdlog::error("Master: failed to parse nng message: {}", e.what());
            }
        }
    }
}

void Master::on_exit() {
    spdlog::info("Master: shutting down");
    if (pub_socket_) pub_socket_->close();
    if (pull_socket_) pull_socket_->close();
}

void Master::register_app(const yijinjing::practice::RegisterMessage& reg) {
    uint32_t app_uid = reg.uid;
    app_pids_[app_uid] = reg.pid;

    // Reconstruct location from register message
    auto app_location = yijinjing::practice::location_from_register(reg);
    register_location(app_location);

    spdlog::info("Master: registered app uid={} name={}/{} pid={}",
                 app_uid, reg.group, reg.name, reg.pid);

    // Get writer to app (private command channel)
    auto writer = get_writer(master_location_, app_uid);

    // Write RequestStart to the app's private channel
    longfist::types::RequestStart start_msg;
    writer->mark(now_ns(), longfist::types::RequestStart::tag);
}

void Master::deregister_app(uint32_t app_uid) {
    app_pids_.erase(app_uid);
    spdlog::info("Master: deregistered app uid={}", app_uid);
}

void Master::on_request_write_to(uint32_t source_uid, uint32_t dest_uid) {
    spdlog::info("Master: request_write_to source={} dest={}", source_uid, dest_uid);
    publish_channel(source_uid, dest_uid);
}

void Master::on_request_read_from(uint32_t source_uid, uint32_t dest_uid) {
    spdlog::info("Master: request_read_from source={} dest={}", source_uid, dest_uid);
    // Notify via channel publication so the reader knows about the source
    publish_channel(source_uid, dest_uid);
}

void Master::publish_channel(uint32_t source_uid, uint32_t dest_uid) {
    // Write Channel frame to PUBLIC journal so all apprentices can see it
    auto public_writer = get_writer(master_location_, PUBLIC_UID);

    longfist::types::Channel channel;
    channel.source_uid = source_uid;
    channel.dest_uid = dest_uid;

    public_writer->write(now_ns(), channel);

    // Also broadcast via NNG PUB so apprentices receive it immediately
    if (pub_socket_) {
        nlohmann::json j;
        j["msg_type"] = longfist::types::Channel::tag;
        j["source_uid"] = source_uid;
        j["dest_uid"] = dest_uid;

        auto it = locations_.find(source_uid);
        if (it != locations_.end()) {
            auto& loc = it->second;
            j["source_category"] = static_cast<int32_t>(loc->cat);
            j["source_group"] = loc->group;
            j["source_name"] = loc->name;
            j["source_mode"] = static_cast<int32_t>(loc->m);
        }

        pub_socket_->send(j.dump());
    }

    spdlog::info("Master: published channel source={} dest={}", source_uid, dest_uid);
}

} // namespace kungfu::service
