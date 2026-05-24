#include <gtest/gtest.h>
#include <kungfu/common/hash.h>

TEST(HashTest, DeterministicOutput) {
    auto h1 = kungfu::common::hash_str_32("test", 42);
    auto h2 = kungfu::common::hash_str_32("test", 42);
    EXPECT_EQ(h1, h2);
}

TEST(HashTest, DifferentInputsDifferentHash) {
    auto h1 = kungfu::common::hash_str_32("hello", 42);
    auto h2 = kungfu::common::hash_str_32("world", 42);
    EXPECT_NE(h1, h2);
}

TEST(HashTest, DifferentSeedsDifferentHash) {
    auto h1 = kungfu::common::hash_str_32("test", 42);
    auto h2 = kungfu::common::hash_str_32("test", 0);
    EXPECT_NE(h1, h2);
}

TEST(HashTest, LocationUname) {
    std::string uname = "system/master/master/live";
    auto uid = kungfu::common::hash_str_32(uname, 42);
    EXPECT_NE(uid, 0u);
    // Same input always produces same output
    EXPECT_EQ(uid, kungfu::common::hash_str_32(uname, 42));
}
