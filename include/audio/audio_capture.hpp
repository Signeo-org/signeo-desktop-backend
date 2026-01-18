#pragma once

/**
 * @file audio_capture.hpp
 * @brief Audio input capture using PortAudio with WASAPI support
 */

#include <portaudio.h>

#include <memory>
#include <string>
#include <vector>

#include "../core/common_types.hpp"
#include "../core/result.hpp"
#include "ringbuffer.hpp"

namespace audio {

/**
 * @brief Audio device information
 */
struct AudioDevice {
    int index;
    std::string name;
    int max_input_channels;
    double default_sample_rate;
    bool is_default;
    bool is_loopback = false;
};

/**
 * @brief Audio capture from input devices with ring buffer
 *
 * Provides real-time audio capture with automatic resampling support.
 * Uses PortAudio for cross-platform audio input.
 */
class AudioCapture {
public:
    /**
     * @brief Factory method to create an AudioCapture instance
     * @param sample_rate Target sample rate (will use device native if different)
     * @param frames_per_buffer Buffer size in frames
     * @param file_path Optional path to WAV file for simulation/testing
     * @return Result containing unique_ptr to AudioCapture, or error message
     */
    static auto create(int sample_rate = 16000, int frames_per_buffer = 512, const std::string& file_path = "")
        -> core::Result<std::unique_ptr<AudioCapture>>;

    ~AudioCapture();

    // Prevent copying
    AudioCapture(const AudioCapture&) = delete;
    auto operator=(const AudioCapture&) -> AudioCapture& = delete;

    /**
     * @brief Start audio capture on specified device
     * @param device_index Device index (-1 for default device)
     * @return Status indicating success or error with message
     */
    auto start(int device_index = -1) -> core::Status;

    /**
     * @brief Stop audio capture
     */
    void stop();

    /**
     * @brief Read audio chunk from ring buffer
     * @param max_frames Maximum frames to read
     * @return Audio chunk with captured data
     */
    auto read_chunk(size_t max_frames) -> core::AudioChunk;

    /**
     * @brief List available audio devices
     * @return Result containing device list or error message
     */
    auto list_devices() -> core::Result<std::vector<AudioDevice>>;

    /**
     * @brief Check if capture is active
     * @return true if actively capturing
     */
    auto is_active() const -> bool;

    auto sample_rate() const -> int;
    auto channels() const -> int;

    void set_gain(float gain);
    auto get_gain() const -> float;

    // Helper methods
    static auto is_loopback_device(int device_index) -> bool;
    auto try_open_stream(const PaStreamParameters& params, double sample_rate) -> core::Status;

private:
    // Private constructor - use create() factory
    AudioCapture(int sample_rate, int frames_per_buffer, const std::string& file_path = "");

    auto init_portaudio() -> core::Status;
    auto load_wav_file() -> core::Status;

    static auto pa_callback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData)
        -> int;

    // Helpers
    auto open_pa_stream(const PaDeviceInfo* deviceInfo, PaStreamParameters& params) -> core::Status;
    static auto read_wav_header(std::ifstream& file, uint16_t& channels, uint32_t& sample_rate,
                                uint16_t& bits_per_sample) -> core::Status;

    // Member variables
    int sample_rate_ = 16000;
    int channels_ = 0;
    int frames_per_buffer_;
    std::atomic<float> input_gain_{1.0F};

    // File simulation members
    bool file_mode_ = false;
    std::vector<float> wav_data_;
    size_t wav_playback_pos_ = 0;
    std::string wav_path_;

    struct PaStreamDeleter {
        void operator()(PaStream* stream) const {
            if (stream != nullptr) {
                Pa_StopStream(stream);
                Pa_CloseStream(stream);
            }
        }
    };

    std::unique_ptr<PaStream, PaStreamDeleter> stream_;
    std::unique_ptr<RingBuffer> ring_buffer_;
    std::vector<float> temp_buffer_;
    bool active_ = false;
    bool pa_initialized_ = false;
};

}  // namespace audio
