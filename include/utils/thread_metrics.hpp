#pragma once

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace utils {

struct ThreadCpuStats {
    std::string name;
    double cpu_usage_percent = 0.0;

    // Internal state for delta calculation
    uint64_t last_system_time = 0;
    uint64_t last_user_time = 0;
    uint64_t last_check_time = 0;
};

class ThreadMetrics {
public:
    static auto get() -> ThreadMetrics&;

    void register_thread(const std::string& name, std::thread::native_handle_type handle);
    void unregister_thread(const std::string& name);

    // Updates and returns latest stats for all registered threads
    auto update_and_get() -> std::vector<ThreadCpuStats>;

private:
    ThreadMetrics() = default;

    struct ThreadInfo {
        std::thread::native_handle_type handle{};
        ThreadCpuStats stats;
    };

    std::mutex mutex_;
    std::map<std::string, ThreadInfo> threads_;
};

}  // namespace utils
