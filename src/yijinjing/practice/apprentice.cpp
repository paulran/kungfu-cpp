#include <kungfu/yijinjing/practice/apprentice.h>

namespace kungfu::yijinjing::practice {

apprentice::apprentice(const io::location_ptr& home, io::Locator& locator, bool low_latency)
    : hero(locator, home->m, low_latency), home_(home) {
    register_location(home_);
}

void apprentice::request_write_to(uint32_t dest_uid) {
    auto writer = get_writer(home_, dest_uid);
    // In full implementation, this would send a RequestWriteTo message to master
}

void apprentice::request_read_from(const io::location_ptr& source, uint32_t dest_uid, int64_t from_time) {
    register_location(source);
    reader_.join(source, dest_uid, from_time);
}

} // namespace kungfu::yijinjing::practice
