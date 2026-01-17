#include "stt/stt_engine.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <format>

#include "output/logging.hpp"
#include "whisper.h"

namespace stt {

// Factory Method
core::Result<std::unique_ptr<SttEngine>> SttEngine::create(const SttConfig& config) {
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
SttEngine::SttEngine(const SttConfig& config) : config_(config) {
    // Initialization moved to init_whisper()
}

core::Status SttEngine::init_whisper() {
    LOG_SCOPED_TRACE();
    spdlog::debug("init_whisper() loading model from {}", config_.model_path);

    whisper_context_params cparams = whisper_context_default_params();
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
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
    spdlog::debug("SttEngine: Cleanup complete.");
}

SttEngine::SttEngine(SttEngine&& other) noexcept : config_(std::move(other.config_)), ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

SttEngine& SttEngine::operator=(SttEngine&& other) noexcept {
    if (this != &other) {
        if (ctx_) {
            whisper_free(ctx_);
        }
        config_ = std::move(other.config_);
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

core::Result<SttEngine::TranscriptionResult> SttEngine::transcribe(const std::vector<float>& audio) {
    if (!ctx_) {
        return core::log_error("SttEngine: Context not initialized");
    }

    if (audio.empty()) {
        return core::log_error("SttEngine: Empty audio buffer");
    }

    const auto t_start = std::chrono::high_resolution_clock::now();

    // Configure inference parameters
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    wparams.print_progress = config_.print_progress;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = config_.print_timestamps;
    wparams.translate = false;
    wparams.single_segment = config_.single_segment;
    wparams.no_context = config_.no_context;
    wparams.language = config_.language.c_str();
    wparams.n_threads = config_.n_threads;

    // Real-time Optimization: Use Greedy (faster) instead of Beam Search
    wparams.strategy = WHISPER_SAMPLING_GREEDY;
    wparams.greedy.best_of = 1;

    // Initial Prompt to stabilize output
    wparams.initial_prompt = "The following is a transcription.";

    // Anti-hallucination settings
    wparams.suppress_blank = true;
    wparams.suppress_nst = true;
    wparams.no_speech_thold = 0.6f;
    wparams.entropy_thold = 2.4f;
    wparams.logprob_thold = -1.0f;

    // Run inference
    if (whisper_full(ctx_, wparams, audio.data(), static_cast<int>(audio.size())) != 0) {
        return core::log_error("SttEngine: Inference failed");
    }

    // Extract transcription
    const int n_segments = whisper_full_n_segments(ctx_);
    std::string full_text;
    float prob_sum = 0.0f;
    int token_count = 0;

    for (int i = 0; i < n_segments; ++i) {
        const char* segment_text = whisper_full_get_segment_text(ctx_, i);
        if (segment_text) {
            full_text += segment_text;
        }

        // Calculate average probability
        const int n_tokens = whisper_full_n_tokens(ctx_, i);
        for (int j = 0; j < n_tokens; ++j) {
            prob_sum += whisper_full_get_token_p(ctx_, i, j);
            token_count++;
        }
    }

    const auto t_end = std::chrono::high_resolution_clock::now();

    TranscriptionResult result;
    result.text = full_text;
    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    result.avg_probability = token_count > 0 ? prob_sum / token_count : 0.0f;

    // Log audio duration vs processing time
    float audio_duration_s = static_cast<float>(audio.size()) / 16000.0f;
    float rtf = static_cast<float>(result.duration_ms) / 1000.0f / audio_duration_s;

    spdlog::debug("SttEngine: Transcribed {:.2f}s audio in {}ms (RTF: {:.2f})", audio_duration_s, result.duration_ms,
                  rtf);

    return result;
}

bool SttEngine::is_ready() const { return ctx_ != nullptr; }

const std::string& SttEngine::language() const { return config_.language; }

} // namespace stt
