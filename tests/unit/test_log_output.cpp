#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

#include "output/log_output.hpp"

// ============================================================================
// Global Test Environment for spdlog state management
// ============================================================================

class SpdlogEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Ensure spdlog is initialized at test start
        core::init_logging("", true);
    }

    void TearDown() override {
        // Reinitialize spdlog to a clean state after all tests
        // This ensures later test suites have a working logger
        spdlog::drop_all();
        core::init_logging("", true);
    }
};

// Register global environment
static ::testing::Environment* const g_spdlog_env = ::testing::AddGlobalTestEnvironment(new SpdlogEnvironment);

// ============================================================================
// Tests for core::Timer
// ============================================================================

TEST(LoggingTest, TimerBasic) {
    core::Timer timer;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int64_t elapsed_ms = timer.elapsed_ms();
    int64_t elapsed_us = timer.elapsed_us();

    EXPECT_GE(elapsed_ms, 9);     // At least 9ms
    EXPECT_LE(elapsed_ms, 50);    // But not more than 50ms
    EXPECT_GE(elapsed_us, 9000);  // At least 9000us
}

TEST(LoggingTest, TimerReset) {
    core::Timer timer;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.reset();

    int64_t elapsed = timer.elapsed_ms();
    EXPECT_LT(elapsed, 5);  // Should be close to 0 after reset
}

TEST(LoggingTest, TimerPrecision) {
    core::Timer timer;

    // Measure microsecond precision
    auto start = std::chrono::steady_clock::now();
    int64_t timer_us = timer.elapsed_us();
    auto end = std::chrono::steady_clock::now();

    auto actual_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Timer should report a value close to actual elapsed time
    EXPECT_LE(std::abs(timer_us - actual_us), 1000);  // Within 1ms tolerance
}

// ============================================================================
// Tests for ScopedTrace (compile-time check)
// ============================================================================

TEST(LoggingTest, ScopedTraceCompiles) {
    // This test just verifies ScopedTrace compiles and doesn't crash
    {
        core::ScopedTrace trace(__func__);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // If we get here, it worked
    SUCCEED();
}

// ============================================================================
// Tests for init_logging (with proper spdlog reset)
// Note: These tests are run LAST alphabetically (LoggingInitTest > LoggingTest)
// The global TearDown() reinitializes spdlog after these complete.
// ============================================================================

class LoggingInitTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset spdlog state before each test
        spdlog::drop_all();
    }

    void TearDown() override {
        // Reinitialize after each logging init test
        spdlog::drop_all();
        core::init_logging("", false);  // Silent reinitialization
    }
};

TEST_F(LoggingInitTest, InitLoggingConsoleOnly) {
    core::init_logging("", true);
    spdlog::info("Test log message to console");
    SUCCEED();
}

TEST_F(LoggingInitTest, InitLoggingWithFile) {
    std::string temp_log = "test_output.log";

    core::init_logging(temp_log, true);
    spdlog::info("Test log message to file");

    // Verify file was created
    EXPECT_TRUE(std::filesystem::exists(temp_log));

    // Cleanup before file removal
    spdlog::drop_all();
    std::filesystem::remove(temp_log);
}

TEST_F(LoggingInitTest, InitLoggingFileOnly) {
    std::string temp_log = "test_file_only.log";

    core::init_logging(temp_log, false);  // Console disabled
    spdlog::info("File only log message");

    EXPECT_TRUE(std::filesystem::exists(temp_log));

    spdlog::drop_all();
    std::filesystem::remove(temp_log);
}
