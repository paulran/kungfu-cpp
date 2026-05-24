#include <kungfu/yijinjing/journal/page.h>
#include <stdexcept>
#include <cstring>

namespace kungfu::yijinjing::journal {

std::shared_ptr<Page> Page::open(const std::string& path, size_t size, bool is_writing) {
    auto page = std::shared_ptr<Page>(new Page());
    page->mf_ = common::mmap_open(path, size, is_writing);

    auto* hdr = page->header();
    if (hdr->last_frame_position == 0) {
        if (!is_writing) {
            throw std::runtime_error("Cannot open non-existent page for reading: " + path);
        }
        hdr->version = kungfu::JOURNAL_VERSION;
        hdr->header_length = sizeof(PageHeader);
        hdr->page_size = static_cast<uint32_t>(size);
        hdr->frame_header_length = sizeof(FrameHeader);
        hdr->last_frame_position = sizeof(PageHeader);
    } else {
        if (hdr->version != kungfu::JOURNAL_VERSION) {
            throw std::runtime_error("Page version mismatch in: " + path);
        }
    }

    return page;
}

Page::~Page() {
    if (mf_.address) {
        common::mmap_close(mf_);
    }
}

} // namespace kungfu::yijinjing::journal
