#include <gtest/gtest.h>

#include <numeric>
#include <vector>

#include "audio/ringbuffer.hpp"

// ============================================================================
// Basic Write/Read Tests
// ============================================================================

TEST(RingBufferTest, BasicWriteRead) {
    audio::RingBuffer buffer(1024);

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    size_t written = buffer.write(data.data(), data.size());
    EXPECT_EQ(written, data.size());

    std::vector<float> output(data.size());
    size_t read = buffer.read(output.data(), output.size());
    EXPECT_EQ(read, data.size());

    EXPECT_EQ(data, output);
}

TEST(RingBufferTest, ReadEmpty) {
    audio::RingBuffer buffer(1024);

    std::vector<float> output(10);
    size_t read = buffer.read(output.data(), output.size());
    EXPECT_EQ(read, 0u);  // Nothing to read
}

TEST(RingBufferTest, AvailableCount) {
    audio::RingBuffer buffer(1024);

    EXPECT_EQ(buffer.available_read(), 0u);
    EXPECT_EQ(buffer.available_write(), 1024u);

    std::vector<float> data(100, 1.0f);
    buffer.write(data.data(), data.size());

    EXPECT_EQ(buffer.available_read(), 100u);
    EXPECT_EQ(buffer.available_write(), 924u);

    std::vector<float> output(50);
    buffer.read(output.data(), output.size());

    EXPECT_EQ(buffer.available_read(), 50u);
    EXPECT_EQ(buffer.available_write(), 974u);
}

// ============================================================================
// Overflow/Underflow Tests
// ============================================================================

TEST(RingBufferTest, WriteOverflow) {
    audio::RingBuffer buffer(10);

    std::vector<float> data(20, 1.0f);  // Larger than capacity
    size_t written = buffer.write(data.data(), data.size());
    EXPECT_EQ(written, 10u);  // Should only write what fits
}

TEST(RingBufferTest, ExactCapacity) {
    audio::RingBuffer buffer(10);

    std::vector<float> data(10, 1.0f);
    size_t written = buffer.write(data.data(), data.size());
    EXPECT_EQ(written, 10u);

    // Try to write one more (should write 0)
    float extra = 1.0f;
    size_t extra_written = buffer.write(&extra, 1);
    EXPECT_EQ(extra_written, 0u);
}

// ============================================================================
// Wrap-Around Tests
// ============================================================================

TEST(RingBufferTest, WrapAround) {
    audio::RingBuffer buffer(10);

    // Fill buffer
    std::vector<float> data1(10);
    std::iota(data1.begin(), data1.end(), 0.0f);  // 0, 1, 2, ..., 9
    EXPECT_EQ(buffer.write(data1.data(), data1.size()), 10u);

    // Read half
    std::vector<float> output1(5);
    EXPECT_EQ(buffer.read(output1.data(), output1.size()), 5u);
    EXPECT_EQ(output1, std::vector<float>({0, 1, 2, 3, 4}));

    // Write again (this will wrap around)
    std::vector<float> data2(5);
    std::iota(data2.begin(), data2.end(), 10.0f);  // 10, 11, 12, 13, 14
    EXPECT_EQ(buffer.write(data2.data(), data2.size()), 5u);

    // Read remaining data
    std::vector<float> output2(10);
    EXPECT_EQ(buffer.read(output2.data(), output2.size()), 10u);

    std::vector<float> expected = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    EXPECT_EQ(output2, expected);
}

// ============================================================================
// Large Data Tests
// ============================================================================

TEST(RingBufferTest, LargeDataIntegrity) {
    audio::RingBuffer buffer(10000);

    // Write large data
    std::vector<float> data(5000);
    std::iota(data.begin(), data.end(), 0.0f);
    EXPECT_EQ(buffer.write(data.data(), data.size()), 5000u);

    // Read in chunks
    std::vector<float> output(5000);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(buffer.read(output.data() + i * 1000, 1000), 1000u);
    }

    EXPECT_EQ(data, output);
}
