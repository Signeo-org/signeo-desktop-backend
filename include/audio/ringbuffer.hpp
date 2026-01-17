#pragma once

/**
 * @file ringbuffer.h
 * @brief Thread-safe circular buffer for real-time audio streaming
 *
 * Provides a fixed-size ring buffer optimized for audio I/O between
 * the audio capture callback and the processing thread.
 */

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace audio {

/**
 * @brief Thread-safe ring buffer for audio samples
 *
 * Designed for producer-consumer scenarios with:
 * - Fixed capacity allocation (no dynamic resizing)
 * - Thread-safe read/write operations
 * - Efficient circular buffer implementation
 *
 * @note Uses mutex for thread safety. Future optimization may
 *       implement lock-free operations for lower latency.
 */
class RingBuffer {
public:
    /**
     * @brief Construct a ring buffer with fixed capacity
     * @param capacity Maximum number of float samples to store
     */
    explicit RingBuffer(size_t capacity);

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    ~RingBuffer() = default;

    /**
     * @brief Write audio samples into the buffer
     * @param data Pointer to source samples
     * @param count Number of samples to write
     * @return Number of samples actually written (may be less if full)
     */
    size_t write(const float* data, size_t count);

    /**
     * @brief Read audio samples from the buffer
     * @param dest Destination buffer for samples
     * @param count Maximum number of samples to read
     * @return Number of samples actually read (may be less if empty)
     */
    size_t read(float* dest, size_t count);

    /**
     * @brief Get number of samples available for reading
     * @return Available read count
     */
    [[nodiscard]] size_t available_read() const;

    /**
     * @brief Get number of samples that can be written
     * @return Available write capacity
     */
    [[nodiscard]] size_t available_write() const;

    /**
     * @brief Get the total buffer capacity
     * @return Maximum number of samples the buffer can hold
     */
    [[nodiscard]] size_t capacity() const noexcept;

    /**
     * @brief Clear all data from the buffer
     */
    void clear();

private:
    // Member variables
    std::vector<float> buffer_;
    size_t capacity_;
    size_t size_ = 0;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;

    mutable std::mutex mutex_;
};

} // namespace audio
