#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <string>
#include <locale>
#include <codecvt>
#include <portaudio.h>
#include "whisper.h"
#include "onnxruntime_cxx_api.h"

#ifdef _WIN32
#define NOMINMAX // prevent Windows.h from defining min/max macros
#include <windows.h>
#include "pa_win_wasapi.h"
#endif

// Convert UTF-8 std::string to UTF-16 std::wstring on Windows
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
constexpr float VAD_THRESHOLD = 0.7f;

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
        std::cout << "Audio API: WASAPI" << std::endl;
        std::cout << "--------------------------------------------------" << std::endl;
        std::cout << "Devices (Input or Loopback):" << std::endl;
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
            if (!di) continue;
            const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
            if (!hai) continue;

#ifdef _WIN32
            std::string hostApiName(hai->name ? hai->name : "");
            if (hostApiName.find("WASAPI") == std::string::npos)
                continue;
            bool hasInput = (di->maxInputChannels > 0);
            int isLoop = PaWasapi_IsLoopback(i);
            if (hasInput || isLoop == 1) {
                std::cout << i << ": " << di->name;
                if (hasInput) std::cout << " [Input]";
                if (di->maxOutputChannels > 0) std::cout << " [Output]";
                if (isLoop == 1) std::cout << " [Loopback]";
                if ((hai->defaultInputDevice == i) || (hai->defaultOutputDevice == i)) std::cout << " (Default)";
                std::cout << std::endl;
            }
#else
            if (di->maxInputChannels > 0) {
                std::cout << i << ": " << di->name << " [Input]" << std::endl;
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

class SileroVAD {
public:
    SileroVAD(const std::string& modelPath)
        : env_(ORT_LOGGING_LEVEL_WARNING, "silero-vad"),
          sessionOptions_(),
          session_(nullptr),
          allocator_(),
          memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
    {
        sessionOptions_.SetIntraOpNumThreads(1);
        sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        std::wstring wModelPath = StringToWString(modelPath);
        session_ = std::make_unique<Ort::Session>(env_, wModelPath.c_str(), sessionOptions_);

        Ort::AllocatorWithDefaultOptions allocator;

        size_t inputCount = session_->GetInputCount();
        inputNodeNames_.resize(inputCount);
        for (size_t i = 0; i < inputCount; ++i) {
            char* inputName = nullptr;
            OrtSession* c_session = session_->operator OrtSession*();
            OrtStatus* status = Ort::GetApi().SessionGetInputName(c_session, i, allocator, &inputName);
            if (status != nullptr) {
                throw std::runtime_error("Failed to get input name");
            }
            inputNodeNames_[i] = inputName;
            inputNamesOwnership_.push_back(inputName);
        }

        size_t outputCount = session_->GetOutputCount();
        outputNodeNames_.resize(outputCount);
        for (size_t i = 0; i < outputCount; ++i) {
            char* outputName = nullptr;
            OrtSession* c_session = session_->operator OrtSession*();
            OrtStatus* status = Ort::GetApi().SessionGetOutputName(c_session, i, allocator, &outputName);
            if (status != nullptr) {
                throw std::runtime_error("Failed to get output name");
            }
            outputNodeNames_[i] = outputName;
            outputNamesOwnership_.push_back(outputName);
        }
    }

    ~SileroVAD() {
        for (auto name : inputNamesOwnership_) allocator_.Free(name);
        for (auto name : outputNamesOwnership_) allocator_.Free(name);
    }

    bool isSpeech(const std::vector<float>& audioSamples) {
        std::vector<int64_t> inputShape = { 1, static_cast<int64_t>(audioSamples.size()) };
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo_, const_cast<float*>(audioSamples.data()), audioSamples.size(),
            inputShape.data(), inputShape.size());

        auto outputs = session_->Run(
            Ort::RunOptions{ nullptr },
            inputNodeNames_.data(), &inputTensor, 1,
            outputNodeNames_.data(), outputNodeNames_.size());

        float* data = outputs[0].GetTensorMutableData<float>();
        size_t len = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();

        float avg_prob = 0.0f;
        for (size_t i = 0; i < len; ++i) avg_prob += data[i];
        avg_prob /= len;

        return avg_prob > VAD_THRESHOLD;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;
    Ort::MemoryInfo memoryInfo_;
    std::vector<const char*> inputNodeNames_;
    std::vector<const char*> outputNodeNames_;
    std::vector<char*> inputNamesOwnership_;
    std::vector<char*> outputNamesOwnership_;
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

        if (whisper_full(ctx_, wp, audio.data(), (int)audio.size()) != 0)
            return {};

        std::string result;
        int nSegments = whisper_full_n_segments(ctx_);
        for (int i = 0; i < nSegments; ++i) {
            const char* t = whisper_full_get_segment_text(ctx_, i);
            if (t) result += t;
        }
        return result;
    }
private:
    whisper_context* ctx_;
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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: app <whisper-model.bin> <silero-vad.onnx>\n";
        return 1;
    }

    std::string whisperPath = argv[1];
    std::string vadPath = argv[2];

    AudioQueue audioQueue;
    AudioCapture paCapture(audioQueue);

    if (!paCapture.initialize()) return 1;

    paCapture.listDevices();

    std::cout << "Select audio input device ID: ";
    int deviceId;
    std::cin >> deviceId;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (!paCapture.start(deviceId)) {
        std::cerr << "Failed to start capture on device " << deviceId << "\n";
        return 1;
    }

    SileroVAD vad(vadPath);
    WhisperTranscriber whisper(whisperPath);

    std::atomic<bool> running(true);
    std::thread stopThread([&] {
        std::cout << "Press ENTER to stop capture...\n";
        std::cin.get();
        running = false;
        audioQueue.stop();
        paCapture.stop();
    });

    const size_t chunkSize = WHISPER_SAMPLE_RATE_CUSTOM * 2; // 2 seconds
    double sampleRate = paCapture.sampleRate();

    while (running) {
        auto chunk = audioQueue.pop(chunkSize);
        if (chunk.empty()) break;

        auto floatChunk = resampleTo16kHz(chunk.data(), chunk.size(), sampleRate);

        if (!vad.isSpeech(floatChunk)) continue;

        std::string text = whisper.transcribe(floatChunk);
        if (!text.empty()) std::cout << "[Transcription]: " << text << "\n";
    }

    if (stopThread.joinable()) stopThread.join();
    return 0;
}
