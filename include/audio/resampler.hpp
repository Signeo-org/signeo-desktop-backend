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
    static constexpr int kDefaultQuality = 5;

    struct Config {
        int input_rate = 0;
        int output_rate = 0;
        int quality = kDefaultQuality;
    };

    /**
     * @brief Create a resampler instance
     *
     * @param config Configuration for the resampler
     * @return Result containing unique_ptr to Resampler or error
     */
    static auto create(const Config& config) -> core::Result<std::unique_ptr<AudioResampler>>;

private:
    explicit AudioResampler(const Config& config);

public:
    ~AudioResampler();

    // Disable copy and move
    AudioResampler(const AudioResampler&) = delete;
    auto operator=(const AudioResampler&) -> AudioResampler& = delete;
    AudioResampler(AudioResampler&&) = delete;
    auto operator=(AudioResampler&&) -> AudioResampler& = delete;

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

    [[nodiscard]] auto input_rate() const -> int {
        return input_rate_;
    }
    [[nodiscard]] auto output_rate() const -> int {
        return output_rate_;
    }

    /**
     * @brief Calculate expected output size for given input size
     */
    [[nodiscard]] auto expected_output_size(size_t input_size) const -> size_t;

private:
    SpeexResamplerState* resampler_ = nullptr;
    int input_rate_;
    int output_rate_;
};

}  // namespace audio
