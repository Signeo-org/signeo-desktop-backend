#ifdef _WIN32
#define NOMINMAX  // prevent Windows.h from defining min/max macros
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <cctype>

// PortAudio
#include "portaudio.h"
#include "pa_win_wasapi.h"

// Whisper
#include "whisper.h"

//---------------------------------------------------------------------------
// Constants for chunk-based approach
//--------------------------------------------------------------------------- 
static const int   FRAMES_PER_BUFFER = 1024; // typical WASAPI buffer size
static const int   CHUNK_MS         = 100;    // used for callback block duration estimation
static const int   WHISPER_RATE     = 16000;  // Whisper expects 16 kHz input
static const float RECORD_SECONDS   = 2.0f;   // chunk duration in seconds
static const int   KEEP_MS          = 200;    // overlap duration (milliseconds)

//---------------------------------------------------------------------------
// 1) Save 16-bit mono WAV for debugging (unchanged)
//--------------------------------------------------------------------------- 
static bool save_wav_16bit(const std::string &filename,
                           const float *samples,
                           int numSamples,
                           int sampleRate)
{
    FILE *fp = std::fopen(filename.c_str(), "wb");
    if (!fp) {
        std::perror("Failed to open file for WAV");
        return false;
    }
    uint32_t chunkSize      = 36 + (numSamples * 2);
    uint16_t audioFormat    = 1;   // PCM
    uint16_t numChannels    = 1;   // mono
    uint32_t byteRate       = sampleRate * numChannels * 2;
    uint16_t blockAlign     = numChannels * 2;
    uint16_t bitsPerSample  = 16;
    uint32_t dataSize       = numSamples * 2;

    std::fwrite("RIFF", 1, 4, fp);
    std::fwrite(&chunkSize, 4, 1, fp);
    std::fwrite("WAVE", 1, 4, fp);
    std::fwrite("fmt ", 1, 4, fp);
    uint32_t subChunk1Size = 16;
    std::fwrite(&subChunk1Size, 4, 1, fp);
    std::fwrite(&audioFormat,   2, 1, fp);
    std::fwrite(&numChannels,   2, 1, fp);
    std::fwrite(&sampleRate,    4, 1, fp);
    std::fwrite(&byteRate,      4, 1, fp);
    std::fwrite(&blockAlign,    2, 1, fp);
    std::fwrite(&bitsPerSample, 2, 1, fp);
    std::fwrite("data", 1, 4, fp);
    std::fwrite(&dataSize, 4, 1, fp);
    for (int i = 0; i < numSamples; i++) {
        float fval = samples[i];
        if (fval < -1.0f) fval = -1.0f;
        if (fval >  1.0f) fval =  1.0f;
        int16_t sval = (int16_t)std::floor(fval * 32767.0f + 0.5f);
        std::fwrite(&sval, sizeof(int16_t), 1, fp);
    }
    std::fclose(fp);
    return true;
}

//---------------------------------------------------------------------------
// 2) Downmix + Resample from deviceSampleRate to 16 kHz (unchanged)
//--------------------------------------------------------------------------- 
static std::vector<float> downsample_mono_16k(const int16_t* inData,
                                              size_t inFrames,
                                              int inChannels,
                                              double deviceSampleRate)
{
    float ratio = static_cast<float>(deviceSampleRate) / static_cast<float>(WHISPER_RATE);
    size_t outFrames = static_cast<size_t>(std::floor(static_cast<float>(inFrames) / ratio));
    std::vector<float> outData(outFrames);
    for (size_t i = 0; i < outFrames; i++) {
        float inPosF = i * ratio;
        size_t inPos = static_cast<size_t>(std::floor(inPosF));
        if (inPos >= inFrames) break;
        float sampleMono = 0.0f;
        if (inChannels == 1) {
            sampleMono = static_cast<float>(inData[inPos]);
        } else {
            size_t sIdx = inPos * inChannels;
            float left  = static_cast<float>(inData[sIdx]);
            float right = static_cast<float>(inData[sIdx + 1]);
            sampleMono  = 0.5f * (left + right);
        }
        outData[i] = sampleMono / 32768.0f;
    }
    return outData;
}

//---------------------------------------------------------------------------
// 3) High-Performance Ring Buffer Implementation
//--------------------------------------------------------------------------- 
class RingBuffer {
public:
    RingBuffer(size_t capacity)
        : buffer(capacity), capacity(capacity), head(0), tail(0), count(0) {}

    // Push data into the ring buffer. Drops oldest data if necessary.
    void push(const int16_t* data, size_t num) {
        std::lock_guard<std::mutex> lock(mtx);
        if (num > free_space()) {
            size_t toDrop = num - free_space();
            tail = (tail + toDrop) % capacity;
            count -= toDrop;
        }
        for (size_t i = 0; i < num; i++) {
            buffer[head] = data[i];
            head = (head + 1) % capacity;
        }
        count += num;
    }

    // Pop exactly num samples into out if available.
    bool pop(size_t num, std::vector<int16_t>& out) {
        std::lock_guard<std::mutex> lock(mtx);
        if (count < num) return false;
        out.resize(num);
        for (size_t i = 0; i < num; i++) {
            out[i] = buffer[tail];
            tail = (tail + 1) % capacity;
        }
        count -= num;
        return true;
    }

    size_t available() {
        std::lock_guard<std::mutex> lock(mtx);
        return count;
    }

private:
    size_t free_space() const { return capacity - count; }
    std::vector<int16_t> buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    mutable std::mutex mtx;
};

//---------------------------------------------------------------------------
// 4) AudioData Structure Using the Ring Buffer
//---------------------------------------------------------------------------
struct AudioData {
    RingBuffer ringBuffer;
    int channels;  // Set dynamically from selected device.
    AudioData(size_t capacity, int ch) : ringBuffer(capacity), channels(ch) {}
};

//---------------------------------------------------------------------------
// 5) Asynchronous PortAudio Callback Function
//---------------------------------------------------------------------------
static int audioCallback(const void *inputBuffer,
                         void * /*outputBuffer*/,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* /*timeInfo*/,
                         PaStreamCallbackFlags /*statusFlags*/,
                         void *userData)
{
    AudioData* audioData = reinterpret_cast<AudioData*>(userData);
    if (!inputBuffer) return paContinue;
    const int16_t* in = static_cast<const int16_t*>(inputBuffer);
    size_t numSamples = framesPerBuffer * audioData->channels;
    audioData->ringBuffer.push(in, numSamples);
    return paContinue;
}

//---------------------------------------------------------------------------
// 6) Simple VAD function based on RMS energy
//---------------------------------------------------------------------------
bool simpleVAD(const std::vector<int16_t>& audio, int /*channels*/, int /*sampleRate*/, float threshold) {
    if (audio.empty()) return false;
    double sum = 0.0;
    for (auto sample : audio) {
        sum += std::abs(sample);
    }
    double avg = sum / audio.size();
    double normalized = avg / 32767.0;  // Normalize to [0,1]
    return normalized > threshold;
}

//---------------------------------------------------------------------------
// 7) Deduplication Function: remove overlap between previous and current transcription
//---------------------------------------------------------------------------
std::string deduplicateTranscription(const std::string &prev, const std::string &curr) {
    // Find the longest suffix of prev that matches a prefix of curr.
    size_t maxOverlap = std::min(prev.size(), curr.size());
    size_t overlap = 0;
    // Require a minimum overlap length (e.g., 3 characters) for deduplication.
    for (size_t len = maxOverlap; len >= 3; len--) {
        if (prev.substr(prev.size() - len) == curr.substr(0, len)) {
            overlap = len;
            break;
        }
    }
    return (overlap > 0) ? curr.substr(overlap) : curr;
}

//---------------------------------------------------------------------------
// 8) Main Function: Dual Mode (Fixed vs. VAD) with Deduplication and Sliding Window Overlap
//---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Command-line: usage: program [mode] [vadThreshold]
    // Mode: "fixed" (default) or "vad"
    std::string mode = "fixed";
    float vadThreshold = 0.6f; // Default VAD threshold.
    if (argc >= 2) {
        mode = argv[1];
        // Convert mode to lowercase for reliability.
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    }
    if (argc >= 3) {
        vadThreshold = std::stof(argv[2]);
    }
    std::cout << "Transcription mode: " << mode << "\n";
    if(mode == "vad") {
        std::cout << "VAD threshold set to " << vadThreshold << "\n";
    }

    // Initialize PortAudio.
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Pa_Initialize error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    // List available devices.
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "Pa_GetDeviceCount() error: " << Pa_GetErrorText(numDevices) << "\n";
        Pa_Terminate();
        return 1;
    }
    std::cout << "Available Devices Across All Host APIs:\n";
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
        if (!di) continue;
        const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
        std::cout << "Device [" << i << "]: " << di->name
                  << " (Host API: " << (hai ? hai->name : "unknown") << ")";
        if (di->maxInputChannels > 0)
            std::cout << " [Input]";
        if (di->maxOutputChannels > 0)
            std::cout << " [Output]";
        std::cout << "\n";
    }

    // Filter for WASAPI devices.
    std::cout << "\nWASAPI Devices (Input or Loopback):\n";
    std::vector<PaDeviceIndex> wasapiInputDevices;
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
        if (!di) continue;
        const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
        if (!hai || hai->type != paWASAPI) continue;
        bool hasInput = (di->maxInputChannels > 0);
        int isLoop = PaWasapi_IsLoopback(i);
        if (hasInput || (isLoop == 1)) {
            wasapiInputDevices.push_back(i);
            size_t idx = wasapiInputDevices.size() - 1;
            std::cout << "[" << idx << "] " << di->name;
            if (isLoop == 1)
                std::cout << " [Loopback]";
            std::cout << "\n";
        }
    }
    if (wasapiInputDevices.empty()) {
        std::cerr << "No WASAPI input/loopback devices found!\n";
        Pa_Terminate();
        return 1;
    }

    // Let user pick a device.
    std::cout << "\nEnter the index of the WASAPI device you want: ";
    int userIndex = 0;
    std::cin >> userIndex;
    if (userIndex < 0 || userIndex >= static_cast<int>(wasapiInputDevices.size())) {
        std::cerr << "Invalid choice!\n";
        Pa_Terminate();
        return 1;
    }
    PaDeviceIndex devIndex = wasapiInputDevices[userIndex];
    const PaDeviceInfo* dInf = Pa_GetDeviceInfo(devIndex);
    if (!dInf) {
        std::cerr << "Failed to get device info!\n";
        Pa_Terminate();
        return 1;
    }
    bool inputCapable = (dInf->maxInputChannels > 0);
    int isLoop = PaWasapi_IsLoopback(devIndex);
    if (!inputCapable && (isLoop != 1)) {
        std::cerr << "Selected device is neither input nor loopback.\n";
        Pa_Terminate();
        return 1;
    }

    // Dynamic channel count.
    int channels = dInf->maxInputChannels;

    // Calculate chunk sizes.
    int chunkFrames  = static_cast<int>(dInf->defaultSampleRate * RECORD_SECONDS);
    int chunkSamples = chunkFrames * channels;
    int keepSamples  = static_cast<int>(dInf->defaultSampleRate * (KEEP_MS / 1000.0f) * channels);

    // Preallocate ring buffer with capacity for 10 chunks.
    size_t ringCapacity = static_cast<size_t>(chunkSamples * 10);
    AudioData audioData(ringCapacity, channels);

    // Set up input stream parameters.
    PaStreamParameters inParams{};
    inParams.device = devIndex;
    inParams.channelCount = channels;
    inParams.sampleFormat = paInt16;
    inParams.suggestedLatency = dInf->defaultHighInputLatency;
    inParams.hostApiSpecificStreamInfo = nullptr;

    // Open the stream in callback mode.
    PaStream* stream = nullptr;
    err = Pa_OpenStream(&stream,
                        &inParams,
                        nullptr, // no output
                        dInf->defaultSampleRate,
                        FRAMES_PER_BUFFER,
                        paClipOff,
                        audioCallback,
                        &audioData);
    if (err != paNoError) {
        std::cerr << "Pa_OpenStream error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return 1;
    }
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Pa_StartStream error: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }
    std::cout << "\nRecording from: " << dInf->name
              << (isLoop == 1 ? " (Loopback)" : " (Mic/Input)")
              << " at " << dInf->defaultSampleRate << " Hz, " << channels << " channels.\n";

    // Initialize Whisper.
    const char* modelPath = "models/ggml-base.bin";
    whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context* wctx = whisper_init_from_file_with_params(modelPath, cparams);
    if (!wctx) {
        std::cerr << "Failed to init Whisper model\n";
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    // Overlap buffer to hold the last part of the previous chunk.
    std::vector<int16_t> overlapBuffer;

    int chunkCounter = 0;
    std::string previousTranscript;  // for deduplication

    // Termination flag and input thread.
    std::atomic<bool> running(true);
    std::thread inputThread([&running]() {
        std::cout << "Press ENTER to stop...\n";
        std::cin.ignore(); // clear leftover newline
        std::cin.get();
        running = false;
    });

    std::cout << "Audio callback running asynchronously. Processing chunks...\n";

    // Main processing loop.
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Determine how many new samples we need to create a full chunk with overlap.
        size_t requiredNewSamples = chunkSamples > overlapBuffer.size() ? chunkSamples - overlapBuffer.size() : 0;
        std::vector<int16_t> newData;
        if (requiredNewSamples > 0) {
            if (!audioData.ringBuffer.pop(requiredNewSamples, newData))
                continue;  // not enough new data yet
        }
        
        // Build the full chunk: previous overlap + new data.
        std::vector<int16_t> fullChunk;
        fullChunk.reserve(overlapBuffer.size() + newData.size());
        fullChunk.insert(fullChunk.end(), overlapBuffer.begin(), overlapBuffer.end());
        fullChunk.insert(fullChunk.end(), newData.begin(), newData.end());
        if (fullChunk.size() < static_cast<size_t>(chunkSamples))
            continue;  // safety check

        // For VAD mode: check if there's speech in this chunk.
        if (mode == "vad") {
            if (!simpleVAD(fullChunk, channels, static_cast<int>(dInf->defaultSampleRate), vadThreshold)) {
                // No speech detected – update overlap buffer and wait.
                if (fullChunk.size() >= static_cast<size_t>(keepSamples)) {
                    overlapBuffer.assign(fullChunk.end() - keepSamples, fullChunk.end());
                }
                continue;
            }
        }
        
        // Downmix & resample the full chunk.
        std::vector<float> mono16k = downsample_mono_16k(
            fullChunk.data(),
            fullChunk.size() / channels,
            channels,
            dInf->defaultSampleRate
        );

        // (Optional) Save WAV for debugging.
        {
            std::string fname = "chunk_" + std::to_string(chunkCounter++) + ".wav";
            if (!save_wav_16bit(fname, mono16k.data(), static_cast<int>(mono16k.size()), WHISPER_RATE)) {
                std::cerr << "Failed to save WAV: " << fname << "\n";
            } else {
                std::cout << "[Debug] Wrote " << fname << " (" << mono16k.size() << " samples)\n";
            }
        }

        // Transcribe with Whisper.
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress   = false;
        wparams.print_special    = false;
        wparams.print_realtime   = false;
        wparams.print_timestamps = false;
        wparams.translate        = false;  // change to true if translation is desired
        wparams.language         = "auto";
        wparams.n_threads        = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));

        int ret = whisper_full(wctx, wparams, mono16k.data(), static_cast<int>(mono16k.size()));
        if (ret != 0) {
            std::cerr << "whisper_full() failed!\n";
        } else {
            std::string currentTranscript;
            int n_segments = whisper_full_n_segments(wctx);
            for (int i = 0; i < n_segments; i++) {
                const char* text = whisper_full_get_segment_text(wctx, i);
                if (text)
                    currentTranscript += text;
            }

            // Deduplicate with the previous transcript.
            std::string deduped = deduplicateTranscription(previousTranscript, currentTranscript);
            std::cout << "\n[Transcription] " << deduped << "\n";
            previousTranscript = currentTranscript; // update for future deduplication
        }

        // Update the overlap buffer to keep the last keepSamples of the full chunk.
        if (fullChunk.size() >= static_cast<size_t>(keepSamples))
            overlapBuffer.assign(fullChunk.end() - keepSamples, fullChunk.end());
    } // end main loop

    running = false;
    if (inputThread.joinable())
        inputThread.join();

    std::cout << "Terminating... cleaning up resources.\n";
    whisper_free(wctx);
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
