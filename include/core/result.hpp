#pragma once

/**
 * @file result.h
 * @brief Modern error handling using C++23 std::expected
 *
 * Provides:
 * - Result<T> type for error propagation
 */

#include <spdlog/spdlog.h>

#include <expected>
#include <format>
#include <source_location>
#include <string>

namespace core {

/**
 * @brief Result type alias for operations that can fail
 * @tparam T The success value type
 */
template <typename T>
using Result = std::expected<T, std::string>;

/**
 * @brief Result type for void operations (success/failure only)
 */
using Status = std::expected<void, std::string>;

/**
 * @brief Create an error result with formatted message
 */
template <typename... Args>
[[nodiscard]] auto make_error(std::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(std::format(fmt, std::forward<Args>(args)...));
}

/**
 * @brief Log and return an error (convenience for error cascade)
 */
[[nodiscard]] inline auto log_error(const std::string& msg,
                                    const std::source_location& loc = std::source_location::current()) {
    spdlog::error("[{}:{}] {}", loc.function_name(), loc.line(), msg);
    return std::unexpected(msg);
}

/**
 * @brief Propagate errors with logging
 */
#define TRY(expr)                                                                  \
    do {                                                                           \
        auto&& _result = (expr);                                                   \
        if (!_result) {                                                            \
            spdlog::error("Error propagated from {}: {}", #expr, _result.error()); \
            return std::unexpected(_result.error());                               \
        }                                                                          \
    } while (0)

/**
 * @brief Propagate errors and extract value
 */
#define TRY_VALUE(expr)                                                            \
    ({                                                                             \
        auto&& _result = (expr);                                                   \
        if (!_result) {                                                            \
            spdlog::error("Error propagated from {}: {}", #expr, _result.error()); \
            return std::unexpected(_result.error());                               \
        }                                                                          \
        std::move(*_result);                                                       \
    })

}  // namespace core
