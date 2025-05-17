#include <iostream>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <mmreg.h>
#include "portaudio.h"
#include "pa_win_wasapi.h"

#define NUM_SECONDS 5
#define FRAMES_PER_BUFFER 1024

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    PaError err;

    // Initialize PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Error initializing PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return -1;
    }

    // List all WASAPI devices
    PaDeviceIndex deviceCount = Pa_GetDeviceCount();
    if (deviceCount < 0) {
        std::cerr << "Error getting device count: " << Pa_GetErrorText(deviceCount) << std::endl;
        Pa_Terminate();
        return -1;
    }

    std::cout << "Available Devices Across All Host APIs:" << std::endl;
    for (PaDeviceIndex i = 0; i < deviceCount; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);

        std::cout << "Device [" << i << "]: " << deviceInfo->name
                << " (Host API: " << hostApiInfo->name << ")";
        if (deviceInfo->maxInputChannels > 0)
            std::cout << " [Input]";
        if (deviceInfo->maxOutputChannels > 0)
            std::cout << " [Output]";
        std::cout << std::endl;
    }

    std::vector<PaDeviceIndex> wasapiDeviceIndices;

    std::cout << "Available WASAPI Devices:" << std::endl;
    for (PaDeviceIndex i = 0; i < deviceCount; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);

        if (hostApiInfo->type == paWASAPI) {
            wasapiDeviceIndices.push_back(i); // Keep track of WASAPI devices

            size_t displayIndex = wasapiDeviceIndices.size() - 1;

            std::cout << "[" << displayIndex << "] " << deviceInfo->name << " (";
            if (deviceInfo->maxInputChannels > 0)
                std::cout << "Input";
            if (deviceInfo->maxOutputChannels > 0) {
                if (deviceInfo->maxInputChannels > 0)
                    std::cout << ", ";
                std::cout << "Output";
            }
            std::cout << ")";
            std::cout << std::endl;
        }
    }

    // Check if we have any WASAPI devices
    if (wasapiDeviceIndices.empty()) {
        std::cerr << "No WASAPI devices found." << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Prompt the user to select a device
    size_t selectedDeviceIndex;
    std::cout << "Enter the device number you wish to record from: ";
    std::cin >> selectedDeviceIndex;

    // Validate the input
    if (selectedDeviceIndex >= wasapiDeviceIndices.size()) {
        std::cerr << "Invalid device number." << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Map back to the actual device index
    PaDeviceIndex actualDeviceIndex = wasapiDeviceIndices[selectedDeviceIndex];
    const PaDeviceInfo* selectedDeviceInfo = Pa_GetDeviceInfo(actualDeviceIndex);
    const PaHostApiInfo* selectedHostApiInfo = Pa_GetHostApiInfo(selectedDeviceInfo->hostApi);

    // Determine if the device is suitable for recording
    bool isInputDevice = selectedDeviceInfo->maxInputChannels > 0;
    bool isLoopbackDevice = false;

    int isLoopback = PaWasapi_IsLoopback(actualDeviceIndex);
    if (isLoopback == 1) {
        std::cout << "Is loopback device" << std::endl;
        isLoopbackDevice = true;
    } else if (isLoopback < 0) {
        std::cerr << "Error checking if device " << actualDeviceIndex << " is loopback: " << Pa_GetErrorText(isLoopback) << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Check if the device can be used for recording
    if (!isInputDevice && !isLoopbackDevice) {
        std::cerr << "Selected device cannot be used for recording." << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Set up input stream parameters
    PaStreamParameters inputParameters{};
    inputParameters.device = actualDeviceIndex;
    inputParameters.channelCount = selectedDeviceInfo->maxInputChannels;
    inputParameters.sampleFormat = paInt16;
    inputParameters.suggestedLatency = selectedDeviceInfo->defaultHighInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open input stream
    PaStream* inputStream;
    err = Pa_OpenStream(
        &inputStream,
        &inputParameters,
        nullptr,
        selectedDeviceInfo->defaultSampleRate,
        FRAMES_PER_BUFFER,
        paNoFlag,
        nullptr,
        nullptr
    );
    if (err != paNoError) {
        std::cerr << "Error opening input stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    err = Pa_StartStream(inputStream);
    if (err != paNoError) {
        std::cerr << "Error starting input stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(inputStream);
        Pa_Terminate();
        return -1;
    }

    // Allocate buffer for recording
    long totalFrames = NUM_SECONDS * static_cast<long>(selectedDeviceInfo->defaultSampleRate);
    size_t numSamples = totalFrames * inputParameters.channelCount;
    size_t bytesPerSample = Pa_GetSampleSize(inputParameters.sampleFormat);
    if (bytesPerSample == paSampleFormatNotSupported) {
        std::cerr << "Sample format not supported." << std::endl;
        Pa_StopStream(inputStream);
        Pa_CloseStream(inputStream);
        Pa_Terminate();
        return -1;
    }

    std::vector<uint8_t> recordedSamples(numSamples * bytesPerSample);

    std::cout << "Recording..." << std::endl;
    long numFramesCaptured = 0;
    while (numFramesCaptured < totalFrames) {
        long framesToRead = FRAMES_PER_BUFFER;
        if (numFramesCaptured + framesToRead > totalFrames) {
            framesToRead = totalFrames - numFramesCaptured;
        }
        void* bufferPtr = recordedSamples.data() + numFramesCaptured * inputParameters.channelCount * bytesPerSample;
        err = Pa_ReadStream(inputStream, bufferPtr, framesToRead);
        if (err != paNoError) {
            std::cerr << "Error reading from stream: " << Pa_GetErrorText(err) << std::endl;
            break;
        }
        numFramesCaptured += framesToRead;
    }
    std::cout << "Finished recording." << std::endl;

    // Close input stream
    err = Pa_StopStream(inputStream);
    if (err != paNoError) {
        std::cerr << "Error stopping input stream: " << Pa_GetErrorText(err) << std::endl;
    }
    Pa_CloseStream(inputStream);

    // Play back the recorded audio
    PaDeviceIndex outputDeviceIndex = Pa_GetDefaultOutputDevice();
    if (outputDeviceIndex == paNoDevice) {
        std::cerr << "No default output device." << std::endl;
        Pa_Terminate();
        return -1;
    }

    // Set up output stream parameters
    PaStreamParameters outputParameters{};
    outputParameters.device = outputDeviceIndex;
    outputParameters.channelCount = Pa_GetDeviceInfo(outputDeviceIndex)->maxOutputChannels;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultHighOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open output stream
    PaStream* outputStream;
    err = Pa_OpenStream(
        &outputStream,
        nullptr,
        &outputParameters,
        selectedDeviceInfo->defaultSampleRate,
        FRAMES_PER_BUFFER,
        paNoFlag,
        nullptr,
        nullptr
    );
    if (err != paNoError) {
        std::cerr << "Error opening output stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return -1;
    }

    err = Pa_StartStream(outputStream);
    if (err != paNoError) {
        std::cerr << "Error starting output stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(outputStream);
        Pa_Terminate();
        return -1;
    }

    std::cout << "Playing back recorded audio..." << std::endl;
    long numFramesPlayed = 0;
    while (numFramesPlayed < totalFrames) {
        long framesToWrite = FRAMES_PER_BUFFER;
        if (numFramesPlayed + framesToWrite > totalFrames) {
            framesToWrite = totalFrames - numFramesPlayed;
        }
        void* bufferPtr = recordedSamples.data() + numFramesPlayed * outputParameters.channelCount * bytesPerSample;
        err = Pa_WriteStream(outputStream, bufferPtr, framesToWrite);
        if (err != paNoError) {
            std::cerr << "Error writing to output stream: " << Pa_GetErrorText(err) << std::endl;
            break;
        }
        numFramesPlayed += framesToWrite;
    }
    std::cout << "Finished playback." << std::endl;

    // Close output stream
    err = Pa_StopStream(outputStream);
    if (err != paNoError) {
        std::cerr << "Error stopping output stream: " << Pa_GetErrorText(err) << std::endl;
    }
    Pa_CloseStream(outputStream);

    // Clean up
    Pa_Terminate();

    return 0;
}
