#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace ipc {

/**
 * @brief Commands that can be received from the frontend via stdin.
 */
struct SelectDeviceCmd {
    int index;
};

struct SetConfigCmd {
    std::string key;
    std::variant<int, float, bool, std::string> value;
};

struct GetDevicesCmd {};

struct StopCmd {};

using IpcCommand = std::variant<SelectDeviceCmd, SetConfigCmd, GetDevicesCmd, StopCmd>;

/**
 * @brief Handles IPC communication via stdin.
 * 
 * Reads JSON commands from stdin in a background thread and dispatches
 * them to registered handlers.
 */
class IpcHandler {
public:
    using CommandCallback = std::function<void(const IpcCommand&)>;

    IpcHandler() = default;
    ~IpcHandler();

    IpcHandler(const IpcHandler&) = delete;
    auto operator=(const IpcHandler&) -> IpcHandler& = delete;
    IpcHandler(IpcHandler&&) = delete;
    auto operator=(IpcHandler&&) -> IpcHandler& = delete;

    void start(CommandCallback callback);
    void stop();
    [[nodiscard]] auto is_running() const -> bool { return running_.load(); }

private:
    void read_loop();
    static auto parse_command(const std::string& line) -> std::optional<IpcCommand>;

    std::atomic<bool> running_{false};
    std::thread reader_thread_;
    CommandCallback callback_;
};

}  // namespace ipc
