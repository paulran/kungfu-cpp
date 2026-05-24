#include <gtest/gtest.h>
#include <kungfu/common/mmap.h>
#include <filesystem>
#include <cstring>

class MmapTest : public ::testing::Test {
protected:
    std::string test_path;

    void SetUp() override {
        auto tmp = std::filesystem::temp_directory_path() / "kf_test_mmap";
        std::filesystem::create_directories(tmp);
        test_path = (tmp / "test.dat").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(
            std::filesystem::temp_directory_path() / "kf_test_mmap");
    }
};

TEST_F(MmapTest, CreateAndWrite) {
    auto mf = kungfu::common::mmap_open(test_path, 4096, true);
    ASSERT_NE(mf.address, nullptr);
    EXPECT_EQ(mf.size, 4096u);

    std::memcpy(mf.address, "hello", 5);
    kungfu::common::mmap_close(mf);

    // Re-open and verify
    mf = kungfu::common::mmap_open(test_path, 4096, false);
    EXPECT_EQ(std::memcmp(mf.address, "hello", 5), 0);
    kungfu::common::mmap_close(mf);
}

TEST_F(MmapTest, LargerFile) {
    size_t size = 1024 * 1024;  // 1MB
    auto mf = kungfu::common::mmap_open(test_path, size, true);
    ASSERT_NE(mf.address, nullptr);

    auto* data = static_cast<uint32_t*>(mf.address);
    for (uint32_t i = 0; i < 1000; i++) {
        data[i] = i;
    }

    kungfu::common::mmap_close(mf);

    mf = kungfu::common::mmap_open(test_path, size, false);
    data = static_cast<uint32_t*>(mf.address);
    for (uint32_t i = 0; i < 1000; i++) {
        EXPECT_EQ(data[i], i);
    }
    kungfu::common::mmap_close(mf);
}
