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

namespace stt {

/**
 * @brief Configuration for STT Engine
 */
struct SttConfig {
    std::string model_path = "models/ggml-base.bin";
    std::string language = "en";
    int n_threads = 4;
    bool use_gpu = true;
    bool flash_attn = true;

    // Quality settings
    bool print_progress = false;
    bool print_timestamps = false;
    bool single_segment = true;  ///< For VAD-gated approach
    bool no_context = false;     ///< Keep context for accuracy
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
    auto is_ready() const -> bool;

    /**
     * @brief Get the configured language
     */
    auto language() const -> const std::string&;

private:
    // Private constructor - use create() factory
    explicit SttEngine(SttConfig config);

    auto init_whisper() -> core::Status;

    SttConfig config_;
    whisper_context* ctx_ = nullptr;
};

}  // namespace stt
