/**
 * @file tui_settings.cpp
 * @brief Implementation of Settings UI logic
 */

#include "ui/tui_settings.hpp"

#include <spdlog/spdlog.h>

#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "utils/thread_metrics.hpp"

namespace ui {

using namespace ftxui;

// Helper to create a slider with label
// Component SliderWithLabel(...) - Moved to static method

// Helper to create an int slider with callback
Component intSliderWithLabel(const std::string& label, int* value, int min, int max,
                             const std::function<void()>& on_change) {
    auto slider = Slider(label, value, min, max, 1);

    // Wrap slider to detect value changes and fire callback
    auto wrapped_slider = slider | CatchEvent([=, last_value = *value](const Event&) mutable {
                              if (*value != last_value) {
                                  last_value = *value;
                                  if (on_change) {
                                      on_change();
                                  }
                              }
                              return false; // Let slider handle the event
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

Component SettingsRenderer::Create(SettingsState* state, const std::function<void()>& on_change,
                                   std::function<void()> on_close) {
    // Tab Controller
    static const auto tab_values = std::vector<std::string>{"🔊 Audio", "🧠 VAD", "📝 STT", "📊 System"};
    auto tab_toggle = Toggle(&tab_values, &state->selected_tab);

    auto vad_container = Container::Vertical({
        SettingsRenderer::CreateSlider("Threshold", &state->vad_threshold, 0.01F, 0.99F, 0.01F, on_change),
        SettingsRenderer::CreateSlider("Energy Gate", &state->vad_energy_thresh, 0.0001F, 0.01F, 0.0001F, on_change),
        SettingsRenderer::CreateSlider("Smoothing", &state->vad_smoothing, 0.01F, 0.99F, 0.01F, on_change),
        Renderer([] { return separator(); }),
        SettingsRenderer::CreateCheckbox("Adaptive Mode", &state->vad_adaptive, on_change),
        SettingsRenderer::CreateSlider("  Min Thresh", &state->vad_adaptive_min, 0.01F, 0.99F, 0.01F, on_change),
        SettingsRenderer::CreateSlider("  Max Thresh", &state->vad_adaptive_max, 0.01F, 0.99F, 0.01F, on_change),
        SettingsRenderer::CreateSlider("  Alpha", &state->vad_adaptive_alpha, 0.01F, 0.99F, 0.01F, on_change),
    });

    // --- MAIN LAYOUT ---
    auto container = Container::Vertical({
        Container::Horizontal({
            tab_toggle,
            Button("❌ Close", std::move(on_close), ButtonOption::Ascii()),
        }),
        Renderer([] { return separator(); }),
        Container::Tab(
            {
                // Audio Tab
                Container::Vertical({
                    SettingsRenderer::CreateSlider("Input Gain", &state->input_gain, 0.0F, 5.0F, 0.1F, on_change),
                    // Device selector could go here too but it's complex
                }),
                vad_container,
                // STT Tab
                Container::Vertical({
                    SettingsRenderer::CreateCheckbox("Use GPU", &state->stt_use_gpu, on_change),
                    SettingsRenderer::CreateCheckbox("Flash Attn", &state->stt_flash_attn, on_change),
                    SettingsRenderer::CreateCheckbox("Token Utils", &state->stt_token_dedup, on_change),
                    Renderer([] { return separator(); }),
                    intSliderWithLabel("Step (ms)", &state->stt_step_ms, 500, 5000, on_change),
                    intSliderWithLabel("Keep (ms)", &state->stt_keep_ms, 0, 1000, on_change),
                    Renderer([] { return separator(); }),
                    intSliderWithLabel("Min Repet.", &state->stt_min_repetition, 4, 30, on_change),
                    intSliderWithLabel("Hallucination", &state->stt_hallucination_len, 0, 10, on_change),
                }),

                // System Monitor Tab
                Renderer([=] {
                    auto stats = utils::ThreadMetrics::Get().update_and_get();
                    Elements list;
                    list.push_back(text("Thread CPU Usage") | bold);
                    list.push_back(separator());

                    for (const auto& s : stats) {
                        list.push_back(hbox({
                            text(s.name) | size(WIDTH, GREATER_THAN, 15),
                            gauge(static_cast<float>(s.cpu_usage_percent) / 100.0F) | flex,
                            text(" " + std::to_string((int)s.cpu_usage_percent) + "%"),
                        }));
                    }

                    return vbox(std::move(list));
                }),
            },
            &state->selected_tab),
    });

    return Renderer(container, [=] {
        return vbox({
                   text(" ⚙️ Settings ") | bold | hcenter,
                   separator(),
                   container->Render() | flex,
               }) |
               border;
    });
}
Component SettingsRenderer::CreateSlider(const std::string& label, float* value, float min, float max, float step,
                                         const std::function<void()>& on_change) {
    auto slider = Slider(label, value, min, max, step);

    // Wrap slider to detect value changes and fire callback
    auto wrapped_slider = slider | CatchEvent([=, last_value = *value](const Event&) mutable {
                              if (*value != last_value) {
                                  last_value = *value;
                                  if (on_change) {
                                      on_change();
                                  }
                              }
                              return false; // Let slider handle the event
                          });

    return Container::Vertical({
        Renderer([=] {
            return hbox({
                text(label) | size(WIDTH, GREATER_THAN, 20),
                text(std::to_string(*value).substr(0, 5)) | bold | color(Color::Cyan),
            });
        }),
        wrapped_slider | borderEmpty,
    });
}

Component SettingsRenderer::CreateCheckbox(const std::string& label, bool* state,
                                           const std::function<void()>& on_change) {
    return Container::Horizontal({
        Renderer([=] { return text(label) | size(WIDTH, GREATER_THAN, 20); }),
        Checkbox("", state) | CatchEvent([=, last_value = *state](const Event&) mutable {
            // Detect state change on any event (similar to slider pattern)
            // This fires on the NEXT event after the change, which is acceptable for TUI
            if (*state != last_value) {
                last_value = *state;
                if (on_change) {
                    on_change();
                }
            }
            return false;
        }),
    });
}

Component SettingsRenderer::CreateInput(const std::string& label, std::string* state, std::function<void()> on_change) {
    InputOption opt;
    opt.on_change = std::move(on_change);
    return Container::Horizontal({
        Renderer([=] { return text(label) | size(WIDTH, GREATER_THAN, 20); }),
        Input(state, "", opt) | borderEmpty | flex,
    });
}

} // namespace ui
