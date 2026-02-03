#include "config/app_config.hpp"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "audio/audio_capture.hpp" // For loopback detection
#include "core/version.hpp"
#include "utils/config_file.hpp"

// Helper: Trim string
static auto trim_str(const std::string& str) -> std::string {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Helper: Load .env file into environment variables
static void load_dotenv() {
    std::ifstream file(".env");
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        size_t equal_pos = line.find('=');
        if (equal_pos == std::string::npos) {
            continue;
        }

        std::string key = trim_str(line.substr(0, equal_pos));
        std::string val = trim_str(line.substr(equal_pos + 1));

        if (val.size() >= 2) {
            if ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\'')) {
                val = val.substr(1, val.size() - 2);
            }
        }

#ifdef _WIN32
        char* buf = nullptr;
        size_t size = 0;
        if (_dupenv_s(&buf, &size, key.c_str()) == 0 && buf != nullptr) {
            std::unique_ptr<char, decltype(&std::free)> safe_buf(buf, &std::free);
            continue;
        }
        _putenv_s(key.c_str(), val.c_str());
#else
        setenv(key.c_str(), val.c_str(), 0);
#endif
    }
}

// Helper: Get environment variable with default
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto get_env(const std::string& name, const std::string& default_value = "") -> std::string {
#ifdef _WIN32
    char* buf = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buf, &size, name.c_str()) == 0 && buf != nullptr) {
        std::unique_ptr<char, decltype(&std::free)> safe_buf(buf, &std::free);
        return {safe_buf.get()};
    }
    return default_value;
#else
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : default_value;
#endif
}

static auto get_env_int(const std::string& name, int default_val) -> int {
    std::string val = get_env(name);
    if (val.empty()) {
        return default_val;
    }
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

static auto get_env_float(const std::string& name, float default_val) -> float {
    std::string val = get_env(name);
    if (val.empty()) {
        return default_val;
    }
    try {
        return std::stof(val);
    } catch (...) {
        return default_val;
    }
}

static auto get_env_bool(const std::string& name, bool default_val) -> bool {
    std::string val = get_env(name);
    if (val.empty()) {
        return default_val;
    }
    return (val == "1" || val == "true" || val == "yes" || val == "on");
}

static void set_log_level(const std::string& level) {
    auto log_enum = spdlog::level::info;
    if (level == "trace") {
        log_enum = spdlog::level::trace;
    } else if (level == "debug") {
        log_enum = spdlog::level::debug;
    } else if (level == "warn") {
        log_enum = spdlog::level::warn;
    } else if (level == "error") {
        log_enum = spdlog::level::err;
    }
    spdlog::set_level(log_enum);
}

// --- Configuration Profiles ---

static void apply_quiet_office_params(AppConfig& cfg) {
    // Explicitly set base defaults from centralized constants.
    // This function can be called at runtime to "Reset" to the Quiet profile.
    cfg.vad_threshold = core::vad_constants::DEFAULT_THRESHOLD;
    cfg.vad_smoothing_alpha = core::vad_constants::DEFAULT_SMOOTHING_ALPHA;
    cfg.vad_hangover_frames = core::vad_constants::DEFAULT_HANGOVER_FRAMES;
    cfg.stt_no_speech_threshold = core::stt_constants::DEFAULT_NO_SPEECH_THOLD;
    cfg.stt_entropy_threshold = core::stt_constants::DEFAULT_ENTROPY_THOLD;
    cfg.stt_beam_size = core::stt_constants::DEFAULT_BEAM_SIZE; 
    cfg.stt_no_fallback = core::stt_constants::DEFAULT_NO_FALLBACK;
}

static void apply_noisy_env_params(AppConfig& cfg) {
    cfg.vad_threshold = 0.6F;
    cfg.vad_smoothing_alpha = 0.5F;
    cfg.vad_hangover_frames = 25;
    cfg.stt_no_speech_threshold = 0.8F;
    cfg.stt_entropy_threshold = 2.0F;
}

static void apply_phone_call_params(AppConfig& cfg) {
    cfg.vad_threshold = 0.4F;
    cfg.vad_smoothing_alpha = 0.3F;
    cfg.vad_hangover_frames = 30;
    cfg.stt_no_speech_threshold = 0.4F;
    cfg.stt_entropy_threshold = 2.6F;
}

static void apply_podcast_params(AppConfig& cfg) {
    cfg.vad_threshold = 0.45F;
    cfg.vad_smoothing_alpha = 0.4F;
    cfg.stt_no_speech_threshold = 0.55F;
    cfg.stt_entropy_threshold = 2.3F;
}

static void apply_configuration_profile(AppConfig& cfg) {
    switch (cfg.input_env) {
        case AppConfig::InputEnvironment::QuietOffice:
            apply_quiet_office_params(cfg);
            break;
        case AppConfig::InputEnvironment::NoisyEnvironment:
            apply_noisy_env_params(cfg);
            break;
        case AppConfig::InputEnvironment::PhoneVideoCall:
            apply_phone_call_params(cfg);
            break;
        case AppConfig::InputEnvironment::PodcastStreaming:
            apply_podcast_params(cfg);
            break;
        case AppConfig::InputEnvironment::Custom:
            break; // No override, use base/cli
    }
}

auto AppConfig::parse(int argc, char** argv) -> AppConfig {
    load_dotenv();
    AppConfig config;

    // 1. Initial Defaults set in struct definition (Base: QuietOffice / Microphone)

    // 2. Load Config File (Lowest Priority Overrides)
    std::string default_config = config::ConfigFile::get_default_path();
    config::ConfigFile cfg_file;
    if (cfg_file.load(default_config)) {
        config.json_output = cfg_file.get_bool("json_output", config.json_output);
        config.device_index = cfg_file.get_int("device", config.device_index);
        
        // STT Core
        config.stt_model_path = cfg_file.get("model", config.stt_model_path);
        config.language = cfg_file.get("language", config.language);
        config.n_threads = cfg_file.get_int("n_threads", config.n_threads);
        config.use_gpu = cfg_file.get_bool("use_gpu", config.use_gpu);
        config.flash_attn = cfg_file.get_bool("flash_attn", config.flash_attn);

        // STT Quality (New Parity)
        config.stt_beam_size = cfg_file.get_int("beam_size", config.stt_beam_size);
        config.stt_temperature = cfg_file.get_float("temperature", config.stt_temperature);
        config.stt_temperature_inc = cfg_file.get_float("temperature_inc", config.stt_temperature_inc);
        config.stt_no_fallback = cfg_file.get_bool("no_fallback", config.stt_no_fallback);
        config.stt_no_context = cfg_file.get_bool("no_context", config.stt_no_context);
        config.stt_suppress_blank = cfg_file.get_bool("suppress_blank", config.stt_suppress_blank);
        config.stt_suppress_nst = cfg_file.get_bool("suppress_nst", config.stt_suppress_nst);
        config.transcriber_timestamp_merge = cfg_file.get_bool("stt_ts_merge", config.transcriber_timestamp_merge);

        // VAD
        config.vad_threshold = cfg_file.get_float("vad_threshold", config.vad_threshold);
        config.vad_energy_threshold = cfg_file.get_float("vad_energy_threshold", config.vad_energy_threshold);
        config.vad_smoothing_alpha = cfg_file.get_float("vad_smoothing", config.vad_smoothing_alpha);
        config.vad_hangover_frames = cfg_file.get_int("vad_hangover", config.vad_hangover_frames);
        config.vad_pre_roll_frames = cfg_file.get_int("vad_preroll", config.vad_pre_roll_frames);
        config.vad_adaptive_threshold = cfg_file.get_bool("vad_adaptive", config.vad_adaptive_threshold);
        
        // Smart Filter
        config.stt_suspicious_no_speech_threshold = cfg_file.get_float("smart_no_speech", config.stt_suspicious_no_speech_threshold);
        config.stt_suspicious_confidence_threshold = cfg_file.get_float("smart_confidence", config.stt_suspicious_confidence_threshold);

        // Lists (Comma-separated)
        std::string suspicious_str = cfg_file.get("smart_phrases", "");
        if (!suspicious_str.empty()) {
             // Basic CSV parser
             config.stt_suspicious_phrases.clear();
             std::stringstream ss(suspicious_str);
             std::string item;
             while (std::getline(ss, item, ',')) {
                 config.stt_suspicious_phrases.push_back(trim_str(item));
             }
        }

        std::string blacklist_str = cfg_file.get("blacklist", "");
        if (!blacklist_str.empty()) {
             config.stt_blacklist.clear();
             std::stringstream ss(blacklist_str);
             std::string item;
             while (std::getline(ss, item, ',')) {
                 config.stt_blacklist.push_back(trim_str(item));
             }
        }
        
        // Logging
        config.log_level = cfg_file.get("log_level", config.log_level);
        config.verbose = cfg_file.get_bool("verbose", config.verbose);
    }

    // 3. Environment Variables (Medium Priority)
    config.stt_model_path = get_env("SUBTITLER_STT_MODEL", config.stt_model_path);
    config.n_threads = get_env_int("SUBTITLER_THREADS", config.n_threads);
    config.stt_no_context = get_env_bool("SUBTITLER_STT_NO_CONTEXT", config.stt_no_context);
    config.stt_beam_size = get_env_int("SUBTITLER_STT_BEAM_SIZE", config.stt_beam_size);
    config.transcriber_timestamp_merge = get_env_bool("SUBTITLER_STT_TS_MERGE", config.transcriber_timestamp_merge);
    config.input_env = static_cast<InputEnvironment>(get_env_int("SUBTITLER_INPUT_ENV", static_cast<int>(config.input_env)));

    // Lists via Env (SUBTITLER_SMART_PHRASES="thanks,thank you")
    std::string env_smart = get_env("SUBTITLER_SMART_PHRASES");
    if (!env_smart.empty()) {
        config.stt_suspicious_phrases.clear();
        std::stringstream ss(env_smart);
        std::string item;
        while (std::getline(ss, item, ',')) {
             config.stt_suspicious_phrases.push_back(trim_str(item));
        }
    }

    std::string env_blacklist = get_env("SUBTITLER_BLACKLIST");
    if (!env_blacklist.empty()) {
        config.stt_blacklist.clear();
        std::stringstream ss(env_blacklist);
        std::string item;
        while (std::getline(ss, item, ',')) {
             config.stt_blacklist.push_back(trim_str(item));
        }
    }

    // 4. CLI Arguments (Highest Priority Determination)
    CLI::App app{"Signeo - AI Live Captions"};
    app.set_version_flag("--version", core::kVersionString);

    // Initial parsing to Determine Mode/Env before re-applying specifics
    std::string profile_str = "quiet"; // Default

    app.add_option("--profile,--env", profile_str, "Input profile: quiet, noisy, call, podcast, custom")
       ->check(CLI::IsMember({"quiet", "noisy", "call", "podcast", "custom"}));

    app.add_option("-d,--device", config.device_index, "Audio device index (auto-detect loopback)");
    app.add_flag("--list-devices", config.list_devices_requested, "List devices");
    app.add_flag("--json", config.json_output, "JSON Output");
    app.add_flag("--ui,!--no-ui", config.use_ui, "TUI Mode");

    // Standard params
    app.add_option("-m,--model", config.stt_model_path, "Whisper model path")->check(CLI::ExistingFile);
    app.add_option("-t,--threads", config.n_threads, "Inference threads");
    app.add_flag("--gpu,!--no-gpu", config.use_gpu, "GPU acceleration");
    app.add_flag("--flash-attn,!--no-flash-attn", config.flash_attn, "Flash Attention");

    // Advanced STT (Parity Check)
    app.add_option("--stt-temp", config.stt_temperature, "Temperature");
    app.add_option("--stt-temp-inc", config.stt_temperature_inc, "Temperature increment");
    app.add_flag("--stt-no-fallback,!--stt-fallback", config.stt_no_fallback, "Disable fallback");
    app.add_flag("--stt-no-context,!--stt-context", config.stt_no_context, "Independent segments");
    app.add_flag("--stt-suppress-blank,!--no-stt-suppress-blank", config.stt_suppress_blank, "Suppress blank");
    app.add_flag("--stt-suppress-nst,!--no-stt-suppress-nst", config.stt_suppress_nst, "Suppress NST"); 
    app.add_option("--stt-beam-size", config.stt_beam_size, "Beam size");
    app.add_flag("--stt-ts-merge,!--no-stt-ts-merge", config.transcriber_timestamp_merge, "Timestamp merge");
    
    // Streaming Params
    app.add_option("--stt-step", config.stt_step_ms, "Step size (ms)");
    app.add_option("--stt-keep", config.stt_keep_ms, "Context to keep (ms)");
    app.add_option("--stt-max-len", config.stt_max_length_ms, "Max window length (ms)");
    app.add_flag("--stt-dedup,!--no-stt-dedup", config.stt_token_dedup, "Token deduplication");

    // VAD Params
    app.add_option("--vad-threshold", config.vad_threshold, "VAD Threshold");
    app.add_option("--vad-smoothing", config.vad_smoothing_alpha, "VAD Smoothing");
    app.add_option("--vad-hangover", config.vad_hangover_frames, "VAD Hangover");
    
    // STT Quality Ext
    app.add_option("--stt-no-speech-th", config.stt_no_speech_threshold, "No speech threshold");
    app.add_option("--stt-entropy-th", config.stt_entropy_threshold, "Entropy threshold");

    // Logging
    app.add_option("--log-level", config.log_level, "Log level");
    app.add_flag("-v,--verbose", config.verbose, "Verbose logging");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::exit(app.exit(e));
    }

    if (config.verbose) {
        config.log_level = "debug";
    }
    set_log_level(config.log_level);

    // 5. Logic: Determine Input Environment from flags
    if (profile_str == "noisy") config.input_env = InputEnvironment::NoisyEnvironment;
    else if (profile_str == "call") config.input_env = InputEnvironment::PhoneVideoCall;
    else if (profile_str == "podcast") config.input_env = InputEnvironment::PodcastStreaming;
    else if (profile_str == "custom") config.input_env = InputEnvironment::Custom;
    else config.input_env = InputEnvironment::QuietOffice; // Default "quiet"

    // 6. Logic: Auto-Detect Input Mode from Device
    if (config.device_index >= 0) {
        // AudioCapture needs to be instantiated to check device list or use static check if available
        // Since we are static parse(), we rely on helper
        if (audio::AudioCapture::is_loopback_device(config.device_index)) {
            config.input_mode = InputMode::Loopback;
            spdlog::debug("Auto-detected InputMode: Loopback (Device {})", config.device_index);
        } else {
            config.input_mode = InputMode::Microphone;
            spdlog::debug("Auto-detected InputMode: Microphone (Device {})", config.device_index);
        }
    } else {
        config.input_mode = InputMode::Microphone; 
    }

    // 7. Save CLI-provided values (overrides)
    // We capture values *as they are after CLI parse* but *before* profile overwrites them.
    // If CLI flag was NOT provided, these holds ConfigFile/Env/Default values.
    // If CLI flag WAS provided, these holds user's explicit values.
    // The profile should only overwrite *defaults*, not explicit user overrides.
    // Strategy: Apply profile to a *copy* of defaults, then apply CLI?
    // Current strategy fix: Check app.count() and if true, KEEP the current value.
    // To do that, we save the current value locally.
    
    float cli_vad_threshold = config.vad_threshold;
    float cli_vad_smoothing = config.vad_smoothing_alpha;
    int cli_vad_hangover = config.vad_hangover_frames;
    float cli_no_speech = config.stt_no_speech_threshold;
    float cli_entropy = config.stt_entropy_threshold;
    int cli_beam = config.stt_beam_size;
    bool cli_fallback = config.stt_no_fallback;

    // 8. Apply Profile (Sets defaults for the Env)
    // Only apply if the profile is NOT the default (QuietOffice), OR if it was explicitly requested via CLI/ENV.
    // This allows INI/ENV overrides to be respected for the default startup case, 
    // while enforcing the profile values if the user explicitly asks for it (e.g. --profile quiet).
    bool explicit_profile = (app.count("--profile") > 0) || (app.count("--env") > 0) || (!get_env("SUBTITLER_INPUT_ENV").empty());
    
    if (config.input_env != InputEnvironment::QuietOffice || explicit_profile) {
        apply_configuration_profile(config);
    }

    // 9. Restore CLI Overrides if they were explicitly set
    if (app.count("--vad-threshold")) config.vad_threshold = cli_vad_threshold;
    if (app.count("--vad-smoothing")) config.vad_smoothing_alpha = cli_vad_smoothing;
    if (app.count("--vad-hangover")) config.vad_hangover_frames = cli_vad_hangover;
    
    if (app.count("--stt-no-speech-th")) config.stt_no_speech_threshold = cli_no_speech;
    if (app.count("--stt-entropy-th")) config.stt_entropy_threshold = cli_entropy;
    if (app.count("--stt-beam-size")) config.stt_beam_size = cli_beam;
    // For flags/bools, count works the same
    if (app.count("--stt-no-fallback") || app.count("--stt-fallback")) config.stt_no_fallback = cli_fallback;

    return config;
}
