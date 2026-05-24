#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/yijinjing/cache/store.h>
#include <memory>
#include <vector>
#include <utility>
#include <cstdint>
#include <string>

namespace kungfu::service {

class Cached : public yijinjing::practice::apprentice {
public:
    Cached(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator, bool low_latency);

    void react() override;
    void on_start() override;
    void on_active() override;

private:
    void flush_pending();

    std::unique_ptr<yijinjing::cache::StateStore> store_;
    std::vector<std::pair<int32_t, std::vector<char>>> pending_writes_; // msg_type + raw data
    int64_t last_flush_time_ = 0;
};

} // namespace kungfu::service
