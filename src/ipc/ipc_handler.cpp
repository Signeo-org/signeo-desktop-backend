#include "ipc/ipc_handler.hpp"

#include <iostream>
#include <optional>
#include <spdlog/spdlog.h>

namespace ipc {

IpcHandler::~IpcHandler() {
    stop();
}

void IpcHandler::start(CommandCallback callback) {
    if (running_.load()) {
        return;
    }
    callback_ = std::move(callback);
    running_.store(true);
    reader_thread_ = std::thread(&IpcHandler::read_loop, this);
}

void IpcHandler::stop() {
    running_.store(false);
    // Note: Can't easily interrupt std::cin, but the thread will exit
    // on next read or when stdin closes
    if (reader_thread_.joinable()) {
        reader_thread_.detach();  // Allow thread to finish on its own
    }
}

void IpcHandler::read_loop() {
    spdlog::debug("IPC: stdin reader started");
    std::string line;
    
    while (running_.load() && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        
        spdlog::debug("IPC: received: {}", line);
        
        auto cmd = parse_command(line);
        if (cmd && callback_) {
            callback_(*cmd);
        } else {
            spdlog::warn("IPC: failed to parse command: {}", line);
        }
    }
    
    spdlog::debug("IPC: stdin reader stopped");
}

auto IpcHandler::parse_command(const std::string& line) -> std::optional<IpcCommand> {
    // Simple JSON parsing without external library
    // Format: {"cmd":"command_name",...}
    
    // Find "cmd" field
    auto cmd_pos = line.find(R"("cmd":")");
    if (cmd_pos == std::string::npos) {
        return std::nullopt;
    }
    
    auto cmd_start = cmd_pos + 7;  // After "cmd":"
    auto cmd_end = line.find('"', cmd_start);
    if (cmd_end == std::string::npos) {
        return std::nullopt;
    }
    
    std::string cmd_name = line.substr(cmd_start, cmd_end - cmd_start);
    
    if (cmd_name == "select_device") {
        // Extract index
        auto index_pos = line.find(R"("index":)");
        if (index_pos == std::string::npos) {
            return std::nullopt;
        }
        auto index_start = index_pos + 8;
        int index = 0;
        try {
            // Find end of number (next , or })
            auto index_end = line.find_first_of(",}", index_start);
            index = std::stoi(line.substr(index_start, index_end - index_start));
        } catch (...) {
            return std::nullopt;
        }
        return SelectDeviceCmd{index};
    }
    
    if (cmd_name == "get_devices") {
        return GetDevicesCmd{};
    }
    
    if (cmd_name == "stop") {
        return StopCmd{};
    }
    
    if (cmd_name == "set_config") {
        // Extract key and value
        auto key_pos = line.find(R"("key":")");
        if (key_pos == std::string::npos) {
            return std::nullopt;
        }
        auto key_start = key_pos + 7;
        auto key_end = line.find('"', key_start);
        if (key_end == std::string::npos) {
            return std::nullopt;
        }
        std::string key = line.substr(key_start, key_end - key_start);
        
        // Extract value - could be number, bool, or string
        auto value_pos = line.find(R"("value":)");
        if (value_pos == std::string::npos) {
            return std::nullopt;
        }
        auto value_start = value_pos + 8;
        
        // Check value type
        if (line[value_start] == '"') {
            // String
            auto val_end = line.find('"', value_start + 1);
            std::string val = line.substr(value_start + 1, val_end - value_start - 1);
            return SetConfigCmd{key, val};
        } else if (line.substr(value_start, 4) == "true") {
            return SetConfigCmd{key, true};
        } else if (line.substr(value_start, 5) == "false") {
            return SetConfigCmd{key, false};
        } else {
            // Number
            try {
                auto val_end = line.find_first_of(",}", value_start);
                std::string num_str = line.substr(value_start, val_end - value_start);
                if (num_str.find('.') != std::string::npos) {
                    return SetConfigCmd{key, std::stof(num_str)};
                } else {
                    return SetConfigCmd{key, std::stoi(num_str)};
                }
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
    return std::nullopt;
}

}  // namespace ipc
