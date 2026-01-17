#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace core {

struct AudioChunk {
    std::vector<float> data;
    std::chrono::steady_clock::time_point capture_time;
};

struct TranscriptionSegment {
    std::string text;
    float confidence = 0.0f;
    std::chrono::steady_clock::time_point capture_time;
    std::chrono::steady_clock::time_point finalize_time;
    bool is_final = false;
};

} // namespace core
