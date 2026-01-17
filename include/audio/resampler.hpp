#pragma once

#include <speex/speex_resampler.h>

#include <memory>
#include <vector>

#include "../core/result.hpp"

namespace audio {

/**
 * @brief High-quality audio resampler using SpeexDSP
 *
 * Converts audio between sample rates (e.g., 48kHz -> 16kHz)
 */
class AudioResampler {
public:
    /**
     * @brief Create a resampler instance
     *
     * @param input_rate Source sample rate (e.g., 48000)
     * @param output_rate Target sample rate (e.g., 16000)
     * @param quality Resampling quality (0-10, higher = better quality, more CPU)
     * @return Result containing unique_ptr to Resampler or error
     */
    static core::Result<std::unique_ptr<AudioResampler>> create(int input_rate, int output_rate, int quality = 5);

private:
    AudioResampler(int input_rate, int output_rate, int quality);

public:
    ~AudioResampler();

    // Disable copy
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /**
     * @brief Resample audio data
     *
     * @param input Input samples at source rate
     * @return Resampled output at target rate
     */
    std::vector<float> process(const std::vector<float>& input);

    /**
     * @brief Reset resampler state (clears internal buffers)
     */
    void reset();

    int input_rate() const { return input_rate_; }
    int output_rate() const { return output_rate_; }

    /**
     * @brief Calculate expected output size for given input size
     */
    size_t expected_output_size(size_t input_size) const;

private:
    SpeexResamplerState* resampler_ = nullptr;
    int input_rate_;
    int output_rate_;
};

} // namespace audio
