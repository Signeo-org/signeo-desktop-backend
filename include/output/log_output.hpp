#pragma once

/**
 * @file logging.hpp
 * @brief Structured logging and timing utilities
 */

#include <spdlog/spdlog.h>

#include <chrono>
#include <format>
#include <source_location>
#include <string>

namespace core {

/**
 * @brief Initialize the logging system
 * @param log_file Path to log file (if empty, file logging is disabled)
 * @param use_console If true, log to console (stdout)
 */
void init_logging(const std::string& log_file, bool use_console);

// ============================================================================
// Structured Logging Macros
// ============================================================================

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

/**
 * @brief Log function entry with arguments
 * Usage: LOG_TRACE_ENTRY("Processing audio samples={}", count);
 */
#define LOG_TRACE_ENTRY(...) spdlog::trace("[ENTER] {}() " __VA_OPT__("| ") __VA_ARGS__, __func__)

/**
 * @brief Log function exit
 */
#define LOG_TRACE_EXIT() spdlog::trace("[EXIT] {}()", __func__)

/**
 * @brief Log function exit with return value
 */
#define LOG_TRACE_EXIT_VAL(val) spdlog::trace("[EXIT] {}() -> {}", __func__, val)

// NOLINTEND(cppcoreguidelines-macro-usage)

/**
 * @brief RAII scoped trace for automatic entry/exit logging with timing
 */
class ScopedTrace {
public:
    explicit ScopedTrace(const char* func, const std::source_location& loc = std::source_location::current());
    
    // Non-copyable/movable to enforce scoping
    ScopedTrace(const ScopedTrace&) = delete;
    auto operator=(const ScopedTrace&) -> ScopedTrace& = delete;
    ScopedTrace(ScopedTrace&&) = delete;
    auto operator=(ScopedTrace&&) -> ScopedTrace& = delete;
    ~ScopedTrace();

private:
    const char* func_;
    std::source_location loc_;
    std::chrono::steady_clock::time_point start_;
};

/**
 * @brief Create a scoped trace for current function
 * @note Named LOG_SCOPED_TRACE to avoid conflict with Google Test's SCOPED_TRACE
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_SCOPED_TRACE() ::core::ScopedTrace _trace_scope_(__func__)

/**
 * @brief Log a checkpoint within a function
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_CHECKPOINT(msg) spdlog::trace("[CHECKPOINT] {}() | {}", __func__, msg)

// ============================================================================
// Timing Utilities
// ============================================================================

/**
 * @brief Simple timer for measuring durations
 */
class Timer {
public:
    Timer();

    void reset();

    [[nodiscard]] auto elapsed_us() const -> int64_t;

    [[nodiscard]] auto elapsed_ms() const -> int64_t;

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace core
