#pragma once

/**
 * @file vad_processor.hpp
 * @brief State-of-the-Art Voice Activity Detection using Silero VAD
 */

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "../core/result.hpp"

// Suppress warnings from ONNX Runtime headers
#pragma warning(push)
#pragma warning(disable : 4100 4244 4267)
#include <onnxruntime_cxx_api.h>
#pragma warning(pop)

namespace vad {

/**
 * @brief Configuration for Voice Activity Detection
 */
struct VadConfig {
    float threshold = 0.5f;          ///< Base speech probability threshold
    float energy_threshold = 0.001f; ///< RMS gate threshold
    float smoothing_alpha = 0.3f;    ///< EMA smoothing (0.1=stable, 0.5=responsive)
    int hangover_frames = 8;         ///< Frames to extend after speech ends (~256ms)
    int pre_roll_frames = 6;         ///< Frames to include before trigger (~192ms)
    bool adaptive_enabled = true;    ///< Enable adaptive threshold

    // Adaptive Parameters
    float adaptive_min_threshold = 0.35f;
    float adaptive_max_threshold = 0.6f;
    float adaptive_alpha = 0.95f; // Noise floor update rate
};

class VadProcessor {
public:
    /**
     * @brief Factory method to create a VadProcessor
     * @param model_path Path to silero_vad.onnx
     * @param sample_rate Audio sample rate (default 16000)
     * @param frame_size Frame size in samples (default 512 = 32ms @ 16kHz)
     * @param config Advanced VAD configuration
     * @return Result containing unique_ptr to VadProcessor, or error message
     */
    static core::Result<std::unique_ptr<VadProcessor>> create(const std::string& model_path, int sample_rate = 16000,
                                                              int frame_size = 512,
                                                              const VadConfig& config = VadConfig{});

    ~VadProcessor();

    // Prevent copying
    VadProcessor(const VadProcessor&) = delete;
    VadProcessor& operator=(const VadProcessor&) = delete;

    /**
     * @brief Process a single frame of audio
     * @param frame Input audio data (must match frame_size)
     * @param raw_probability [out] Raw probability from neural network
     * @param smoothed_probability [out] Smoothed probability after EMA
     * @return Result<bool> - true if speech detected, or error message
     */
    core::Result<bool> process(const std::vector<float>& frame, float& raw_probability, float& smoothed_probability);

    /**
     * @brief Get pre-roll buffer (audio before speech trigger)
     */
    const std::deque<std::vector<float>>& get_pre_roll_buffer() const;

    /**
     * @brief Clear pre-roll buffer (call after consuming)
     */
    void consume_pre_roll();

    /**
     * @brief Reset all internal state
     */
    void reset();

    // Runtime Tuning
    void set_threshold(float threshold);
    float get_threshold() const;

    void set_energy_threshold(float val);
    void set_smoothing_alpha(float val);

    void set_adaptive_enabled(bool enabled);
    void set_adaptive_params(float min_th, float max_th, float alpha);

    float get_adaptive_threshold() const;
    float get_noise_floor() const;

private:
    // Private constructor - use create() factory
    VadProcessor(int sample_rate, int frame_size, const VadConfig& config);

    core::Status init_session(const std::string& model_path);
    void reset_states();
    float calculate_rms(const std::vector<float>& frame);
    float run_inference(const std::vector<float>& frame);
    float update_adaptive_threshold(float raw_probability);
    void update_hangover(bool is_speech_now);

    // Process helpers
    bool check_energy_gate(float rms);
    void update_probability_state(float raw_probability, float& smoothed_probability);
    void update_pre_roll(const std::vector<float>& frame);

    // ONNX Runtime Resources
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "VadProcessor"};
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

    // Silero Model State
    std::vector<float> _state;
    std::vector<float> _context;
    std::vector<int64_t> _sr;

    // Configuration
    int sample_rate_;
    int window_size_samples_;
    int effective_window_size_;
    const int context_samples_ = 64;
    VadConfig config_;

    // Advanced State
    float smoothed_prob_ = 0.0f;
    float noise_floor_ = 0.1f;
    float adaptive_threshold_;
    int hangover_counter_ = 0;
    bool is_speaking_ = false;
    std::deque<std::vector<float>> pre_roll_buffer_;

    // Tensor Shapes
    int64_t input_node_dims_[2] = {};
    const int64_t state_node_dims_[3] = {2, 1, 128};
    const int64_t sr_node_dims_[1] = {1};

    // Reusable buffers
    std::vector<float> _input_buffer;

    // Node Names
    std::vector<const char*> input_node_names_ = {"input", "state", "sr"};
    std::vector<const char*> output_node_names_ = {"output", "stateN"};
};

} // namespace vad
