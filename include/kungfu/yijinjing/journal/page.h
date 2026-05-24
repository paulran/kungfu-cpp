#pragma once

#include <kungfu/yijinjing/journal/frame.h>
#include <kungfu/common/mmap.h>
#include <kungfu/common/types.h>
#include <memory>
#include <string>

namespace kungfu::yijinjing::journal {

#pragma pack(push, 1)
struct PageHeader {
    uint32_t version;
    uint32_t header_length;
    uint32_t page_size;
    uint32_t frame_header_length;
    uint64_t last_frame_position;
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 24, "PageHeader must be 24 bytes");

class Page {
public:
    static std::shared_ptr<Page> open(const std::string& path, size_t size, bool is_writing);
    ~Page();

    PageHeader* header() { return reinterpret_cast<PageHeader*>(mf_.address); }
    const PageHeader* header() const { return reinterpret_cast<const PageHeader*>(mf_.address); }

    Frame frame_at(uint64_t position) {
        return Frame(static_cast<char*>(mf_.address) + position);
    }

    uint64_t first_frame_position() const { return sizeof(PageHeader); }

    uint64_t last_frame_position() const { return header()->last_frame_position; }

    bool is_full(uint32_t needed) const {
        return header()->last_frame_position + needed + sizeof(FrameHeader) >= mf_.size;
    }

    size_t size() const { return mf_.size; }
    void* raw() { return mf_.address; }

private:
    Page() = default;
    common::MappedFile mf_{};
};

} // namespace kungfu::yijinjing::journal
