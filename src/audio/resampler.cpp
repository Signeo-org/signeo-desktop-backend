#include "audio/resampler.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <stdexcept>

#include "core/result.hpp"

namespace audio {

core::Result<std::unique_ptr<AudioResampler>> AudioResampler::create(int input_rate, int output_rate, int quality) {
    auto resampler = std::unique_ptr<AudioResampler>(new AudioResampler(input_rate, output_rate, quality));

    int err = 0;
    resampler->resampler_ = speex_resampler_init(1, // channels (mono)
                                                 static_cast<spx_uint32_t>(input_rate),
                                                 static_cast<spx_uint32_t>(output_rate), quality, &err);

    if (err != RESAMPLER_ERR_SUCCESS || resampler->resampler_ == nullptr) {
        return core::log_error(std::format("Failed to initialize SpeexDSP resampler: error {}", err));
    }

    spdlog::info("AudioResampler initialized: {}Hz -> {}Hz (quality: {})", input_rate, output_rate, quality);

    return resampler;
}

AudioResampler::AudioResampler(int input_rate, int output_rate, int quality)
    : input_rate_(input_rate), output_rate_(output_rate) {}

AudioResampler::~AudioResampler() {
    if (resampler_) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }
}

std::vector<float> AudioResampler::process(const std::vector<float>& input) {
    if (input.empty()) {
        return {};
    }

    // Calculate expected output size with some margin
    size_t out_size = expected_output_size(input.size()) + 16;
    std::vector<float> output(out_size);

    spx_uint32_t in_len = static_cast<spx_uint32_t>(input.size());
    spx_uint32_t out_len = static_cast<spx_uint32_t>(out_size);

    int err = speex_resampler_process_float(resampler_,
                                            0, // channel index (mono)
                                            input.data(), &in_len, output.data(), &out_len);

    if (err != RESAMPLER_ERR_SUCCESS) {
        spdlog::error("Resampling failed: error {}", err);
        return {};
    }

    // Resize to actual output
    output.resize(out_len);
    return output;
}

void AudioResampler::reset() {
    if (resampler_) {
        speex_resampler_reset_mem(resampler_);
    }
}

size_t AudioResampler::expected_output_size(size_t input_size) const {
    // output_size = input_size * (output_rate / input_rate)
    return static_cast<size_t>(std::ceil(static_cast<double>(input_size) * output_rate_ / input_rate_));
}

} // namespace audio
