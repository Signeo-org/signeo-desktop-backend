#include "audio/ringbuffer.hpp"

#include <algorithm>

namespace audio {

RingBuffer::RingBuffer(size_t capacity) : capacity_(capacity), buffer_(capacity) {}

auto RingBuffer::write(const float* data, size_t count) -> size_t {
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t available = capacity_ - size_;
    count = std::min(count, available);

    if (count == 0) {
        return 0;
    }

    const size_t first_chunk = std::min(count, capacity_ - write_pos_);
    std::copy_n(data, first_chunk, buffer_.begin() + write_pos_);

    if (first_chunk < count) {
        std::copy_n(data + first_chunk, count - first_chunk, buffer_.begin());
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

    const size_t first_chunk = std::min(count, capacity_ - read_pos_);
    std::copy_n(buffer_.begin() + read_pos_, first_chunk, dest);

    if (first_chunk < count) {
        std::copy_n(buffer_.begin(), count - first_chunk, dest + first_chunk);
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
