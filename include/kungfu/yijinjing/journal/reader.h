#pragma once

#include <kungfu/yijinjing/journal/page.h>
#include <kungfu/yijinjing/io/locator.h>
#include <memory>
#include <vector>

namespace kungfu::yijinjing::journal {

class Reader {
public:
    explicit Reader(io::Locator& locator);
    ~Reader() = default;

    void join(const io::location_ptr& location, uint32_t dest_uid, int64_t from_time = 0);
    void disjoin(uint32_t location_uid);

    bool data_available();
    Frame current_frame();
    void next();

    bool empty() const { return slots_.empty(); }

private:
    struct Slot {
        io::location_ptr location;
        uint32_t dest_uid;
        std::shared_ptr<Page> current_page;
        uint32_t page_id;
        uint64_t position;

        Frame get_frame();
        bool has_data();
        void advance(io::Locator& locator);
        void load_page(io::Locator& locator, uint32_t pid);
    };

    void sort_current();

    io::Locator& locator_;
    std::vector<Slot> slots_;
    int current_idx_ = -1;
};

} // namespace kungfu::yijinjing::journal
