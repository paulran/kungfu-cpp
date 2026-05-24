#pragma once

#include <kungfu/common/types.h>
#include <cstdint>
#include <cstring>
#include <atomic>

namespace kungfu::yijinjing::journal {

#pragma pack(push, 1)
struct FrameHeader {
    volatile uint32_t length;
    uint32_t header_length;
    int64_t gen_time;
    int64_t trigger_time;
    volatile int32_t msg_type;
    uint32_t source;
    uint32_t dest;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 36, "FrameHeader must be 36 bytes");

class Frame {
public:
    Frame() : address_(nullptr) {}
    explicit Frame(void* address) : address_(static_cast<char*>(address)) {}

    bool valid() const { return address_ != nullptr; }
    bool has_data() const {
        auto* h = header();
        return h->length > 0 && h->msg_type > 0;
    }

    FrameHeader* header() { return reinterpret_cast<FrameHeader*>(address_); }
    const FrameHeader* header() const { return reinterpret_cast<const FrameHeader*>(address_); }

    int32_t msg_type() const { return header()->msg_type; }
    int64_t gen_time() const { return header()->gen_time; }
    int64_t trigger_time() const { return header()->trigger_time; }
    uint32_t source() const { return header()->source; }
    uint32_t dest() const { return header()->dest; }
    uint32_t frame_length() const { return header()->length; }
    uint32_t data_length() const { return header()->length - header()->header_length; }

    void* data_address() { return address_ + header()->header_length; }
    const void* data_address() const { return address_ + header()->header_length; }

    template<typename T>
    const T& data() const { return *static_cast<const T*>(data_address()); }

    template<typename T>
    void copy_data(const T& d) { std::memcpy(data_address(), &d, sizeof(T)); }

    void* next_address() const { return address_ + header()->length; }
    void* raw_address() const { return address_; }

private:
    char* address_;
};

} // namespace kungfu::yijinjing::journal
