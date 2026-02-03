/**
 * @file application.cpp
 * @brief Implementation of the main Application controller
 */

#include "core/application.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include "audio/audio_capture.hpp"
#include "audio/audio_processor.hpp"
#include "core/metrics_collector.hpp"
#include "core/version.hpp"  // Generated version header
#include "output/log_output.hpp"
#include "stt/streaming_transcriber.hpp"
#include "stt/stt_engine.hpp"
#include "ui/tui_renderer.hpp"
#include "utils/signal_handler.hpp"
#include "utils/thread_metrics.hpp"
#include "vad/vad_processor.hpp"
#include "output/json_output.hpp"
#ifdef _WIN32
    #include <conio.h>  // For _kbhit, _getch
#else
    #include <fcntl.h>
    #include <stdio.h>
    #include <termios.h>
    #include <unistd.h>

// Linux implementation of _kbhit
int _kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}

// Linux implementation of _getch
int _getch(void) {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

Application::Application(AppConfig config) : config_(std::move(config)) {}

Application::~Application() = default;

auto Application::run() -> int {
    // Logging Setup
    core::init_logging(config_.log_file, !config_.use_ui);

    // Check for device list request
    if (config_.list_devices_requested) {
        handle_cli_device_selection();
        return 0;  // Exit after listing
    }

    print_startup_banner();

    // Hardware/UI Initialization
    std::unique_ptr<ui::TuiRenderer> tui;
    std::thread tui_thread;

    if (config_.use_ui) {
        tui = std::make_unique<ui::TuiRenderer>();
        // Run TUI in a separate thread so main thread can handle audio loop
        tui_thread = std::thread([&tui]() { tui->run(); });
    }

    // Initialize atomic VAD config from initial config
    vad_threshold_.store(config_.vad_threshold);
    vad_energy_threshold_.store(config_.vad_energy_threshold);
    vad_smoothing_alpha_.store(config_.vad_smoothing_alpha);

    vad_adaptive_enabled_.store(config_.vad_adaptive_threshold);
    vad_adaptive_min_.store(config_.vad_adaptive_min_threshold);
    vad_adaptive_max_.store(config_.vad_adaptive_max_threshold);
    vad_adaptive_alpha_.store(config_.vad_adaptive_alpha);

    stt_min_repetition_len_.store(config_.stt_min_repetition_len);
    stt_hallucination_len_.store(config_.stt_hallucination_min_len);

    // Init UI State & Callbacks
    if (tui) {
        initialize_ui_state(tui.get());
        setup_ui_callbacks(tui.get());
    }

    // IPC Setup (JSON mode) - emit initial messages
    if (config_.json_output) {
        output::JsonOutput::print_ready(core::kVersionString);
        emit_device_list();
        setup_ipc_handler();
    }

    // Reset Queues/Flags
    running_ = true;
    audio_queue_.restart();
    inference_queue_.restart();

    // Spawn Threads - all loops now manage their own processors internally
    vad_thread_ = std::thread(&Application::vad_loop, this, tui.get());
    stt_thread_ = std::thread(&Application::stt_loop, this, tui.get());
    audio_thread_ = std::thread(&Application::audio_loop, this, tui.get());

    // Register threads for monitoring
    auto& metrics = utils::ThreadMetrics::get();
    if (stt_thread_.joinable()) {
        metrics.register_thread("STT Worker", stt_thread_.native_handle());
    }
    if (audio_thread_.joinable()) {
        metrics.register_thread("Audio Capture", audio_thread_.native_handle());
    }

    // Main Loop
    run_main_loop(tui.get());

    // Shutdown
    spdlog::info("Application stopping...");
    if (tui) {
        spdlog::debug("Stopping TUI...");
        tui->stop();
    }
    if (tui_thread.joinable()) {
        spdlog::debug("Joining TUI thread...");
        tui_thread.join();
        spdlog::debug("TUI thread joined.");
    }

    // Stop Queues to unblock threads
    spdlog::debug("Stopping queues...");
    audio_queue_.stop();
    inference_queue_.stop();

    if (vad_thread_.joinable()) {
        spdlog::debug("Joining VAD thread...");
        vad_thread_.join();
        spdlog::debug("VAD thread joined.");
    }
    if (stt_thread_.joinable()) {
        spdlog::debug("Joining STT thread...");
        stt_thread_.join();
        spdlog::debug("STT thread joined.");
    }
    if (audio_thread_.joinable()) {
        spdlog::debug("Joining Audio thread...");
        audio_thread_.join();
        spdlog::debug("Audio thread joined.");
    }

    // Stop IPC handler
    if (ipc_handler_) {
        spdlog::debug("Stopping IPC handler...");
        ipc_handler_->stop();
    }

    spdlog::info("Shutdown complete.");
    return 0;
}

void Application::print_startup_banner() {
    if (config_.use_ui) {
        spdlog::info("Real-time Subtitler Started (UI Mode)");
        spdlog::info("Version: {}", core::kVersionString);
        spdlog::info("Git Commit: {}", core::kGitDescribe);
    } else {
        spdlog::info("Real-Time Audio-to-Subtitles {} (Multi-Threaded)", core::kVersionString);
        spdlog::info("Git Commit: {}", core::kGitDescribe);
        spdlog::info("Press Ctrl+C to stop gracefully.");
        spdlog::info("Config: VAD Model={}, STT Model={}, Threads={}, GPU={}", config_.vad_model_path, config_.stt_model_path, config_.n_threads,
                     config_.use_gpu ? "ON" : "OFF");
    }
}

void Application::handle_cli_device_selection() const {
    auto capture_result = audio::AudioCapture::create();
    if (!capture_result) {
        std::cerr << "Error creating AudioCapture: " << capture_result.error() << "\n";
        return;
    }
    auto& audio_capture = *capture_result;
    auto devices_result = audio_capture->list_devices();
    if (!devices_result) {
        std::cerr << "Error listing devices: " << devices_result.error() << "\n";
        return;
    }
    auto& devices = *devices_result;

    if (config_.json_output) {
        output::JsonOutput::print_devices(devices);
        return;
    }

    std::cout << "\nAvailable Audio Devices:\n-------------------------\n";
    for (const auto& device : devices) {
        std::cout << "  [" << device.index << "] " << device.name << " (Channels: " << device.max_input_channels
                  << ", Rate: " << device.default_sample_rate << ")" << (device.is_default ? " [DEFAULT]" : "")
                  << (device.is_loopback ? " [Loopback]" : "") << "\n";
    }
    std::cout << "\nUse --device <index> to select a device.\n";
}

void Application::initialize_ui_state(ui::TuiRenderer* tui) const {
    if (tui == nullptr) {
        return;
    }

    // Populate Device List
    auto temp_result = audio::AudioCapture::create();
    std::vector<ui::DeviceItem> ui_devices;
    if (temp_result) {
        auto devices_result = (*temp_result)->list_devices();
        if (devices_result) {
            for (const auto& device : *devices_result) {
                if (device.max_input_channels > 0 || device.is_loopback) {
                    ui_devices.push_back({.id = device.index,
                                          .name = "[" + std::to_string(device.index) + "] " + device.name,
                                          .channels = device.max_input_channels,
                                          .sample_rate = static_cast<int>(device.default_sample_rate)});
                }
            }
        }
    }
    tui->update_state([&](ui::AppState& state) {
        state.is_running = true;
        state.device_name = "Initializing...";
        state.available_devices = ui_devices;
    });

    // Push initial config to UI
    ui::SettingsState init_settings;
    init_settings.vad_threshold = config_.vad_threshold;
    init_settings.vad_energy_thresh = config_.vad_energy_threshold;
    init_settings.vad_smoothing = config_.vad_smoothing_alpha;
    init_settings.vad_hangover = config_.vad_hangover_frames;
    init_settings.vad_adaptive = config_.vad_adaptive_threshold;
    init_settings.vad_adaptive_min = config_.vad_adaptive_min_threshold;
    init_settings.vad_adaptive_max = config_.vad_adaptive_max_threshold;
    init_settings.vad_adaptive_alpha = config_.vad_adaptive_alpha;

    init_settings.stt_threads = config_.n_threads;
    init_settings.stt_language = config_.language;
    init_settings.stt_use_gpu = config_.use_gpu;
    init_settings.stt_flash_attn = config_.flash_attn;
    init_settings.stt_token_dedup = config_.stt_token_dedup;
    init_settings.stt_step_ms = config_.stt_step_ms;
    init_settings.stt_keep_ms = config_.stt_keep_ms;
    init_settings.stt_min_repetition = config_.stt_min_repetition_len;
    init_settings.stt_hallucination_len = config_.stt_hallucination_min_len;

    init_settings.input_gain = 1.0F;  // Default gain

    tui->set_settings(init_settings);
}

void Application::setup_ui_callbacks(ui::TuiRenderer* tui) {
    if (tui == nullptr) {
        return;
    }

    tui->set_on_device_selected([this](int device_id) { pending_device_switch_ = device_id; });

    // VAD callbacks now update atomic config values
    tui->set_on_threshold_changed([this](float threshold) {
        spdlog::info("VAD Threshold set to {:.2f}", threshold);
        vad_threshold_.store(threshold);
    });
    tui->set_on_gain_changed([this](float gain) { pending_gain_.store(gain); });
    tui->set_on_energy_changed([this](float val) { vad_energy_threshold_.store(val); });
    tui->set_on_smoothing_changed([this](float val) { vad_smoothing_alpha_.store(val); });

    tui->set_on_vad_adaptive_changed([this](bool enabled, float min_t, float max_t, float alpha) {
        vad_adaptive_enabled_.store(enabled);
        vad_adaptive_min_.store(min_t);
        vad_adaptive_max_.store(max_t);
        vad_adaptive_alpha_.store(alpha);
    });

    // STT Config Callbacks
    tui->set_on_stt_heuristics_changed([this](int rep_len, int hal_len) {
        stt_min_repetition_len_.store(rep_len);
        stt_hallucination_len_.store(hal_len);
    });
    tui->set_on_stt_params_changed([this](int threads, std::string lang) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.n_threads = threads;
        config_.language = lang;
        stt_reload_requested_ = true;
        spdlog::info("STT Config Changed: Threads={}, Lang={}. Reload flagged.", threads, lang);
    });

    tui->set_on_stt_gpu_changed([this](bool gpu) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.use_gpu = gpu;
        stt_reload_requested_ = true;
        spdlog::info("STT GPU Toggled: {}. Reload flagged.", gpu);
    });
}

void Application::perform_device_scan_and_select() {
    std::cout << "\nScanning devices... (Audio continuing)\n";
    auto temp_result = audio::AudioCapture::create();
    if (!temp_result) {
        std::cout << "Error: " << temp_result.error() << "\n";
        return;
    }

    auto devices_result = (*temp_result)->list_devices();
    if (!devices_result) {
        std::cout << "Error: " << devices_result.error() << "\n";
        return;
    }

    int idx = 1;
    std::vector<int> map_internal{-1};

    for (const auto& device : *devices_result) {
        if (device.max_input_channels > 0 || device.is_loopback) {
            std::cout << "[" << idx << "] " << device.name << "\n";
            map_internal.push_back(device.index);
            idx++;
        }
    }

    std::cout << "Select Device Index (1-" << (idx - 1) << ") or 'q' to cancel: ";
    std::string input_line;
    if (std::cin >> input_line) {
        if (input_line != "q" && input_line != "Q") {
            try {
                int selection = std::stoi(input_line);
                if (selection > 0 && selection < idx) {
                    pending_device_switch_ = map_internal[selection];
                }
            } catch (...) {
                spdlog::warn("Invalid device selection input");
            }
        }
    }
    // Helper to clear stream
    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
    std::cin.clear();
}

void Application::handle_cli_input(ui::TuiRenderer* tui) {
    if (tui != nullptr || _kbhit() == 0) {
        return;
    }

    int key = _getch();
    if (key == 'q') {
        running_ = false;
        return;
    }

    if (key == 'd') {
        perform_device_scan_and_select();
    }
}

void Application::run_main_loop(ui::TuiRenderer* tui) {
    while (running_) {
        // TUI/Signal Check
        if (!utils::SignalHandler::is_running() || ((tui != nullptr) && !tui->is_running())) {
            running_ = false;
        }

        // CLI Input
        handle_cli_input(tui);

        std::this_thread::sleep_for(std::chrono::milliseconds(core::app_constants::MAIN_LOOP_SLEEP_MS));
    }
}

void Application::audio_loop(ui::TuiRenderer* tui) {
    spdlog::info("Audio Worker Thread started.");

    std::unique_ptr<audio::AudioCapture> audio_capture;
    std::unique_ptr<audio::AudioProcessor> audio_processor;

    if (!initialize_audio_system(audio_capture, audio_processor, tui)) {
        return;
    }

    while (running_) {
        handle_audio_device_switch(audio_capture, audio_processor, tui);

        // Apply Gain
        // Apply Gain
        float current_gain = pending_gain_.load();
        if (std::abs(audio_capture->get_gain() - current_gain) > core::app_constants::GAIN_EPSILON) {
            audio_capture->set_gain(current_gain);
        }

        // Reads 32ms chunks
        // Adjust read size based on capture rate
        // We target kVadFrameSize (512) samples at 16kHz
        double ratio = static_cast<double>(audio_capture->sample_rate()) / core::audio_constants::SAMPLE_RATE;
        auto target_samples = static_cast<size_t>(core::audio_constants::FRAME_SIZE * ratio * audio_capture->channels());

        auto chunk = audio_capture->read_chunk(target_samples);
        if (!chunk.data.empty() && audio_processor) {
            auto processed = audio_processor->process(chunk.data);  // Resample/Downmix
            if (!processed.empty()) {
                // Forward original timestamp with processed data
                audio_queue_.push({.data = std::move(processed), .capture_time = chunk.capture_time});
            }
        } else {
            // Buffer underrun or wait
            std::this_thread::sleep_for(std::chrono::milliseconds(core::app_constants::AUDIO_WAIT_MS));
        }
    }

    audio_capture->stop();
    spdlog::info("Audio Worker Thread stopped.");
}
void Application::vad_loop(ui::TuiRenderer* tui) {
    spdlog::debug("VAD Worker Thread started.");

    std::unique_ptr<vad::VadProcessor> vad;

    // Initial init
    if (!initialize_vad_processor(vad)) {
        spdlog::error("VAD initialization failed, exiting vad_loop");
        return;
    }

    std::vector<float> vad_buffer;
    std::chrono::steady_clock::time_point buffer_start_time;
    bool has_timestamp = false;
    bool was_speech = false;

    // Metrics
    auto& metrics = metrics_;

    while (running_) {
        // Check for reload request
        if (vad_reload_requested_.exchange(false)) {
            spdlog::warn("Reloading VAD Processor due to config change...");
            initialize_vad_processor(vad);
        }

        // Apply live parameter updates (no reload needed)
        update_vad_parameters(vad.get());

        // Blocking pop
        auto chunk_opt = audio_queue_.pop();
        if (!chunk_opt) {
            break;  // Queue stopped/empty
        }

        auto& chunk = *chunk_opt;

        metrics.on_audio_chunk(chunk.data.size());

        // If this is the start of a new contiguous block, grab timestamp
        if (!has_timestamp && !chunk.data.empty()) {
            buffer_start_time = chunk.capture_time;
            has_timestamp = true;
        }

        vad_buffer.insert(vad_buffer.end(), chunk.data.begin(), chunk.data.end());

        // Pass-through RMS for UI immediately
        if ((tui != nullptr) && !chunk.data.empty()) {
            float sum_sq = 0.0F;
            for (float sample : chunk.data) {
                sum_sq += sample * sample;
            }
            float rms = std::sqrt(sum_sq / static_cast<float>(chunk.data.size()));
            tui->update_state([rms](ui::AppState& state) { state.vad_energy = rms * 100.0F; });
        }

            while (vad_buffer.size() >= static_cast<size_t>(core::audio_constants::FRAME_SIZE)) {
            std::vector<float> frame(vad_buffer.begin(), vad_buffer.begin() + core::audio_constants::FRAME_SIZE);
            vad_buffer.erase(vad_buffer.begin(), vad_buffer.begin() + core::audio_constants::FRAME_SIZE);

            auto frame_time = buffer_start_time;
            buffer_start_time += std::chrono::milliseconds(core::audio_constants::FRAME_DURATION_MS);

            float raw = 0.0F;
            float smoothed = 0.0F;
            metrics.on_vad_processing_start();
            auto process_result = vad->process(frame, raw, smoothed);
            metrics.on_vad_processing_end();

            if (!process_result) {
                spdlog::error("VAD process failed: {}", process_result.error());
                continue;
            }
            bool is_speech = *process_result;

            if (tui != nullptr) {
                tui->update_state([smoothed, is_speech](ui::AppState& state) {
                    state.vad_probability = smoothed;
                    state.is_speech = is_speech;
                });
            }

            // VAD State Machine
            handle_vad_speech_state(is_speech, was_speech, frame, frame_time, vad.get());
        }

        if (vad_buffer.empty()) {
            has_timestamp = false;
        }
    }
    spdlog::debug("VAD Worker Thread stopped.");
}

// -------------------------------------------------------------------------
// Helper Implementations
// -------------------------------------------------------------------------

auto Application::resolve_device_name(const std::vector<audio::AudioDevice>& devices, int index) -> std::string {
    if (index == -1) {
        for (const auto& device : devices) {
            if (device.is_default) {
                return device.name;
            }
        }
    } else {
        for (const auto& device : devices) {
            if (device.index == index) {
                return device.name;
            }
        }
    }
    return "Unknown";
}

void Application::handle_vad_speech_state(bool is_speech, bool& was_speech, const std::vector<float>& frame,
                                          std::chrono::steady_clock::time_point frame_time, vad::VadProcessor* vad) {
    if (is_speech) {
        if (!was_speech) {
            spdlog::info("Speech started...");
            const auto& pre_roll = vad->get_pre_roll_buffer();
            auto pre_roll_time = frame_time - std::chrono::milliseconds(core::audio_constants::FRAME_DURATION_MS * pre_roll.size());

            for (const auto& pre_frame : pre_roll) {
                inference_queue_.push({.data = pre_frame, .capture_time = pre_roll_time});
                pre_roll_time += std::chrono::milliseconds(core::audio_constants::FRAME_DURATION_MS);
            }
        }

        inference_queue_.push({.data = frame, .capture_time = frame_time});
        was_speech = true;

    } else {
        if (was_speech) {
            spdlog::info("Speech ended. Finalizing...");
            inference_queue_.push({.data = {}, .capture_time = frame_time});  // Sentinel
            was_speech = false;
        }
    }
}



auto Application::initialize_audio_system(std::unique_ptr<audio::AudioCapture>& capture,
                                          std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui) const
    -> bool {
    auto capture_result = audio::AudioCapture::create();
    if (!capture_result) {
        spdlog::error("Failed to create AudioCapture: {}", capture_result.error());
        if (tui != nullptr) {
            tui->update_state([](ui::AppState& state) { state.device_name = "Error: Audio Init Failed"; });
        }
        return false;
    }
    capture = std::move(*capture_result);

    auto start_result = capture->start(config_.device_index);
    if (!start_result) {
        spdlog::error("Audio Start Failed: {}", start_result.error());
        if (tui != nullptr) {
            tui->update_state([](ui::AppState& state) { state.device_name = "Error: Audio Start Failed"; });
        }
        return false;
    }

    // Find actual device name and emit device_selected
    std::string device_label = "Unknown";
    bool is_default = (config_.device_index == -1);
    int actual_index = config_.device_index;
    auto devs_result = capture->list_devices();
    if (devs_result) {
        device_label = resolve_device_name(*devs_result, config_.device_index);
        // Find actual index if using default
        if (is_default) {
            for (const auto& dev : *devs_result) {
                if (dev.is_default) {
                    actual_index = dev.index;
                    break;
                }
            }
        }
    }
    
    if (tui != nullptr) {
        tui->update_state([device_label](ui::AppState& state) {
            state.device_name = device_label;
            state.is_recording = true;
        });
    }
    
    // Emit device_selected for JSON mode
    if (config_.json_output) {
        output::JsonOutput::print_device_selected(actual_index, device_label, is_default);
        output::JsonOutput::print_status("listening");
    }

    auto proc_result = audio::AudioProcessor::create(capture->sample_rate(), capture->channels(), core::audio_constants::SAMPLE_RATE);
    if (!proc_result) {
        spdlog::error("Failed to create AudioProcessor: {}", proc_result.error());
        if (tui != nullptr) {
            tui->update_state([](ui::AppState& state) { state.device_name = "Error: Processor Init Failed"; });
        }
        return false;
    }
    processor = std::move(*proc_result);
    return true;
}

void Application::handle_audio_device_switch(std::unique_ptr<audio::AudioCapture>& capture,
                                             std::unique_ptr<audio::AudioProcessor>& processor, ui::TuiRenderer* tui) {
    int new_device_id = pending_device_switch_.exchange(-1);
    if (new_device_id == -1) {
        return;
    }

    spdlog::info("Switching Device to {}", new_device_id);
    capture->stop();
    auto switch_result = capture->start(new_device_id);

    if (switch_result) {
        config_.device_index = new_device_id;

        auto proc_result = audio::AudioProcessor::create(capture->sample_rate(), capture->channels(), core::audio_constants::SAMPLE_RATE);
        if (proc_result) {
            processor = std::move(*proc_result);
        } else {
            spdlog::error("Failed to recreate AudioProcessor: {}", proc_result.error());
        }

        std::string new_name = "Device " + std::to_string(new_device_id);
        auto devs_result = capture->list_devices();
        if (devs_result) {
            for (const auto& device : *devs_result) {
                if (device.index == new_device_id) {
                    new_name = device.name;
                    break;
                }
            }
        }

        if (tui != nullptr) {
            tui->update_state([new_name](ui::AppState& state) { state.device_name = new_name; });
        }
        
        // Emit device_selected for JSON mode
        if (config_.json_output) {
            output::JsonOutput::print_device_selected(new_device_id, new_name, false);
        }
    } else {
        spdlog::error("Device switch failed: {}", switch_result.error());
    }
}

auto Application::initialize_vad_processor(std::unique_ptr<vad::VadProcessor>& vad) -> bool {
    spdlog::info("Initializing VAD Processor...");
    vad::VadConfig vad_config;
    vad_config.threshold = vad_threshold_.load();
    vad_config.energy_threshold = vad_energy_threshold_.load();
    vad_config.smoothing_alpha = vad_smoothing_alpha_.load();
    vad_config.hangover_frames = config_.vad_hangover_frames;
    vad_config.pre_roll_frames = config_.vad_pre_roll_frames;
    vad_config.adaptive_enabled = config_.vad_adaptive_threshold;

    // Adaptive Params
    vad_config.adaptive_min_threshold = config_.vad_adaptive_min_threshold;
    vad_config.adaptive_max_threshold = config_.vad_adaptive_max_threshold;
    vad_config.adaptive_alpha = config_.vad_adaptive_alpha;

    auto result = vad::VadProcessor::create(config_.vad_model_path, core::audio_constants::SAMPLE_RATE, core::audio_constants::FRAME_SIZE, vad_config);
    if (!result) {
        spdlog::error("Failed to initialize VAD: {}", result.error());
        return false;
    }
    vad = std::move(*result);
    spdlog::info("VAD Initialized: Threshold={:.2f}, Energy={:.4f}", vad_config.threshold, vad_config.energy_threshold);
    return true;
}

void Application::update_vad_parameters(vad::VadProcessor* vad) {
    if (vad == nullptr) {
        return;
    }

    vad->set_threshold(vad_threshold_.load());
    vad->set_energy_threshold(vad_energy_threshold_.load());
    vad->set_smoothing_alpha(vad_smoothing_alpha_.load());

    vad->set_adaptive_enabled(vad_adaptive_enabled_.load());
    vad->set_adaptive_params(vad_adaptive_min_.load(), vad_adaptive_max_.load(), vad_adaptive_alpha_.load());
}

void Application::stt_loop(ui::TuiRenderer* tui) {
    spdlog::debug("STT Worker Thread started.");

    // Helper to Initialize Engine stack
    // We use unique_ptr for easy replacement
    std::unique_ptr<stt::SttEngine> stt_engine;
    std::unique_ptr<stt::StreamingTranscriber> transcriber;
    stt::TranscriberConfig stream_config;

    auto init_stack = [&]() -> bool {
        spdlog::info("Initializing STT Engine...");
        stt::SttConfig stt_config;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            stt_config.model_path = config_.stt_model_path;
            stt_config.language = config_.language;
            stt_config.n_threads = config_.n_threads;
            stt_config.use_gpu = config_.use_gpu;
            stt_config.flash_attn = config_.flash_attn;

            // STT Quality parameters (Full Parity)
            stt_config.beam_size = config_.stt_beam_size;
            stt_config.max_tokens = config_.stt_max_tokens;
            stt_config.audio_ctx = config_.stt_audio_ctx;
            stt_config.no_speech_threshold = config_.stt_no_speech_threshold;
            stt_config.entropy_threshold = config_.stt_entropy_threshold;
            stt_config.logprob_threshold = config_.stt_logprob_threshold; // Config parity
            
            stt_config.temperature = config_.stt_temperature;
            stt_config.temperature_inc = config_.stt_temperature_inc;
            stt_config.no_fallback = config_.stt_no_fallback;
            stt_config.suppress_blank = config_.stt_suppress_blank;
            stt_config.suppress_nst = config_.stt_suppress_nst;
            stt_config.no_context = config_.stt_no_context; 

            // Update stream config from main config
            stream_config.step_ms = config_.stt_step_ms;
            stream_config.keep_ms = config_.stt_keep_ms;
            stream_config.max_length_ms = config_.stt_max_length_ms;
            stream_config.token_dedup = config_.stt_token_dedup;
            stream_config.timestamp_merge = config_.transcriber_timestamp_merge; // New field
            stream_config.min_repetition_len = config_.stt_min_repetition_len;
            stream_config.hallucination_min_len = config_.stt_hallucination_min_len;
            stream_config.hallucination_min_len = config_.stt_hallucination_min_len;
            stream_config.hallucination_blacklist = config_.stt_blacklist;
            
            // Smart Filter
            stream_config.suspicious_phrases = config_.stt_suspicious_phrases;
            stream_config.suspicious_no_speech_threshold = config_.stt_suspicious_no_speech_threshold;
            stream_config.suspicious_confidence_threshold = config_.stt_suspicious_confidence_threshold;
        }

        auto engine_result = stt::SttEngine::create(stt_config);
        if (!engine_result) {
            spdlog::error("Failed to create SttEngine: {}", engine_result.error());
            return false;
        }
        stt_engine = std::move(*engine_result);
        transcriber = std::make_unique<stt::StreamingTranscriber>(*stt_engine, stream_config);
        spdlog::info("STT Configured: Threads={}, GPU={}, Lang={}, BeamSize={}", stt_config.n_threads, stt_config.use_gpu,
                     stt_config.language, stt_config.beam_size);
        return true;
    };

    // Initial Init
    if (!init_stack()) {
        spdlog::error("STT initialization failed, exiting inference_loop");
        return;
    }

    auto& metrics = metrics_;

    while (running_) {
        // Check for Reload Request
        if (stt_reload_requested_) {
            spdlog::warn("Reloading STT Engine due to config change...");
            if (!init_stack()) {
                spdlog::error("Failed to reload STT Engine");
            }
            stt_reload_requested_ = false;
        }

        // Blocking pop
        // Note: If user changes config while idle, reload happens on NEXT audio packet.
        auto chunk_opt = inference_queue_.pop();
        if (!chunk_opt) {
            break;
        }

        auto& chunk = *chunk_opt;  // AudioChunk

        if (!transcriber) {
            continue;
        }

        // Live Config Update for Heuristics
        stt::TranscriberConfig current_cfg = stream_config;
        current_cfg.min_repetition_len = stt_min_repetition_len_.load();
        current_cfg.hallucination_min_len = stt_hallucination_len_.load();
        transcriber->set_config(current_cfg);

        process_inference_chunk(chunk, transcriber.get(), tui, metrics, config_.json_output);
    }
    spdlog::debug("STT Worker Thread stopped.");
}

void Application::process_inference_chunk(const core::AudioChunk& chunk, stt::StreamingTranscriber* transcriber,
                                          ui::TuiRenderer* tui, core::MetricsCollector& metrics, bool json_output) {
    if (chunk.data.empty()) {
        // Finalize
        auto final_seg = transcriber->finalize();
        if (!final_seg.text.empty()) {
            if (tui != nullptr) {
                tui->update_state([final_seg](ui::AppState& state) {
                    state.partial_transcript = "";
                    state.subtitles.push_back({.text = final_seg.text, .confidence = 1.0F, .is_final = true, .timestamp = "Now"});
                });
            } else if (json_output) {
                output::JsonOutput::print_final(final_seg.text);
            } else {
                std::cout << "\n[FINAL] " << final_seg.text << "\n";
            }
        } else {
            if (tui != nullptr) {
                tui->update_state([](ui::AppState& state) { state.partial_transcript = ""; });
            }
        }
        return;
    }

    transcriber->push_audio(chunk.data);

    if (transcriber->should_transcribe()) {
        metrics.on_inference_start();
        auto seg = transcriber->process();
        metrics.on_inference_end(static_cast<int>(seg.text.length()));

        if (!seg.text.empty()) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double, std::milli> latency = now - chunk.capture_time;
            metrics.record_pipeline_latency(latency.count());

            if (tui != nullptr) {
                tui->update_state([seg](ui::AppState& state) { state.partial_transcript = seg.text; });
            } else if (json_output) {
                output::JsonOutput::print_partial(seg.text);
            } else {
                std::cout << "\r[Partial] " << seg.text << std::flush;
            }
        }
    }
}

// -------------------------------------------------------------------------
// IPC Handler Implementation
// -------------------------------------------------------------------------

void Application::setup_ipc_handler() {
    if (!config_.json_output) {
        return;
    }
    
    ipc_handler_ = std::make_unique<ipc::IpcHandler>();
    ipc_handler_->start([this](const ipc::IpcCommand& cmd) {
        handle_ipc_command(cmd);
    });
}

void Application::handle_ipc_command(const ipc::IpcCommand& cmd) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, ipc::SelectDeviceCmd>) {
            spdlog::info("IPC: select_device {}", arg.index);
            pending_device_switch_.store(arg.index);
        }
        else if constexpr (std::is_same_v<T, ipc::GetDevicesCmd>) {
            spdlog::debug("IPC: get_devices");
            emit_device_list();
        }
        else if constexpr (std::is_same_v<T, ipc::StopCmd>) {
            spdlog::info("IPC: stop");
            running_.store(false);
        }
        else if constexpr (std::is_same_v<T, ipc::SetConfigCmd>) {
            spdlog::info("IPC: set_config {}={}", arg.key, 
                std::visit([](auto&& v) -> std::string {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<V, std::string>) return v;
                    else if constexpr (std::is_same_v<V, bool>) return v ? "true" : "false";
                    else return std::to_string(v);
                }, arg.value));
            
            // Handle config updates
            if (arg.key == "vad_threshold") {
                if (auto* val = std::get_if<float>(&arg.value)) {
                    vad_threshold_.store(*val);
                }
            } else if (arg.key == "vad_energy_threshold") {
                if (auto* val = std::get_if<float>(&arg.value)) {
                    vad_energy_threshold_.store(*val);
                }
            }
            // Add more config keys as needed
        }
    }, cmd);
}

void Application::emit_device_list() {
    auto capture_result = audio::AudioCapture::create();
    if (!capture_result) {
        output::JsonOutput::print_error("device_error", capture_result.error());
        return;
    }
    
    auto devices_result = (*capture_result)->list_devices();
    if (!devices_result) {
        output::JsonOutput::print_error("device_error", devices_result.error());
        return;
    }
    
    output::JsonOutput::print_devices(*devices_result);
}
