#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/constants.hpp"
#include "stt_engine.hpp"

namespace stt {

/**
 * @brief Configuration for Streaming Transcriber
 */
struct TranscriberConfig {
    int step_ms = core::stt_constants::DEFAULT_STEP_MS;           // Transcribe every N ms of new audio
    int keep_ms = core::stt_constants::DEFAULT_KEEP_MS;           // Keep N ms of audio for context
    int max_length_ms = core::stt_constants::MAX_WINDOW_MS;    // Maximum audio window size
    bool token_dedup = core::stt_constants::DEFAULT_DEDUP;      // Enable token-based deduplication
    bool timestamp_merge = core::stt_constants::DEFAULT_TIMESTAMP_MERGE;  // Enable timestamp-based merging
    int min_audio_ms = core::audio_constants::MIN_AUDIO_LENGTH_MS;       // Minimum audio length to transcribe (reduced for short words)

    // Quality / Filter
    int min_repetition_len = core::stt_constants::DEFAULT_MIN_REPETITION;
    int hallucination_min_len = core::stt_constants::DEFAULT_HALLUCINATION_LEN;
    std::vector<std::string> hallucination_blacklist = core::stt_constants::DEFAULT_BLACKLIST;  // Hard filtering

    // Smart Filter
    std::vector<std::string> suspicious_phrases = {};
    float suspicious_no_speech_threshold = core::stt_constants::DEFAULT_SUSPICIOUS_NO_SPEECH_THRESHOLD; 
    float suspicious_confidence_threshold = core::stt_constants::DEFAULT_SUSPICIOUS_CONFIDENCE_THRESHOLD;
};

/**
 * @brief Streaming Transcriber with Sliding Window
 *
 * Implements 3 approaches for continuous transcription:
 * 1. Audio Context Keepalive - maintains context between transcriptions
 * 2. Token-Based Overlap Detection - removes duplicate words
 * 3. Time-Aligned Segment Merging - uses timestamps to merge overlaps
 */
class StreamingTranscriber {
public:
    struct Segment {
        std::string text;
        int64_t start_ms = 0;
        int64_t end_ms = 0;
        bool is_final = false;
        float confidence = 0.0F;
    };

    /**
     * @brief Construct streaming transcriber
     * @param engine Reference to initialized SttEngine
     * @param config Configuration parameters
     */
    StreamingTranscriber(SttEngine& engine, TranscriberConfig config = TranscriberConfig{});

    /**
     * @brief Update configuration at runtime
     */
    void set_config(const TranscriberConfig& config);

    /**
     * @brief Push audio samples (16kHz mono float)
     * @param audio Audio samples to add to buffer
     */
    void push_audio(const std::vector<float>& audio);

    /**
     * @brief Force transcription of current buffer (e.g., on speech end)
     * @return Finalized segment with complete transcription
     */
    auto finalize() -> Segment;

    /**
     * @brief Get partial transcription of current buffer
     * @return Non-final segment with current state
     */
    auto get_partial() -> Segment;

    /**
     * @brief Check if enough audio has accumulated for transcription
     */
    [[nodiscard]] auto should_transcribe() const -> bool;

    /**
     * @brief Process and transcribe if ready
     * @return Segment if transcription occurred, empty otherwise
     */
    auto process() -> Segment;

    /**
     * @brief Reset all state (call between utterances if needed)
     */
    void reset();

    /**
     * @brief Get accumulated audio length in ms
     */
    [[nodiscard]] auto audio_length_ms() const -> int;

    /**
     * @brief Get complete transcription history (for final output)
     */
    [[nodiscard]] auto get_full_text() const -> const std::string&;

private:
    auto transcribe_buffer() -> Segment;
    auto deduplicate_text(const std::string& new_text) -> std::string;
    // Helper methods
    [[nodiscard]] auto remove_repetition(const std::string& text) const -> std::string;
    static auto find_overlap(const std::vector<std::string>& prev, const std::vector<std::string>& curr) -> int;
    
    // Updated filtering signature
    auto is_hallucination(const std::string& text, const SttEngine::TranscriptionResult& result) -> bool;
    
    static auto tokenize(const std::string& text) -> std::vector<std::string>;

    // Helper methods
    auto post_process_text(const SttEngine::TranscriptionResult& result) -> std::string;

    // Refactoring helpers
    auto prepare_combined_buffer() -> std::vector<float>;
    void append_to_full_text(const std::string& final_text);
    void update_state_after_transcription(const SttEngine::TranscriptionResult& result, int audio_len_ms);

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    SttEngine& engine_;
    TranscriberConfig config_;

    // Audio buffers
    std::vector<float> audio_buffer_;  // Current audio window
    std::vector<float> keep_buffer_;   // Context from previous transcription
    int64_t audio_offset_ms_ = 0;      // Time offset for timestamps

    // Text state
    std::string previous_text_;                 // Last transcription output
    std::string full_text_;                     // Complete accumulated text
    std::vector<std::string> previous_tokens_;  // For overlap detection

    // Timing
    int64_t last_transcription_ms_ = 0;

    static constexpr int kSampleRate = 16000;
};

}  // namespace stt
