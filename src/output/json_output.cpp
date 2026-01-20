#include "output/json_output.hpp"

#include <iostream>

namespace output {

void JsonOutput::print_devices(const std::vector<audio::AudioDevice>& devices) {
    std::cout << "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::cout << R"({"index":)" << device.index
                  << R"(,"name":")" << device.name << R"(")"
                  << R"(,"channels":)" << device.max_input_channels
                  << R"(,"sample_rate":)" << device.default_sample_rate
                  << R"(,"is_default":)" << (device.is_default ? "true" : "false")
                  << R"(,"is_loopback":)" << (device.is_loopback ? "true" : "false")
                  << "}";
        if (i < devices.size() - 1) {
            std::cout << ",";
        }
    }
    std::cout << "]" << '\n';
}

void JsonOutput::print_partial(const std::string& text) {
    std::cout << R"({"type":"partial","text":")" << text << R"("})" << '\n';
}

void JsonOutput::print_final(const std::string& text) {
    std::cout << R"({"type":"final","text":")" << text << R"("})" << '\n';
}

}  // namespace output
