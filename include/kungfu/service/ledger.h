#pragma once

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/wingchun/book/book.h>

namespace kungfu::service {

class Ledger : public yijinjing::practice::apprentice {
public:
    Ledger(const yijinjing::io::location_ptr& home, yijinjing::io::Locator& locator, bool low_latency);

    void react() override;
    void on_start() override;

private:
    kungfu::wingchun::BookKeeper book_keeper_;
};

} // namespace kungfu::service
