#include "utils/signal_handler.hpp"

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace utils {

std::atomic<bool> SignalHandler::g_running{true};
std::atomic<bool> SignalHandler::g_force_exit{false};

void SignalHandler::handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        if (g_running.load()) {
            spdlog::warn("Interrupt received (Ctrl+C). Shutting down gracefully...");
            g_running.store(false);
        } else {
            spdlog::error("Force exit requested. Terminating immediately.");
            g_force_exit.store(true);
            std::exit(1);
        }
    }
}

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        SignalHandler::trigger_shutdown();  // Just trigger shutdown, let loop handle it
        // Or call handle_signal(SIGINT) directly?
        // Let's call the internal logic via public method if we expose it, or just use raise?
        // Simpler: just set flag.
        // Wait, handle_signal does logging.
        // Let's make handle_signal public or friend? No, static private.
        // I can just re-implement the logic here or call a public static helper.
        // Let's rely on std::raise(SIGINT) if possible? No, referencing local static is tricky.

        // Proper way:
        spdlog::warn("Interrupt received (Windows Console). Shutting down gracefully...");
        SignalHandler::trigger_shutdown();
        return TRUE;
    }
    return FALSE;
}
#endif

void SignalHandler::init() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

auto SignalHandler::is_running() -> bool {
    return g_running.load();
}

auto SignalHandler::is_force_exit() -> bool {
    return g_force_exit.load();
}

void SignalHandler::trigger_shutdown() {
    g_running.store(false);
}

}  // namespace utils
