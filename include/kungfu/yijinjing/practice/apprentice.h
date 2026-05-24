#pragma once

#include <kungfu/yijinjing/practice/hero.h>
#include <kungfu/yijinjing/practice/protocol.h>

namespace kungfu::yijinjing::practice {

class apprentice : public hero {
public:
    apprentice(const io::location_ptr& home, io::Locator& locator, bool low_latency);
    ~apprentice() override = default;

    void on_start() override;
    void on_active() override;

    io::location_ptr home() const { return home_; }
    uint32_t home_uid() const { return home_->uid; }

    void request_write_to(uint32_t dest_uid);
    void request_read_from(const io::location_ptr& source, uint32_t dest_uid, int64_t from_time = 0);

protected:
    io::location_ptr home_;
    io::location_ptr master_location_;

    std::unique_ptr<nanomsg::Socket> push_socket_;
    std::unique_ptr<nanomsg::Socket> sub_socket_;

    void register_to_master();
    virtual void on_channel(uint32_t source_uid, uint32_t dest_uid) {}
};

} // namespace kungfu::yijinjing::practice
