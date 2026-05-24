#pragma once

#include <kungfu/yijinjing/journal/page.h>
#include <kungfu/yijinjing/io/locator.h>
#include <memory>
#include <chrono>
#include <atomic>
#include <functional>

namespace kungfu::yijinjing::journal {

class Writer {
public:
    Writer(const io::location_ptr& source, uint32_t dest_uid,
           io::Locator& locator, size_t page_size = kungfu::DEFAULT_PAGE_SIZE);
    ~Writer() = default;

    template<typename T>
    void write(int64_t trigger_time, const T& data) {
        auto* hdr = open_frame(sizeof(T));
        std::memcpy(reinterpret_cast<char*>(hdr) + sizeof(FrameHeader), &data, sizeof(T));
        close_frame(hdr, sizeof(FrameHeader) + sizeof(T), now_ns(), T::tag);
    }

    void write_raw(int64_t trigger_time, int32_t msg_type,
                   const void* data, uint32_t data_length);
    void mark(int64_t trigger_time, int32_t msg_type);

    uint32_t dest_uid() const { return dest_uid_; }
    uint32_t source_uid() const { return source_->uid; }

    using NotifyCallback = std::function<void()>;
    void set_notify_callback(NotifyCallback cb) { notify_cb_ = std::move(cb); }

private:
    FrameHeader* open_frame(uint32_t data_length);
    void close_frame(FrameHeader* hdr, uint32_t total_length, int64_t gen_time, int32_t msg_type);
    void ensure_page(uint32_t needed);
    int64_t now_ns() const;

    io::location_ptr source_;
    uint32_t dest_uid_;
    io::Locator& locator_;
    size_t page_size_;
    std::shared_ptr<Page> current_page_;
    uint32_t page_id_ = 0;
    uint64_t write_position_ = 0;
    NotifyCallback notify_cb_;
};

} // namespace kungfu::yijinjing::journal
