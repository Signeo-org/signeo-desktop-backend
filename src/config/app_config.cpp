#include "config/app_config.hpp"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>

#include "core/version.hpp"
#include "utils/config_file.hpp"

// Helper: Get environment variable with default
static std::string getEnv(const std::string& name, const std::string& default_value = "") {
#ifdef _WIN32
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name.c_str()) == 0 && buf != nullptr) {
        std::string value(buf);
        free(buf);
        return value;
    }
    return default_value;
#else
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : default_value;
#endif
}

static int getEnvInt(const std::string& name, int default_val) {
    std::string val = getEnv(name);
    if (val.empty()) {
        return default_val;
    }
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

static float getEnvFloat(const std::string& name, float default_val) {
    std::string val = getEnv(name);
    if (val.empty()) {
        return default_val;
    }
    try {
        return std::stof(val);
    } catch (...) {
        return default_val;
    }
}

static bool getEnvBool(const std::string& name, bool default_val) {
    std::string val = getEnv(name);
    if (val.empty()) {
        return default_val;
    }
    return (val == "1" || val == "true" || val == "yes" || val == "on");
}

// Helper: Set log level from string
static void setLogLevel(const std::string& level) {
    if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}

AppConfig AppConfig::parse(int argc, char* argv[]) {
    AppConfig config;

    // 1. Load config file (lowest priority)
    std::string default_config = config::ConfigFile::get_default_path();
    config::ConfigFile cfg_file;

    if (cfg_file.load(default_config)) {
        config.device_index = cfg_file.get_int("device", config.device_index);
        config.model_path = cfg_file.get("model", config.model_path);
        config.language = cfg_file.get("language", config.language);
        config.n_threads = cfg_file.get_int("n_threads", config.n_threads);
        config.use_gpu = cfg_file.get_bool("use_gpu", config.use_gpu);
        config.flash_attn = cfg_file.get_bool("flash_attn", config.flash_attn);
        config.vad_model_path = cfg_file.get("vad_model", config.vad_model_path);
        config.vad_threshold = cfg_file.get_float("vad_threshold", config.vad_threshold);
        config.vad_energy_threshold = cfg_file.get_float("vad_energy_threshold", config.vad_energy_threshold);
        config.vad_smoothing_alpha = cfg_file.get_float("vad_smoothing", config.vad_smoothing_alpha);
        config.vad_hangover_frames = cfg_file.get_int("vad_hangover", config.vad_hangover_frames);
        config.vad_pre_roll_frames = cfg_file.get_int("vad_preroll", config.vad_pre_roll_frames);
        config.vad_adaptive_threshold = cfg_file.get_bool("vad_adaptive", config.vad_adaptive_threshold);
        config.vad_adaptive_min_threshold = cfg_file.get_float("vad_adaptive_min", config.vad_adaptive_min_threshold);
        config.vad_adaptive_max_threshold = cfg_file.get_float("vad_adaptive_max", config.vad_adaptive_max_threshold);
        config.vad_adaptive_alpha = cfg_file.get_float("vad_adaptive_alpha", config.vad_adaptive_alpha);

        config.stt_step_ms = cfg_file.get_int("stt_step", config.stt_step_ms);
        config.stt_keep_ms = cfg_file.get_int("stt_keep", config.stt_keep_ms);
        config.stt_max_length_ms = cfg_file.get_int("stt_max_length", config.stt_max_length_ms);
        config.stt_token_dedup = cfg_file.get_bool("stt_dedup", config.stt_token_dedup);

        config.stt_min_repetition_len = cfg_file.get_int("stt_min_repetition", config.stt_min_repetition_len);
        config.stt_hallucination_min_len = cfg_file.get_int("stt_hallucination_len", config.stt_hallucination_min_len);

        // Parse Blacklist
        std::string blacklist_str = cfg_file.get("stt_blacklist", "");
        if (!blacklist_str.empty()) {
            config.stt_blacklist.clear();
            std::stringstream ss(blacklist_str);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Trim logic inside loop
                size_t first = item.find_first_not_of(" \t");
                if (std::string::npos != first) {
                    size_t last = item.find_last_not_of(" \t");
                    config.stt_blacklist.push_back(item.substr(first, (last - first + 1)));
                }
            }
        }

        config.log_level = cfg_file.get("log_level", config.log_level);
        config.log_file = cfg_file.get("log_file", config.log_file);
        spdlog::info("Loaded configuration from: {}", default_config);
    } else {
        // Default config missing, proceed with defaults
        if (!default_config.empty()) {
            spdlog::debug("Config file not found: {}, using defaults", default_config);
        }
    }

    // 2. Load environment variables (medium priority)
    // Global
    config.model_path = getEnv("SUBTITLER_MODEL_PATH", config.model_path);
    config.device_index = getEnvInt("SUBTITLER_DEVICE", config.device_index);
    config.language = getEnv("SUBTITLER_LANGUAGE", config.language);
    config.n_threads = getEnvInt("SUBTITLER_THREADS", config.n_threads);
    config.use_gpu = getEnvBool("SUBTITLER_USE_GPU", config.use_gpu);
    config.flash_attn = getEnvBool("SUBTITLER_FLASH_ATTN", config.flash_attn);
    config.use_ui = getEnvBool("SUBTITLER_UI", config.use_ui);

    // VAD
    config.vad_model_path = getEnv("SUBTITLER_VAD_MODEL", config.vad_model_path);
    config.vad_threshold = getEnvFloat("SUBTITLER_VAD_THRESHOLD", config.vad_threshold);
    config.vad_energy_threshold = getEnvFloat("SUBTITLER_VAD_ENERGY_THRESHOLD", config.vad_energy_threshold);
    config.vad_smoothing_alpha = getEnvFloat("SUBTITLER_VAD_SMOOTHING", config.vad_smoothing_alpha);
    config.vad_hangover_frames = getEnvInt("SUBTITLER_VAD_HANGOVER", config.vad_hangover_frames);
    config.vad_pre_roll_frames = getEnvInt("SUBTITLER_VAD_PREROLL", config.vad_pre_roll_frames);
    config.vad_adaptive_threshold = getEnvBool("SUBTITLER_VAD_ADAPTIVE", config.vad_adaptive_threshold);
    config.vad_adaptive_min_threshold = getEnvFloat("SUBTITLER_VAD_ADAPTIVE_MIN", config.vad_adaptive_min_threshold);
    config.vad_adaptive_max_threshold = getEnvFloat("SUBTITLER_VAD_ADAPTIVE_MAX", config.vad_adaptive_max_threshold);
    config.vad_adaptive_alpha = getEnvFloat("SUBTITLER_VAD_ADAPTIVE_ALPHA", config.vad_adaptive_alpha);

    // STT
    config.stt_step_ms = getEnvInt("SUBTITLER_STT_STEP", config.stt_step_ms);
    config.stt_keep_ms = getEnvInt("SUBTITLER_STT_KEEP", config.stt_keep_ms);
    config.stt_max_length_ms = getEnvInt("SUBTITLER_STT_MAX_LENGTH", config.stt_max_length_ms);
    config.stt_token_dedup = getEnvBool("SUBTITLER_STT_DEDUP", config.stt_token_dedup);

    config.stt_min_repetition_len = getEnvInt("SUBTITLER_STT_MIN_REPETITION", config.stt_min_repetition_len);
    config.stt_hallucination_min_len = getEnvInt("SUBTITLER_STT_HALLUCINATION_LEN", config.stt_hallucination_min_len);
    // Note: STT Blacklist via ENV is not supported (list parsing complexity) or could be added if needed via string
    // split helper

    // Logging
    config.log_level = getEnv("SUBTITLER_LOG_LEVEL", config.log_level);
    config.log_file = getEnv("SUBTITLER_LOG_FILE", config.log_file);
    config.verbose = getEnvBool("SUBTITLER_VERBOSE", config.verbose);

    // 3. CLI Args (highest priority)
    CLI::App app{"Real-Time Audio-to-Subtitles - Local speech-to-text with live display"};
    app.set_version_flag("--version", core::VERSION_STRING);

    app.add_option("-d,--device", config.device_index, "Audio device index (-1 for default)");

    app.add_flag("--list-devices", config.list_devices_requested, "List available audio devices and exit");
    app.add_flag("--ui,!--no-ui", config.use_ui, "Enable TUI mode (Terminal User Interface)");

    app.add_option("-m,--model", config.model_path, "Path to Whisper model file")->check(CLI::ExistingFile);
    app.add_option("-l,--language", config.language, "Input language code");
    app.add_option("-t,--threads", config.n_threads, "Number of threads")->check(CLI::Range(1, 32));
    app.add_flag("--gpu,!--no-gpu", config.use_gpu, "Enable/disable GPU acceleration");

    app.add_option("--vad-model", config.vad_model_path, "Path to Silero VAD model")->check(CLI::ExistingFile);
    app.add_option("--vad-threshold", config.vad_threshold, "VAD speech threshold")->check(CLI::Range(0.0F, 1.0F));
    app.add_option("--vad-energy-th", config.vad_energy_threshold, "VAD energy threshold (RMS)");
    app.add_option("--vad-smoothing", config.vad_smoothing_alpha, "VAD EMA smoothing (0.1-0.5)")
        ->check(CLI::Range(0.05F, 0.9F));
    app.add_option("--vad-hangover", config.vad_hangover_frames, "VAD hangover frames")->check(CLI::Range(0, 30));
    app.add_option("--vad-preroll", config.vad_pre_roll_frames, "VAD pre-roll frames")->check(CLI::Range(0, 20));
    app.add_flag("--vad-adaptive,!--no-vad-adaptive", config.vad_adaptive_threshold,
                 "Enable/disable adaptive VAD threshold");
    app.add_option("--vad-adaptive-min", config.vad_adaptive_min_threshold, "VAD adaptive min threshold")
        ->check(CLI::Range(0.0F, 1.0F));
    app.add_option("--vad-adaptive-max", config.vad_adaptive_max_threshold, "VAD adaptive max threshold")
        ->check(CLI::Range(0.0F, 1.0F));
    app.add_option("--vad-adaptive-alpha", config.vad_adaptive_alpha, "VAD audio floor smoothing alpha")
        ->check(CLI::Range(0.01F, 0.99F));

    // Streaming transcription
    app.add_option("--stt-step", config.stt_step_ms, "STT step size in ms")->check(CLI::Range(500, 10000));
    app.add_option("--stt-keep", config.stt_keep_ms, "STT context keep buffer in ms")->check(CLI::Range(0, 2000));
    app.add_option("--stt-max-length", config.stt_max_length_ms, "STT max audio window in ms")
        ->check(CLI::Range(2000, 30000));
    app.add_flag("--stt-dedup,!--no-stt-dedup", config.stt_token_dedup, "Enable/disable token deduplication");

    app.add_option("--stt-min-repetition", config.stt_min_repetition_len, "Min length to detect repetition")
        ->check(CLI::Range(4, 50));
    app.add_option("--stt-hallucination-len", config.stt_hallucination_min_len, "Min length for hallucination filter")
        ->check(CLI::Range(1, 10));
    app.add_option("--stt-blacklist", config.stt_blacklist, "Comma-separated list of hallucination phrases");

    app.add_option("--log-level", config.log_level, "Log level");
    app.add_option("--log-file", config.log_file, "Log file path (empty to disable)");
    app.add_flag("-v,--verbose", config.verbose, "Enable verbose output");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::cerr << "[DEBUG] CLI Parse Error: " << e.what() << '\n';
        std::exit(app.exit(e));
    }

    if (config.verbose) {
        config.log_level = "debug";
    }
    setLogLevel(config.log_level);

    return config;
};
