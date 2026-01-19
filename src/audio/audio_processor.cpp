#include "audio/audio_processor.hpp"

#include <spdlog/spdlog.h>

#include "core/result.hpp"

namespace audio {

auto AudioProcessor::create(int input_rate, int input_channels, int output_rate)
    -> core::Result<std::unique_ptr<AudioProcessor>> {
    if (input_channels < 1 || input_channels > kMaxInputChannels) {
        return core::log_error(std::format("Invalid channel count (must be 1-{})", kMaxInputChannels));
    }

    Config config{
        .input_rate = input_rate,
        .input_channels = input_channels,
        .output_rate = output_rate,
    };

    // Use unique_ptr with private constructor (need to access private ctor so can't use make_unique directly without
    // friend)
    std::unique_ptr<AudioProcessor> processor(new AudioProcessor(config));

    // Only create resampler if rate conversion is needed
    if (input_rate != output_rate) {
        auto res_result = AudioResampler::create(AudioResampler::Config{
            .input_rate = input_rate,
            .output_rate = output_rate,
            .quality = kResamplerQuality,
        });
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

AudioProcessor::AudioProcessor(const Config& config)
    : input_rate_(config.input_rate), input_channels_(config.input_channels), output_rate_(config.output_rate) {}

auto AudioProcessor::process(const std::vector<float>& interleaved_input) -> std::vector<float> {
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

auto AudioProcessor::downmix_to_mono(const std::vector<float>& interleaved) const -> std::vector<float> {
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

}  // namespace audio
