#pragma once

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

struct AppConfig {
    // Defaults
    static constexpr int kDefaultDeviceIndex = -1;
    static constexpr int kDefaultThreadCount = 4;
    static constexpr float kDefaultVadThreshold = 0.5F;
    static constexpr float kDefaultVadEnergyThreshold = 0.001F;
    static constexpr float kDefaultVadSmoothing = 0.3F;
    static constexpr int kDefaultVadHangover = 20;
    static constexpr int kDefaultVadPreroll = 6;
    static constexpr float kDefaultVadAdaptiveMin = 0.35F;
    static constexpr float kDefaultVadAdaptiveMax = 0.6F;
    static constexpr float kDefaultVadAdaptiveAlpha = 0.95F;
    static constexpr int kDefaultSttStep = 2000;
    static constexpr int kDefaultSttKeep = 500;
    static constexpr int kDefaultSttMaxLen = 10000;
    static constexpr int kDefaultSttMinRepetition = 10;
    static constexpr int kDefaultSttHallucinationLen = 2;

    // Ranges for CLI validation
    static constexpr int kMinThreads = 1;
    static constexpr int kMaxThreads = 32;
    static constexpr float kMinVadProb = 0.0F;
    static constexpr float kMaxVadProb = 1.0F;
    static constexpr float kMinVadSmoothing = 0.05F;
    static constexpr float kMaxVadSmoothing = 0.9F;
    static constexpr int kMaxVadHangover = 30;
    static constexpr int kMaxVadPreroll = 20;
    static constexpr float kMinVadAdaptiveAlpha = 0.01F;
    static constexpr float kMaxVadAdaptiveAlpha = 0.99F;

    static constexpr int kMinSttStep = 500;
    static constexpr int kMaxSttStep = 10000;
    static constexpr int kMaxSttKeep = 2000;
    static constexpr int kMinSttMaxLength = 2000;
    static constexpr int kMaxSttMaxLength = 30000;

    static constexpr int kMinSttRepetition = 4;
    static constexpr int kMaxSttRepetition = 50;
    static constexpr int kMinSttHallucination = 1;
    static constexpr int kMaxSttHallucination = 10;

    // Audio
    int device_index = kDefaultDeviceIndex;  // -1 = default device

    // STT
    std::string stt_model_path = "models/ggml-base.bin";
    std::string language = "en";
    int n_threads = kDefaultThreadCount;
    bool use_gpu = true;
    bool flash_attn = true;

    // VAD Core
    std::string vad_model_path = "models/silero_vad.onnx";
    float vad_threshold = kDefaultVadThreshold;
    float vad_energy_threshold = kDefaultVadEnergyThreshold;

    // VAD Advanced
    float vad_smoothing_alpha = kDefaultVadSmoothing;
    int vad_hangover_frames = kDefaultVadHangover;
    int vad_pre_roll_frames = kDefaultVadPreroll;
    bool vad_adaptive_threshold = true;
    float vad_adaptive_min_threshold = kDefaultVadAdaptiveMin;
    float vad_adaptive_max_threshold = kDefaultVadAdaptiveMax;
    float vad_adaptive_alpha = kDefaultVadAdaptiveAlpha;  // Noise floor update rate

    // Streaming Transcription
    int stt_step_ms = kDefaultSttStep;          // Transcribe every N ms
    int stt_keep_ms = kDefaultSttKeep;          // Context keep buffer
    int stt_max_length_ms = kDefaultSttMaxLen;  // Max audio window
    bool stt_token_dedup = true;                // Token overlap detection

    // STT Quality / Filter
    int stt_min_repetition_len = kDefaultSttMinRepetition;
    int stt_hallucination_min_len = kDefaultSttHallucinationLen;
    std::vector<std::string> stt_blacklist = {"thank you", "thank you very much", "you", "bye",
                                              "the following is a transcription"};

    // Logging
    std::string log_level = "info";
    std::string log_file = "logs/realtime-subtitler.log";
    bool verbose = false;

    // Config file
    std::string config_path;

    // Actions
    bool list_devices_requested = false;
    bool json_output = false; // Enable JSON output mode
    bool use_ui = false;  // Enable TUI mode

    // Helper to load all configuration sources
    static auto parse(int argc, char** argv) -> AppConfig;
};
