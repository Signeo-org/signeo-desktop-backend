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
#include "ftxui/dom/elements.hpp"

namespace ui {

struct SubtitleItem {
    std::string text;
    float confidence;
    bool is_final;
    std::string timestamp;
};

struct DeviceItem {
    int id;           // Internal PortAudio index
    std::string name; // Display name
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
    float vad_energy = 0.0f;
    float vad_probability = 0.0f;
    bool is_speech = false;
    float vad_threshold = 0.5f;

    // STT Output
    std::vector<SubtitleItem> subtitles;
    std::string partial_transcript;

    // Performance
    int last_latency_ms = 0;
    float vad_latency_ms = 0.0f;       // float
    float inference_latency_ms = 0.0f; // float
    float rtf = 0.0f;                  // Real Time Factor
    float throughput = 0.0f;           // Char/sec

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
    float input_gain = 1.0f;
    int sample_rate = 16000;

    // VAD
    float vad_threshold = 0.5f;
    float vad_energy_thresh = 0.001f;
    int vad_hangover = 20;
    float vad_smoothing = 0.3f;
    bool vad_adaptive = true;
    float vad_adaptive_min = 0.35f;
    float vad_adaptive_max = 0.6f;
    float vad_adaptive_alpha = 0.95f;

    // STT
    int stt_threads = 4;
    bool stt_token_dedup = true;
    bool stt_no_context = false;
    std::string stt_language = "en";
    bool stt_use_gpu = true;
    bool stt_flash_attn = true;
    int stt_min_repetition = 10;
    int stt_hallucination_len = 2;
    int stt_step_ms = 2000;
    int stt_keep_ms = 500;
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

    /**
     * @brief Build the main component layout
     */
    ftxui::Component check_quit();

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
    bool is_running() const;

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
    void set_on_stt_heuristics_changed(std::function<void(int, int)> callback); // rep_len, hal_len
    void set_on_stt_params_changed(std::function<void(int, std::string)> callback);

    // Helper methods for run()
    ftxui::Component setup_components();
    ftxui::Component setup_layout(const ftxui::Component& tab_content, const ftxui::Component& tab_toggle);

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
    std::function<void(int, std::string)> on_stt_params_changed_; // threads, lang
    std::function<void(bool, float, float, float)> on_vad_adaptive_changed_;
    std::function<void(int, int)> on_stt_heuristics_changed_;

    // Render helpers
    ftxui::Element render_header() const;
    ftxui::Element render_subtitles();
    ftxui::Element render_metrics(); // Mini summary
    ftxui::Element render_vad_metrics();
    ftxui::Element render_system_metrics();
    static ftxui::Element render_footer();

    // Component factories
    ftxui::Component create_logs_component();
    ftxui::Component create_devices_component();
    ftxui::Component create_audio_settings_component();
    ftxui::Component create_vad_settings_component();
    ftxui::Component create_stt_settings_component();
    ftxui::Component create_system_view_component();
    static ftxui::Component create_help_component();

    // Pages
    // (Logic implemented inline in run() for closure access)

    int active_page_index_ = 0; // 0=Dash, 1=Settings, 2=Devices

    // TUI State (formerly local to run())
    std::vector<std::string> device_menu_entries_;
    int device_menu_selected_ = 0;
    int tab_index_ = 0;
    std::vector<std::string> tab_names_ = {" 📜 Logs ", " 🎙️ Devices ", " 🔊 Audio ", " 🧠 VAD ",
                                           " 📝 STT ",  " 📊 System ",  " ❓ Help "};
};

} // namespace ui
