#pragma once

#include <memory>
#include <vector>

#include "../core/result.hpp"
#include "resampler.hpp"

namespace audio {

/**
 * @brief Audio processor for downmixing and resampling
 *
 * Converts multi-channel audio at any sample rate to mono 16kHz
 * format required by VAD (Silero) and STT (Whisper).
 */
class AudioProcessor {
public:
    /**
     * @brief Create an audio processor
     *
     * @param input_rate Source sample rate (e.g., 48000)
     * @param input_channels Number of input channels (1=mono, 2=stereo)
     * @param output_rate Target sample rate (default: 16000 for VAD/STT)
     * @return Result containing unique_ptr to processor or error
     */
    static auto create(int input_rate, int input_channels, int output_rate = 16000)
        -> core::Result<std::unique_ptr<AudioProcessor>>;

private:
    AudioProcessor(int input_rate, int input_channels, int output_rate);

public:
    ~AudioProcessor() = default;

    // Disable copy
    AudioProcessor(const AudioProcessor&) = delete;
    auto operator=(const AudioProcessor&) -> AudioProcessor& = delete;

    /**
     * @brief Process interleaved audio data
     *
     * Performs stereo-to-mono downmix (if needed) and resampling.
     *
     * @param interleaved_input Interleaved input samples
     * @return Mono output samples at output_rate
     */
    auto process(const std::vector<float>& interleaved_input) -> std::vector<float>;

    /**
     * @brief Reset processor state
     */
    void reset();

    auto input_rate() const -> int {
        return input_rate_;
    }
    auto input_channels() const -> int {
        return input_channels_;
    }
    auto output_rate() const -> int {
        return output_rate_;
    }

private:
    int input_rate_;
    int input_channels_;
    int output_rate_;

    std::unique_ptr<AudioResampler> resampler_;

    /**
     * @brief Downmix stereo/multi-channel to mono
     *
     * Averages all channels: (ch1 + ch2 + ...) / num_channels
     *
     * @param interleaved Interleaved multi-channel samples
     * @return Mono samples
     */
    auto downmix_to_mono(const std::vector<float>& interleaved) const -> std::vector<float>;
};

}  // namespace audio
