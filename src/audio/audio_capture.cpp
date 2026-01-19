#include "audio/audio_capture.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

#include "core/result.hpp"
#include "output/logging.hpp"

#ifdef _WIN32
    #include <pa_win_wasapi.h>
    #include <windows.h>
#endif

namespace {
template <typename T>
void read_pod(std::ifstream& file, T& value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
}

void read_bytes(std::ifstream& file, void* data, size_t size) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
}
}  // namespace

namespace audio {

// Factory Method
auto AudioCapture::create(int sample_rate, int frames_per_buffer, const std::string& file_path)
    -> core::Result<std::unique_ptr<AudioCapture>> {
    LOG_SCOPED_TRACE();
    spdlog::debug("AudioCapture::create() called with sample_rate={}, frames_per_buffer={}, file={}", sample_rate,
                  frames_per_buffer, file_path);

    // Create config object
    Config config{
        .sample_rate = sample_rate,
        .frames_per_buffer = frames_per_buffer,
        .file_path = file_path,
    };

    // We can't use make_unique with private constructor easily, so we use raw new
    std::unique_ptr<AudioCapture> capture(new AudioCapture(config));

    if (capture->file_mode_) {
        auto load_res = capture->load_wav_file();
        if (!load_res) {
            return std::unexpected(load_res.error());
        }
        spdlog::info("AudioCapture created in FILE MODE. Loaded '{}' ({} Hz, {} Ch, {} samples)", file_path,
                     capture->sample_rate_, capture->channels_, capture->wav_data_.size());
    } else {
        auto init_result = capture->init_portaudio();
        if (!init_result) {
            return std::unexpected(init_result.error());
        }
        spdlog::info("AudioCapture created. Requested Rate: {}, Frames: {}", sample_rate, frames_per_buffer);
    }

    return capture;
}

// Private Constructor
AudioCapture::AudioCapture(const Config& config)
    : sample_rate_(config.sample_rate)
    , frames_per_buffer_(config.frames_per_buffer)
    , file_mode_(!config.file_path.empty())
    , wav_path_(config.file_path) {
    // Ring buffer will be allocated in start() for PA mode, or we don't use it for file mode
}

auto AudioCapture::init_portaudio() -> core::Status {
    spdlog::debug("AudioCapture::init_portaudio() initializing PortAudio...");

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return core::log_error(std::format("PortAudio initialization failed: {}", Pa_GetErrorText(err)));
    }

    pa_initialized_ = true;
    spdlog::debug("AudioCapture::init_portaudio() PortAudio initialized successfully");
    return {};
}

auto AudioCapture::load_wav_file() -> core::Status {
    std::ifstream file(wav_path_, std::ios::binary);
    if (!file.is_open()) {
        return core::log_error(std::format("Failed to open WAV file: {}", wav_path_));
    }

    WavAudioFormat format{};
    auto header_res = read_wav_header(file, format);
    if (!header_res) {
        return header_res;
    }

    // Assign to members after successful read
    this->channels_ = format.channels;
    this->sample_rate_ = static_cast<int>(format.sample_rate);

    if (format.bits_per_sample != kBitsPerSample) {
        return core::log_error("Unsupported bit depth (only 16-bit supported currently)");
    }

    // Find data chunk
    std::array<char, 4> chunk_id{};
    uint32_t chunk_size = 0;

    while (true) {
        read_bytes(file, chunk_id.data(), chunk_id.size());
        if (!file) {
            break;
        }

        read_pod(file, chunk_size);
        if (!file) {
            break;
        }

        if (std::strncmp(chunk_id.data(), "data", 4) == 0) {
            break;
        }
        file.seekg(chunk_size, std::ios::cur);
    }

    if (file.eof()) {
        return core::log_error("No data chunk found in WAV file");
    }

    // Read samples
    size_t num_samples = chunk_size / 2;  // 16-bit = 2 bytes
    std::vector<int16_t> pcm_data(num_samples);
    read_bytes(file, pcm_data.data(), chunk_size);

    // Convert to float [-1.0, 1.0]
    wav_data_.resize(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        wav_data_[i] = static_cast<float>(pcm_data[i]) / kPcmToFloat;
    }

    spdlog::info("Loaded WAV: {} Hz, {} ch, {} samples", sample_rate_, channels_, num_samples);
    return {};
}

auto AudioCapture::read_wav_header(std::ifstream& file, WavAudioFormat& format) -> core::Status {
    struct WavHeader {
        std::array<char, 4> riff;
        uint32_t file_size;
        std::array<char, 4> wave;
        std::array<char, 4> fmt;
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
    } header{};

    read_pod(file, header);

    if (std::strncmp(header.riff.data(), "RIFF", 4) != 0 || std::strncmp(header.wave.data(), "WAVE", 4) != 0) {
        return core::log_error("Invalid WAV file format");
    }

    if (header.audio_format != 1) {  // PCM = 1
        return core::log_error("Unsupported WAV format (only PCM supported)");
    }

    format.channels = header.num_channels;
    format.sample_rate = header.sample_rate;
    format.bits_per_sample = header.bits_per_sample;

    return {};
}

AudioCapture::~AudioCapture() {
    spdlog::debug("AudioCapture: Destructor called, cleaning up...");
    stop();
    if (pa_initialized_) {
        Pa_Terminate();
    }
    spdlog::debug("AudioCapture: Cleanup complete.");
}

auto AudioCapture::start(int device_index) -> core::Status {
    LOG_SCOPED_TRACE();
    spdlog::debug("AudioCapture::start() device_index={}", device_index);

    if (active_) {
        spdlog::debug("AudioCapture::start() - already active, returning success");
        return {};
    }

    if (file_mode_) {
        wav_playback_pos_ = 0;
        active_ = true;
        spdlog::info("AudioCapture started in FILE MODE (virtual device).");
        return {};
    }

    PaStreamParameters input_parameters = {};

    if (device_index < 0) {
        input_parameters.device = Pa_GetDefaultInputDevice();
    } else {
        input_parameters.device = device_index;
    }

    if (input_parameters.device == paNoDevice) {
        return core::log_error("No default input device found");
    }

    const PaDeviceInfo* device_info = Pa_GetDeviceInfo(input_parameters.device);
    if (device_info == nullptr) {
        return core::log_error(std::format("Failed to get device info for device index {}", input_parameters.device));
    }

    spdlog::info("Device info: name='{}', maxInputChannels={}, defaultSampleRate={}", device_info->name,
                 device_info->maxInputChannels, device_info->defaultSampleRate);

    // Use device-native settings for maximum compatibility
    sample_rate_ = static_cast<int>(device_info->defaultSampleRate);
    channels_ = device_info->maxInputChannels;
    input_parameters.sampleFormat = paFloat32;
    input_parameters.hostApiSpecificStreamInfo = nullptr;

// Check for WASAPI Loopback on Windows
#ifdef _WIN32
    int loopback_state = PaWasapi_IsLoopback(input_parameters.device);
    if (loopback_state == 1) {
        spdlog::info("Configuring device '{}' as WASAPI Loopback", device_info->name);
        if (channels_ == 0 && device_info->maxOutputChannels > 0) {
            channels_ = device_info->maxOutputChannels;
        }
    }
#endif

    // Validate channels
    if (channels_ <= 0) {
        return core::log_error(std::format("Device '{}' has no channels available for capture (IN: {}, OUT: {})",
                                           device_info->name, device_info->maxInputChannels,
                                           device_info->maxOutputChannels));
    }

    auto open_res = open_pa_stream(device_info, input_parameters);
    if (!open_res) {
        return open_res;
    }

    spdlog::info("Opening input device: {} (Rate: {}, Channels: {})", device_info->name, sample_rate_, channels_);

    // Allocate ring buffer: 10 seconds of interleaved audio
    ring_buffer_ = std::make_unique<RingBuffer>(sample_rate_ * channels_ * kRingBufferDurationSeconds);

    PaError err = Pa_StartStream(stream_.get());
    if (err != paNoError) {
        return core::log_error(std::format("Failed to start stream: {}", Pa_GetErrorText(err)));
    }

    active_ = true;
    spdlog::info("Audio stream started successfully.");
    return {};
}

auto AudioCapture::open_pa_stream(const PaDeviceInfo* deviceInfo, PaStreamParameters& params) -> core::Status {
    bool is_loopback = is_loopback_device(params.device);

    // Try to open stream with channel fallback
    PaError err = paInvalidDevice;
    bool try_high_latency = false;

    while (channels_ >= 1 && err != paNoError) {
        params.channelCount = channels_;

        // Use helper to set latency based on try_high_latency flag
        if (is_loopback) {
            params.suggestedLatency =
                try_high_latency ? deviceInfo->defaultHighOutputLatency : deviceInfo->defaultLowOutputLatency;
        } else {
            params.suggestedLatency =
                try_high_latency ? deviceInfo->defaultHighInputLatency : deviceInfo->defaultLowInputLatency;
        }

        spdlog::debug("Trying to open stream with {} channels, latency={}{}", channels_, params.suggestedLatency,
                      is_loopback ? " (loopback)" : "");

        // Try opening with current parameters
        auto status = try_open_stream(params, sample_rate_);
        if (status) {
            return {};  // Success
        }

        // If failed, adjust strategy
        if (!try_high_latency) {
            try_high_latency = true;
        } else {
            channels_--;
            try_high_latency = false;
        }
        // Error is implicitly handled by loop continuing or exiting
        // We need to keep the last specific error if we exit?
        // For now, if we bottom out channels, we return a generic error or the last one.
    }

    return core::log_error("Failed to open stream after trying all channel configurations");
}

void AudioCapture::stop() {
    if (!active_) {
        return;
    }

    if (file_mode_) {
        active_ = false;
        spdlog::info("AudioCapture stopped (FILE MODE).");
        return;
    }

    if (!stream_) {
        return;
    }

    // PaStreamDeleter handles Stop/Close automatically
    stream_.reset();
    active_ = false;
    spdlog::info("Audio stream stopped.");
}

auto AudioCapture::read_chunk(size_t max_frames) -> core::AudioChunk {
    core::AudioChunk chunk;

    if (file_mode_) {
        if (!active_) {
            return chunk;
        }

        size_t samples_to_read = max_frames * channels_;
        size_t samples_remaining = wav_data_.size() - wav_playback_pos_;

        if (samples_remaining == 0) {
            return chunk;
        }

        size_t actual_samples = (std::min)(samples_to_read, samples_remaining);

        chunk.data.resize(actual_samples);
        std::memcpy(chunk.data.data(), &wav_data_[wav_playback_pos_], actual_samples * sizeof(float));

        wav_playback_pos_ += actual_samples;
        chunk.capture_time = std::chrono::steady_clock::now();

        return chunk;
    }

    chunk.data.resize(max_frames * channels_);
    size_t read = ring_buffer_->read(chunk.data.data(), max_frames * channels_);
    chunk.data.resize(read);
    chunk.capture_time = std::chrono::steady_clock::now();
    return chunk;
}

auto AudioCapture::is_loopback_device(int device_index) -> bool {
#ifdef _WIN32
    return PaWasapi_IsLoopback(device_index) == 1;
#else
    return false;
#endif
}

auto AudioCapture::try_open_stream(const PaStreamParameters& params, double sample_rate) -> core::Status {
    PaStream* temp_stream = nullptr;
    PaError err = Pa_OpenStream(&temp_stream, &params, nullptr, sample_rate, paFramesPerBufferUnspecified, paClipOff,
                                (PaStreamCallback*)&AudioCapture::pa_callback, this);

    if (err == paNoError) {
        stream_.reset(temp_stream);
        return {};
    }
    return std::unexpected(std::string(Pa_GetErrorText(err)));
}

auto AudioCapture::list_devices() -> core::Result<std::vector<AudioDevice>> {
    spdlog::debug("AudioCapture::list_devices() called");

    if (file_mode_) {
        return std::vector<AudioDevice>{{.index = 0,
                                         .name = "Virtual WAV File Device",
                                         .max_input_channels = channels_,
                                         .default_sample_rate = static_cast<double>(sample_rate_),
                                         .is_default = true,
                                         .is_loopback = false}};
    }

    std::vector<AudioDevice> devices;
    int num_devices = Pa_GetDeviceCount();
    if (num_devices < 0) {
        return core::log_error(std::format("PortAudio Pa_GetDeviceCount error: {}", num_devices));
    }

    int default_input = Pa_GetDefaultInputDevice();

    for (int i = 0; i < num_devices; i++) {
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        bool is_valid = device_info->maxInputChannels > 0;
        bool is_loopback = is_loopback_device(i);

        if (is_valid || is_loopback) {
            std::string name = device_info->name;
            const PaHostApiInfo* api_info = Pa_GetHostApiInfo(device_info->hostApi);
            if (api_info != nullptr) {
                name += " [" + std::string(api_info->name) + "]";
            }
            if (is_loopback) {
                name += " [Loopback]";
            }

            int effective_channels = is_valid ? device_info->maxInputChannels : device_info->maxOutputChannels;

            devices.push_back({.index = i,
                               .name = name,
                               .max_input_channels = effective_channels,
                               .default_sample_rate = device_info->defaultSampleRate,
                               .is_default = (i == default_input),
                               .is_loopback = is_loopback});
        }
    }

    spdlog::debug("AudioCapture::list_devices() found {} devices", devices.size());
    return devices;
}

auto AudioCapture::is_active() const -> bool {
    return active_;
}

auto AudioCapture::sample_rate() const -> int {
    return sample_rate_;
}

auto AudioCapture::channels() const -> int {
    return channels_;
}

void AudioCapture::set_gain(float gain) {
    input_gain_.store(gain);
}

auto AudioCapture::get_gain() const -> float {
    return input_gain_.load();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto AudioCapture::pa_callback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags,
                               void* userData) -> int {
    auto* self = static_cast<AudioCapture*>(userData);
    const auto* input_data = static_cast<const float*>(inputBuffer);

    (void)outputBuffer;  // Unused
    (void)timeInfo;
    (void)statusFlags;

    if (inputBuffer == nullptr) {
        // Silence input (rare) or underflow
        return paContinue;
    }

    size_t samples = static_cast<size_t>(framesPerBuffer) * self->channels_;
    float current_gain = self->input_gain_.load();

    // Safety check for input buffer access
    std::span<const float> input_span(input_data, samples);

    if (std::abs(current_gain - 1.0F) > kGainThreshold) {
        // Apply gain
        if (self->temp_buffer_.size() < samples) {
            self->temp_buffer_.resize(samples);
        }

        for (size_t i = 0; i < samples; ++i) {
            self->temp_buffer_[i] = input_span[i] * current_gain;
        }
        self->ring_buffer_->write(self->temp_buffer_.data(), samples);
    } else {
        // Pass through
        self->ring_buffer_->write(input_data, samples);
    }

    return paContinue;
}

}  // namespace audio
