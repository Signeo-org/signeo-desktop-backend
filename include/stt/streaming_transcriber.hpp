#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "stt_engine.hpp"

namespace stt {

/**
 * @brief Configuration for Streaming Transcriber
 */
struct TranscriberConfig {
    int step_ms = 2000;          // Transcribe every N ms of new audio
    int keep_ms = 500;           // Keep N ms of audio for context
    int max_length_ms = 10000;   // Maximum audio window size
    bool token_dedup = true;     // Enable token-based deduplication
    bool timestamp_merge = true; // Enable timestamp-based merging
    int min_audio_ms = 200;      // Minimum audio length to transcribe (reduced for short words)

    // Quality / Filter
    int min_repetition_len = 10;
    int hallucination_min_len = 2;
    std::vector<std::string> hallucination_blacklist = {"thank you", "thank you very much", "you", "bye",
                                                        "the following is a transcription"};
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
        float confidence = 0.0f;
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
    Segment finalize();

    /**
     * @brief Get partial transcription of current buffer
     * @return Non-final segment with current state
     */
    Segment get_partial();

    /**
     * @brief Check if enough audio has accumulated for transcription
     */
    bool should_transcribe() const;

    /**
     * @brief Process and transcribe if ready
     * @return Segment if transcription occurred, empty otherwise
     */
    Segment process();

    /**
     * @brief Reset all state (call between utterances if needed)
     */
    void reset();

    /**
     * @brief Get accumulated audio length in ms
     */
    int audio_length_ms() const;

    /**
     * @brief Get complete transcription history (for final output)
     */
    const std::string& get_full_text() const;

private:
    Segment transcribe_buffer();
    std::string deduplicate_text(const std::string& new_text);
    // Helper methods
    std::string remove_repetition(const std::string& text) const;
    static int find_overlap(const std::vector<std::string>& prev, const std::vector<std::string>& curr);
    bool is_hallucination(const std::string& text);
    static std::vector<std::string> tokenize(const std::string& text);

    // Helper methods
    std::string post_process_text(const std::string& raw_text);

    // Refactoring helpers
    std::vector<float> prepare_combined_buffer();
    void append_to_full_text(const std::string& final_text);
    void update_state_after_transcription(const SttEngine::TranscriptionResult& result, int audio_len_ms);

    SttEngine& engine_;
    TranscriberConfig config_;

    // Audio buffers
    std::vector<float> audio_buffer_; // Current audio window
    std::vector<float> keep_buffer_;  // Context from previous transcription
    int64_t audio_offset_ms_ = 0;     // Time offset for timestamps

    // Text state
    std::string previous_text_;                // Last transcription output
    std::string full_text_;                    // Complete accumulated text
    std::vector<std::string> previous_tokens_; // For overlap detection

    // Timing
    int64_t last_transcription_ms_ = 0;

    static constexpr int SAMPLE_RATE = 16000;
};

} // namespace stt
