#include "audio/audio_processor.hpp"

#include <spdlog/spdlog.h>

#include "core/result.hpp"

namespace audio {

core::Result<std::unique_ptr<AudioProcessor>> AudioProcessor::create(int input_rate, int input_channels,
                                                                     int output_rate) {
    if (input_channels < 1 || input_channels > 8) {
        return core::log_error("Invalid channel count (must be 1-8)");
    }

    // Use unique_ptr with private constructor (need to access private ctor so can't use make_unique directly without
    // friend)
    std::unique_ptr<AudioProcessor> processor(new AudioProcessor(input_rate, input_channels, output_rate));

    // Only create resampler if rate conversion is needed
    if (input_rate != output_rate) {
        auto res_result = AudioResampler::create(input_rate, output_rate, 5);
        if (!res_result) {
            return std::unexpected(res_result.error());
        }
        processor->resampler_ = std::move(*res_result);
    }

    std::string downmix_str =
        (input_channels > 1) ? std::to_string(input_channels) + "ch->Mono" : "None (already Mono)";

    std::string resample_str = (input_rate != output_rate)
                                   ? std::to_string(input_rate) + "Hz->" + std::to_string(output_rate) + "Hz"
                                   : "None (already " + std::to_string(output_rate) + "Hz)";

    spdlog::info("AudioProcessor initialized. Downmix: {}, Resample: {}", downmix_str, resample_str);

    return processor;
}

AudioProcessor::AudioProcessor(int input_rate, int input_channels, int output_rate)
    : input_rate_(input_rate), input_channels_(input_channels), output_rate_(output_rate) {}

std::vector<float> AudioProcessor::process(const std::vector<float>& interleaved_input) {
    if (interleaved_input.empty()) {
        return {};
    }

    // Step 1: Downmix to mono (if multi-channel)
    std::vector<float> mono_data;
    if (input_channels_ > 1) {
        mono_data = downmix_to_mono(interleaved_input);
    } else {
        mono_data = interleaved_input;
    }

    // Step 2: Resample to target rate (if needed)
    if (resampler_) {
        return resampler_->process(mono_data);
    }

    return mono_data;
}

void AudioProcessor::reset() {
    if (resampler_) {
        resampler_->reset();
    }
}

std::vector<float> AudioProcessor::downmix_to_mono(const std::vector<float>& interleaved) const {
    size_t num_frames = interleaved.size() / input_channels_;
    std::vector<float> mono(num_frames);

    float inv_channels = 1.0F / static_cast<float>(input_channels_);

    for (size_t i = 0; i < num_frames; ++i) {
        float sum = 0.0F;
        for (int ch = 0; ch < input_channels_; ++ch) {
            sum += interleaved[(i * input_channels_) + ch];
        }
        mono[i] = sum * inv_channels;
    }

    return mono;
}

} // namespace audio
