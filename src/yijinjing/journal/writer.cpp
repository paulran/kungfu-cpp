#include <kungfu/yijinjing/journal/writer.h>
#include <cstring>
#include <atomic>

namespace kungfu::yijinjing::journal {

Writer::Writer(const io::location_ptr& source, uint32_t dest_uid,
               io::Locator& locator, size_t page_size)
    : source_(source), dest_uid_(dest_uid), locator_(locator), page_size_(page_size) {
    locator_.ensure_dir(source_, io::layout::JOURNAL);

    auto ids = locator_.list_page_ids(source_, dest_uid_);
    page_id_ = ids.empty() ? 0 : ids.back();

    std::string path = locator_.journal_path(source_, dest_uid_, page_id_);
    current_page_ = Page::open(path, page_size_, true);
    write_position_ = current_page_->last_frame_position();
}

FrameHeader* Writer::open_frame(uint32_t data_length) {
    uint32_t needed = sizeof(FrameHeader) + data_length;
    ensure_page(needed);
    return reinterpret_cast<FrameHeader*>(
        static_cast<char*>(current_page_->raw()) + write_position_);
}

void Writer::close_frame(FrameHeader* hdr, uint32_t total_length, int64_t gen_time, int32_t msg_type) {
    hdr->header_length = sizeof(FrameHeader);
    hdr->trigger_time = gen_time;
    hdr->source = source_->uid;
    hdr->dest = dest_uid_;

    hdr->gen_time = gen_time;
    hdr->msg_type = msg_type;

    std::atomic_thread_fence(std::memory_order_release);
    hdr->length = total_length;

    write_position_ += total_length;
    current_page_->header()->last_frame_position = write_position_;

    // Zero next frame header as sentinel
    if (write_position_ + sizeof(FrameHeader) < page_size_) {
        std::memset(static_cast<char*>(current_page_->raw()) + write_position_, 0, sizeof(FrameHeader));
    }

    if (notify_cb_) notify_cb_();
}

void Writer::write_raw(int64_t trigger_time, int32_t msg_type,
                       const void* data, uint32_t data_length) {
    auto* hdr = open_frame(data_length);
    std::memcpy(reinterpret_cast<char*>(hdr) + sizeof(FrameHeader), data, data_length);
    close_frame(hdr, sizeof(FrameHeader) + data_length, now_ns(), msg_type);
}

void Writer::mark(int64_t trigger_time, int32_t msg_type) {
    auto* hdr = open_frame(0);
    close_frame(hdr, sizeof(FrameHeader), now_ns(), msg_type);
}

void Writer::ensure_page(uint32_t needed) {
    if (!current_page_ || current_page_->is_full(needed)) {
        page_id_++;
        std::string path = locator_.journal_path(source_, dest_uid_, page_id_);
        current_page_ = Page::open(path, page_size_, true);
        write_position_ = current_page_->first_frame_position();
    }
}

int64_t Writer::now_ns() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

} // namespace kungfu::yijinjing::journal
