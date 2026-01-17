#pragma once

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

struct AppConfig {
    // Audio
    int device_index = -1; // -1 = default device

    // STT
    std::string model_path = "models/ggml-base.bin";
    std::string language = "en";
    int n_threads = 4;
    bool use_gpu = true;
    bool flash_attn = true;

    // VAD Core
    std::string vad_model_path = "models/silero_vad.onnx";
    float vad_threshold = 0.5f;          // Lowered from 0.6f
    float vad_energy_threshold = 0.001f; // Lowered from 0.01f

    // VAD Advanced
    float vad_smoothing_alpha = 0.3f;
    int vad_hangover_frames = 20;
    int vad_pre_roll_frames = 6;
    bool vad_adaptive_threshold = true;
    float vad_adaptive_min_threshold = 0.35f;
    float vad_adaptive_max_threshold = 0.6f;
    float vad_adaptive_alpha = 0.95f; // Noise floor update rate

    // Streaming Transcription
    int stt_step_ms = 2000;        // Transcribe every N ms
    int stt_keep_ms = 500;         // Context keep buffer
    int stt_max_length_ms = 10000; // Max audio window
    bool stt_token_dedup = true;   // Token overlap detection

    // STT Quality / Filter
    int stt_min_repetition_len = 10;
    int stt_hallucination_min_len = 2;
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
    bool use_ui = false; // Enable TUI mode

    // Helper to load all configuration sources
    static AppConfig parse(int argc, char* argv[]);
};
