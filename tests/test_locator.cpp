#include <gtest/gtest.h>
#include <kungfu/yijinjing/io/locator.h>
#include <filesystem>
#include <fstream>

using namespace kungfu::yijinjing::io;

class LocatorTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::unique_ptr<Locator> locator;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_locator").string();
        std::filesystem::remove_all(test_dir);
        locator = std::make_unique<Locator>(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(LocatorTest, LocationUname) {
    auto loc = location::make(category::MD, "xtp", "xtp01", mode::LIVE);
    EXPECT_EQ(loc->uname(), "md/xtp/xtp01/live");
}

TEST_F(LocatorTest, UidDeterministic) {
    auto loc1 = location::make(category::MD, "xtp", "xtp01", mode::LIVE);
    auto loc2 = location::make(category::MD, "xtp", "xtp01", mode::LIVE);
    EXPECT_EQ(loc1->uid, loc2->uid);
}

TEST_F(LocatorTest, UidUnique) {
    auto loc1 = location::make(category::MD, "xtp", "xtp01", mode::LIVE);
    auto loc2 = location::make(category::TD, "xtp", "account01", mode::LIVE);
    EXPECT_NE(loc1->uid, loc2->uid);
}

TEST_F(LocatorTest, JournalPath) {
    auto loc = location::make(category::SYSTEM, "master", "master", mode::LIVE);
    auto path = locator->journal_path(loc, 0, 0);

    // Should contain the layout directory structure
    EXPECT_NE(path.find("journal"), std::string::npos);
    EXPECT_NE(path.find("system"), std::string::npos);
    EXPECT_NE(path.find("master"), std::string::npos);
    EXPECT_NE(path.find("live"), std::string::npos);
    EXPECT_NE(path.find("00000000.0.journal"), std::string::npos);
}

TEST_F(LocatorTest, EnsureDir) {
    auto loc = location::make(category::MD, "xtp", "md01", mode::LIVE);
    locator->ensure_dir(loc, layout::JOURNAL);

    std::string dir = locator->layout_dir(loc, layout::JOURNAL);
    EXPECT_TRUE(std::filesystem::exists(dir));
}

TEST_F(LocatorTest, ListPageIds) {
    auto loc = location::make(category::SYSTEM, "test", "test", mode::LIVE);
    locator->ensure_dir(loc, layout::JOURNAL);

    // Create some fake journal files
    auto dir = locator->layout_dir(loc, layout::JOURNAL);
    std::ofstream(dir + "/00000000.0.journal").put('x');
    std::ofstream(dir + "/00000000.1.journal").put('x');
    std::ofstream(dir + "/00000000.3.journal").put('x');

    auto ids = locator->list_page_ids(loc, 0);
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], 0u);
    EXPECT_EQ(ids[1], 1u);
    EXPECT_EQ(ids[2], 3u);
}
