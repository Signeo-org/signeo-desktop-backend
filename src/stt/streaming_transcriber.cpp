#include "stt/streaming_transcriber.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "whisper.h"

namespace stt {

// Local constant removed, using core::audio_constants

StreamingTranscriber::StreamingTranscriber(SttEngine& engine, TranscriberConfig config)
    : engine_(engine), config_(std::move(config)) {
    spdlog::info("StreamingTranscriber initialized");
    spdlog::info("  Step: {}ms, Keep: {}ms, MaxLen: {}ms", config_.step_ms, config_.keep_ms, config_.max_length_ms);
    spdlog::info("  Token dedup: {}, Timestamp merge: {}", config_.token_dedup ? "on" : "off",
                 config_.timestamp_merge ? "on" : "off");
}

void StreamingTranscriber::set_config(const TranscriberConfig& config) {
    config_ = config;
}

void StreamingTranscriber::push_audio(const std::vector<float>& audio) {
    audio_buffer_.insert(audio_buffer_.end(), audio.begin(), audio.end());

    // Trim if exceeding max length
    int max_samples = (config_.max_length_ms * core::audio_constants::SAMPLE_RATE) / static_cast<int>(core::audio_constants::MILLISECONDS_PER_SECOND);
    if (std::cmp_greater(audio_buffer_.size(), max_samples)) {
        int excess = static_cast<int>(audio_buffer_.size()) - max_samples;
        audio_buffer_.erase(audio_buffer_.begin(), audio_buffer_.begin() + excess);
        audio_offset_ms_ += static_cast<int>((excess * core::audio_constants::MILLISECONDS_PER_SECOND) / core::audio_constants::SAMPLE_RATE);
    }
}

auto StreamingTranscriber::should_transcribe() const -> bool {
    int step_samples = (config_.step_ms * core::audio_constants::SAMPLE_RATE) / static_cast<int>(core::audio_constants::MILLISECONDS_PER_SECOND);
    int keep_samples = static_cast<int>(keep_buffer_.size());
    int new_samples = static_cast<int>(audio_buffer_.size()) - keep_samples;
    return new_samples >= step_samples;
}

auto StreamingTranscriber::audio_length_ms() const -> int {
    return static_cast<int>((audio_buffer_.size() * core::audio_constants::MILLISECONDS_PER_SECOND) / core::audio_constants::SAMPLE_RATE);
}

auto StreamingTranscriber::process() -> StreamingTranscriber::Segment {
    if (!should_transcribe()) {
        return Segment{};
    }
    return transcribe_buffer();
}

auto StreamingTranscriber::get_partial() -> StreamingTranscriber::Segment {
    if (audio_buffer_.empty()) {
        return Segment{};
    }

    int min_samples = (config_.min_audio_ms * core::audio_constants::SAMPLE_RATE) / static_cast<int>(core::audio_constants::MILLISECONDS_PER_SECOND);
    if (std::cmp_less(audio_buffer_.size(), min_samples)) {
        return Segment{};
    }

    // Create combined buffer with keep context
    std::vector<float> combined;
    combined.reserve(keep_buffer_.size() + audio_buffer_.size());
    combined.insert(combined.end(), keep_buffer_.begin(), keep_buffer_.end());
    combined.insert(combined.end(), audio_buffer_.begin(), audio_buffer_.end());

    auto result = engine_.transcribe(combined);
    if (!result) {
        spdlog::error("StreamingTranscriber::get_partial(): {}", result.error());
        return Segment{};
    }

    Segment seg;
    seg.text = result->text;
    seg.start_ms = audio_offset_ms_;
    seg.end_ms = audio_offset_ms_ + audio_length_ms();
    seg.is_final = false;
    seg.confidence = result->avg_probability;

    return seg;
}

auto StreamingTranscriber::finalize() -> StreamingTranscriber::Segment {
    if (audio_buffer_.empty()) {
        spdlog::debug("StreamingTranscriber::finalize(): Audio buffer is empty");
        return Segment{};
    }

    int audio_len = audio_length_ms();
    spdlog::debug("StreamingTranscriber::finalize(): Buffer size {} samples ({}ms)", audio_buffer_.size(), audio_len);

    // 1. Prepare Audio
    std::vector<float> combined = prepare_combined_buffer();

    spdlog::debug("StreamingTranscriber::finalize(): Transcribing {} samples (keep: {})", combined.size(),
                  keep_buffer_.size());

    // 2. Transcribe
    auto result = engine_.transcribe(combined);
    if (!result) {
        spdlog::error("StreamingTranscriber::finalize(): {}", result.error());
        return Segment{};
    }

    // 3. Post-process Text
    std::string final_text = post_process_text(*result);

    // 4. Update Full Text
    append_to_full_text(final_text);

    // 5. Create Segment
    Segment seg;
    seg.text = final_text;
    seg.start_ms = audio_offset_ms_;
    seg.end_ms = audio_offset_ms_ + audio_len;
    seg.is_final = true;
    seg.confidence = result->avg_probability;

    // 6. Update State
    update_state_after_transcription(*result, audio_len);

    return seg;
}

auto StreamingTranscriber::prepare_combined_buffer() -> std::vector<float> {
    std::vector<float> combined;
    combined.reserve(keep_buffer_.size() + audio_buffer_.size());
    combined.insert(combined.end(), keep_buffer_.begin(), keep_buffer_.end());
    combined.insert(combined.end(), audio_buffer_.begin(), audio_buffer_.end());
    return combined;
}

void StreamingTranscriber::append_to_full_text(const std::string& final_text) {
    if (!final_text.empty()) {
        if (!full_text_.empty() && (std::isspace(full_text_.back()) == 0) && (std::isspace(final_text.front()) == 0)) {
            full_text_ += " ";
        }
        full_text_ += final_text;
    }
}

void StreamingTranscriber::update_state_after_transcription(const SttEngine::TranscriptionResult& result,
                                                            int audio_len_ms) {
    // Update state for next iteration
    previous_text_ = result.text;
    previous_tokens_ = tokenize(result.text);

    // Keep last portion for context
    int keep_samples = (config_.keep_ms * core::audio_constants::SAMPLE_RATE) / static_cast<int>(core::audio_constants::MILLISECONDS_PER_SECOND);
    if (std::cmp_greater(audio_buffer_.size(), keep_samples)) {
        keep_buffer_.assign(audio_buffer_.end() - keep_samples, audio_buffer_.end());
    } else {
        keep_buffer_ = audio_buffer_;
    }

    // Update offset and clear main buffer
    audio_offset_ms_ += audio_len_ms;
    audio_buffer_.clear();
}

auto StreamingTranscriber::post_process_text(const SttEngine::TranscriptionResult& result) -> std::string {
    std::string raw_text = result.text;
    spdlog::debug("StreamingTranscriber: Raw text: '{}'", raw_text);

    // Deduplicate if enabled
    std::string text = raw_text;
    if (config_.token_dedup && !previous_text_.empty()) {
        text = deduplicate_text(raw_text);
        if (text != raw_text) {
            spdlog::debug("StreamingTranscriber: After dedup: '{}'", text);
        }
    }

    // Remove internal repetitions (loops)
    std::string before_rep = text;
    text = remove_repetition(text);
    if (text != before_rep) {
        spdlog::debug("StreamingTranscriber: After rep removal: '{}'", text);
    }

    // Filter out hallucinations and short noise
    if (is_hallucination(text, result)) {
        spdlog::debug("StreamingTranscriber: Filtered hallucination: '{}' (min_p={:.2f}, no_speech={:.2f})", 
                      text, result.min_probability, result.max_no_speech_prob);
        return "";
    }

    return text;
}

auto StreamingTranscriber::transcribe_buffer() -> StreamingTranscriber::Segment {
    return finalize();
}

void StreamingTranscriber::reset() {
    audio_buffer_.clear();
    keep_buffer_.clear();
    previous_text_.clear();
    previous_tokens_.clear();
    full_text_.clear();
    audio_offset_ms_ = 0;
    last_transcription_ms_ = 0;
    spdlog::debug("StreamingTranscriber: Reset complete");
}

auto StreamingTranscriber::tokenize(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        // Normalize: lowercase and remove punctuation for comparison
        std::string normalized;
        for (char chr : word) {
            if (std::isalnum(static_cast<unsigned char>(chr)) != 0) {
                normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
            }
        }
        if (!normalized.empty()) {
            tokens.push_back(normalized);
        }
    }
    return tokens;
}

auto StreamingTranscriber::find_overlap(const std::vector<std::string>& prev, const std::vector<std::string>& curr)
    -> int {
    if (prev.empty() || curr.empty()) {
        return 0;
    }

    // Find longest suffix of prev that is prefix of curr
    size_t max_overlap = std::min(prev.size(), curr.size());

    for (size_t overlap = max_overlap; overlap >= 1; --overlap) {
        bool match = true;
        for (size_t i = 0; i < overlap; ++i) {
            if (prev[prev.size() - overlap + i] != curr[i]) {
                match = false;
            }
        }
        if (match) {
            return static_cast<int>(overlap);
        }
    }
    return 0;
}

auto StreamingTranscriber::deduplicate_text(const std::string& new_text) -> std::string {
    if (previous_tokens_.empty()) {
        return new_text;
    }

    std::vector<std::string> new_tokens = tokenize(new_text);
    int overlap = find_overlap(previous_tokens_, new_tokens);

    if (overlap == 0) {
        return new_text;
    }

    spdlog::debug("StreamingTranscriber: Found {} overlapping tokens", overlap);

    // Reconstruct from original words, skipping first 'overlap' words
    std::istringstream iss(new_text);
    std::string word;
    int word_idx = 0;
    std::string result;

    while (iss >> word) {
        if (word_idx >= overlap) {
            if (!result.empty()) {
                result += " ";
            }
            result += word;
        }
        word_idx++;
    }

    return result;
}

auto StreamingTranscriber::remove_repetition(const std::string& text) const -> std::string {
    if (text.empty()) {
        return text;
    }

    std::string current = text;
    bool changed = true;
    while (changed) {
        changed = false;
        size_t len = current.length();
        // Check for repeating suffixes of length min_repetition_len to n/2
        // We use config_.min_repetition_len to avoid merging short valid repetitions like "No, no."
        for (size_t idx = config_.min_repetition_len; idx <= len / 2; ++idx) {
            std::string sub = current.substr(len - idx, idx);
            std::string prev_sub = current.substr(len - (2 * idx), idx);

            if (sub == prev_sub) {
                // Found repetition "A A" at end, reduce to "A"
                current = current.substr(0, len - idx);
                // Check if we left a trailing space that should be trimmed if the original didn't have it?
                // "Hello World. Hello World." -> "Hello World." (Correct)
                // "Test Test" -> "Test" (Correct)
                changed = true;
                break;  // Restart scan on new string
            }
        }
    }
    return current;
}

auto StreamingTranscriber::is_hallucination(const std::string& text, const SttEngine::TranscriptionResult& result) -> bool {
    if (text.empty()) {
        return true;
    }

    // Length filter: Ignore short noise
    if (text.length() < static_cast<size_t>(config_.hallucination_min_len)) {
        return true;
    }

    // Normalize for case-insensitive check
    std::string lower = text;
    std::ranges::transform(lower, lower.begin(), [](unsigned char chr) { return std::tolower(chr); });

    // Trim punctuation
    while (!lower.empty() && (std::ispunct(lower.back()) != 0)) {
        lower.pop_back();
    }

    // 1. Blacklist check (Always Reject)
    bool blacklisted = std::ranges::any_of(config_.hallucination_blacklist, [&lower](const auto& phrase) {
        return lower == phrase;
    });
    if (blacklisted) return true;

    // 2. Smart Filter (Suspicious Phrases)
    bool is_suspicious = std::ranges::any_of(config_.suspicious_phrases, [&lower](const auto& phrase) {
        return lower == phrase;
    });

    if (is_suspicious) {
        // Only reject if model is unsure or thinks it's silence
        bool looks_like_silence = result.max_no_speech_prob > config_.suspicious_no_speech_threshold;
        bool low_confidence = result.min_probability < config_.suspicious_confidence_threshold;

        if (looks_like_silence || low_confidence) {
            spdlog::debug("Smart Filter: Rejected '{}' (no_speech={:.2f} > {:.2f} || min_prob={:.2f} < {:.2f})",
                          text, result.max_no_speech_prob, config_.suspicious_no_speech_threshold,
                          result.min_probability, config_.suspicious_confidence_threshold);
            return true;
        }
        spdlog::debug("Smart Filter: Allowed '{}' (High confidence)", text);
    }

    return false;
}

auto StreamingTranscriber::get_full_text() const -> const std::string& {
    return full_text_;
}

}  // namespace stt
