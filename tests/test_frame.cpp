#include <gtest/gtest.h>
#include <kungfu/yijinjing/journal/frame.h>
#include <cstring>

using namespace kungfu::yijinjing::journal;

TEST(FrameTest, HeaderSize) {
    EXPECT_EQ(sizeof(FrameHeader), 36u);
}

TEST(FrameTest, HasDataCheck) {
    alignas(8) char buffer[128] = {};
    Frame frame(buffer);

    EXPECT_FALSE(frame.has_data());

    auto* hdr = frame.header();
    hdr->msg_type = 101;
    EXPECT_FALSE(frame.has_data());  // length still 0

    hdr->length = 36 + 8;
    EXPECT_TRUE(frame.has_data());
}

TEST(FrameTest, DataAccess) {
    alignas(8) char buffer[128] = {};
    Frame frame(buffer);

    auto* hdr = frame.header();
    hdr->header_length = sizeof(FrameHeader);
    hdr->length = sizeof(FrameHeader) + sizeof(double);
    hdr->msg_type = 101;
    hdr->gen_time = 12345;
    hdr->source = 100;
    hdr->dest = 0;

    double value = 3.14;
    std::memcpy(frame.data_address(), &value, sizeof(value));

    EXPECT_EQ(frame.msg_type(), 101);
    EXPECT_EQ(frame.gen_time(), 12345);
    EXPECT_EQ(frame.source(), 100u);
    EXPECT_EQ(frame.data<double>(), 3.14);
    EXPECT_EQ(frame.data_length(), sizeof(double));
}

TEST(FrameTest, NextAddress) {
    alignas(8) char buffer[256] = {};
    Frame frame(buffer);

    auto* hdr = frame.header();
    hdr->header_length = sizeof(FrameHeader);
    hdr->length = sizeof(FrameHeader) + 16;

    void* next = frame.next_address();
    EXPECT_EQ(static_cast<char*>(next) - buffer, sizeof(FrameHeader) + 16);
}
