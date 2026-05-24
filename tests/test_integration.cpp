#include <gtest/gtest.h>
#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/yijinjing/journal/writer.h>
#include <kungfu/yijinjing/journal/reader.h>
#include <kungfu/longfist/types.h>
#include <kungfu/longfist/serialize.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>

using namespace kungfu::yijinjing;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;

class IntegrationTest : public ::testing::Test {
protected:
    std::string test_dir;
    std::unique_ptr<io::Locator> locator;

    void SetUp() override {
        test_dir = (std::filesystem::temp_directory_path() / "kf_test_integration").string();
        std::filesystem::remove_all(test_dir);
        locator = std::make_unique<io::Locator>(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(IntegrationTest, FullPipeline) {
    auto md_loc = io::location::make(io::category::MD, "sim", "sim01", io::mode::LIVE);
    auto strat_loc = io::location::make(io::category::STRATEGY, "test", "my_strat", io::mode::LIVE);

    std::atomic<int> received_count{0};
    std::atomic<bool> producer_done{false};

    // Producer thread: simulates MD writing quotes
    std::thread producer([&]() {
        journal::Writer writer(md_loc, kungfu::PUBLIC_DEST, *locator, 64 * 1024);
        for (int i = 0; i < 100; i++) {
            Quote q{};
            std::strncpy(q.instrument_id, "600000", 31);
            std::strncpy(q.exchange_id, "SSE", 15);
            q.last_price = 10.0 + i * 0.01;
            q.volume = (i + 1) * 100;
            q.data_time = i;
            writer.write<Quote>(0, q);
        }
        producer_done = true;
    });

    // Consumer: apprentice-style reader
    std::thread consumer([&]() {
        // Wait a bit for producer to start writing
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        journal::Reader reader(*locator);
        reader.join(md_loc, kungfu::PUBLIC_DEST, 0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (received_count < 100 && std::chrono::steady_clock::now() < deadline) {
            if (reader.data_available()) {
                auto frame = reader.current_frame();
                EXPECT_EQ(frame.msg_type(), Quote::tag);
                received_count++;
                reader.next();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received_count.load(), 100);
}

TEST_F(IntegrationTest, TimeOrdering) {
    auto loc_a = io::location::make(io::category::MD, "a", "a1", io::mode::LIVE);
    auto loc_b = io::location::make(io::category::MD, "b", "b1", io::mode::LIVE);

    // Write interleaved timestamps to two journals
    journal::Writer writer_a(loc_a, kungfu::PUBLIC_DEST, *locator, 4096);
    journal::Writer writer_b(loc_b, kungfu::PUBLIC_DEST, *locator, 4096);

    // A writes: 1, 3, 5
    // B writes: 2, 4, 6
    for (int i = 0; i < 3; i++) {
        Quote qa{};
        qa.last_price = static_cast<double>(2 * i + 1);
        writer_a.write<Quote>(0, qa);
        std::this_thread::sleep_for(std::chrono::microseconds(10));

        Quote qb{};
        qb.last_price = static_cast<double>(2 * i + 2);
        writer_b.write<Quote>(0, qb);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // Reader merges both journals in time order
    journal::Reader reader(*locator);
    reader.join(loc_a, kungfu::PUBLIC_DEST, 0);
    reader.join(loc_b, kungfu::PUBLIC_DEST, 0);

    int64_t prev_time = 0;
    int count = 0;
    while (reader.data_available()) {
        auto frame = reader.current_frame();
        EXPECT_GE(frame.gen_time(), prev_time) << "Frame " << count << " out of order";
        prev_time = frame.gen_time();
        count++;
        reader.next();
    }
    EXPECT_EQ(count, 6);
}

TEST_F(IntegrationTest, SerializationRoundTrip) {
    auto loc = io::location::make(io::category::SYSTEM, "test", "ser", io::mode::LIVE);

    // Write packed type
    journal::Writer writer(loc, kungfu::PUBLIC_DEST, *locator, 4096);
    OrderInput oi{};
    oi.order_id = 99;
    std::strncpy(oi.instrument_id, "IF2403", 31);
    oi.limit_price = 4500.0;
    oi.volume = 3;
    oi.side = enums::Side::Buy;
    writer.write<OrderInput>(0, oi);

    // Read back and verify
    journal::Reader reader(*locator);
    reader.join(loc, kungfu::PUBLIC_DEST, 0);

    ASSERT_TRUE(reader.data_available());
    auto frame = reader.current_frame();
    EXPECT_EQ(frame.msg_type(), OrderInput::tag);

    const auto& read_oi = frame.data<OrderInput>();
    EXPECT_EQ(read_oi.order_id, 99u);
    EXPECT_STREQ(read_oi.instrument_id, "IF2403");
    EXPECT_DOUBLE_EQ(read_oi.limit_price, 4500.0);
    EXPECT_EQ(read_oi.volume, 3);

    // Also test JSON round-trip
    auto j = to_json(read_oi);
    OrderInput restored{};
    from_json(j, restored);
    EXPECT_EQ(restored.order_id, 99u);
    EXPECT_DOUBLE_EQ(restored.limit_price, 4500.0);
}
