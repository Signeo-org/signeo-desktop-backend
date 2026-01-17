#pragma once

/**
 * @file thread_safe_queue.h
 * @brief Thread-safe blocking queue for producer-consumer patterns
 *
 * Provides a mutex-protected queue with blocking pop operations.
 * Used for inter-thread communication in the audio processing pipeline.
 */

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace utils {

/**
 * @brief Thread-safe queue with blocking pop and graceful shutdown
 *
 * @tparam T The element type stored in the queue
 *
 * Features:
 * - Blocking pop() waits for data or stop signal
 * - Thread-safe push/pop operations
 * - Graceful shutdown with stop() method
 * - Non-blocking size() and empty() queries
 */
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : stopped_(false) {}

    // Non-copyable
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /**
     * @brief Push a value into the queue
     * @param value Value to push (moved into queue)
     * @note No-op if queue has been stopped
     */
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_)
            return;
        queue_.push(std::move(value));
        cond_var_.notify_one();
    }

    /**
     * @brief Pop a value from the queue (blocking)
     * @return std::optional<T> containing value, or nullopt if stopped
     * @note Blocks until data is available or stop() is called
     */
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty() || stopped_; });

        if (queue_.empty() && stopped_) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /**
     * @brief Signal the queue to stop and unblock all waiters
     * @note After stop(), push() becomes no-op and pop() returns nullopt
     */
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cond_var_.notify_all();
    }

    /**
     * @brief Clear all items from the queue
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

    /**
     * @brief Check if the queue is empty
     * @return true if empty
     */
    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Get the current queue size
     * @return Number of elements in queue
     */
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Reset the queue to accept new items after stop()
     */
    void restart() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = false;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    std::atomic<bool> stopped_;
};

} // namespace utils
