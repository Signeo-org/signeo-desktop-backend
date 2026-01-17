/**
 * @file tui_renderer.cpp
 * @brief Implementation of the Terminal User Interface
 */

#include "ui/tui_renderer.hpp"

#include <iomanip>
#include <sstream>

#include "ui/tui_settings.hpp"

namespace ui {

using namespace ftxui;

// Helper for Int Sliders
static Component IntSliderWithLabel(std::string label, int* value, int min, int max, std::function<void()> on_change) {
    auto slider = Slider(label, value, min, max, 1);

    // Wrap slider to detect value changes and fire callback
    auto wrapped_slider = slider | CatchEvent([=, last_value = *value](Event) mutable {
                              if (*value != last_value) {
                                  last_value = *value;
                                  if (on_change)
                                      on_change();
                              }
                              return false;
                          });

    return Container::Vertical({
        Renderer([=] {
            return hbox({
                text(label) | size(WIDTH, GREATER_THAN, 20),
                text(std::to_string(*value)) | bold | color(Color::Cyan),
            });
        }),
        wrapped_slider | borderEmpty,
    });
}

TuiRenderer::TuiRenderer() : screen_(ScreenInteractive::Fullscreen()) {}

TuiRenderer::~TuiRenderer() {}

bool TuiRenderer::is_running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.is_running;
}

void TuiRenderer::run() {
    // 1. Setup Components (Tabs, Settings, etc.)
    auto components = setup_components();

    // 2. Setup Layout (Header, Footer, Tabs)
    // We need to recreate the toggle explicitly to bind it to the controller
    auto tab_toggle = Toggle(&tab_names_, &tab_index_);
    auto main_layout = setup_layout(components, tab_toggle);

    // 3. Global Event Handler
    auto main_api = CatchEvent(main_layout, [&](Event event) {
        if (event == Event::Escape) {
            screen_.ExitLoopClosure()();
            return true;
        }
        // Tab Navigation
        if (event == Event::ArrowRight) {
            tab_index_ = (tab_index_ + 1) % (int)tab_names_.size();
            return true;
        }
        if (event == Event::ArrowLeft) {
            tab_index_ = (tab_index_ - 1 + (int)tab_names_.size()) % (int)tab_names_.size();
            return true;
        }
        return false;
    });

    // 4. Run Loop
    screen_.Loop(main_api);

    // FIX: Ensure app knows we stopped
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.is_running = false;
    }
}

Component TuiRenderer::setup_components() {
    return Container::Tab({create_logs_component(), create_devices_component(), create_audio_settings_component(),
                           create_vad_settings_component(), create_stt_settings_component(),
                           create_system_view_component(), create_help_component()},
                          &tab_index_);
}

Component TuiRenderer::setup_layout(Component tab_content, Component tab_toggle) {
    return Renderer(tab_content, [this, tab_toggle, tab_content] {
        return vbox({render_header(), separator(),
                     // Custom styled tabs
                     hbox({filler(), tab_toggle->Render(), filler()}), separator(),
                     // Cast or explicit Element
                     tab_content->Render() | flex, separator(), render_footer()}) |
               border;
    });
}
// -------------------------------------------------------------------------

// -------------------------------------------------------------------------
// Component Factories
// -------------------------------------------------------------------------

ftxui::Component TuiRenderer::create_logs_component() {
    return Renderer([this] {
        return vbox({
            render_subtitles() | flex, separator(),
            render_metrics() // Mini Status
        });
    });
}

ftxui::Component TuiRenderer::create_devices_component() {
    auto device_menu = Menu(&device_menu_entries_, &device_menu_selected_);

    auto comp = Renderer(device_menu, [this, device_menu] {
        // Sync Logic
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (device_menu_entries_.size() != state_.available_devices.size()) {
                device_menu_entries_.clear();
                for (const auto& d : state_.available_devices) {
                    std::stringstream ss;
                    ss << d.name << " (" << d.channels << "ch, " << d.sample_rate << "Hz)";
                    device_menu_entries_.push_back(ss.str());
                }
            }
        }

        return vbox({text(" Select Input Device ") | bold | hcenter | color(Color::Green), separator(),
                     device_menu->Render() | frame | flex, separator(),
                     text(" [Enter] Select Device ") | dim | hcenter}) |
               borderEmpty;
    });

    return CatchEvent(comp, [this](Event event) {
        if (event == Event::Return) {
            int selected_id = -1;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (device_menu_selected_ >= 0 && device_menu_selected_ < (int)state_.available_devices.size()) {
                    selected_id = state_.available_devices[device_menu_selected_].id;
                }
            }
            if (selected_id != -1 && on_device_selected_)
                on_device_selected_(selected_id);
            return true;
        }
        return false;
    });
}

ftxui::Component TuiRenderer::create_audio_settings_component() {
    return Container::Vertical({
        SettingsRenderer::CreateInput("Input Gain", &settings_state_.input_gain_str,
                                      [this] {
                                          try {
                                              float val = std::stof(settings_state_.input_gain_str);
                                              settings_state_.input_gain = val;
                                              if (on_gain_changed_)
                                                  on_gain_changed_(val);
                                          } catch (...) {
                                          }
                                      }),
        Renderer([this] {
            return hbox({
                text("Sample Rate: ") | dim,
                text(std::to_string(settings_state_.sample_rate) + " Hz") | color(Color::Cyan),
            });
        }),
    });
}

ftxui::Component TuiRenderer::create_vad_settings_component() {
    return Container::Vertical({
        Renderer([this] { return render_vad_metrics(); }),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateInput("Threshold", &settings_state_.vad_threshold_str,
                                      [this] {
                                          try {
                                              float val = std::stof(settings_state_.vad_threshold_str);
                                              settings_state_.vad_threshold = val;
                                              if (on_threshold_changed_)
                                                  on_threshold_changed_(val);
                                          } catch (...) {
                                          }
                                      }),
        SettingsRenderer::CreateInput("Energy Gate", &settings_state_.vad_energy_str,
                                      [this] {
                                          try {
                                              float val = std::stof(settings_state_.vad_energy_str);
                                              settings_state_.vad_energy_thresh = val;
                                              if (on_energy_changed_)
                                                  on_energy_changed_(val);
                                          } catch (...) {
                                          }
                                      }),
        SettingsRenderer::CreateInput("Smoothing", &settings_state_.vad_smoothing_str,
                                      [this] {
                                          try {
                                              float val = std::stof(settings_state_.vad_smoothing_str);
                                              settings_state_.vad_smoothing = val;
                                              if (on_smoothing_changed_)
                                                  on_smoothing_changed_(val);
                                          } catch (...) {
                                          }
                                      }),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateInput("Hangover Frames", &settings_state_.vad_hangover_str,
                                      [this] {
                                          try {
                                              settings_state_.vad_hangover =
                                                  std::stoi(settings_state_.vad_hangover_str);
                                          } catch (...) {
                                          }
                                      }),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateCheckbox("Adaptive Mode", &settings_state_.vad_adaptive,
                                         [this] {
                                             if (on_vad_adaptive_changed_)
                                                 on_vad_adaptive_changed_(settings_state_.vad_adaptive,
                                                                          settings_state_.vad_adaptive_min,
                                                                          settings_state_.vad_adaptive_max,
                                                                          settings_state_.vad_adaptive_alpha);
                                         }),
        SettingsRenderer::CreateSlider("  Min Thresh", &settings_state_.vad_adaptive_min, 0.01f, 0.99f, 0.01f,
                                       [this] {
                                           if (on_vad_adaptive_changed_)
                                               on_vad_adaptive_changed_(settings_state_.vad_adaptive,
                                                                        settings_state_.vad_adaptive_min,
                                                                        settings_state_.vad_adaptive_max,
                                                                        settings_state_.vad_adaptive_alpha);
                                       }),
        SettingsRenderer::CreateSlider("  Max Thresh", &settings_state_.vad_adaptive_max, 0.01f, 0.99f, 0.01f,
                                       [this] {
                                           if (on_vad_adaptive_changed_)
                                               on_vad_adaptive_changed_(settings_state_.vad_adaptive,
                                                                        settings_state_.vad_adaptive_min,
                                                                        settings_state_.vad_adaptive_max,
                                                                        settings_state_.vad_adaptive_alpha);
                                       }),
        SettingsRenderer::CreateSlider("  Alpha", &settings_state_.vad_adaptive_alpha, 0.01f, 0.99f, 0.01f,
                                       [this] {
                                           if (on_vad_adaptive_changed_)
                                               on_vad_adaptive_changed_(settings_state_.vad_adaptive,
                                                                        settings_state_.vad_adaptive_min,
                                                                        settings_state_.vad_adaptive_max,
                                                                        settings_state_.vad_adaptive_alpha);
                                       }),
    });
}

ftxui::Component TuiRenderer::create_stt_settings_component() {
    return Container::Vertical({
        SettingsRenderer::CreateInput("Threads", &settings_state_.stt_threads_str,
                                      [this] {
                                          try {
                                              int val = std::stoi(settings_state_.stt_threads_str);
                                              settings_state_.stt_threads = val;
                                              if (on_stt_params_changed_)
                                                  on_stt_params_changed_(val, settings_state_.stt_language);
                                          } catch (...) {
                                          }
                                      }),
        SettingsRenderer::CreateInput("Language", &settings_state_.stt_language,
                                      [this] {
                                          if (on_stt_params_changed_)
                                              on_stt_params_changed_(settings_state_.stt_threads,
                                                                     settings_state_.stt_language);
                                      }),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateCheckbox("Use GPU (CUDA)", &settings_state_.stt_use_gpu,
                                         [this] {
                                             if (on_stt_gpu_changed_)
                                                 on_stt_gpu_changed_(settings_state_.stt_use_gpu);
                                         }),
        SettingsRenderer::CreateCheckbox("Flash Attn", &settings_state_.stt_flash_attn, nullptr),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateCheckbox("Token Dedup", &settings_state_.stt_token_dedup, nullptr),
        SettingsRenderer::CreateCheckbox("No Context", &settings_state_.stt_no_context, nullptr),
        Renderer([] { return separator(); }),
        IntSliderWithLabel("Step (ms)", &settings_state_.stt_step_ms, 500, 5000, nullptr),
        IntSliderWithLabel("Keep (ms)", &settings_state_.stt_keep_ms, 0, 1000, nullptr),
        Renderer([] { return separator(); }),
        IntSliderWithLabel("Min Repet.", &settings_state_.stt_min_repetition, 4, 30,
                           [this] {
                               if (on_stt_heuristics_changed_)
                                   on_stt_heuristics_changed_(settings_state_.stt_min_repetition,
                                                              settings_state_.stt_hallucination_len);
                           }),
        IntSliderWithLabel("Hallucinat.", &settings_state_.stt_hallucination_len, 0, 10,
                           [this] {
                               if (on_stt_heuristics_changed_)
                                   on_stt_heuristics_changed_(settings_state_.stt_min_repetition,
                                                              settings_state_.stt_hallucination_len);
                           }),
    });
}

ftxui::Component TuiRenderer::create_help_component() {
    return Renderer([] {
        return vbox({
                   text("Help & Info") | bold | hcenter | color(Color::Cyan),
                   separator(),
                   text("Navigation:") | bold,
                   text("  [Left/Right Arrow] : Switch Tabs"),
                   text("  [Esc]              : Quit Application"),
                   text("  [Enter]            : Select Device / Toggle Checkbox"),
                   separator(),
                   text("Inputs:") | bold,
                   text("  Type values directly into input boxes."),
                   text("  Changes apply immediately (mostly)."),
                   separator(),
                   text("About:") | bold,
                   text("  SIGNEO Real-time Subtitler v0.6.1"),
                   text("  Powered by Whisper.cpp & Silero VAD"),
               }) |
               borderEmpty | flex;
    });
}

ftxui::Component TuiRenderer::create_system_view_component() {
    return Renderer([this] { return render_system_metrics(); });
}

void TuiRenderer::stop() { screen_.ExitLoopClosure()(); }

void TuiRenderer::post_redraw() { screen_.PostEvent(Event::Custom); }

void TuiRenderer::update_state(const std::function<void(AppState&)>& update_fn) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    update_fn(state_);
    post_redraw();
}

void TuiRenderer::set_settings(const SettingsState& settings) {
    settings_state_ = settings;
    // Sync string buffers for inputs
    settings_state_.vad_threshold_str = std::to_string(settings.vad_threshold);
    settings_state_.vad_energy_str = std::to_string(settings.vad_energy_thresh);
    settings_state_.vad_smoothing_str = std::to_string(settings.vad_smoothing);
    settings_state_.vad_hangover_str = std::to_string(settings.vad_hangover);
    settings_state_.stt_threads_str = std::to_string(settings.stt_threads);
    settings_state_.input_gain_str = std::to_string(settings.input_gain);
}

void TuiRenderer::set_on_threshold_changed(std::function<void(float)> callback) { on_threshold_changed_ = callback; }

void TuiRenderer::set_on_gain_changed(std::function<void(float)> callback) { on_gain_changed_ = callback; }

void TuiRenderer::set_on_energy_changed(std::function<void(float)> callback) { on_energy_changed_ = callback; }

void TuiRenderer::set_on_smoothing_changed(std::function<void(float)> callback) { on_smoothing_changed_ = callback; }

void TuiRenderer::set_on_device_selected(std::function<void(int)> callback) { on_device_selected_ = callback; }

void TuiRenderer::set_on_stt_gpu_changed(std::function<void(bool)> callback) { on_stt_gpu_changed_ = callback; }

void TuiRenderer::set_on_stt_params_changed(std::function<void(int, std::string)> callback) {
    on_stt_params_changed_ = callback;
}

void TuiRenderer::set_on_vad_adaptive_changed(std::function<void(bool, float, float, float)> callback) {
    on_vad_adaptive_changed_ = callback;
}

void TuiRenderer::set_on_stt_heuristics_changed(std::function<void(int, int)> callback) {
    on_stt_heuristics_changed_ = callback;
}

Element TuiRenderer::render_header() {
    // Header is now integrated into the main layout as the Tab Bar
    // We keep this for the "Logo" part if needed, but the main navigation is separate.
    return hbox({
               text(" SIGNEO ") | bold | color(Color::Cyan),
               text(" v0.6.0 ") | color(Color::GrayDark),
               filler(),
               text(" Device: " + state_.device_name) | color(Color::Green),
               text(" | Lang: " + state_.language) | color(Color::Magenta),
           }) |
           borderEmpty;
}

Element TuiRenderer::render_subtitles() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    Elements list;

    // History (reverse order)
    int index = 0;
    for (auto it = state_.subtitles.rbegin(); it != state_.subtitles.rend(); ++it) {
        auto style = Color::White;
        if (index >= 3)
            style = Color::GrayLight;
        if (index >= 6)
            style = Color::GrayDark;

        list.push_back(hbox({
            text("[" + it->timestamp + "] ") | color(Color::GrayDark) | dim,
            text(it->text) | color(style) | bold,
            text(" (" + std::to_string(int(it->confidence * 100)) + "%)") | color(Color::BlueLight) | dim,
        }));
        index++;
    }

    // Partial (streaming)
    if (!state_.partial_transcript.empty()) {
        list.push_back(hbox({
            text("[Streaming] ") | color(Color::Yellow),
            text(state_.partial_transcript) | color(Color::GrayLight) | dim,
        }));
        // Autoscroll?
    }

    return vbox(std::move(list)) | vscroll_indicator | frame | flex;
}

Element TuiRenderer::render_metrics() {
    // Legacy / Mini Status
    std::lock_guard<std::mutex> lock(state_mutex_);
    return hbox({
               text("Status: " + std::string(state_.is_speech ? "SPEECH" : "SILENCE")) | bold |
                   color(state_.is_speech ? Color::Red : Color::Green),
               filler(),
               text("End-to-End: " + std::to_string(state_.last_latency_ms) + "ms") | dim,
           }) |
           borderEmpty;
}

Element TuiRenderer::render_vad_metrics() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto make_gauge = [](const std::string& label, float val, Color c) {
        return hbox({
            text(label + ": "),
            gauge(val) | color(c) | flex,
            text(" " + std::to_string(int(val * 100)) + "%"),
        });
    };

    return vbox({
               text("VAD Real-Time Metrics") | bold | hcenter | color(Color::Yellow),
               separator(),
               make_gauge("Energy", std::min(1.0f, state_.vad_energy * 100.0f), Color::Green),
               make_gauge("Prob  ", state_.vad_probability, state_.is_speech ? Color::Red : Color::Blue),
               separator(),
               text("Stats: ") | bold,
               text("VAD Incr Latency: " + std::to_string(state_.vad_latency_ms).substr(0, 4) + "ms"),
           }) |
           border;
}

Element TuiRenderer::render_system_metrics() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto make_gauge = [](const std::string& label, float val, Color c) {
        return hbox({
            text(label + ": "),
            gauge(val) | color(c) | flex,
            text(" " + std::to_string(int(val * 100)) + "%"),
        });
    };

    return vbox({
               text("System Performance") | bold | hcenter | color(Color::Magenta),
               separator(),
               make_gauge("Audio Queue", std::min(1.0f, state_.audio_queue_depth / 50.0f), Color::Yellow),
               make_gauge("Infer Queue", std::min(1.0f, state_.inference_queue_depth / 50.0f), Color::Magenta),
               separator(),
               text("Latency: ") | bold,
               text("STT Inference:  " + std::to_string(state_.inference_latency_ms).substr(0, 6) + "ms"),
               text("End-to-End:     " + std::to_string(state_.last_latency_ms) + "ms") | color(Color::Cyan),
               text("Overhead:       " +
                    std::to_string(std::max(0, state_.last_latency_ms - (int)state_.inference_latency_ms)) + "ms") |
                   dim,
               separator(),
               text("Throughput: ") | bold,
               text("RTF:   " + std::to_string(state_.rtf).substr(0, 4)),
               text("Speed: " + std::to_string((int)state_.throughput) + " cps"),
           }) |
           border;
}

Element TuiRenderer::render_footer() {
    return hbox({
        text(" [Esc] Exit ") | color(Color::GrayLight),
        filler(),
        text("SIGNEO Backend Service") | color(Color::GrayDark),
    });
}

} // namespace ui
