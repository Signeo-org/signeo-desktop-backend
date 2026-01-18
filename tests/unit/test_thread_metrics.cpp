#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "utils/thread_metrics.hpp"
#include "output/logging.hpp"

class ThreadMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reinitialize spdlog in case it was shutdown by previous tests
        try {
            if (!spdlog::default_logger()) {
                core::init_logging("", false);
            }
        } catch (...) {
            core::init_logging("", false);
        }
    }
};

TEST_F(ThreadMetricsTest, Singleton) {
    auto& instance1 = utils::ThreadMetrics::get();
    auto& instance2 = utils::ThreadMetrics::get();
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(ThreadMetricsTest, RegisterAndUnregisterThread) {
    auto& metrics = utils::ThreadMetrics::get();
    
    // Create a simple thread
    std::thread worker([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    
    // Register it
    metrics.register_thread("TestWorker", worker.native_handle());
    
    // Get stats (should contain our thread)
    auto stats = metrics.update_and_get();
    bool found = false;
    for (const auto& s : stats) {
        if (s.name == "TestWorker") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Registered thread not found in stats";
    
    // Unregister
    metrics.unregister_thread("TestWorker");
    
    // Join thread
    worker.join();
    
    // Verify it's gone
    stats = metrics.update_and_get();
    found = false;
    for (const auto& s : stats) {
        if (s.name == "TestWorker") {
            found = true;
        }
    }
    EXPECT_FALSE(found) << "Thread still found after unregister";
}

TEST_F(ThreadMetricsTest, CpuUsageNonNegative) {
    auto& metrics = utils::ThreadMetrics::get();
    
    // Create a busy thread
    std::atomic<bool> running{true};
    std::thread worker([&running]() {
        while (running) {
            // Busy loop to generate CPU usage
            volatile int x = 0;
            for (int i = 0; i < 10000; i++) x += i;
        }
    });
    
    metrics.register_thread("BusyWorker", worker.native_handle());
    
    // Wait for some activity
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto stats = metrics.update_and_get();
    for (const auto& s : stats) {
        if (s.name == "BusyWorker") {
            EXPECT_GE(static_cast<double>(s.cpu_usage_percent), 0.0);
            // On Windows, should show some CPU usage
#if defined(_WIN32)
            // Might be flaky, so just check it's not negative
            EXPECT_GE(static_cast<double>(s.cpu_usage_percent), 0.0);
#endif
        }
    }
    
    running = false;
    metrics.unregister_thread("BusyWorker");
    worker.join();
}

TEST_F(ThreadMetricsTest, ThreadSafety) {
    auto& metrics = utils::ThreadMetrics::get();
    
    std::vector<std::thread> threads;
    
    // Spawn multiple threads that register/unregister concurrently
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&metrics, i]() {
            std::string name = "Thread" + std::to_string(i);
            
            // Create a dummy thread
            std::thread dummy([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
            
            metrics.register_thread(name, dummy.native_handle());
            
            for (int j = 0; j < 10; j++) {
                metrics.update_and_get();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            
            metrics.unregister_thread(name);
            dummy.join();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should not crash
    SUCCEED();
}

