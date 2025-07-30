#include "WindowsAudioPlayback.hpp"
#include <iostream>

WindowsAudioPlayback::WindowsAudioPlayback() : stream_(nullptr) {}

WindowsAudioPlayback::~WindowsAudioPlayback() {
}

bool WindowsAudioPlayback::start(AAudioDevice *device, std::vector<uint8_t> &capturedData, double sampleRate) {
    WindowsAudioDevice windowsDevice(*device);

    std::cout << "Setting up playback with " << windowsDevice.getDeviceInfo().maxOutputChannels << " channels at " << windowsDevice.getDeviceInfo().defaultSampleRate << " Hz." << std::endl;
    PaError err = Pa_OpenStream(&stream_, nullptr, &windowsDevice.getStreamParams(), sampleRate, 1024, paClipOff, nullptr, nullptr);
    if (err != paNoError) {
        std::cerr << "Error opening output stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "Error starting output stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream_);
        return false;
    }
    std::cout << "Playback started successfully on device: " << windowsDevice.getDeviceInfo().name << std::endl;
    std::cout << "Playing back recorded audio..." << std::endl;
    long numFramesPlayed = 0;
    long totalFrames = 5 * static_cast<long>(sampleRate);
    size_t bytesPerSample = Pa_GetSampleSize(paInt16);
    while (numFramesPlayed < totalFrames) {
        long framesToWrite = 1024;
        if (numFramesPlayed + framesToWrite > totalFrames) {
            framesToWrite = totalFrames - numFramesPlayed;
        }
        void *bufferPtr = capturedData.data() + numFramesPlayed * device->getDeviceInfo().maxOutputChannels * bytesPerSample;
        err = Pa_WriteStream(stream_, bufferPtr, framesToWrite);
        if (err != paNoError) {
            std::cerr << "Error writing to output stream: " << Pa_GetErrorText(err) << std::endl;
            break;
        }
        numFramesPlayed += framesToWrite;
    }
    std::cout << "Finished playback." << std::endl;
    err = Pa_StopStream(stream_);
    if (err != paNoError) {
        std::cerr << "Error stopping output stream: " << Pa_GetErrorText(err) << std::endl;
    }
    Pa_CloseStream(stream_);
    return true;
}
