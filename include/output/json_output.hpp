#pragma once

#include <string>
#include <vector>

#include "audio/audio_capture.hpp"

namespace output {

class JsonOutput {
public:
    static void print_devices(const std::vector<audio::AudioDevice>& devices);
    static void print_partial(const std::string& text);
    static void print_final(const std::string& text);
};

}  // namespace output
