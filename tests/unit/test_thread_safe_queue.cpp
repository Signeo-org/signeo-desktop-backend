#include <gtest/gtest.h>
#include "utils/thread_safe_queue.hpp"
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// ============================================================================
// Basic Push/Pop Tests
// ============================================================================

TEST(ThreadSafeQueueTest, BasicPushPop) {
    utils::ThreadSafeQueue<int> queue;
    queue.push(42);
    
    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
}

TEST(ThreadSafeQueueTest, PopEmpty) {
    utils::ThreadSafeQueue<int> queue;
    queue.stop();  // Stop immediately so pop doesn't block
    
    auto val = queue.pop();
    EXPECT_FALSE(val.has_value());  // Should return nullopt
}

TEST(ThreadSafeQueueTest, MultiplePushPop) {
    utils::ThreadSafeQueue<int> queue;
    
    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }
    
    for (int i = 0; i < 10; ++i) {
        auto val = queue.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
}

// ============================================================================
// Threading Tests
// ============================================================================

TEST(ThreadSafeQueueTest, BlockingPop) {
    utils::ThreadSafeQueue<int> queue;
    bool popped = false;
    
    std::thread consumer([&]() {
        auto val = queue.pop();  // This will block until push
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, 99);
        popped = true;
    });
    
    std::this_thread::sleep_for(100ms);  // Ensure pop is waiting
    EXPECT_FALSE(popped);  // Should still be blocking
    
    queue.push(99);  // Unblock the pop
    consumer.join();
    
    EXPECT_TRUE(popped);
}

TEST(ThreadSafeQueueTest, StopUnblocksPop) {
    utils::ThreadSafeQueue<int> queue;
    bool unblocked = false;
    
    std::thread consumer([&]() {
        auto val = queue.pop();  // Will block
        EXPECT_FALSE(val.has_value());  // Should return nullopt after stop
        unblocked = true;
    });
    
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(unblocked);  // Should still be blocked
    
    queue.stop();  // This should unblock pop
    consumer.join();
    
    EXPECT_TRUE(unblocked);
}

TEST(ThreadSafeQueueTest, MultiProducerMultiConsumer) {
    utils::ThreadSafeQueue<int> queue;
    std::atomic<int> sum{0};
    constexpr int num_items = 1000;
    constexpr int num_producers = 4;
    constexpr int num_consumers = 4;
    
    // Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < num_items / num_producers; ++i) {
                queue.push(1);
            }
        });
    }
    
    // Consumers
    std::vector<std::thread> consumers;
    int items_consumed = 0;
    std::mutex count_mutex;
    
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&]() {
            while (true) {
                auto val = queue.pop();
                if (!val.has_value()) break;  // Queue stopped
                
                sum += *val;
                std::lock_guard<std::mutex> lock(count_mutex);
                items_consumed++;
            }
        });
    }
    
    // Wait for producers to finish
    for (auto& t : producers) {
        t.join();
    }
    
    // Stop queue to unblock consumers
    queue.stop();
    
    // Wait for consumers
    for (auto& t : consumers) {
        t.join();
    }
    
    EXPECT_EQ(sum.load(), num_items);
    EXPECT_EQ(items_consumed, num_items);
}

// ============================================================================
// Size/Empty Tests
// ============================================================================

TEST(ThreadSafeQueueTest, SizeTracking) {
    utils::ThreadSafeQueue<int> queue;
    
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
    
    queue.push(1);
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_FALSE(queue.empty());
    
    queue.push(2);
    queue.push(3);
    EXPECT_EQ(queue.size(), 3u);
    
    (void)queue.pop();
    EXPECT_EQ(queue.size(), 2u);
    
    (void)queue.pop();
    (void)queue.pop();
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
}

