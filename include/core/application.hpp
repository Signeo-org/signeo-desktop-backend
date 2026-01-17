#pragma once

/**
 * @file application.h
 * @brief Main Application Controller
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../config/app_config.hpp"
#include "../utils/thread_safe_queue.hpp"
#include "common_types.hpp"
#include "metrics_collector.hpp"

// Forward declarations
namespace ui {
class TuiRenderer;
}
namespace audio {
class AudioCapture;
class AudioProcessor;
} // namespace audio
namespace vad {
class VadProcessor;
}

/**
 * @brief Main application controller for the realtime subtitler.
 *
 * Manages the audio capture, VAD, and STT pipelines using a multi-threaded
 * architecture with lock-free queues for inter-thread communication.
 */
class Application {
public:
    explicit Application(const AppConfig& config);
    ~Application();

    /// @brief Run the main application loop. Returns exit code.
    int run();

private:
    // ─────────────────────────────────────────────────────────────
    // Worker Loops (each manages its own processor internally)
    // ─────────────────────────────────────────────────────────────
    void audio_loop(ui::TuiRenderer* tui);
    void vad_loop(ui::TuiRenderer* tui);
    void stt_loop(ui::TuiRenderer* tui);

    // Helpers
    void print_startup_banner();
    void handle_cli_device_selection();
    void initialize_ui_state(ui::TuiRenderer* tui);
    void setup_ui_callbacks(ui::TuiRenderer* tui);
    void run_main_loop(ui::TuiRenderer* tui);

    // Refactored Worker Helpers
    bool initialize_audio_system(std::unique_ptr<audio::AudioCapture>& capture,
                                 std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui);
    void handle_audio_device_switch(std::unique_ptr<audio::AudioCapture>& capture,
                                    std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui);

    bool initialize_vad_processor(std::unique_ptr<vad::VadProcessor>& vad);
    void update_vad_parameters(vad::VadProcessor* vad);

    // ─────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────
    AppConfig config_;
    std::mutex config_mutex_; // Protects config_ for hot-reload operations

    // ─────────────────────────────────────────────────────────────
    // Threading
    // ─────────────────────────────────────────────────────────────
    std::atomic<bool> running_{false};
    std::thread audio_thread_;
    std::thread vad_thread_;
    std::thread stt_thread_;

    // ─────────────────────────────────────────────────────────────
    // Inter-Thread Queues
    // ─────────────────────────────────────────────────────────────
    utils::ThreadSafeQueue<core::AudioChunk> audio_queue_;     // Audio → VAD
    utils::ThreadSafeQueue<core::AudioChunk> inference_queue_; // VAD → STT

    // ─────────────────────────────────────────────────────────────
    // Live State (atomic for lock-free updates from UI)
    // ─────────────────────────────────────────────────────────────
    // Audio
    std::atomic<int> pending_device_switch_{-1};
    std::atomic<float> pending_gain_{1.0f};

    // VAD
    std::atomic<float> vad_threshold_{0.5f};
    std::atomic<float> vad_energy_threshold_{0.0001f};
    std::atomic<float> vad_smoothing_alpha_{0.5f};
    std::atomic<bool> vad_reload_requested_{false};

    // STT
    // STT
    std::atomic<bool> stt_reload_requested_{false};
    std::atomic<int> stt_min_repetition_len_{10};
    std::atomic<int> stt_hallucination_len_{2};
    // Note: step/keep require reload so they stay in config_ + mutex

    // VAD Adaptive
    std::atomic<bool> vad_adaptive_enabled_{true};
    std::atomic<float> vad_adaptive_min_{0.35f};
    std::atomic<float> vad_adaptive_max_{0.6f};
    std::atomic<float> vad_adaptive_alpha_{0.95f};

    // ─────────────────────────────────────────────────────────────
    // Metrics
    // ─────────────────────────────────────────────────────────────
    core::MetricsCollector metrics_;
};
