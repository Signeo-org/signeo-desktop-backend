#pragma once

#include "core/constants.hpp"
#include <thread>
#include <string>
#include <vector>

struct AppConfig {
    // Enums for Configuration Profiles
    enum class InputMode {
        Microphone,
        Loopback
    };

    enum class InputEnvironment {
        QuietOffice,
        NoisyEnvironment,
        PhoneVideoCall,
        PodcastStreaming,
        Custom
    };

    // Range Constants (for CLI validation)
    static constexpr int kMinThreads = core::app_constants::MIN_THREADS;
    static constexpr int kMaxThreads = core::app_constants::MAX_THREADS;
    static constexpr float kMinVadProb = core::app_constants::MIN_VAD_PROB;
    static constexpr float kMaxVadProb = core::app_constants::MAX_VAD_PROB;
    static constexpr float kMinVadSmoothing = core::app_constants::MIN_VAD_SMOOTHING;
    static constexpr float kMaxVadSmoothing = core::app_constants::MAX_VAD_SMOOTHING;
    static constexpr int kMaxVadHangover = core::app_constants::MAX_VAD_HANGOVER;
    static constexpr int kMaxVadPreroll = core::app_constants::MAX_VAD_PREROLL;
    static constexpr float kMinVadAdaptiveAlpha = core::app_constants::MIN_VAD_ADAPTIVE_ALPHA;
    static constexpr float kMaxVadAdaptiveAlpha = core::app_constants::MAX_VAD_ADAPTIVE_ALPHA;

    static constexpr int kMinSttStep = core::app_constants::MIN_STT_STEP;
    static constexpr int kMaxSttStep = core::app_constants::MAX_STT_STEP;
    static constexpr int kMaxSttKeep = core::app_constants::MAX_STT_KEEP;
    static constexpr int kMinSttMaxLength = core::app_constants::MIN_STT_MAXLEN;
    static constexpr int kMaxSttMaxLength = core::app_constants::MAX_STT_MAXLEN;

    static constexpr int kMinSttRepetition = core::app_constants::MIN_STT_REP;
    static constexpr int kMaxSttRepetition = core::app_constants::MAX_STT_REP;
    static constexpr int kMinSttHallucination = core::app_constants::MIN_STT_HAL;
    static constexpr int kMaxSttHallucination = core::app_constants::MAX_STT_HAL;

    // Configuration Fields

    // Mode Selection
    InputMode input_mode = InputMode::Microphone;
    InputEnvironment input_env = InputEnvironment::QuietOffice;

    // Audio
    int device_index = -1;  // -1 = default device (or auto-detected loopback default)

    // STT Core
    std::string stt_model_path = core::stt_constants::DEFAULT_MODEL_PATH;
    std::string language = core::stt_constants::DEFAULT_LANGUAGE;
    int n_threads = std::thread::hardware_concurrency() > 0 
                    ? static_cast<int>(std::thread::hardware_concurrency()) - 1 
                    : core::stt_constants::DEFAULT_THREADS_FALLBACK;
    bool use_gpu = true;
    bool flash_attn = true;

    // VAD Core
    std::string vad_model_path = "models/silero_vad.onnx";
    float vad_threshold = core::vad_constants::DEFAULT_THRESHOLD;
    float vad_energy_threshold = core::vad_constants::DEFAULT_ENERGY_THRESHOLD;

    // VAD Advanced
    float vad_smoothing_alpha = core::vad_constants::DEFAULT_SMOOTHING_ALPHA;
    int vad_hangover_frames = core::vad_constants::DEFAULT_HANGOVER_FRAMES;
    int vad_pre_roll_frames = core::vad_constants::DEFAULT_PRE_ROLL_FRAMES;
    bool vad_adaptive_threshold = core::vad_constants::DEFAULT_ADAPTIVE;
    float vad_adaptive_min_threshold = core::vad_constants::DEFAULT_ADAPTIVE_MIN;
    float vad_adaptive_max_threshold = core::vad_constants::DEFAULT_ADAPTIVE_MAX;
    float vad_adaptive_alpha = core::vad_constants::DEFAULT_ADAPTIVE_ALPHA;

    // Streaming Transcription
    int stt_step_ms = core::stt_constants::DEFAULT_STEP_MS;
    int stt_keep_ms = core::stt_constants::DEFAULT_KEEP_MS;
    int stt_max_length_ms = core::stt_constants::MAX_WINDOW_MS;
    bool stt_token_dedup = core::stt_constants::DEFAULT_DEDUP;
    bool transcriber_timestamp_merge = core::stt_constants::DEFAULT_TIMESTAMP_MERGE;

    // STT Quality / Filter
    int stt_min_repetition_len = core::stt_constants::DEFAULT_MIN_REPETITION;
    int stt_hallucination_min_len = core::stt_constants::DEFAULT_HALLUCINATION_LEN;

    std::vector<std::string> stt_blacklist = core::stt_constants::DEFAULT_BLACKLIST;

    // Smart Hallucination Filter
    std::vector<std::string> stt_suspicious_phrases = core::stt_constants::DEFAULT_SUSPICIOUS_PHRASES;
    float stt_suspicious_no_speech_threshold = core::stt_constants::DEFAULT_SUSPICIOUS_NO_SPEECH_THRESHOLD; 
    float stt_suspicious_confidence_threshold = core::stt_constants::DEFAULT_SUSPICIOUS_CONFIDENCE_THRESHOLD;

    // STT Inference Quality
    int stt_beam_size = core::stt_constants::DEFAULT_BEAM_SIZE;
    int stt_max_tokens = core::stt_constants::DEFAULT_MAX_TOKENS;
    int stt_audio_ctx = core::stt_constants::DEFAULT_AUDIO_CTX;
    float stt_no_speech_threshold = core::stt_constants::DEFAULT_NO_SPEECH_THOLD;
    float stt_entropy_threshold = core::stt_constants::DEFAULT_ENTROPY_THOLD;
    float stt_logprob_threshold = core::stt_constants::DEFAULT_LOGPROB_THOLD;

    float stt_temperature = core::stt_constants::DEFAULT_TEMPERATURE;
    float stt_temperature_inc = core::stt_constants::DEFAULT_TEMPERATURE_INC;
    bool stt_no_fallback = core::stt_constants::DEFAULT_NO_FALLBACK;
    
    // Live Optimization
    bool stt_no_context = core::stt_constants::DEFAULT_NO_CONTEXT;
    bool stt_suppress_blank = core::stt_constants::DEFAULT_SUPPRESS_BLANK;
    bool stt_suppress_nst = core::stt_constants::DEFAULT_SUPPRESS_NST;

    // Logging
    std::string log_level = "info";
    std::string log_file = "logs/signeo-core.log";
    bool verbose = false;

    // Config file
    std::string config_path;

    // Actions
    bool list_devices_requested = false;
    bool json_output = true;  // Default true for Electron frontend 
    bool use_ui = false;

    // Helper to load all configuration sources
    static auto parse(int argc, char** argv) -> AppConfig;
};
