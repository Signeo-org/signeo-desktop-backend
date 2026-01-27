#include "utils/thread_metrics.hpp"
#include "core/constants.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

#if !defined(_WIN32)
    #include <pthread.h>
    #include <time.h>
#endif

namespace utils {

// Local constants removed (See core::app_constants)

auto ThreadMetrics::get() -> ThreadMetrics& {
    static ThreadMetrics instance;
    return instance;
}

void ThreadMetrics::register_thread(const std::string& name, std::thread::native_handle_type handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    ThreadInfo info;
    info.handle = handle;
    info.stats.name = name;

    // Initialize timestamps
#if defined(_WIN32)
    FILETIME ft_creation;
    FILETIME ft_exit;
    FILETIME ft_kernel;
    FILETIME ft_user;
    auto* h_thread = (HANDLE)handle;
    if (GetThreadTimes(h_thread, &ft_creation, &ft_exit, &ft_kernel, &ft_user) != 0) {
        ULARGE_INTEGER u_kernel;
        ULARGE_INTEGER u_user;
        u_kernel.LowPart = ft_kernel.dwLowDateTime;
        u_kernel.HighPart = ft_kernel.dwHighDateTime;
        u_user.LowPart = ft_user.dwLowDateTime;
        u_user.HighPart = ft_user.dwHighDateTime;

        info.stats.last_user_time = u_user.QuadPart;
        info.stats.last_system_time = u_kernel.QuadPart;
    }
    info.stats.last_check_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
#else
    // Linux/POSIX implementation
    clockid_t cid;
    if (pthread_getcpuclockid((pthread_t)handle, &cid) == 0) {
        struct timespec ts;
        if (clock_gettime(cid, &ts) == 0) {
            // Store total nanoseconds in last_system_time (we combine user+sys here)
            info.stats.last_system_time = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
            info.stats.last_user_time = 0;
        }
    }
    info.stats.last_check_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
#endif

    threads_[name] = info;
    spdlog::info("Metrics: Registered thread '{}'", name);
}

void ThreadMetrics::unregister_thread(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.erase(name);
}

auto ThreadMetrics::update_and_get() -> std::vector<ThreadCpuStats> {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ThreadCpuStats> results;

    uint64_t now_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();

#if defined(_WIN32)
    // Windows uses 100ns units, we convert to match consistent internal logic if needed,
    // but the existing windows logic is self-contained.
    // Let's keep existing Windows logic as is, but note that now_ms was used there.
    // The previous code calculated now_ms. Let's restore that for Windows block.
    uint64_t now_ms = now_ns / core::app_constants::NANOSECONDS_PER_MILLISECOND;

    for (auto& [name, info] : threads_) {
        // Prepare handles
        auto* h_thread = (HANDLE)info.handle;
        FILETIME ft_creation;
        FILETIME ft_exit;
        FILETIME ft_kernel;
        FILETIME ft_user;

        if (GetThreadTimes(h_thread, &ft_creation, &ft_exit, &ft_kernel, &ft_user) != 0) {
            ULARGE_INTEGER u_kernel;
            ULARGE_INTEGER u_user;
            u_kernel.LowPart = ft_kernel.dwLowDateTime;
            u_kernel.HighPart = ft_kernel.dwHighDateTime;
            u_user.LowPart = ft_user.dwLowDateTime;
            u_user.HighPart = ft_user.dwHighDateTime;

            uint64_t current_system = u_kernel.QuadPart;
            uint64_t current_user = u_user.QuadPart;

            uint64_t delta_system = current_system - info.stats.last_system_time;
            uint64_t delta_user = current_user - info.stats.last_user_time;
            uint64_t delta_time_ms = now_ms - info.stats.last_check_time;

            if (delta_time_ms > 0) {
                // Times are in 100ns units.
                // Total (100ns) / (Duration_ms * 10000) * 100%
                // 1ms = 10,000 * 100ns
                uint64_t total_delta_100ns = delta_system + delta_user;
                double cpu_percent = static_cast<double>(total_delta_100ns) / (static_cast<double>(delta_time_ms) * core::app_constants::WINDOWS_TIME_UNIT) * core::app_constants::PERCENT_MULTIPLIER;

                // Clamp and smooth
                cpu_percent = std::max<double>(cpu_percent, 0);
                // if (cpu_percent > 100) cpu_percent = 100; // Can exceed 100% on multi-core? No, thread is single core
                // execution context.

                info.stats.cpu_usage_percent = cpu_percent;

                // Update state
                info.stats.last_system_time = current_system;
                info.stats.last_user_time = current_user;
                info.stats.last_check_time = now_ms;
            }
        }
        results.push_back(info.stats);
    }
#else
    for (auto& [name, info] : threads_) {
        clockid_t cid;
        double cpu_percent = 0.0;

        if (pthread_getcpuclockid((pthread_t)info.handle, &cid) == 0) {
            struct timespec ts;
            if (clock_gettime(cid, &ts) == 0) {
                uint64_t current_total_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

                uint64_t delta_cpu_ns = current_total_ns - info.stats.last_system_time;
                uint64_t delta_time_ns = now_ns - info.stats.last_check_time;

                if (delta_time_ns > 0) {
                    // (cpu_delta / time_delta) * 100
                    cpu_percent = (double)delta_cpu_ns / (double)delta_time_ns * 100.0;
                    if (cpu_percent < 0)
                        cpu_percent = 0;
                    // if (cpu_percent > 100) cpu_percent = 100; // soft clamp if needed
                }

                info.stats.last_system_time = current_total_ns;
            }
        }

        info.stats.cpu_usage_percent = cpu_percent;
        info.stats.last_check_time = now_ns;
        results.push_back(info.stats);
    }
#endif

    return results;
}

}  // namespace utils
