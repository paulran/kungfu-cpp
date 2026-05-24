#include <gtest/gtest.h>
#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/longfist/types.h>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace kungfu::yijinjing;
using namespace kungfu::longfist::types;

class PracticeTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::unique_ptr<io::Locator> locator;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_practice").string();
        std::filesystem::remove_all(test_dir);
        locator = std::make_unique<io::Locator>(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

class TestApprentice : public practice::apprentice {
public:
    TestApprentice(const io::location_ptr& home, io::Locator& locator)
        : apprentice(home, locator, true) {}

    void react() override {
        events_.subscribe(lifetime_,
            [this](practice::event_ptr event) {
                received_events.push_back(event);
            }
        );
    }

    std::vector<practice::event_ptr> received_events;
};

TEST_F(PracticeTest, ApprenticeCreation) {
    auto home = io::location::make(io::category::STRATEGY, "test", "strat01", io::mode::LIVE);
    TestApprentice app(home, *locator);

    EXPECT_EQ(app.home_uid(), home->uid);
}

TEST_F(PracticeTest, WriteAndReceive) {
    auto source = io::location::make(io::category::MD, "xtp", "md01", io::mode::LIVE);
    auto home = io::location::make(io::category::STRATEGY, "test", "strat01", io::mode::LIVE);

    // Write some data
    journal::Writer writer(source, kungfu::PUBLIC_DEST, *locator, 4096);
    for (int i = 0; i < 5; i++) {
        Quote q{};
        q.last_price = static_cast<double>(i + 1) * 10.0;
        writer.write<Quote>(0, q);
    }

    // Set up apprentice to read
    TestApprentice app(home, *locator);
    app.request_read_from(source, kungfu::PUBLIC_DEST, 0);

    // Run event loop briefly
    std::thread runner([&app]() { app.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    app.stop();
    runner.join();

    EXPECT_EQ(app.received_events.size(), 5u);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(app.received_events[i]->msg_type(), Quote::tag);
        EXPECT_DOUBLE_EQ(app.received_events[i]->data<Quote>().last_price,
                        static_cast<double>(i + 1) * 10.0);
    }
}
