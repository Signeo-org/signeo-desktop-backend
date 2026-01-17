// Windows.h must be included BEFORE onnxruntime headers to avoid macro conflicts
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "output/logging.hpp"
#include "vad/vad_processor.hpp"

namespace vad {

// Factory Method
core::Result<std::unique_ptr<VadProcessor>> VadProcessor::create(const std::string& model_path, int sample_rate,
                                                                 int frame_size, const VadConfig& config) {
    LOG_SCOPED_TRACE();
    spdlog::debug("VadProcessor::create() model_path={}", model_path);

    // Use unique_ptr with custom deleter pattern for exception safety
    std::unique_ptr<VadProcessor> processor(new VadProcessor(sample_rate, frame_size, config));

    auto init_result = processor->init_session(model_path);
    if (!init_result) {
        return std::unexpected(init_result.error());
    }

    processor->reset_states();

    spdlog::info("VadProcessor initialized [ADVANCED MODE]");
    spdlog::info("  Rate: {}, Frame: {} samples ({}ms)", sample_rate, frame_size, frame_size * 1000 / sample_rate);
    spdlog::info("  Threshold: {:.2f}, Energy: {:.4f}", config.threshold, config.energy_threshold);
    spdlog::info("  Smoothing α: {:.2f}, Hangover: {} frames, Pre-roll: {} frames", config.smoothing_alpha,
                 config.hangover_frames, config.pre_roll_frames);
    spdlog::info("  Adaptive thresholds: {}", config.adaptive_enabled ? "enabled" : "disabled");

    return processor;
}

// Private Constructor
VadProcessor::VadProcessor(int sample_rate, int frame_size, const VadConfig& config)
    : sample_rate_(sample_rate)
    , window_size_samples_(frame_size)
    , config_(config)
    , adaptive_threshold_(config.threshold) {
    effective_window_size_ = window_size_samples_ + context_samples_;

    input_node_dims_[0] = 1;
    input_node_dims_[1] = effective_window_size_;

    _state.resize(2 * 1 * 128);
    _context.assign(context_samples_, 0.0f);
    _sr.resize(1);
    _sr[0] = sample_rate;

    // Pre-allocate input buffer
    _input_buffer.resize(effective_window_size_);
}

VadProcessor::~VadProcessor() {
    spdlog::debug("VadProcessor: Destructor called, cleaning up...");
    session.reset();
    spdlog::debug("VadProcessor: Cleanup complete.");
}

core::Status VadProcessor::init_session(const std::string& model_path) {
    LOG_SCOPED_TRACE();
    spdlog::debug("init_session() loading model from {}", model_path);

    try {
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const char* model_path_cstr = nullptr;
#ifdef _WIN32
        // Use Windows API for UTF-8 to wide string conversion
        int size_needed =
            MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), static_cast<int>(model_path.size()), nullptr, 0);
        std::wstring w_model_path(static_cast<size_t>(size_needed), 0);
        MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), static_cast<int>(model_path.size()), &w_model_path[0],
                            size_needed);

        session = std::make_unique<Ort::Session>(env, w_model_path.c_str(), session_options);
#else
        session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
#endif
        spdlog::debug("VadProcessor::init_session() ONNX session created successfully");
        return {};

    } catch (const std::exception& e) {
        return core::log_error(std::format("Failed to initialize ONNX session: {}", e.what()));
    }
}

void VadProcessor::reset_states() {
    std::fill(_state.begin(), _state.end(), 0.0f);
    std::fill(_context.begin(), _context.end(), 0.0f);
    smoothed_prob_ = 0.0f;
    noise_floor_ = 0.1f;
    adaptive_threshold_ = config_.threshold;
    hangover_counter_ = 0;
    is_speaking_ = false;
    pre_roll_buffer_.clear();
}

void VadProcessor::reset() {
    reset_states();
    spdlog::debug("VadProcessor state reset.");
}

float VadProcessor::calculate_rms(const std::vector<float>& frame) {
    if (frame.empty())
        return 0.0f;
    float sum_squares = 0.0f;
    for (float x : frame) {
        sum_squares += x * x;
    }
    return std::sqrt(sum_squares / static_cast<float>(frame.size()));
}

float VadProcessor::run_inference(const std::vector<float>& frame) {
    // Prepare input with context (using pre-allocated buffer)
    std::copy(_context.begin(), _context.end(), _input_buffer.begin());
    std::copy(frame.begin(), frame.end(), _input_buffer.begin() + context_samples_);

    // Create tensors
    Ort::Value input_ort =
        Ort::Value::CreateTensor<float>(memory_info, _input_buffer.data(), _input_buffer.size(), input_node_dims_, 2);
    Ort::Value state_ort =
        Ort::Value::CreateTensor<float>(memory_info, _state.data(), _state.size(), state_node_dims_, 3);
    Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(memory_info, _sr.data(), _sr.size(), sr_node_dims_, 1);

    std::vector<Ort::Value> ort_inputs;
    ort_inputs.push_back(std::move(input_ort));
    ort_inputs.push_back(std::move(state_ort));
    ort_inputs.push_back(std::move(sr_ort));

    // Run inference
    auto ort_outputs = session->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), ort_inputs.data(),
                                    ort_inputs.size(), output_node_names_.data(), output_node_names_.size());

    float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];

    // Update state for next frame
    float* stateN = ort_outputs[1].GetTensorMutableData<float>();
    std::copy(stateN, stateN + _state.size(), _state.begin());
    std::copy(_input_buffer.end() - context_samples_, _input_buffer.end(), _context.begin());

    return speech_prob;
}

core::Result<bool> VadProcessor::process(const std::vector<float>& frame, float& raw_probability,
                                         float& smoothed_probability) {
    if (frame.size() != static_cast<size_t>(window_size_samples_)) {
        return core::log_error(
            std::format("VadProcessor frame size mismatch. Expected {}, got {}", window_size_samples_, frame.size()));
    }

    // === STAGE 1: RMS Energy Gate ===
    float rms = calculate_rms(frame);
    if (!check_energy_gate(rms)) {
        raw_probability = 0.0f;
        smoothed_probability = smoothed_prob_; // check_energy_gate updates smoothed_prob_
        return false;
    }

    // === STAGE 2: Neural VAD Inference ===
    raw_probability = run_inference(frame);

    // === STAGE 3 & 4: Smoothing & Adaptive Threshold ===
    update_probability_state(raw_probability, smoothed_probability);

    // === STAGE 5: Speech Detection with Hangover ===
    bool is_speech_now = smoothed_prob_ >= adaptive_threshold_;
    update_hangover(is_speech_now);

    // Determines effective speech state based on hangover
    bool effective_speech = is_speaking_ || (hangover_counter_ > 0);

    // === STAGE 6: Pre-Roll Buffer Management ===
    update_pre_roll(frame);

    return effective_speech;
}

bool VadProcessor::check_energy_gate(float rms) {
    if (rms < config_.energy_threshold) {
        // Decay smoothed probability towards 0
        smoothed_prob_ = config_.smoothing_alpha * 0.0f + (1.0f - config_.smoothing_alpha) * smoothed_prob_;

        if (config_.adaptive_enabled) {
            // Slowly decay noise floor
            noise_floor_ = config_.adaptive_alpha * noise_floor_ + (1.0f - config_.adaptive_alpha) * smoothed_prob_;
        }

        // Handle hangover decay if we early exit
        if (hangover_counter_ > 0) {
            hangover_counter_--;
            // If still in hangover, we don't return false immediately check in process
            // Wait, the original code returned true if hangover > 0
            // But here we are just checking the gate.
            // If gate is closed (low energy), we normally say "no speech input",
            // BUT hangover could keep it alive.
        } else {
            is_speaking_ = false;
        }
        return false; // Gate closed
    }
    return true; // Gate open
}

void VadProcessor::update_probability_state(float raw_probability, float& smoothed_probability) {
    // EMA Smoothing
    smoothed_prob_ = config_.smoothing_alpha * raw_probability + (1.0f - config_.smoothing_alpha) * smoothed_prob_;
    smoothed_probability = smoothed_prob_;

    // Adaptive Threshold
    if (config_.adaptive_enabled) {
        adaptive_threshold_ = update_adaptive_threshold(raw_probability);
    } else {
        adaptive_threshold_ = config_.threshold;
    }
}

void VadProcessor::update_pre_roll(const std::vector<float>& frame) {
    pre_roll_buffer_.push_back(frame);
    while (pre_roll_buffer_.size() > static_cast<size_t>(config_.pre_roll_frames)) {
        pre_roll_buffer_.pop_front();
    }
}

float VadProcessor::update_adaptive_threshold(float raw_probability) {
    if (!is_speaking_) {
        // Update noise floor with configurable alpha
        noise_floor_ = config_.adaptive_alpha * noise_floor_ + (1.0f - config_.adaptive_alpha) * raw_probability;

        // Clamp threshold between min/max config
        return std::max(config_.adaptive_min_threshold, std::min(config_.adaptive_max_threshold, noise_floor_ + 0.25f));
    }
    return adaptive_threshold_; // Keep existing if speaking
}

const std::deque<std::vector<float>>& VadProcessor::get_pre_roll_buffer() const { return pre_roll_buffer_; }

void VadProcessor::consume_pre_roll() { pre_roll_buffer_.clear(); }

// Runtime Tuning
void VadProcessor::set_threshold(float threshold) { config_.threshold = threshold; }

float VadProcessor::get_threshold() const { return config_.threshold; }

void VadProcessor::set_energy_threshold(float val) { config_.energy_threshold = val; }

void VadProcessor::set_smoothing_alpha(float val) { config_.smoothing_alpha = val; }

void VadProcessor::set_adaptive_enabled(bool enabled) { config_.adaptive_enabled = enabled; }

void VadProcessor::set_adaptive_params(float min_th, float max_th, float alpha) {
    config_.adaptive_min_threshold = min_th;
    config_.adaptive_max_threshold = max_th;
    config_.adaptive_alpha = alpha;
}

float VadProcessor::get_adaptive_threshold() const { return adaptive_threshold_; }

float VadProcessor::get_noise_floor() const { return noise_floor_; }

void VadProcessor::update_hangover(bool is_speech_now) {
    if (is_speech_now) {
        hangover_counter_ = config_.hangover_frames;
    } else if (hangover_counter_ > 0) {
        hangover_counter_--;
    }
    is_speaking_ = is_speech_now || (hangover_counter_ > 0);
}

} // namespace vad
