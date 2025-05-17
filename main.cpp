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

// PortAudio
#include "portaudio.h"
#include "pa_win_wasapi.h"

// Whisper
#include "whisper.h"

//---------------------------------------------------------------------------
// Constants for chunk-based approach
//---------------------------------------------------------------------------
static const int   FRAMES_PER_BUFFER = 1024; // typical WASAPI buffer size
static const int   CHUNK_MS         = 100;   // how many ms of audio we read each loop
static const int   WHISPER_RATE     = 16000; // Whisper expects 16 kHz input
static const float RECORD_SECONDS   = 2.0f;  // chunk length in seconds

//---------------------------------------------------------------------------
// 1) Save 16-bit mono WAV for debugging
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

    // WAV header fields
    uint32_t chunkSize      = 36 + (numSamples * 2);
    uint16_t audioFormat    = 1;   // PCM
    uint16_t numChannels    = 1;   // mono
    uint32_t byteRate       = sampleRate * numChannels * 2;
    uint16_t blockAlign     = numChannels * 2;
    uint16_t bitsPerSample  = 16;
    uint32_t dataSize       = numSamples * 2;

    // RIFF header
    std::fwrite("RIFF", 1, 4, fp);
    std::fwrite(&chunkSize, 4, 1, fp);
    std::fwrite("WAVE", 1, 4, fp);

    // fmt chunk
    std::fwrite("fmt ", 1, 4, fp);
    uint32_t subChunk1Size = 16;
    std::fwrite(&subChunk1Size, 4, 1, fp);
    std::fwrite(&audioFormat,   2, 1, fp);
    std::fwrite(&numChannels,   2, 1, fp);
    std::fwrite(&sampleRate,    4, 1, fp);
    std::fwrite(&byteRate,      4, 1, fp);
    std::fwrite(&blockAlign,    2, 1, fp);
    std::fwrite(&bitsPerSample, 2, 1, fp);

    // data chunk
    std::fwrite("data", 1, 4, fp);
    std::fwrite(&dataSize, 4, 1, fp);

    // samples: float -> int16
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
// 2) Downmix + Resample from deviceSampleRate to 16 kHz
//---------------------------------------------------------------------------
static std::vector<float> downsample_mono_16k(const int16_t* inData,
                                              size_t inFrames,
                                              int inChannels,
                                              double deviceSampleRate)
{
    // e.g. if deviceSampleRate = 48000, ratio = 48000 / 16000 = 3
    // if deviceSampleRate = 16000, ratio = 1, etc.
    float ratio = (float)deviceSampleRate / (float)WHISPER_RATE;

    size_t outFrames = (size_t)std::floor((float)inFrames / ratio);
    std::vector<float> outData(outFrames);

    for (size_t i = 0; i < outFrames; i++) {
        float inPosF = i * ratio;
        size_t inPos = (size_t)std::floor(inPosF);
        if (inPos >= inFrames) {
            break;
        }

        float sampleMono = 0.0f;
        if (inChannels == 1) {
            // single channel
            sampleMono = (float)inData[inPos];
        } else {
            // naive stereo downmix
            size_t sIdx = inPos * inChannels;
            float left  = (float)inData[sIdx];
            float right = (float)inData[sIdx + 1];
            sampleMono  = 0.5f * (left + right);
        }

        // scale int16 -> float [-1,1]
        outData[i] = sampleMono / 32768.0f;
    }

    return outData;
}

//---------------------------------------------------------------------------
// 3) Main
//---------------------------------------------------------------------------
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 3A) Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Pa_Initialize error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    // 3B) List all devices (all host APIs)
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
        if (di->maxInputChannels > 0)  std::cout << " [Input]";
        if (di->maxOutputChannels > 0) std::cout << " [Output]";
        std::cout << "\n";
    }

    // 3C) Filter for WASAPI devices that can do input or loopback
    std::cout << "\nWASAPI Devices (Input or Loopback):\n";
    std::vector<PaDeviceIndex> wasapiInputDevices;
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(i);
        if (!di) continue;
        const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
        if (!hai || hai->type != paWASAPI) continue;

        bool hasInput = (di->maxInputChannels > 0);
        int isLoop    = PaWasapi_IsLoopback(i); // 1 if loopback, 0 normal, <0 error
        if (hasInput || (isLoop == 1)) {
            wasapiInputDevices.push_back(i);
            size_t idx = wasapiInputDevices.size() - 1;

            std::cout << "[" << idx << "] " << di->name;
            if (isLoop == 1) {
                std::cout << " [Loopback]";
            }
            std::cout << "\n";
        }
    }

    if (wasapiInputDevices.empty()) {
        std::cerr << "No WASAPI input/loopback devices found!\n";
        Pa_Terminate();
        return 1;
    }

    // 3D) User picks device
    std::cout << "\nEnter the index of the WASAPI device you want: ";
    int userIndex = 0;
    std::cin >> userIndex;
    if (userIndex < 0 || userIndex >= (int)wasapiInputDevices.size()) {
        std::cerr << "Invalid choice!\n";
        Pa_Terminate();
        return 1;
    }

    PaDeviceIndex devIndex   = wasapiInputDevices[userIndex];
    const PaDeviceInfo* dInf = Pa_GetDeviceInfo(devIndex);
    if (!dInf) {
        std::cerr << "Failed to get device info!\n";
        Pa_Terminate();
        return 1;
    }

    // 3E) Check if it's truly input-capable or loopback
    bool inputCapable = (dInf->maxInputChannels > 0);
    int isLoop        = PaWasapi_IsLoopback(devIndex);

    if (!inputCapable && (isLoop != 1)) {
        std::cerr << "Selected device is neither input nor loopback.\n";
        Pa_Terminate();
        return 1;
    }

    // 3F) Open a blocking input stream at device sample rate
    PaStreamParameters inParams{};
    inParams.device           = devIndex;
    inParams.channelCount     = dInf->maxInputChannels;  // e.g. 1 or 2
    inParams.sampleFormat     = paInt16;
    inParams.suggestedLatency = dInf->defaultHighInputLatency;

    double deviceSampleRate   = dInf->defaultSampleRate;  // e.g. 16000 or 48000

    PaStream* stream = nullptr;
    err = Pa_OpenStream(
        &stream,
        &inParams,
        nullptr,
        deviceSampleRate,
        FRAMES_PER_BUFFER,
        paNoFlag,
        nullptr,
        nullptr
    );
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
              << (isLoop==1 ? " (Loopback)" : " (Mic/Input)")
              << " at " << deviceSampleRate << " Hz, "
              << dInf->maxInputChannels << " channels.\n";

    // 3G) Initialize Whisper
    const char* modelPath = "models/ggml-base.bin";
    whisper_context_params cparams = whisper_context_default_params();
    // e.g. cparams.use_gpu = false; // if you want CPU only

    struct whisper_context* wctx = whisper_init_from_file_with_params(modelPath, cparams);
    if (!wctx) {
        std::cerr << "Failed to init Whisper model\n";
        Pa_Terminate();
        return 1;
    }

    // We capture ~2s each chunk
    int chunkFrames   = (int)(deviceSampleRate * RECORD_SECONDS);
    int chunkSamples  = chunkFrames * dInf->maxInputChannels;
    std::vector<int16_t> audioBuffer(chunkSamples);

    // We'll read ~CHUNK_MS each iteration
    int framesPerRead = (int)((deviceSampleRate * CHUNK_MS)/1000.0 + 0.5);
    int samplesPerRead= framesPerRead * dInf->maxInputChannels;
    int collectedFrames = 0;
    int chunkCounter    = 0;

    // 3H) Capture + Transcribe Loop
    while (true) {
        // read up to framesPerRead
        int16_t* ptr = audioBuffer.data() + collectedFrames * dInf->maxInputChannels;
        int framesToRead = framesPerRead;
        if (collectedFrames + framesToRead > chunkFrames) {
            framesToRead = chunkFrames - collectedFrames;
        }
        if (framesToRead <= 0) {
            // chunk is full
        } else {
            err = Pa_ReadStream(stream, ptr, framesToRead);
            if (err != paNoError) {
                std::cerr << "Pa_ReadStream error: " << Pa_GetErrorText(err) << "\n";
                break;
            }
            collectedFrames += framesToRead;
        }

        // if we have ~2s, downsample -> whisper
        if (collectedFrames >= chunkFrames) {
            // Downmix + resample to 16k
            std::vector<float> mono16k = downsample_mono_16k(
                audioBuffer.data(),
                collectedFrames,
                dInf->maxInputChannels,
                deviceSampleRate
            );

            // (Optional) Save chunk to WAV
            {
                std::string fname = "chunk_" + std::to_string(chunkCounter++) + ".wav";
                if (!save_wav_16bit(fname, mono16k.data(), (int)mono16k.size(), 16000)) {
                    std::cerr << "Failed to save WAV: " << fname << "\n";
                } else {
                    std::cout << "[Debug] Wrote " << fname << " (" << mono16k.size() << " samples)\n";
                }
            }

            // Transcribe with whisper
            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            wparams.print_progress   = false;
            wparams.print_special    = false;
            wparams.print_realtime   = false;
            wparams.print_timestamps = false;
            wparams.translate        = false; // set true if you want translation
            wparams.language         = "auto";
            wparams.n_threads        = std::min(4, (int)std::thread::hardware_concurrency());

            int ret = whisper_full(wctx, wparams, mono16k.data(), (int)mono16k.size());
            if (ret != 0) {
                std::cerr << "whisper_full() failed!\n";
            } else {
                int n_segments = whisper_full_n_segments(wctx);
                std::cout << "\n[Transcription] ";
                for (int i = 0; i < n_segments; i++) {
                    const char* text = whisper_full_get_segment_text(wctx, i);
                    std::cout << text;
                }
                std::cout << "\n";
            }

            // reset for next chunk
            collectedFrames = 0;
        }

        // small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // cleanup
    whisper_free(wctx);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
