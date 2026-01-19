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

namespace {
constexpr int kLabelMinWidth = 20;
constexpr float kSliderMin = 0.01F;
constexpr float kSliderMax = 0.99F;
constexpr float kSliderStep = 0.01F;
constexpr float kEnergySliderMin = 0.0001F;
constexpr float kEnergySliderMax = 0.01F;
constexpr float kEnergySliderStep = 0.0001F;
constexpr float kGainSliderMax = 5.0F;
constexpr float kGainSliderStep = 0.1F;
constexpr int kMinStepMs = 500;
constexpr int kMaxStepMs = 5000;
constexpr int kMaxKeepMs = 1000;
constexpr int kMinRepetition = 4;
constexpr int kMaxRepetition = 30;
constexpr int kMaxHallucination = 10;
constexpr int kValueDisplayLen = 5;
constexpr int kGaugeMaxWidth = 15;
}  // namespace

using namespace ftxui;

// Helper to create a slider with label
// Component SliderWithLabel(...) - Moved to static method

// Helper to create an int slider with callback
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto int_slider_with_label(const std::string& label, const int* value, int min_val, int max_val,
                           const std::function<void()>& on_change) -> Component {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto slider = Slider(label, const_cast<int*>(value), min_val, max_val, 1);

    // Wrap slider to detect value changes and fire callback
    auto wrapped_slider = slider | CatchEvent([=, last_value = *value](const Event&) mutable { // NOLINT(bugprone-exception-escape)
                              if (*value != last_value) {
                                  last_value = *value;
                                  if (on_change) {
                                      on_change();
                                  }
                              }
                              return false;  // Let slider handle the event
                          });

    return Container::Vertical({
        Renderer([=] { // NOLINT(bugprone-exception-escape)
            return hbox({
                text(label) | size(WIDTH, GREATER_THAN, kLabelMinWidth),
                text(std::to_string(*value)) | bold | color(Color::Cyan),
            });
        }),
        wrapped_slider | borderEmpty,
    });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SettingsRenderer::create(SettingsState* state, const std::function<void()>& on_change,
                              std::function<void()> on_close) -> Component {
    // Tab Controller
    static const auto tab_values = std::vector<std::string>{"🔊 Audio", "🧠 VAD", "📝 STT", "📊 System"};
    auto tab_toggle = Toggle(&tab_values, &state->selected_tab);

    auto vad_container = Container::Vertical({
        SettingsRenderer::create_slider("Threshold", &state->vad_threshold, kSliderMin, kSliderMax, kSliderStep, on_change),
        SettingsRenderer::create_slider("Energy Gate", &state->vad_energy_thresh, kEnergySliderMin, kEnergySliderMax, kEnergySliderStep, on_change),
        SettingsRenderer::create_slider("Smoothing", &state->vad_smoothing, kSliderMin, kSliderMax, kSliderStep, on_change),
        Renderer([] { return separator(); }),
        SettingsRenderer::create_checkbox("Adaptive Mode", &state->vad_adaptive, on_change),
        SettingsRenderer::create_slider("  Min Thresh", &state->vad_adaptive_min, kSliderMin, kSliderMax, kSliderStep, on_change),
        SettingsRenderer::create_slider("  Max Thresh", &state->vad_adaptive_max, kSliderMin, kSliderMax, kSliderStep, on_change),
        SettingsRenderer::create_slider("  Alpha", &state->vad_adaptive_alpha, kSliderMin, kSliderMax, kSliderStep, on_change),
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
                    SettingsRenderer::create_slider("Input Gain", &state->input_gain, 0.0F, kGainSliderMax, kGainSliderStep, on_change),
                    // Device selector could go here too but it's complex
                }),
                vad_container,
                // STT Tab
                Container::Vertical({
                    SettingsRenderer::create_checkbox("Use GPU", &state->stt_use_gpu, on_change),
                    SettingsRenderer::create_checkbox("Flash Attn", &state->stt_flash_attn, on_change),
                    SettingsRenderer::create_checkbox("Token Utils", &state->stt_token_dedup, on_change),
                    Renderer([] { return separator(); }),
                    int_slider_with_label("Step (ms)", &state->stt_step_ms, kMinStepMs, kMaxStepMs, on_change),
                    int_slider_with_label("Keep (ms)", &state->stt_keep_ms, 0, kMaxKeepMs, on_change),
                    Renderer([] { return separator(); }),
                    int_slider_with_label("Min Repet.", &state->stt_min_repetition, kMinRepetition, kMaxRepetition, on_change),
                    int_slider_with_label("Hallucination", &state->stt_hallucination_len, 0, kMaxHallucination, on_change),
                }),

                // System Monitor Tab
                Renderer([=] {
                    auto stats = utils::ThreadMetrics::get().update_and_get();
                    Elements list;
                    list.push_back(text("Thread CPU Usage") | bold);
                    list.push_back(separator());

                    for (const auto& stat : stats) {
                        list.push_back(hbox({
                            text(stat.name) | size(WIDTH, GREATER_THAN, kGaugeMaxWidth),
                            gauge(static_cast<float>(stat.cpu_usage_percent) / 100.0F) | flex,
                            text(" " + std::to_string(static_cast<int>(stat.cpu_usage_percent)) + "%"),
                        }));
                    }

                    return vbox(std::move(list));
                }),
            },
            &state->selected_tab),
    });

    return Renderer(container, [=] { // NOLINT(bugprone-exception-escape)
        return vbox({
                   text(" ⚙️ Settings ") | bold | hcenter,
                   separator(),
                   container->Render() | flex,
               }) |
               border;
    });
}
auto SettingsRenderer::create_slider(const std::string& label, const float* value, float min, float max, float step,
                                     const std::function<void()>& on_change) -> Component {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto slider = Slider(label, const_cast<float*>(value), min, max, step);

    // Wrap slider to detect value changes and fire callback
    auto wrapped_slider = slider | CatchEvent([=, last_value = *value](const Event&) mutable { // NOLINT(bugprone-exception-escape)
                              if (*value != last_value) {
                                  last_value = *value;
                                  if (on_change) {
                                      on_change();
                                  }
                              }
                              return false;  // Let slider handle the event
                          });

    return Container::Vertical({
        Renderer([=] { // NOLINT(bugprone-exception-escape)
            return hbox({
                text(label) | size(WIDTH, GREATER_THAN, kLabelMinWidth),
                text(std::to_string(*value).substr(0, kValueDisplayLen)) | bold | color(Color::Cyan),
            });
        }),
        wrapped_slider | borderEmpty,
    });
}

auto SettingsRenderer::create_checkbox(const std::string& label, bool* state, const std::function<void()>& on_change)
    -> Component {
    return Container::Horizontal({
        Renderer([=] { return text(label) | size(WIDTH, GREATER_THAN, kLabelMinWidth); }), // NOLINT(bugprone-exception-escape)
        Checkbox("", state) | CatchEvent([=, last_value = *state](const Event&) mutable { // NOLINT(bugprone-exception-escape)
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

auto SettingsRenderer::create_input(const std::string& label, std::string* state, std::function<void()> on_change)
    -> Component {
    InputOption opt;
    opt.on_change = std::move(on_change);
    return Container::Horizontal({
        Renderer([=] { return text(label) | size(WIDTH, GREATER_THAN, kLabelMinWidth); }), // NOLINT(bugprone-exception-escape)
        Input(state, "", opt) | borderEmpty | flex,
    });
}

}  // namespace ui
