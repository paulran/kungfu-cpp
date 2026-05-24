#include <kungfu/yijinjing/journal/reader.h>
#include <algorithm>
#include <filesystem>

namespace kungfu::yijinjing::journal {

Reader::Reader(io::Locator& locator) : locator_(locator) {}

void Reader::join(const io::location_ptr& location, uint32_t dest_uid, int64_t from_time) {
    Slot slot;
    slot.location = location;
    slot.dest_uid = dest_uid;
    slot.page_id = 0;
    slot.position = sizeof(PageHeader);
    slot.current_page = nullptr;

    auto ids = locator_.list_page_ids(location, dest_uid);
    if (!ids.empty()) {
        slot.page_id = ids.front();
        slot.load_page(locator_, slot.page_id);

        if (from_time > 0 && slot.current_page) {
            // Skip frames until we find one with gen_time >= from_time
            while (slot.has_data()) {
                auto frame = slot.get_frame();
                if (frame.gen_time() >= from_time) break;
                slot.advance(locator_);
            }
        }
    }

    slots_.push_back(std::move(slot));
    current_idx_ = -1;
}

void Reader::disjoin(uint32_t location_uid) {
    slots_.erase(
        std::remove_if(slots_.begin(), slots_.end(),
            [location_uid](const Slot& s) { return s.location->uid == location_uid; }),
        slots_.end()
    );
    current_idx_ = -1;
}

bool Reader::data_available() {
    sort_current();
    return current_idx_ >= 0;
}

Frame Reader::current_frame() {
    if (current_idx_ < 0) return Frame(nullptr);
    return slots_[current_idx_].get_frame();
}

void Reader::next() {
    if (current_idx_ < 0) return;
    slots_[current_idx_].advance(locator_);
    current_idx_ = -1;
}

void Reader::sort_current() {
    current_idx_ = -1;
    int64_t earliest = INT64_MAX;

    for (int i = 0; i < static_cast<int>(slots_.size()); i++) {
        if (slots_[i].has_data()) {
            auto frame = slots_[i].get_frame();
            if (frame.gen_time() < earliest) {
                earliest = frame.gen_time();
                current_idx_ = i;
            }
        }
    }
}

// Slot implementation

Frame Reader::Slot::get_frame() {
    if (!current_page) return Frame(nullptr);
    return current_page->frame_at(position);
}

bool Reader::Slot::has_data() {
    if (!current_page) return false;
    auto frame = current_page->frame_at(position);
    return frame.has_data();
}

void Reader::Slot::advance(io::Locator& locator) {
    if (!current_page) return;

    auto frame = current_page->frame_at(position);
    if (!frame.has_data()) return;

    uint32_t frame_len = frame.frame_length();
    position += frame_len;

    // Check if we need to move to next page
    if (position + sizeof(FrameHeader) >= current_page->size()) {
        load_page(locator, page_id + 1);
    } else {
        auto next_frame = current_page->frame_at(position);
        if (!next_frame.has_data()) {
            // Try loading next page (might have been created since we opened this one)
            auto path = locator.journal_path(location, dest_uid, page_id + 1);
            if (std::filesystem::exists(path)) {
                load_page(locator, page_id + 1);
            }
        }
    }
}

void Reader::Slot::load_page(io::Locator& locator, uint32_t pid) {
    auto path = locator.journal_path(location, dest_uid, pid);
    if (!std::filesystem::exists(path)) {
        current_page = nullptr;
        return;
    }
    try {
        current_page = Page::open(path, std::filesystem::file_size(path), false);
        page_id = pid;
        position = current_page->first_frame_position();
    } catch (...) {
        current_page = nullptr;
    }
}

} // namespace kungfu::yijinjing::journal
