#pragma once

/**
 * @file signal_handler.h
 * @brief Cross-platform signal handling (Ctrl+C) for graceful shutdown
 */

#include <atomic>

namespace utils {

class SignalHandler {
public:
    static void init();

    static auto is_running() -> bool;
    static auto is_force_exit() -> bool;
    static void trigger_shutdown();

private:
    static std::atomic<bool> g_running;
    static std::atomic<bool> g_force_exit;

    static void handle_signal(int signal);
};

}  // namespace utils
