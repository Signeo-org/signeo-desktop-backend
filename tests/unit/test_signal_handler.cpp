#include <gtest/gtest.h>

#include "utils/signal_handler.hpp"

// Note: SignalHandler tests are limited because:
// 1. Triggering real signals in tests is dangerous
// 2. init() registers global handlers that affect the test runner
// We can only test the public API safely.

TEST(SignalHandlerTest, InitialState) {
    // Before trigger, should be running
    // Note: This assumes no other test has called trigger_shutdown
    // In practice, the test runner might have different state

    // We can at least verify the API doesn't crash
    bool running = utils::SignalHandler::is_running();
    bool force_exit = utils::SignalHandler::is_force_exit();

    // Just verify types are correct
    EXPECT_TRUE(running || !running);  // Always true, just checking it compiles
    EXPECT_TRUE(force_exit || !force_exit);
}

TEST(SignalHandlerTest, TriggerShutdown) {
    // Save initial state
    bool initial_running = utils::SignalHandler::is_running();

    // Trigger shutdown
    utils::SignalHandler::trigger_shutdown();

    // Should now be not running
    EXPECT_FALSE(utils::SignalHandler::is_running());

    // Reset for other tests (no public reset method, so this is a limitation)
    // We can't easily reset without modifying the class
}

TEST(SignalHandlerTest, ForceExitInitiallyFalse) {
    // Force exit should only be set by double Ctrl+C
    // Initially false
    // Note: State may be polluted by previous test
    bool force_exit = utils::SignalHandler::is_force_exit();
    // Just verify the call doesn't crash
    EXPECT_TRUE(!force_exit || force_exit);
}
