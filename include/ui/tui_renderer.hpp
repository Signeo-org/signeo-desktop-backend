#pragma once

/**
 * @file tui_renderer.h
 * @brief Terminal User Interface Renderer using FTXUI
 *
 * Manages the main TUI loop, rendering logic for all tabs (Dashboard,
 * Settings, Logs), and handling user input events.
 */

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "core/constants.hpp"

namespace ui {

// Constants for UI defaults - using centralized core constants
namespace detail {
constexpr float kDefaultVadThreshold = core::vad_constants::DEFAULT_THRESHOLD;
constexpr int kDefaultSampleRate = core::audio_constants::SAMPLE_RATE;
constexpr float kDefaultEnergyThreshold = core::vad_constants::DEFAULT_ENERGY_THRESHOLD;
constexpr int kDefaultVadHangover = core::vad_constants::DEFAULT_HANGOVER_FRAMES;
constexpr float kDefaultVadSmoothing = core::vad_constants::DEFAULT_SMOOTHING_ALPHA;
constexpr float kDefaultVadAdaptiveMin = core::vad_constants::DEFAULT_ADAPTIVE_MIN;
constexpr float kDefaultVadAdaptiveMax = core::vad_constants::DEFAULT_ADAPTIVE_MAX;
constexpr float kDefaultVadAdaptiveAlpha = core::vad_constants::DEFAULT_ADAPTIVE_ALPHA;
constexpr int kDefaultSttMinRepetition = core::stt_constants::DEFAULT_MIN_REPETITION;
constexpr int kDefaultSttHallucinationLen = core::stt_constants::DEFAULT_HALLUCINATION_LEN;
constexpr int kDefaultSttStepMs = core::stt_constants::DEFAULT_STEP_MS;
constexpr int kDefaultSttKeepMs = core::stt_constants::DEFAULT_KEEP_MS;
}  // namespace detail

struct SubtitleItem {
    std::string text;
    float confidence;
    bool is_final;
    std::string timestamp;
};

struct DeviceItem {
    int id;            // Internal PortAudio index
    std::string name;  // Display name
    int channels;
    int sample_rate;
};

struct AppState {
    // Status
    bool is_running = true;
    bool is_recording = false;
    std::string device_name;
    std::string model_path;
    std::string language;

    // VAD Metrics
    float vad_energy = 0.0F;
    float vad_probability = 0.0F;
    bool is_speech = false;
    float vad_threshold = detail::kDefaultVadThreshold;

    // STT Output
    std::vector<SubtitleItem> subtitles;
    std::string partial_transcript;

    // Performance
    int last_latency_ms = 0;
    float vad_latency_ms = 0.0F;        // float
    float inference_latency_ms = 0.0F;  // float
    float rtf = 0.0F;                   // Real Time Factor
    float throughput = 0.0F;            // Char/sec

    // Queue Metrics
    int audio_queue_depth = 0;
    int inference_queue_depth = 0;

    std::vector<DeviceItem> available_devices;
    // Moved selection state to a dedicated page controller or local to the device page component
    int selected_device_index = 0;
};

struct SettingsState {
    // Audio
    int current_device_id = -1;
    float input_gain = 1.0F;
    int sample_rate = detail::kDefaultSampleRate;

    // VAD
    float vad_threshold = detail::kDefaultVadThreshold;
    float vad_energy_thresh = detail::kDefaultEnergyThreshold;
    int vad_hangover = detail::kDefaultVadHangover;
    float vad_smoothing = detail::kDefaultVadSmoothing;
    bool vad_adaptive = true;
    float vad_adaptive_min = detail::kDefaultVadAdaptiveMin;
    float vad_adaptive_max = detail::kDefaultVadAdaptiveMax;
    float vad_adaptive_alpha = detail::kDefaultVadAdaptiveAlpha;

    // STT
    int stt_threads = 4;
    bool stt_token_dedup = true;
    bool stt_no_context = false;
    std::string stt_language = "en";
    bool stt_use_gpu = true;
    bool stt_flash_attn = true;
    int stt_min_repetition = detail::kDefaultSttMinRepetition;
    int stt_hallucination_len = detail::kDefaultSttHallucinationLen;
    int stt_step_ms = detail::kDefaultSttStepMs;
    int stt_keep_ms = detail::kDefaultSttKeepMs;
    // DecodingStrategy decoding_strategy = DecodingStrategy::Greedy; // TODO

    // String Buffers for UI Inputs
    std::string input_gain_str = "1.0";
    std::string vad_threshold_str = "0.5";
    std::string vad_energy_str = "0.001";
    std::string vad_smoothing_str = "0.3";
    std::string vad_hangover_str = "20";
    std::string stt_threads_str = "4";

    // System / Debug
    bool show_detailed_latency = false;

    // UI Navigation
    int selected_tab = 0;
};

class TuiRenderer {
public:
    TuiRenderer();
    ~TuiRenderer();

    // Non-copyable/movable
    TuiRenderer(const TuiRenderer&) = delete;
    auto operator=(const TuiRenderer&) -> TuiRenderer& = delete;
    TuiRenderer(TuiRenderer&&) = delete;
    auto operator=(TuiRenderer&&) -> TuiRenderer& = delete;

    /**
     * @brief Build the main component layout
     */
    auto check_quit() -> ftxui::Component;

    /**
     * @brief Start the TUI event loop (Blocking)
     * @param on_tick Callback for periodic updates (optional)
     */
    void run();

    /**
     * @brief Update application state safely
     */
    void update_state(const std::function<void(AppState&)>& update_fn);

    /**
     * @brief Trigger a redraw (thread-safe)
     */
    void post_redraw();

    /**
     * @brief Stop the TUI loop
     */
    void stop();

    /**
     * @brief Check if TUI is running
     */
    auto is_running() const -> bool;

    /**
     * @brief Initialize settings state from config
     */
    void set_settings(const SettingsState& settings);

    /**
     * @brief Set callback for device selection
     */
    void set_on_device_selected(std::function<void(int)> callback);
    void set_on_threshold_changed(std::function<void(float)> callback);
    void set_on_gain_changed(std::function<void(float)> callback);
    void set_on_energy_changed(std::function<void(float)> callback);
    void set_on_smoothing_changed(std::function<void(float)> callback);
    void set_on_vad_adaptive_changed(std::function<void(bool, float, float, float)> callback);

    void set_on_stt_gpu_changed(std::function<void(bool)> callback);
    void set_on_stt_heuristics_changed(std::function<void(int, int)> callback);  // rep_len, hal_len
    void set_on_stt_params_changed(std::function<void(int, std::string)> callback);

    // Helper methods for run()
    auto setup_components() -> ftxui::Component;
    auto setup_layout(const ftxui::Component& tab_content, const ftxui::Component& tab_toggle) -> ftxui::Component;

private:
    ftxui::ScreenInteractive screen_;

    // UI State
    AppState state_;
    SettingsState settings_state_;
    mutable std::mutex state_mutex_;
    std::function<void(int)> on_device_selected_;
    std::function<void(float)> on_threshold_changed_;
    std::function<void(float)> on_gain_changed_;
    std::function<void(float)> on_energy_changed_;
    std::function<void(float)> on_smoothing_changed_;

    // STT Callbacks
    std::function<void(bool)> on_stt_gpu_changed_;
    std::function<void(int, std::string)> on_stt_params_changed_;  // threads, lang
    std::function<void(bool, float, float, float)> on_vad_adaptive_changed_;
    std::function<void(int, int)> on_stt_heuristics_changed_;

    // Render helpers
    auto render_header() const -> ftxui::Element;
    auto render_subtitles() -> ftxui::Element;
    auto render_metrics() -> ftxui::Element;  // Mini summary
    auto render_vad_metrics() -> ftxui::Element;
    auto render_system_metrics() -> ftxui::Element;
    static auto render_footer() -> ftxui::Element;

    // Component factories
    auto create_logs_component() -> ftxui::Component;
    auto create_devices_component() -> ftxui::Component;
    auto create_audio_settings_component() -> ftxui::Component;
    auto create_vad_settings_component() -> ftxui::Component;
    auto create_stt_settings_component() -> ftxui::Component;
    auto create_system_view_component() -> ftxui::Component;
    static auto create_help_component() -> ftxui::Component;

    // Pages
    // (Logic implemented inline in run() for closure access)

    int active_page_index_ = 0;  // 0=Dash, 1=Settings, 2=Devices

    // TUI State (formerly local to run())
    std::vector<std::string> device_menu_entries_;
    int device_menu_selected_ = 0;
    int tab_index_ = 0;
    std::vector<std::string> tab_names_ = {" 📜 Logs ", " 🎙️ Devices ", " 🔊 Audio ", " 🧠 VAD ",
                                           " 📝 STT ",  " 📊 System ",  " ❓ Help "};
};

}  // namespace ui
