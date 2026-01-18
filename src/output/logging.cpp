#include "output/logging.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <vector>

namespace core {

// ============================================================================
// ScopedTrace Implementation
// ============================================================================

ScopedTrace::ScopedTrace(const char* func, const std::source_location& loc)
    : func_(func), loc_(loc), start_(std::chrono::steady_clock::now()) {
    spdlog::trace("[ENTER] {}()", func_);
}

ScopedTrace::~ScopedTrace() {
    auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_).count();
    spdlog::trace("[EXIT] {}() | elapsed={}μs", func_, elapsed);
}

// ============================================================================
// Timer Implementation
// ============================================================================

Timer::Timer() : start_(std::chrono::steady_clock::now()) {}

void Timer::reset() {
    start_ = std::chrono::steady_clock::now();
}

auto Timer::elapsed_us() const -> int64_t {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_).count();
}

auto Timer::elapsed_ms() const -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
}

// ============================================================================
// Init Logging
// ============================================================================

void init_logging(const std::string& log_file, bool use_console) {
    try {
        std::vector<spdlog::sink_ptr> sinks;

        if (use_console) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%^%l%$] %v");
            sinks.push_back(console_sink);
        }

        if (!log_file.empty()) {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file_sink);
        }

        auto logger = std::make_shared<spdlog::logger>("multi_sink", begin(sinks), end(sinks));
        spdlog::set_default_logger(logger);

        // Default level
#ifdef NDEBUG
        spdlog::set_level(spdlog::level::info);
#else
        spdlog::set_level(spdlog::level::debug);
#endif
        spdlog::flush_on(spdlog::level::err);

        spdlog::debug("Logging initialized.");
    } catch (const spdlog::spdlog_ex& ex) {
        // Can't log here if logging failed... to stderr then
        // (Assuming standard cerr is available)
        // fprintf(stderr, "Log init failed: %s\n", ex.what());
    }
}

}  // namespace core
