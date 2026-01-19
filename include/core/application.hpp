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
// Need full definition for AudioDevice
#include "../audio/audio_capture.hpp"
#include "common_types.hpp"
#include "metrics_collector.hpp"

// Forward declarations
namespace ui {
class TuiRenderer;
}
namespace audio {
class AudioCapture;
class AudioProcessor;
}  // namespace audio
namespace vad {
class VadProcessor;
}
namespace stt {
class SttEngine;
class StreamingTranscriber;
}  // namespace stt

/**
 * @brief Main application controller for the realtime subtitler.
 *
 * Manages the audio capture, VAD, and STT pipelines using a multi-threaded
 * architecture with lock-free queues for inter-thread communication.
 */
class Application {
public:
    explicit Application(AppConfig config);
    ~Application();

    // Rule of Five: Delete copy/move operations to manage resources safely
    Application(const Application&) = delete;
    Application(Application&&) = delete;
    auto operator=(const Application&) -> Application& = delete;
    auto operator=(Application&&) -> Application& = delete;

    /// @brief Run the main application loop. Returns exit code.
    auto run() -> int;

    // Constants
    static constexpr float kDefaultVadThreshold = 0.5F;
    static constexpr float kDefaultVadEnergyThreshold = 0.0001F;
    static constexpr float kDefaultVadSmoothing = 0.5F;
    static constexpr float kDefaultVadAdaptiveMin = 0.35F;
    static constexpr float kDefaultVadAdaptiveMax = 0.6F;
    static constexpr float kDefaultVadAdaptiveAlpha = 0.95F;

    static constexpr int kVadFrameSize = 512;
    static constexpr int kAudioSampleRate = 16000;
    static constexpr int kAudioFrameDurationMs = 32;
    static constexpr int kMainLoopSleepMs = 100;
    static constexpr int kAudioWaitMs = 5;
    static constexpr float kGainEpsilon = 0.01F;

    static constexpr int kDefaultSttMinRepetition = 10;
    static constexpr int kDefaultSttHallucinationLen = 2;

private:
    // ─────────────────────────────────────────────────────────────
    // Worker Loops (each manages its own processor internally)
    // ─────────────────────────────────────────────────────────────
    void audio_loop(ui::TuiRenderer* tui);
    void vad_loop(ui::TuiRenderer* tui);
    void stt_loop(ui::TuiRenderer* tui);

    // Helpers
    void print_startup_banner();
    static void handle_cli_device_selection();
    // Extracted CLI input handler to reduce complexity
    void handle_cli_input(ui::TuiRenderer* tui);
    void perform_device_scan_and_select();
    void initialize_ui_state(ui::TuiRenderer* tui) const;
    void setup_ui_callbacks(ui::TuiRenderer* tui);
    void run_main_loop(ui::TuiRenderer* tui);

    // Complexity Helpers
    [[nodiscard]] static auto resolve_device_name(const std::vector<audio::AudioDevice>& devices, int index)
        -> std::string;
    void handle_vad_speech_state(bool is_speech, bool& was_speech, const std::vector<float>& frame,
                                 std::chrono::steady_clock::time_point frame_time, vad::VadProcessor* vad);
    static void process_inference_chunk(const core::AudioChunk& chunk, stt::StreamingTranscriber* transcriber,
                                        ui::TuiRenderer* tui, core::MetricsCollector& metrics);

    // Refactored Worker Helpers
    auto initialize_audio_system(std::unique_ptr<audio::AudioCapture>& capture,
                                 std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui) const -> bool;
    void handle_audio_device_switch(std::unique_ptr<audio::AudioCapture>& capture,
                                    std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui);

    auto initialize_vad_processor(std::unique_ptr<vad::VadProcessor>& vad) -> bool;
    void update_vad_parameters(vad::VadProcessor* vad);

    // ─────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────
    AppConfig config_;
    std::mutex config_mutex_;  // Protects config_ for hot-reload operations

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
    utils::ThreadSafeQueue<core::AudioChunk> audio_queue_;      // Audio → VAD
    utils::ThreadSafeQueue<core::AudioChunk> inference_queue_;  // VAD → STT

    // ─────────────────────────────────────────────────────────────
    // Live State (atomic for lock-free updates from UI)
    // ─────────────────────────────────────────────────────────────
    // Audio
    std::atomic<int> pending_device_switch_{-1};
    std::atomic<float> pending_gain_{1.0F};

    // VAD
    std::atomic<float> vad_threshold_{kDefaultVadThreshold};
    std::atomic<float> vad_energy_threshold_{kDefaultVadEnergyThreshold};
    std::atomic<float> vad_smoothing_alpha_{kDefaultVadSmoothing};
    std::atomic<bool> vad_reload_requested_{false};

    // STT
    // STT
    std::atomic<bool> stt_reload_requested_{false};
    std::atomic<int> stt_min_repetition_len_{kDefaultSttMinRepetition};
    std::atomic<int> stt_hallucination_len_{kDefaultSttHallucinationLen};
    // Note: step/keep require reload so they stay in config_ + mutex

    // VAD Adaptive
    std::atomic<bool> vad_adaptive_enabled_{true};
    std::atomic<float> vad_adaptive_min_{kDefaultVadAdaptiveMin};
    std::atomic<float> vad_adaptive_max_{kDefaultVadAdaptiveMax};
    std::atomic<float> vad_adaptive_alpha_{kDefaultVadAdaptiveAlpha};

    // ─────────────────────────────────────────────────────────────
    // Metrics
    // ─────────────────────────────────────────────────────────────
    core::MetricsCollector metrics_;
};
