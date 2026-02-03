#include "stt/stt_engine.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <format>
#include <utility>

#include "output/log_output.hpp"
#include "whisper.h"

#include "core/constants.hpp"

namespace stt {

// Local constants removed (See core::audio_constants)

// Factory Method
auto SttEngine::create(const SttConfig& config) -> core::Result<std::unique_ptr<SttEngine>> {
    LOG_SCOPED_TRACE();
    spdlog::debug("SttEngine::create() model_path={}", config.model_path);

    std::unique_ptr<SttEngine> engine(new SttEngine(config));

    auto init_result = engine->init_whisper();
    if (!init_result) {
        return std::unexpected(init_result.error());
    }

    spdlog::info("SttEngine created. Language: '{}', Threads: {}, GPU: {}", config.language, config.n_threads,
                 config.use_gpu ? "enabled" : "disabled");

    return engine;
}

// Private Constructor
SttEngine::SttEngine(SttConfig config) : config_(std::move(config)) {
    // Initialization moved to init_whisper()
}

auto SttEngine::init_whisper() -> core::Status {
    LOG_SCOPED_TRACE();
    spdlog::debug("init_whisper() loading model from {}", config_.model_path);

    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = config_.use_gpu;
    cparams.flash_attn = config_.flash_attn;

    ctx_ = whisper_init_from_file_with_params(config_.model_path.c_str(), cparams);

    if (ctx_ == nullptr) {
        return core::log_error(std::format("Failed to load Whisper model from '{}'", config_.model_path));
    }

    spdlog::debug("SttEngine::init_whisper() model loaded successfully");
    return {};
}

SttEngine::~SttEngine() {
    spdlog::debug("SttEngine: Destructor called, freeing Whisper context...");
    if (ctx_ != nullptr) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
    spdlog::debug("SttEngine: Cleanup complete.");
}

SttEngine::SttEngine(SttEngine&& other) noexcept : config_(std::move(other.config_)), ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

auto SttEngine::operator=(SttEngine&& other) noexcept -> SttEngine& {
    if (this != &other) {
        if (ctx_ != nullptr) {
            whisper_free(ctx_);
        }
        config_ = std::move(other.config_);
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

auto SttEngine::transcribe(const std::vector<float>& audio) -> core::Result<SttEngine::TranscriptionResult> {
    if (ctx_ == nullptr) {
        return core::log_error("SttEngine: Context not initialized");
    }

    if (audio.empty()) {
        return core::log_error("SttEngine: Empty audio buffer");
    }

    const auto t_start = std::chrono::high_resolution_clock::now();

    // Configure inference parameters based on beam_size
    struct whisper_full_params wparams;
    if (config_.beam_size > 1) {
        wparams = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        wparams.beam_search.beam_size = config_.beam_size;
        spdlog::debug("SttEngine: Using beam search with beam_size={}", config_.beam_size);
    } else {
        wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.greedy.best_of = 1;
    }

    // Basic settings
    wparams.print_progress = config_.print_progress;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = config_.print_timestamps;
    wparams.translate = false;
    wparams.single_segment = config_.single_segment;
    wparams.no_context = config_.no_context;
    wparams.language = config_.language.c_str();
    wparams.n_threads = config_.n_threads;

    // Configurable quality parameters
    wparams.max_tokens = config_.max_tokens;
    wparams.audio_ctx = config_.audio_ctx;
    wparams.no_speech_thold = config_.no_speech_threshold;
    wparams.entropy_thold = config_.entropy_threshold;
    wparams.logprob_thold = config_.logprob_threshold;

    // Temperature fallback
    wparams.temperature = config_.temperature;
    wparams.temperature_inc = config_.no_fallback ? 0.0F : config_.temperature_inc;

    // Anti-hallucination settings
    wparams.suppress_blank = config_.suppress_blank;
    wparams.suppress_nst = config_.suppress_nst;

    // Initial prompt (text-based)
    if (!config_.initial_prompt.empty()) {
        wparams.initial_prompt = config_.initial_prompt.c_str();
    }

    // Prompt tokens from previous transcription for context continuity
    // This is crucial for maintaining coherence across segments
    if (!config_.no_context && !prompt_tokens_.empty()) {
        wparams.prompt_tokens = prompt_tokens_.data();
        wparams.prompt_n_tokens = static_cast<int>(prompt_tokens_.size());
        spdlog::debug("SttEngine: Using {} prompt tokens from previous transcription", prompt_tokens_.size());
    } else {
        wparams.prompt_tokens = nullptr;
        wparams.prompt_n_tokens = 0;
        spdlog::debug("SttEngine: No prompt tokens used (no_context={} or empty)", config_.no_context);
    }

    spdlog::debug("SttEngine: Running inference on {} samples...", audio.size());

    // Run inference
    int ret = whisper_full(ctx_, wparams, audio.data(), static_cast<int>(audio.size()));
    if (ret != 0) {
        return core::log_error(std::format("SttEngine: Inference failed with code {}", ret));
    }

    // Extract transcription and calculate probability
    const int n_segments = whisper_full_n_segments(ctx_);
    std::string full_text;
    float prob_sum = 0.0F;
    int token_count = 0;

    // Also extract tokens for next transcription (prompt token reuse)
    prompt_tokens_.clear();

    spdlog::debug("SttEngine: Found {} segments", n_segments);

    for (int i = 0; i < n_segments; ++i) {
        const char* segment_text = whisper_full_get_segment_text(ctx_, i);
        if (segment_text != nullptr) {
            std::string text_seg = segment_text;
            spdlog::debug("SttEngine: Segment {}: '{}'", i, text_seg);
            full_text += text_seg;
        }

        // Extract tokens and calculate probability
        const int n_tokens = whisper_full_n_tokens(ctx_, i);
        for (int j = 0; j < n_tokens; ++j) {
            prob_sum += whisper_full_get_token_p(ctx_, i, j);
            token_count++;

            // Store token for next transcription
            if (!config_.no_context) {
                prompt_tokens_.push_back(whisper_full_get_token_id(ctx_, i, j));
            }
        }
    }

    const auto t_end = std::chrono::high_resolution_clock::now();

    TranscriptionResult result;
    result.text = full_text;
    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    result.avg_probability = token_count > 0 ? prob_sum / static_cast<float>(token_count) : 0.0F;

    // Calculate min/max metrics
    float min_prob = 1.0F;
    float max_ns = 0.0F;

    for (int i = 0; i < n_segments; ++i) {
        float ns_prob = whisper_full_get_segment_no_speech_prob(ctx_, i);
        if (ns_prob > max_ns) {
            max_ns = ns_prob;
        }

        int n_tok = whisper_full_n_tokens(ctx_, i);
        for (int j = 0; j < n_tok; ++j) {
            float p = whisper_full_get_token_p(ctx_, i, j);
            if (p < min_prob) {
                min_prob = p;
            }
        }
    }
    // If no tokens, min_prob is 0 (or technically undefined, but 0 is safe for "low confidence")
    result.min_probability = (token_count > 0) ? min_prob : 0.0F;
    result.max_no_speech_prob = max_ns;

    // Log audio duration vs processing time
    float audio_duration_s = static_cast<float>(audio.size()) / static_cast<float>(core::audio_constants::SAMPLE_RATE);
    float rtf = static_cast<float>(result.duration_ms) / core::audio_constants::MILLISECONDS_PER_SECOND / audio_duration_s;

    spdlog::debug("SttEngine: Transcribed {:.2f}s audio in {}ms (RTF: {:.2f}, tokens: {}, no_speech: {:.2f})", 
                  audio_duration_s, result.duration_ms, rtf, prompt_tokens_.size(), max_ns);

    return result;
}

auto SttEngine::is_ready() const -> bool {
    return ctx_ != nullptr;
}

auto SttEngine::language() const -> const std::string& {
    return config_.language;
}

}  // namespace stt
