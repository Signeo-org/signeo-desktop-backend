#pragma once

#include <string>
#include <vector>

#include "audio/audio_capture.hpp"

namespace output {

/**
 * @brief JSON output utilities for IPC communication with frontend.
 * 
 * All messages are newline-delimited JSON (NDJSON) for easy parsing.
 */
class JsonOutput {
public:
    // Startup
    static void print_ready(const std::string& version);
    
    // Device messages
    static void print_devices(const std::vector<audio::AudioDevice>& devices);
    static void print_device_selected(int index, const std::string& name, bool is_default = false);
    
    // Transcription
    static void print_partial(const std::string& text);
    static void print_final(const std::string& text);
    
    // Status
    static void print_status(const std::string& state);  // "listening", "processing", "idle"
    static void print_error(const std::string& code, const std::string& message);
    
private:
    static auto escape_json(const std::string& str) -> std::string;
};

}  // namespace output
