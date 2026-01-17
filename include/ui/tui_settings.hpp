#pragma once

/**
 * @file tui_settings.h
 * @brief Settings Panel UI Factories
 *
 * Provides static factories for creating styled settings widgets (Sliders, Checkboxes)
 * and the main Settings Tab layout.
 */

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "tui_renderer.hpp"

namespace ui {

/**
 * @brief Factory class for creating reusable settings UI components.
 *
 * All methods are static - this class is a namespace-like container
 * for settings widget factories.
 */
class SettingsRenderer {
public:
    // ─────────────────────────────────────────────────────────────
    // Main Settings Panel
    // ─────────────────────────────────────────────────────────────

    /// @brief Create the complete settings panel with tabs
    static ftxui::Component Create(SettingsState* state, std::function<void()> on_change,
                                   std::function<void()> on_close);

    // ─────────────────────────────────────────────────────────────
    // Widget Factories
    // ─────────────────────────────────────────────────────────────

    /// @brief Create a labeled slider for float values
    static ftxui::Component CreateSlider(std::string label, float* value, float min, float max, float step,
                                         std::function<void()> on_change);

    /// @brief Create a labeled checkbox for boolean values
    static ftxui::Component CreateCheckbox(std::string label, bool* state, std::function<void()> on_change);

    /// @brief Create a labeled text input for string values
    static ftxui::Component CreateInput(std::string label, std::string* state, std::function<void()> on_change);
};

} // namespace ui
