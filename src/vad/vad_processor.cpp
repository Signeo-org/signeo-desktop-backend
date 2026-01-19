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

namespace {
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kStateBufferSize = 128;
constexpr int kPreRollBufferMultiplier = 128;
constexpr float kAdaptiveOffset = 0.25F;
}  // namespace

// Factory Method
auto VadProcessor::create(const std::string& model_path, int sample_rate, int frame_size, const VadConfig& config)
    -> core::Result<std::unique_ptr<VadProcessor>> {
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
    spdlog::info("  Rate: {}, Frame: {} samples ({}ms)", sample_rate, frame_size, frame_size * kMillisecondsPerSecond / sample_rate);
    spdlog::info("  Threshold: {:.2f}, Energy: {:.4f}", config.threshold, config.energy_threshold);
    spdlog::info("  Smoothing α: {:.2f}, Hangover: {} frames, Pre-roll: {} frames", config.smoothing_alpha,
                 config.hangover_frames, config.pre_roll_frames);
    spdlog::info("  Adaptive thresholds: {}", config.adaptive_enabled ? "enabled" : "disabled");

    return processor;
}

// Private Constructor
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
VadProcessor::VadProcessor(int sample_rate, int frame_size, const VadConfig& config)
    : sample_rate_(sample_rate)
    , window_size_samples_(frame_size)
    , effective_window_size_(frame_size + kContextSamples)
    , config_(config)
    , adaptive_threshold_(config.threshold) {

    input_node_dims_[0] = 1;
    input_node_dims_[1] = effective_window_size_;

    state_.resize(static_cast<size_t>(2) * kStateBufferSize);
    context_.assign(static_cast<size_t>(kContextSamples), 0.0F);
    sr_.resize(1);
    sr_[0] = sample_rate;

    // Pre-allocate input buffer
    input_buffer_.resize(effective_window_size_);
}

VadProcessor::~VadProcessor() {
    spdlog::debug("VadProcessor: Destructor called, cleaning up...");
    session_.reset();
    spdlog::debug("VadProcessor: Cleanup complete.");
}

auto VadProcessor::init_session(const std::string& model_path) -> core::Status {
    LOG_SCOPED_TRACE();
    spdlog::debug("init_session() loading model from {}", model_path);

    try {
        session_options_.SetIntraOpNumThreads(1);
        session_options_.SetInterOpNumThreads(1);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const char* model_path_cstr = nullptr;
#ifdef _WIN32
        // Use Windows API for UTF-8 to wide string conversion
        int size_needed =
            MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), static_cast<int>(model_path.size()), nullptr, 0);
        std::wstring w_model_path(static_cast<size_t>(size_needed), 0);
        MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), static_cast<int>(model_path.size()), w_model_path.data(),
                            size_needed);

        session_ = std::make_unique<Ort::Session>(env_, w_model_path.c_str(), session_options_);
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
    std::ranges::fill(state_, 0.0F);
    std::ranges::fill(context_, 0.0F);
    smoothed_prob_ = 0.0F;
    noise_floor_ = detail::kDefaultNoiseFloor;
    adaptive_threshold_ = config_.threshold;
    hangover_counter_ = 0;
    is_speaking_ = false;
    pre_roll_buffer_.clear();
}

void VadProcessor::reset() {
    reset_states();
    spdlog::debug("VadProcessor state reset.");
}

auto VadProcessor::calculate_rms(const std::vector<float>& frame) -> float {
    if (frame.empty()) {
        return 0.0F;
    }
    float sum_squares = 0.0F;
    for (float sample : frame) {
        sum_squares += sample * sample;
    }
    return std::sqrt(sum_squares / static_cast<float>(frame.size()));
}

auto VadProcessor::run_inference(const std::vector<float>& frame) -> float {
    // Prepare input with context (using pre-allocated buffer)
    std::ranges::copy(context_, input_buffer_.begin());
    std::ranges::copy(frame, input_buffer_.begin() + kContextSamples);

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    Ort::Value input_ort =
        Ort::Value::CreateTensor<float>(memory_info_, input_buffer_.data(), input_buffer_.size(), input_node_dims_.data(), 2);
    Ort::Value state_ort =
        Ort::Value::CreateTensor<float>(memory_info_, state_.data(), state_.size(), kStateNodeDims.data(), 3);
    Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(memory_info_, sr_.data(), sr_.size(), kSrNodeDims.data(), 1);
    // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

    std::vector<Ort::Value> ort_inputs;
    ort_inputs.push_back(std::move(input_ort));
    ort_inputs.push_back(std::move(state_ort));
    ort_inputs.push_back(std::move(sr_ort));

    // Run inference
    auto ort_outputs = session_->Run(Ort::RunOptions{nullptr}, input_node_names_.data(), ort_inputs.data(),
                                     ort_inputs.size(), output_node_names_.data(), output_node_names_.size());

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];

    // Update state for next frame
    auto* state_n = ort_outputs[1].GetTensorMutableData<float>();
    std::copy(state_n, state_n + state_.size(), state_.begin());
    std::copy(input_buffer_.end() - kContextSamples, input_buffer_.end(), context_.begin());
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    return speech_prob;
}

auto VadProcessor::process(const std::vector<float>& frame, float& raw_probability, float& smoothed_probability)
    -> core::Result<bool> {
    if (frame.size() != static_cast<size_t>(window_size_samples_)) {
        return core::log_error(
            std::format("VadProcessor frame size mismatch. Expected {}, got {}", window_size_samples_, frame.size()));
    }

    // === STAGE 1: RMS Energy Gate ===
    float rms = calculate_rms(frame);
    if (!check_energy_gate(rms)) {
        raw_probability = 0.0F;
        smoothed_probability = smoothed_prob_;  // check_energy_gate updates smoothed_prob_
        return 0;
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

    return static_cast<int>(effective_speech);
}

auto VadProcessor::check_energy_gate(float rms) -> bool {
    if (rms < config_.energy_threshold) {
        // Decay smoothed probability towards 0
        smoothed_prob_ = config_.smoothing_alpha * 0.0F + (1.0F - config_.smoothing_alpha) * smoothed_prob_;

        if (config_.adaptive_enabled) {
            // Slowly decay noise floor
            noise_floor_ = config_.adaptive_alpha * noise_floor_ + (1.0F - config_.adaptive_alpha) * smoothed_prob_;
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
        return false;  // Gate closed
    }
    return true;  // Gate open
}

void VadProcessor::update_probability_state(float raw_probability, float& smoothed_probability) {
    // EMA Smoothing
    smoothed_prob_ = config_.smoothing_alpha * raw_probability + (1.0F - config_.smoothing_alpha) * smoothed_prob_;
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

auto VadProcessor::update_adaptive_threshold(float raw_probability) -> float {
    if (!is_speaking_) {
        // Update noise floor with configurable alpha
        noise_floor_ = config_.adaptive_alpha * noise_floor_ + (1.0F - config_.adaptive_alpha) * raw_probability;

        // Clamp threshold between min/max config
        return std::max(config_.adaptive_min_threshold, std::min(config_.adaptive_max_threshold, noise_floor_ + kAdaptiveOffset));
    }
    return adaptive_threshold_;  // Keep existing if speaking
}

auto VadProcessor::get_pre_roll_buffer() const -> const std::deque<std::vector<float>>& {
    return pre_roll_buffer_;
}

void VadProcessor::consume_pre_roll() {
    pre_roll_buffer_.clear();
}

// Runtime Tuning
void VadProcessor::set_threshold(float threshold) {
    config_.threshold = threshold;
}

auto VadProcessor::get_threshold() const -> float {
    return config_.threshold;
}

void VadProcessor::set_energy_threshold(float val) {
    config_.energy_threshold = val;
}

void VadProcessor::set_smoothing_alpha(float val) {
    config_.smoothing_alpha = val;
}

void VadProcessor::set_adaptive_enabled(bool enabled) {
    config_.adaptive_enabled = enabled;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void VadProcessor::set_adaptive_params(float min_th, float max_th, float alpha) {
    config_.adaptive_min_threshold = min_th;
    config_.adaptive_max_threshold = max_th;
    config_.adaptive_alpha = alpha;
}

auto VadProcessor::get_adaptive_threshold() const -> float {
    return adaptive_threshold_;
}

auto VadProcessor::get_noise_floor() const -> float {
    return noise_floor_;
}

void VadProcessor::update_hangover(bool is_speech_now) {
    if (is_speech_now) {
        hangover_counter_ = config_.hangover_frames;
    } else if (hangover_counter_ > 0) {
        hangover_counter_--;
    }
    is_speaking_ = is_speech_now || (hangover_counter_ > 0);
}

}  // namespace vad
