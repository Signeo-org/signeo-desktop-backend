#pragma once

/**
 * @file stt_engine.hpp
 * @brief Speech-to-Text Engine using Whisper.cpp
 */

#include <memory>
#include <string>
#include <vector>

#include "../core/result.hpp"

// Forward declarations
struct whisper_context;
struct WhisperFullParams;

#include "core/constants.hpp"

// ... (Result include)

namespace stt {

/**
 * @brief Configuration for STT Engine
 */
struct SttConfig {
    std::string model_path = core::stt_constants::DEFAULT_MODEL_PATH;
    std::string language = core::stt_constants::DEFAULT_LANGUAGE;
    int n_threads = core::stt_constants::DEFAULT_THREADS_FALLBACK;
    bool use_gpu = true;
    bool flash_attn = true;

    // Quality settings
    bool print_progress = core::stt_constants::DEFAULT_PRINT_PROGRESS;
    bool print_timestamps = core::stt_constants::DEFAULT_PRINT_TIMESTAMPS;
    bool single_segment = core::stt_constants::DEFAULT_SINGLE_SEGMENT;
    bool no_context = core::stt_constants::DEFAULT_NO_CONTEXT;

    // Decoding Strategy
    int beam_size = core::stt_constants::DEFAULT_BEAM_SIZE;
    int max_tokens = core::stt_constants::DEFAULT_MAX_TOKENS;
    int audio_ctx = core::stt_constants::DEFAULT_AUDIO_CTX;

    // Thresholds
    float no_speech_threshold = core::stt_constants::DEFAULT_NO_SPEECH_THOLD;
    float entropy_threshold = core::stt_constants::DEFAULT_ENTROPY_THOLD;
    float logprob_threshold = core::stt_constants::DEFAULT_LOGPROB_THOLD;

    // Temperature fallback
    float temperature = core::stt_constants::DEFAULT_TEMPERATURE;
    float temperature_inc = core::stt_constants::DEFAULT_TEMPERATURE_INC;
    bool no_fallback = core::stt_constants::DEFAULT_NO_FALLBACK;

    // Output Filtering
    bool suppress_blank = core::stt_constants::DEFAULT_SUPPRESS_BLANK;
    bool suppress_nst = core::stt_constants::DEFAULT_SUPPRESS_NST;

    // Initial prompt (converted to tokens internally)
    std::string initial_prompt = "";
};

/**
 * @brief Speech-to-Text Engine using Whisper.cpp
 *
 * Designed for VAD-gated transcription: accumulate audio during speech,
 * then transcribe the complete segment when speech ends.
 */
class SttEngine {
public:
    struct TranscriptionResult {
        std::string text;
        int64_t duration_ms = 0;  ///< Processing time
        float avg_probability = 0.0F;
        float min_probability = 0.0F;     ///< Minimum token probability (confidence lower bound)
        float max_no_speech_prob = 0.0F;  ///< Maximum probability that segment is silence
    };

    /**
     * @brief Factory method to create an SttEngine instance
     * @param config Engine configuration
     * @return Result containing unique_ptr to SttEngine, or error message
     */
    static auto create(const SttConfig& config = SttConfig{}) -> core::Result<std::unique_ptr<SttEngine>>;

    ~SttEngine();

    // Non-copyable
    SttEngine(const SttEngine&) = delete;
    auto operator=(const SttEngine&) -> SttEngine& = delete;

    // Move-enabled
    SttEngine(SttEngine&& /*other*/) noexcept;
    auto operator=(SttEngine&& /*other*/) noexcept -> SttEngine&;

    /**
     * @brief Transcribe audio buffer (16kHz mono float32)
     * @param audio Audio samples at 16kHz
     * @return Result containing TranscriptionResult, or error message
     */
    auto transcribe(const std::vector<float>& audio) -> core::Result<TranscriptionResult>;

    /**
     * @brief Check if engine is ready for transcription
     */
    [[nodiscard]] auto is_ready() const -> bool;

    /**
     * @brief Get the configured language
     */
    [[nodiscard]] auto language() const -> const std::string&;

private:
    // Private constructor - use create() factory
    explicit SttEngine(SttConfig config);

    auto init_whisper() -> core::Status;

    SttConfig config_;
    whisper_context* ctx_ = nullptr;

    // Prompt tokens from previous transcription for context continuity
    // These are automatically populated after each transcribe() call
    std::vector<int> prompt_tokens_;

public:
    /**
     * @brief Clear stored prompt tokens (call between unrelated utterances)
     */
    void clear_prompt_tokens() { prompt_tokens_.clear(); }

    /**
     * @brief Get number of stored prompt tokens
     */
    [[nodiscard]] auto prompt_token_count() const -> size_t { return prompt_tokens_.size(); }
};

}  // namespace stt
