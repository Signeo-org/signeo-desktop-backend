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
#include <fstream>
#include <sstream>

// PortAudio
#include "portaudio.h"
#include "pa_win_wasapi.h"

// Whisper
#include "whisper.h"

//TEST
#include <filesystem>

//---------------------------------------------------------------------------
// 1) Save 16-bit mono WAV for debugging (unchanged)
//--------------------------------------------------------------------------- 
static bool save_wav_16bit(const std::string &filename,
                           const float *samples,
                           int numSamples,
                           int sampleRate) {
    //safe wav into debug folder
    std::filesystem::path debugDir = "debug";
    if (!std::filesystem::exists(debugDir)) {
        std::filesystem::create_directory(debugDir);
    }
    std::filesystem::path filePath = debugDir / filename;
    FILE* fp = nullptr;
    errno_t err = fopen_s(&fp, filePath.string().c_str(), "wb");
    if (err != 0 || !fp) {
        char errMsg[256];
        strerror_s(errMsg, sizeof(errMsg), err);
        std::cerr << "Failed to open file for WAV: " << errMsg << std::endl;
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
                                              double deviceSampleRate,
                                              int whisperRate)
{
    float ratio = static_cast<float>(deviceSampleRate) / static_cast<float>(whisperRate);
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
                         void *userData) {
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
    if (overlap > 0) {
        return curr;
    }
    return curr.substr(overlap);
}

//---------------------------------------------------------------------------
// 8) Main Function: Dual Mode (Fixed vs. VAD) with Deduplication and Sliding Window Overlap
//---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string arg = "";
    std::string modelPath = "models/ggml-base.bin";
    std::string mode = "fixed";
    float vadThreshold = 0.6f;
    float recordSeconds = 2.0f;
    bool debug = false;
    int framesPerBuffer = 256;
    int chunkMs = 100;
    int keepMs = 200;
    int whisperRate = 16000;

    std::cout << std::endl << "+--------------------------+" << std::endl;
    std::cout << "|Audio Transcription Tool|" << std::endl;
    std::cout << "+--------------------------+" << std::endl;
    for (int i = 1; i < argc; ++i) {
        arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl
            << "Options:" << std::endl
            << "  -h, --help           Show this help message" << std::endl
            << "  -f, --fixed          Use fixed mode without VAD processing (default)" << std::endl
            << "  -v, --vad            Enable Voice Activity Detection mode" << std::endl
            << "  -m, --model <path>   Path to the Whisper model file" << std::endl
            << "  -d, --debug          Enable debug mode (saves WAV files for each chunk)" << std::endl;
            return 0;
        }
        if (arg == "-d" || arg == "--debug") {
            debug = true;
            std::cout << "Debug mode enabled: WAV files will be saved." << std::endl;
        }
        if (arg == "-f" || arg == "--fixed")
            mode = "fixed";
        if (arg == "-v" || arg == "--vad")
            mode = "vad";
        if (arg == "-m" || arg == "--model") {
            i++;
            if (i >= argc) {
                std::cerr << "Error: No model path provided after -m/--model option." << std::endl;
                return 1;
            }
            modelPath = argv[i];
        }
    }
    std::cout << "Transcription mode: " << mode << std::endl;
    std::cout << "Using Whisper model: " << modelPath << std::endl;

    //check if model file exists
    if (!std::filesystem::exists(modelPath)) {
        std::cerr << "Error: Model file does not exist at " << modelPath << std::endl;
        return 1;
    }

    // Initialize PortAudio.
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Pa_Initialize error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // List available devices.
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "Pa_GetDeviceCount() error: " << Pa_GetErrorText(numDevices) << std::endl;
        Pa_Terminate();
        return 1;
    }
    
    const PaDeviceIndex defCapture = paNoDevice;
    const PaDeviceIndex defLoopback = paNoDevice;
    if (debug == true) {
        std::cout << "Available Devices Across All Host APIs:" << std::endl;
        for (int i = 0; i < numDevices; i++) {
            const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
            if (!di) continue;
            const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
            if (!hai) continue;
            if (di->maxInputChannels > 0) {
                std::cout << "Device [" << i << "]: " << di->name
                          << " (Host API: " << (hai ? hai->name : "unknown") << ")";
                if (di->maxInputChannels > 0)
                    std::cout << " [Input]";
                if (di->maxOutputChannels > 0)
                    std::cout << " [Output]";
                if (hai->defaultInputDevice == i || hai->defaultOutputDevice == i)
                    std::cout << " (Default)";
                std::cout << std::endl;
            }
        }
    }

    // Filter for WASAPI devices.
    std::cout << "Audio Api: WASAPI" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "Devices (Input or Loopback):" << std::endl;
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
            if (di->maxInputChannels > 0)
                std::cout << " [Input]";
            if (di->maxOutputChannels > 0)
                std::cout << " [Output]";
            if (hai->defaultInputDevice == i || hai->defaultOutputDevice == i)
                std::cout << " (Default)";
            std::cout << std::endl;
        }
    }
    if (wasapiInputDevices.empty()) {
        std::cerr << "No WASAPI input/loopback devices found!" << std::endl;
        Pa_Terminate();
        return 1;
    }

    // Let user pick a device.
    bool selectionMade = false;
    std::cout << std::endl << "Enter the index of the device you want or Press ENTER to stop..." << std::endl;
    int userIndex = 0;
    std::string line = "";
    while (selectionMade == false) {
        if (!std::getline(std::cin, line)) {      // EOF / stream error
            std::cerr << "Input error - exiting..." << std::endl;
            Pa_Terminate();
            return 1;
        }
        if (line.empty()) {
            std::cout << "No selection made - exiting..." << std::endl;
            Pa_Terminate();
            return 0;
        }
        std::istringstream iss(line);
        if (!(iss >> userIndex)) {                // text wasn’t a number
            std::cerr << "That wasn’t a valid number - exiting..." << std::endl;
            Pa_Terminate();
            return 1;
        }
        if (userIndex > 0 && userIndex < static_cast<int>(wasapiInputDevices.size())) {
            selectionMade = true;
        } else {
            std::cerr << "Invalid choice try again!" << std::endl;
        }
    }

    PaDeviceIndex devIndex = wasapiInputDevices[userIndex];
    const PaDeviceInfo* dInf = Pa_GetDeviceInfo(devIndex);
    if (!dInf) {
        std::cerr << "Failed to get device info!" << std::endl;
        Pa_Terminate();
        return 1;
    }
    bool inputCapable = (dInf->maxInputChannels > 0);
    int isLoop = PaWasapi_IsLoopback(devIndex);
    if (!inputCapable && (isLoop != 1)) {
        std::cerr << "Selected device is neither input nor loopback." << std::endl;
        Pa_Terminate();
        return 1;
    }

    // Dynamic channel count.
    int channels = dInf->maxInputChannels;

    // Calculate chunk sizes.
    int chunkFrames  = static_cast<int>(dInf->defaultSampleRate * recordSeconds);
    int chunkSamples = chunkFrames * channels;
    int keepSamples  = static_cast<int>(dInf->defaultSampleRate * (keepMs / 1000.0f) * channels);

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
                        framesPerBuffer,
                        paClipOff,
                        audioCallback,
                        &audioData);
    if (err != paNoError) {
        std::cerr << "Pa_OpenStream error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return 1;
    }
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Pa_StartStream error: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }
    std::cout << "Selected device: " << dInf->name
              << " at " << dInf->defaultSampleRate << " Hz, " << channels << " channels." << std::endl;;
    std::cout << "--------------------------------------------------" << std::endl;
    // Initialize Whisper.
    whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context* wctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
    if (!wctx) {
        std::cerr << "Failed to init Whisper model" << std::endl;
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    // Overlap buffer to hold the last part of the previous chunk.
    std::vector<int16_t> overlapBuffer = std::vector<int16_t>(keepSamples, 0);

    int chunkCounter = 0;
    std::string previousTranscript = "";

    // Termination flag and input thread.
    std::atomic<bool> running(true);
    std::thread inputThread([&running]() {
        std::cout << "Press ENTER to stop..." << std::endl;
        std::cin.ignore(); // clear leftover newline
        std::cin.get();
        running = false;
    });

    std::cout << "Audio callback running asynchronously. Processing chunks..." << std::endl;

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
            dInf->defaultSampleRate,
            whisperRate
        );

        
        if (debug == true) {
            std::string fname = "chunk_" + std::to_string(chunkCounter++) + ".wav";
            if (!save_wav_16bit(fname, mono16k.data(), static_cast<int>(mono16k.size()), whisperRate)) {
                std::cerr << "Failed to save WAV: " << fname << std::endl;
            } else {
                std::cout << "[Debug] Wrote " << fname << " (" << mono16k.size() << " samples)";
            }
        }
        // Transcribe with Whisper.
        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress   = false;
        wparams.print_special    = false;
        wparams.print_realtime   = false;
        wparams.print_timestamps = false;
        wparams.translate        = false; 
        wparams.language         = "en";
        wparams.n_threads        = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));

        int ret = whisper_full(wctx, wparams, mono16k.data(), static_cast<int>(mono16k.size()));
        if (ret != 0) {
            std::cerr << "whisper_full() failed!" << std::endl;
        } else {
            std::string currentTranscript = "";
            int n_segments = whisper_full_n_segments(wctx);
            for (int i = 0; i < n_segments; i++) {
                const char* text = whisper_full_get_segment_text(wctx, i);
                if (text)
                    currentTranscript += text;
            }

            // Deduplicate with the previous transcript.
            std::string deduped = deduplicateTranscription(previousTranscript, currentTranscript);
            if (debug == true) {
                std::cout << "[Debug] Previous: " << previousTranscript << std::endl;
                std::cout << "[Debug] Current: " << currentTranscript << std::endl;
                std::cout << "[Debug] Deduped: " << deduped << std::endl;
                // Save the deduplicated transcription to a file.
                // This will overwrite the file each time.
                std::ofstream outFile("transcription.txt", std::ios::out | std::ios::trunc);
                if (outFile.is_open()) {
                    outFile << deduped << std::endl;
                } else {
                    std::cerr << "Failed to open transcription.txt for writing." << std::endl;
                }
            }
            std::cout << "[Transcription] " << deduped << std::endl;
            previousTranscript = currentTranscript; // update for future deduplication
        }

        // Update the overlap buffer to keep the last keepSamples of the full chunk.
        if (fullChunk.size() >= static_cast<size_t>(keepSamples))
            overlapBuffer.assign(fullChunk.end() - keepSamples, fullChunk.end());
    }

    running = false;
    if (inputThread.joinable())
        inputThread.join();

    std::cout << "Terminating... cleaning up resources." << std::endl;
    whisper_free(wctx);
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
