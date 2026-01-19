#include "audio/ringbuffer.hpp"

#include <algorithm>
#include <span>

namespace audio {

RingBuffer::RingBuffer(size_t capacity) : capacity_(capacity), buffer_(capacity) {}

auto RingBuffer::write(const float* data, size_t count) -> size_t {
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t available = capacity_ - size_;
    count = std::min(count, available);

    if (count == 0) {
        return 0;
    }

    std::span<const float> input_span(data, count);
    const size_t first_chunk = std::min(count, capacity_ - write_pos_);

    // Fix narrowing conversion warning by casting to ptrdiff_t
    auto write_iter = buffer_.begin() + static_cast<std::ptrdiff_t>(write_pos_);
    std::copy_n(input_span.begin(), first_chunk, write_iter);

    if (first_chunk < count) {
        std::copy_n(input_span.begin() + static_cast<std::ptrdiff_t>(first_chunk), count - first_chunk,
                    buffer_.begin());
    }

    write_pos_ = (write_pos_ + count) % capacity_;
    size_ += count;

    return count;
}

auto RingBuffer::read(float* dest, size_t count) -> size_t {
    std::lock_guard<std::mutex> lock(mutex_);

    count = std::min(count, size_);
    if (count == 0) {
        return 0;
    }

    std::span<float> output_span(dest, count);
    const size_t first_chunk = std::min(count, capacity_ - read_pos_);

    // Fix narrowing conversion warning by casting to ptrdiff_t
    auto read_iter = buffer_.begin() + static_cast<std::ptrdiff_t>(read_pos_);
    std::copy_n(read_iter, first_chunk, output_span.begin());

    if (first_chunk < count) {
        std::copy_n(buffer_.begin(), count - first_chunk,
                    output_span.begin() + static_cast<std::ptrdiff_t>(first_chunk));
    }

    read_pos_ = (read_pos_ + count) % capacity_;
    size_ -= count;

    return count;
}

auto RingBuffer::available_read() const -> size_t {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

auto RingBuffer::available_write() const -> size_t {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_ - size_;
}

auto RingBuffer::capacity() const noexcept -> size_t {
    return capacity_;
}

void RingBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_pos_ = 0;
    read_pos_ = 0;
    size_ = 0;
}

}  // namespace audio
