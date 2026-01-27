#include "output/json_output.hpp"

#include <iostream>
#include <sstream>

namespace output {

auto JsonOutput::escape_json(const std::string& str) -> std::string {
    std::ostringstream escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control character - escape as \uXXXX
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    escaped << c;
                }
        }
    }
    return escaped.str();
}

void JsonOutput::print_ready(const std::string& version) {
    std::cout << R"({"type":"ready","version":")" << escape_json(version) << R"("})" << '\n';
    std::cout.flush();
}

void JsonOutput::print_devices(const std::vector<audio::AudioDevice>& devices) {
    std::cout << R"({"type":"devices","devices":[)";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::cout << R"({"index":)" << device.index
                  << R"(,"name":")" << escape_json(device.name) << R"(")"
                  << R"(,"channels":)" << device.max_input_channels
                  << R"(,"sample_rate":)" << device.default_sample_rate
                  << R"(,"is_default":)" << (device.is_default ? "true" : "false")
                  << R"(,"is_loopback":)" << (device.is_loopback ? "true" : "false")
                  << "}";
        if (i < devices.size() - 1) {
            std::cout << ",";
        }
    }
    std::cout << "]}" << '\n';
    std::cout.flush();
}

void JsonOutput::print_device_selected(int index, const std::string& name, bool is_default) {
    std::cout << R"({"type":"device_selected","index":)" << index
              << R"(,"name":")" << escape_json(name) << R"(")"
              << R"(,"is_default":)" << (is_default ? "true" : "false")
              << "}" << '\n';
    std::cout.flush();
}

void JsonOutput::print_partial(const std::string& text) {
    std::cout << R"({"type":"partial","text":")" << escape_json(text) << R"("})" << '\n';
    std::cout.flush();
}

void JsonOutput::print_final(const std::string& text) {
    std::cout << R"({"type":"final","text":")" << escape_json(text) << R"("})" << '\n';
    std::cout.flush();
}

void JsonOutput::print_status(const std::string& state) {
    std::cout << R"({"type":"status","state":")" << escape_json(state) << R"("})" << '\n';
    std::cout.flush();
}

void JsonOutput::print_error(const std::string& code, const std::string& message) {
    std::cout << R"({"type":"error","code":")" << escape_json(code)
              << R"(","message":")" << escape_json(message) << R"("})" << '\n';
    std::cout.flush();
}

}  // namespace output
