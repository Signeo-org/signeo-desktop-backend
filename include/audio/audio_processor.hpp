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
    static core::Result<std::unique_ptr<AudioProcessor>> create(int input_rate, int input_channels,
                                                                int output_rate = 16000);

private:
    AudioProcessor(int input_rate, int input_channels, int output_rate);

public:
    ~AudioProcessor() = default;

    // Disable copy
    AudioProcessor(const AudioProcessor&) = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    /**
     * @brief Process interleaved audio data
     *
     * Performs stereo-to-mono downmix (if needed) and resampling.
     *
     * @param interleaved_input Interleaved input samples
     * @return Mono output samples at output_rate
     */
    std::vector<float> process(const std::vector<float>& interleaved_input);

    /**
     * @brief Reset processor state
     */
    void reset();

    int input_rate() const { return input_rate_; }
    int input_channels() const { return input_channels_; }
    int output_rate() const { return output_rate_; }

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
    std::vector<float> downmix_to_mono(const std::vector<float>& interleaved);
};

} // namespace audio
