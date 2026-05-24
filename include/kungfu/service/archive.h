#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <cstdint>

namespace kungfu::service {

class Archive : public yijinjing::practice::apprentice {
public:
    Archive(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator,
            bool low_latency, int archive_days = 7);

    void react() override;
    void on_active() override;

private:
    void cleanup_old_journals();

    int archive_days_;
    int64_t last_cleanup_time_ = 0;
};

} // namespace kungfu::service
