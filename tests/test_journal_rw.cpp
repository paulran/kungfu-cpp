#include <gtest/gtest.h>
#include <kungfu/yijinjing/journal/writer.h>
#include <kungfu/yijinjing/journal/reader.h>
#include <kungfu/yijinjing/io/locator.h>
#include <kungfu/longfist/types.h>
#include <filesystem>
#include <cstring>

using namespace kungfu::yijinjing;
using namespace kungfu::longfist::types;

class JournalRWTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::unique_ptr<io::Locator> locator;
    io::location_ptr loc;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_journal").string();
        std::filesystem::remove_all(test_dir);
        locator = std::make_unique<io::Locator>(test_dir);
        loc = io::location::make(io::category::SYSTEM, "test", "writer", io::mode::LIVE);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(JournalRWTest, WriteAndRead) {
    // Write
    journal::Writer writer(loc, kungfu::PUBLIC_DEST, *locator, 4096);

    Quote q{};
    std::strncpy(q.instrument_id, "600000", 31);
    std::strncpy(q.exchange_id, "SSE", 15);
    q.last_price = 10.5;
    q.volume = 1000;

    writer.write<Quote>(0, q);

    // Read
    journal::Reader reader(*locator);
    reader.join(loc, kungfu::PUBLIC_DEST, 0);

    ASSERT_TRUE(reader.data_available());
    auto frame = reader.current_frame();
    EXPECT_EQ(frame.msg_type(), Quote::tag);

    const auto& read_q = frame.data<Quote>();
    EXPECT_STREQ(read_q.instrument_id, "600000");
    EXPECT_STREQ(read_q.exchange_id, "SSE");
    EXPECT_DOUBLE_EQ(read_q.last_price, 10.5);
    EXPECT_EQ(read_q.volume, 1000);
}

TEST_F(JournalRWTest, MultipleFrames) {
    journal::Writer writer(loc, kungfu::PUBLIC_DEST, *locator, 4096);

    for (int i = 0; i < 10; i++) {
        Quote q{};
        q.last_price = static_cast<double>(i);
        q.volume = i * 100;
        writer.write<Quote>(0, q);
    }

    journal::Reader reader(*locator);
    reader.join(loc, kungfu::PUBLIC_DEST, 0);

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(reader.data_available()) << "Frame " << i << " not available";
        auto frame = reader.current_frame();
        EXPECT_EQ(frame.msg_type(), Quote::tag);
        EXPECT_DOUBLE_EQ(frame.data<Quote>().last_price, static_cast<double>(i));
        reader.next();
    }

    EXPECT_FALSE(reader.data_available());
}

TEST_F(JournalRWTest, PageRollover) {
    // Use a very small page to force rollover
    size_t small_page = sizeof(journal::PageHeader) + sizeof(journal::FrameHeader) * 3 + sizeof(Quote) * 2;
    journal::Writer writer(loc, kungfu::PUBLIC_DEST, *locator, small_page);

    // Write enough to trigger page rollover
    for (int i = 0; i < 5; i++) {
        Quote q{};
        q.last_price = static_cast<double>(i + 1);
        writer.write<Quote>(0, q);
    }

    // Verify multiple pages were created
    auto ids = locator->list_page_ids(loc, kungfu::PUBLIC_DEST);
    EXPECT_GT(ids.size(), 1u);

    // Read all frames back
    journal::Reader reader(*locator);
    reader.join(loc, kungfu::PUBLIC_DEST, 0);

    int count = 0;
    while (reader.data_available()) {
        auto frame = reader.current_frame();
        count++;
        EXPECT_DOUBLE_EQ(frame.data<Quote>().last_price, static_cast<double>(count));
        reader.next();
    }
    EXPECT_EQ(count, 5);
}

TEST_F(JournalRWTest, MarkFrame) {
    journal::Writer writer(loc, kungfu::PUBLIC_DEST, *locator, 4096);
    writer.mark(0, TimeReset::tag);

    journal::Reader reader(*locator);
    reader.join(loc, kungfu::PUBLIC_DEST, 0);

    ASSERT_TRUE(reader.data_available());
    auto frame = reader.current_frame();
    EXPECT_EQ(frame.msg_type(), TimeReset::tag);
    EXPECT_EQ(frame.data_length(), 0u);
}
