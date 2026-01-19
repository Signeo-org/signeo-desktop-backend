#include "audio/resampler.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <stdexcept>

#include "core/result.hpp"

namespace audio {

namespace {
constexpr int kBufferMargin = 16;
}

auto AudioResampler::create(const Config& config) -> core::Result<std::unique_ptr<AudioResampler>> {
    auto resampler = std::unique_ptr<AudioResampler>(new AudioResampler(config));

    int err = 0;
    resampler->resampler_ = speex_resampler_init(1,  // channels (mono)
                                                 static_cast<spx_uint32_t>(config.input_rate),
                                                 static_cast<spx_uint32_t>(config.output_rate), config.quality, &err);

    if (err != RESAMPLER_ERR_SUCCESS || resampler->resampler_ == nullptr) {
        return core::log_error(std::format("Failed to initialize SpeexDSP resampler: error {}", err));
    }

    spdlog::info("AudioResampler initialized: {}Hz -> {}Hz (quality: {})", config.input_rate, config.output_rate,
                 config.quality);

    return resampler;
}

AudioResampler::AudioResampler(const Config& config)
    : input_rate_(config.input_rate), output_rate_(config.output_rate) {}

AudioResampler::~AudioResampler() {
    if (resampler_ != nullptr) {
        speex_resampler_destroy(resampler_);
        resampler_ = nullptr;
    }
}

auto AudioResampler::process(const std::vector<float>& input) -> std::vector<float> {
    if (input.empty()) {
        return {};
    }

    // Calculate expected output size with some margin
    size_t out_size = expected_output_size(input.size()) + kBufferMargin;
    std::vector<float> output(out_size);

    auto in_len = static_cast<spx_uint32_t>(input.size());
    auto out_len = static_cast<spx_uint32_t>(out_size);

    int err = speex_resampler_process_float(resampler_,
                                            0,  // channel index (mono)
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
    if (resampler_ != nullptr) {
        speex_resampler_reset_mem(resampler_);
    }
}

auto AudioResampler::expected_output_size(size_t input_size) const -> size_t {
    // output_size = input_size * (output_rate / input_rate)
    return static_cast<size_t>(std::ceil(static_cast<double>(input_size) * output_rate_ / input_rate_));
}

}  // namespace audio
