#pragma once

#include <kungfu/yijinjing/practice/hero.h>
#include <kungfu/yijinjing/practice/protocol.h>
#include <kungfu/longfist/types.h>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace kungfu::service {

class Master : public yijinjing::practice::hero {
public:
    Master(yijinjing::io::Locator& locator, bool low_latency);

    void react() override;
    void on_active() override;
    void on_exit() override;

private:
    void register_app(const yijinjing::practice::RegisterMessage& reg);
    void deregister_app(uint32_t app_uid);
    void on_request_write_to(uint32_t source_uid, uint32_t dest_uid);
    void on_request_read_from(uint32_t source_uid, uint32_t dest_uid);
    void publish_channel(uint32_t source_uid, uint32_t dest_uid);

    yijinjing::io::location_ptr master_location_;
    std::unordered_map<uint32_t, int32_t> app_pids_; // uid -> pid

    static constexpr uint32_t PUBLIC_UID = 0;
};

} // namespace kungfu::service
