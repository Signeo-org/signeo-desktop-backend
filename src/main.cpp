#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <string>
#include <locale>
#include <codecvt>
#include <portaudio.h>
#include "whisper.h"
#include "onnxruntime_cxx_api.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "pa_win_wasapi.h"
#endif

std::wstring StringToWString(const std::string& str) {
#ifdef _WIN32
    using convert_type = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<convert_type, wchar_t> converter;
    return converter.from_bytes(str);
#else
    return std::wstring(str.begin(), str.end());
#endif
}

constexpr int WHISPER_SAMPLE_RATE_CUSTOM = 16000;
constexpr int FRAMES_PER_BUFFER = 512;
constexpr int NUM_CHANNELS = 1;

class AudioQueue {
public:
    void push(const int16_t* samples, size_t count) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.insert(buffer_.end(), samples, samples + count);
        cond_.notify_one();
    }
    std::vector<int16_t> pop(size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [&] { return buffer_.size() >= count || stopped_; });
        if (buffer_.size() < count) return {};
        std::vector<int16_t> out(buffer_.begin(), buffer_.begin() + count);
        buffer_.erase(buffer_.begin(), buffer_.begin() + count);
        return out;
    }
    void stop() {
        stopped_ = true;
        cond_.notify_all();
    }
private:
    std::deque<int16_t> buffer_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool stopped_ = false;
};

class AudioCapture {
public:
    AudioCapture(AudioQueue& queue) : audioQueue_(queue) {}
    ~AudioCapture() { stop(); Pa_Terminate(); }

    bool initialize() {
        return Pa_Initialize() == paNoError;
    }

    void listDevices() {
        int count = Pa_GetDeviceCount();
        std::cout << "Audio API: WASAPI\n";
        std::cout << "--------------------------------------------------\n";
        std::cout << "Devices (Input or Loopback):\n";
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
            if (!di) continue;
            const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
            if (!hai) continue;
#ifdef _WIN32
            std::string hostApiName(hai->name ? hai->name : "");
            if (hostApiName.find("WASAPI") == std::string::npos)
                continue;
            bool hasInput = di->maxInputChannels > 0;
            int isLoop = PaWasapi_IsLoopback(i);
            if (hasInput || isLoop == 1) {
                std::cout << i << ": " << di->name;
                if (hasInput) std::cout << " [Input]";
                if (di->maxOutputChannels > 0) std::cout << " [Output]";
                if ((hai->defaultInputDevice == i) || (hai->defaultOutputDevice == i)) std::cout << " (Default)";
                std::cout << std::endl;
            }
#else
            if (di->maxInputChannels > 0) {
                std::cout << i << ": " << di->name << " [Input]\n";
            }
#endif
        }
    }

    bool start(int deviceId) {
        stop();
        const PaDeviceInfo* di = Pa_GetDeviceInfo(deviceId);
        if (!di || di->maxInputChannels < NUM_CHANNELS) {
            std::cerr << "Invalid device or insufficient channels\n";
            return false;
        }
        PaStreamParameters params{};
        params.device = deviceId;
        params.channelCount = NUM_CHANNELS;
        params.sampleFormat = paInt16;
        params.suggestedLatency = di->defaultLowInputLatency;

        auto err = Pa_OpenStream(&stream_, &params, nullptr,
            di->defaultSampleRate, FRAMES_PER_BUFFER, paClipOff,
            &AudioCapture::paCallback, this);

        if (err != paNoError) return false;

        err = Pa_StartStream(stream_);
        if (err != paNoError) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
            return false;
        }
        deviceSampleRate_ = di->defaultSampleRate;
        running_ = true;
        return true;
    }

    void stop() {
        running_ = false;
        if (stream_) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
    }

    double sampleRate() const { return deviceSampleRate_; }

private:
    static int paCallback(const void* input, void*, unsigned long frameCount,
        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData) {
        auto* self = static_cast<AudioCapture*>(userData);
        if (!input || !self->running_) return paContinue;
        const int16_t* samples = static_cast<const int16_t*>(input);
        self->audioQueue_.push(samples, frameCount * NUM_CHANNELS);
        return paContinue;
    }

    AudioQueue& audioQueue_;
    PaStream* stream_ = nullptr;
    std::atomic<bool> running_ = false;
    double deviceSampleRate_ = 0;
};

class Timestamp {
public:
    int start;
    int end;
    Timestamp(int s = -1, int e = -1) : start(s), end(e) {}
};

class SileroVAD {
public:
    SileroVAD(const std::string& modelPath, int64_t sample_rate = 16000, float threshold = 0.3f)
        : env_(ORT_LOGGING_LEVEL_ERROR, "silero-vad"),
          sessionOptions_(),
          memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          threshold_(threshold),
          sample_rate_(sample_rate),
          context_samples_(64),
          window_ms_(32),
          window_size_samples_(sample_rate * window_ms_ / 1000),
          effective_window_size_(window_size_samples_ + context_samples_),
          samples_per_ms_(sample_rate / 1000),
          currentSample_(0),
          triggered_(false),
          tempEnd_(0),
          prevEnd_(0),
          nextStart_(0)
    {
        sessionOptions_.SetIntraOpNumThreads(1);
        sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        std::wstring wModelPath = StringToWString(modelPath);
        session_ = std::make_unique<Ort::Session>(env_, wModelPath.c_str(), sessionOptions_);

        stateBuffer_.resize(2 * 1 * 128, 0.0f);
        contextBuffer_.resize(context_samples_, 0.0f);

        inputNodeNames_ = { "input", "state", "sr" };
        outputNodeNames_ = { "output", "stateN" };

        minSilenceSamples_ = samples_per_ms_ * 100;
        minSpeechSamples_ = samples_per_ms_ * 250;
        speechPadSamples_ = samples_per_ms_ * 30;
        maxSpeechSamples_ = std::numeric_limits<int>::max();
    }

    void reset() {
        std::fill(stateBuffer_.begin(), stateBuffer_.end(), 0.0f);
        std::fill(contextBuffer_.begin(), contextBuffer_.end(), 0.0f);
        speeches_.clear();
        currentSample_ = 0;
        triggered_ = false;
        tempEnd_ = 0;
        prevEnd_ = 0;
        nextStart_ = 0;
        currentSpeech_ = Timestamp();
    }
    void process(const std::vector<float>& input_wav) {
        std::cout << "Here 1" << std::endl;
        reset();
        std::cout << "Here 2" << std::endl;
        size_t length = input_wav.size();
        for (size_t i = 0; i + window_size_samples_ <= length; i += window_size_samples_) {
            std::cout << "Here 3" << std::endl;
            std::vector<float> chunk(input_wav.begin() + i, input_wav.begin() + i + window_size_samples_);
            std::cout << "Here 4" << std::endl;
            predict(chunk);
            std::cout << "Here 5" << std::endl;
        }
        std::cout << "Here 6" << std::endl;
        if (currentSpeech_.start >= 0) {
            std::cout << "Here 7" << std::endl;
            currentSpeech_.end = static_cast<int>(length);
            speeches_.push_back(currentSpeech_);
            std::cout << "Here 8" << std::endl;
        }
        std::cout << "Here 9" << std::endl;
    }
    const std::vector<Timestamp>& getSpeechTimestamps() const {
        return speeches_;
    }
    int currentSample() const { return currentSample_; }

private:
    void predict(const std::vector<float>& dataChunk) {
        Ort::AllocatorWithDefaultOptions allocator;

        std::vector<float> inputData(effective_window_size_, 0.0f);
        std::memcpy(inputData.data(), contextBuffer_.data(), context_samples_ * sizeof(float));
        std::memcpy(inputData.data() + context_samples_, dataChunk.data(), window_size_samples_ * sizeof(float));

        auto inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo_, inputData.data(), inputData.size(), inputShape_.data(), inputShape_.size());

        auto stateTensor = Ort::Value::CreateTensor<float>(
            memoryInfo_, stateBuffer_.data(), stateBuffer_.size(), stateShape_.data(), stateShape_.size());

        int64_t sample_rate_dim[] = { 1 };
        auto srTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo_, &sample_rate_, 1, sample_rate_dim, 1);

        std::vector<Ort::Value> inputs;
        inputs.reserve(3);
        inputs.emplace_back(std::move(inputTensor));
        inputs.emplace_back(std::move(stateTensor));
        inputs.emplace_back(std::move(srTensor));

        auto outputs = session_->Run(Ort::RunOptions{ nullptr },
                                    inputNodeNames_.data(), inputs.data(), inputs.size(),
                                    outputNodeNames_.data(), outputNodeNames_.size());

        float speech_prob = outputs[0].GetTensorMutableData<float>()[0];
        float* stateData = outputs[1].GetTensorMutableData<float>();
        std::memcpy(stateBuffer_.data(), stateData, stateBuffer_.size() * sizeof(float));

        std::cout << "[Debug VAD] speech_prob: " << speech_prob << ", triggered: " << (triggered_ ? "true" : "false") << std::endl;

        currentSample_ += window_size_samples_;

        if (speech_prob >= threshold_) {
            if (tempEnd_ != 0) {
                tempEnd_ = 0;
                if (nextStart_ < prevEnd_)
                    nextStart_ = currentSample_ - window_size_samples_;
            }
            if (!triggered_) {
                triggered_ = true;
                currentSpeech_.start = currentSample_ - window_size_samples_;
                std::cout << "[Debug VAD] Triggered speech start at sample: " << currentSpeech_.start << std::endl;
            }
            std::memcpy(contextBuffer_.data(), &inputData[effective_window_size_ - context_samples_], context_samples_ * sizeof(float));
            return;
        }

        if (triggered_ && ((currentSample_ - currentSpeech_.start) > maxSpeechSamples_)) {
            if (prevEnd_ > 0) {
                currentSpeech_.end = prevEnd_;
                speeches_.push_back(currentSpeech_);
                currentSpeech_ = Timestamp();
                if (nextStart_ < prevEnd_)
                    triggered_ = false;
                else
                    currentSpeech_.start = nextStart_;
                prevEnd_ = 0;
                nextStart_ = 0;
                tempEnd_ = 0;
            }
            else {
                currentSpeech_.end = currentSample_;
                speeches_.push_back(currentSpeech_);
                currentSpeech_ = Timestamp();
                prevEnd_ = 0;
                nextStart_ = 0;
                tempEnd_ = 0;
                triggered_ = false;
            }
            std::memcpy(contextBuffer_.data(), &inputData[effective_window_size_ - context_samples_], context_samples_ * sizeof(float));
            return;
        }

        if ((speech_prob >= (threshold_ - 0.15f)) && (speech_prob < threshold_)) {
            std::memcpy(contextBuffer_.data(), &inputData[effective_window_size_ - context_samples_], context_samples_ * sizeof(float));
            return;
        }

        if (speech_prob < (threshold_ - 0.15f)) {
            if (triggered_) {
                if (tempEnd_ == 0)
                    tempEnd_ = currentSample_;
                if (currentSample_ - tempEnd_ > 98 * samples_per_ms_)
                    prevEnd_ = tempEnd_;
                if ((currentSample_ - tempEnd_) >= minSilenceSamples_) {
                    currentSpeech_.end = tempEnd_;
                    if (currentSpeech_.end - currentSpeech_.start > minSpeechSamples_) {
                        speeches_.push_back(currentSpeech_);
                        currentSpeech_ = Timestamp();
                        prevEnd_ = 0;
                        nextStart_ = 0;
                        tempEnd_ = 0;
                        triggered_ = false;
                    }
                }
            }
            std::memcpy(contextBuffer_.data(), &inputData[effective_window_size_ - context_samples_], context_samples_ * sizeof(float));
            return;
        }
    }


private:
    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memoryInfo_;

    std::vector<const char*> inputNodeNames_;
    std::vector<const char*> outputNodeNames_;

    std::vector<float> stateBuffer_;
    std::vector<float> contextBuffer_;
    std::vector<int64_t> inputShape_ = {1, 576};
    std::vector<int64_t> stateShape_ = {2, 1, 128};

    int64_t sample_rate_;
    float threshold_;
    int context_samples_;
    int window_ms_;
    int window_size_samples_;
    int effective_window_size_;
    int samples_per_ms_;

    int currentSample_;
    bool triggered_;
    int tempEnd_;
    int prevEnd_;
    int nextStart_;

    int minSilenceSamples_;
    int minSpeechSamples_;
    int speechPadSamples_;
    int maxSpeechSamples_;

    Timestamp currentSpeech_;
    std::vector<Timestamp> speeches_;
};

class WhisperTranscriber {
public:
    explicit WhisperTranscriber(const std::string& modelPath) {
        params_ = whisper_context_default_params();
        ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), params_);
        if (!ctx_) throw std::runtime_error("Failed to load whisper model");
    }
    ~WhisperTranscriber() { if (ctx_) whisper_free(ctx_); }
    std::string transcribe(const std::vector<float>& audio) {
        whisper_full_params wp = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wp.n_threads = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
        if (whisper_full(ctx_, wp, audio.data(), (int)audio.size()) != 0) {
            std::cerr << "[Whisper] transcription failed on this segment\n";
            return {};
        }
        int nSegments = whisper_full_n_segments(ctx_);
        if (nSegments == 0) {
            std::cout << "[Whisper] no transcription segments generated\n";
        }
        std::string result;
        for (int i = 0; i < nSegments; ++i) {
            const char* t = whisper_full_get_segment_text(ctx_, i);
            if (t) result += t;
        }
        return result;
    }
private:
    whisper_context* ctx_ = nullptr;
    whisper_context_params params_;
};

std::vector<float> resampleTo16kHz(const int16_t* samples, size_t length, double inputSampleRate) {
    float ratio = static_cast<float>(inputSampleRate) / WHISPER_SAMPLE_RATE_CUSTOM;
    size_t outLen = static_cast<size_t>(length / ratio);
    std::vector<float> out(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        size_t idx = std::min(static_cast<size_t>(i * ratio), length - 1);
        out[i] = samples[idx] / 32768.0f;
    }
    return out;
}

template<typename T>
void printSamples(const std::vector<T> &samples, size_t maxSamples = 10, const std::string &label = "") {
    std::cout << label << " [size: " << samples.size() << "] samples: ";
    for (size_t i = 0; i < std::min(samples.size(), maxSamples); ++i) {
        std::cout << samples[i] << " ";
    }
    std::cout << "...\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: app <whisper-model.bin> <silero-vad.onnx> [--sliding-window]\n";
        return 1;
    }

    bool useSlidingWindowMode = false;
    if (argc >= 4) {
        std::string modeFlag = argv[3];
        if (modeFlag == "--sliding-window") {
            useSlidingWindowMode = true;
        }
    }

    // Default parameters following examples:
    int step_ms = useSlidingWindowMode ? 0 : 500;
    int length_ms = useSlidingWindowMode ? 30000 : 5000;
    float vad_threshold = useSlidingWindowMode ? 0.6f : 0.5f;

    std::string whisperModelPath = argv[1];
    std::string vadModelPath = argv[2];

    AudioQueue audioQueue;
    AudioCapture audioCapture(audioQueue);

    if (!audioCapture.initialize()) return 1;

    audioCapture.listDevices();
    std::cout << "Select audio input device ID: ";
    int deviceId;
    std::cin >> deviceId;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (!audioCapture.start(deviceId)) {
        std::cerr << "Failed to start audio capture\n";
        return 1;
    }

    SileroVAD vad(vadModelPath, 16000, vad_threshold);
    WhisperTranscriber whisper(whisperModelPath);

    std::atomic<bool> running(true);
    std::thread stopThread([&] {
        std::cout << "Press ENTER to stop...\n";
        std::cin.get();
        running = false;
        audioQueue.stop();
        audioCapture.stop();
    });

    double deviceSampleRate = audioCapture.sampleRate();
    std::cout << "[Log] Device sample rate: " << deviceSampleRate << " Hz\n";

    // Parameters in samples
    const size_t step_samples = WHISPER_SAMPLE_RATE_CUSTOM * step_ms / 1000;
    const size_t length_samples = WHISPER_SAMPLE_RATE_CUSTOM * length_ms / 1000;

    std::deque<float> rollingBuffer;
    std::vector<int16_t> rawAudioBuffer;
    size_t lastProcessedSample = 0;

    std::vector<float> vadInputBuffer;

    // For timing in interval mode
    auto lastTranscriptionTime = std::chrono::steady_clock::now();

    while (running) {
        auto samples = audioQueue.pop(FRAMES_PER_BUFFER);
        if (samples.empty()) break;

        auto floatChunk = resampleTo16kHz(samples.data(), samples.size(), deviceSampleRate);

        // For VAD buffer accumulation
        vadInputBuffer.insert(vadInputBuffer.end(), floatChunk.begin(), floatChunk.end());

        if (useSlidingWindowMode) {
            // Sliding window mode behavior

            // Keep rollingBuffer to length_samples max
            rollingBuffer.insert(rollingBuffer.end(), floatChunk.begin(), floatChunk.end());
            if (rollingBuffer.size() > length_samples) {
                rollingBuffer.erase(rollingBuffer.begin(), rollingBuffer.end() - length_samples);
            }

            // Process VAD on fixed-size blocks (512 samples)
            while (vadInputBuffer.size() >= 512) {
                std::vector<float> vadChunk(vadInputBuffer.begin(), vadInputBuffer.begin() + 512);
                vad.process(vadChunk);
                vadInputBuffer.erase(vadInputBuffer.begin(), vadInputBuffer.begin() + 512);
            }

            // Check for detected speech segments
            const auto& speechSegments = vad.getSpeechTimestamps();
            for (const auto& seg : speechSegments) {
                // Translate segment indices into rollingBuffer indices
                size_t segSamples = seg.end - seg.start;
                if (rollingBuffer.size() < segSamples) continue;

                std::vector<float> segmentAudio(rollingBuffer.end() - segSamples, rollingBuffer.end());

                auto transcription = whisper.transcribe(segmentAudio);
                std::cout << "[Transcription]: " << transcription << std::endl;
            }
        } else {
            // Interval mode behavior

            // Add to rolling buffer and trim to length_samples max
            rollingBuffer.insert(rollingBuffer.end(), floatChunk.begin(), floatChunk.end());
            if (rollingBuffer.size() > length_samples) {
                rollingBuffer.erase(rollingBuffer.begin(), rollingBuffer.end() - length_samples);
            }

            // Transcribe every step_ms milliseconds
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTranscriptionTime).count();
            if (elapsedMs < step_ms) continue; // skip until step passed
            lastTranscriptionTime = now;

            if (rollingBuffer.size() < length_samples) continue; // need enough audio

            std::vector<float> audioToTranscribe(rollingBuffer.end() - length_samples, rollingBuffer.end());

            auto transcription = whisper.transcribe(audioToTranscribe);
            std::cout << "[Transcription]: " << transcription << std::endl;
        }
    }

    if (stopThread.joinable()) stopThread.join();

    return 0;
}
