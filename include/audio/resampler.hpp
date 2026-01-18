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
    static auto create(int input_rate, int output_rate, int quality = 5)
        -> core::Result<std::unique_ptr<AudioResampler>>;

private:
    AudioResampler(int input_rate, int output_rate, int quality);

public:
    ~AudioResampler();

    // Disable copy
    AudioResampler(const AudioResampler&) = delete;
    auto operator=(const AudioResampler&) -> AudioResampler& = delete;

    /**
     * @brief Resample audio data
     *
     * @param input Input samples at source rate
     * @return Resampled output at target rate
     */
    auto process(const std::vector<float>& input) -> std::vector<float>;

    /**
     * @brief Reset resampler state (clears internal buffers)
     */
    void reset();

    auto input_rate() const -> int {
        return input_rate_;
    }
    auto output_rate() const -> int {
        return output_rate_;
    }

    /**
     * @brief Calculate expected output size for given input size
     */
    auto expected_output_size(size_t input_size) const -> size_t;

private:
    SpeexResamplerState* resampler_ = nullptr;
    int input_rate_;
    int output_rate_;
};

}  // namespace audio
