#include <gtest/gtest.h>
#include <kungfu/yijinjing/journal/page.h>
#include <filesystem>

using namespace kungfu::yijinjing::journal;

class PageTest : public ::testing::Test {
protected:
    std::string test_dir;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_page").string();
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(PageTest, HeaderSize) {
    EXPECT_EQ(sizeof(PageHeader), 24u);
}

TEST_F(PageTest, CreateNew) {
    std::string path = test_dir + "/test.journal";
    auto page = Page::open(path, 4096, true);

    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->header()->version, kungfu::JOURNAL_VERSION);
    EXPECT_EQ(page->header()->header_length, sizeof(PageHeader));
    EXPECT_EQ(page->header()->page_size, 4096u);
    EXPECT_EQ(page->header()->frame_header_length, sizeof(FrameHeader));
    EXPECT_EQ(page->header()->last_frame_position, sizeof(PageHeader));
}

TEST_F(PageTest, IsFull) {
    std::string path = test_dir + "/test.journal";
    size_t page_size = 256;
    auto page = Page::open(path, page_size, true);

    // Fresh page shouldn't be full for a small frame
    EXPECT_FALSE(page->is_full(40));
    // But should be full for something that won't fit
    EXPECT_TRUE(page->is_full(static_cast<uint32_t>(page_size)));
}

TEST_F(PageTest, FrameAt) {
    std::string path = test_dir + "/test.journal";
    auto page = Page::open(path, 4096, true);

    auto frame = page->frame_at(sizeof(PageHeader));
    EXPECT_FALSE(frame.has_data());
}
