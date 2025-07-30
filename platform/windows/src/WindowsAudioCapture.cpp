#include "WindowsAudioCapture.hpp"
#include "WindowsAudioDevice.hpp"
#include <iostream>
#include <windows.h>
#include <pa_win_wasapi.h>

WindowsAudioCapture::WindowsAudioCapture() : stream_(nullptr), sampleRate_(0) {
}

WindowsAudioCapture::~WindowsAudioCapture() {
}

bool WindowsAudioCapture::open_stream(WindowsAudioDevice *windowsDevice)
{
    PaError err;

    err = Pa_IsFormatSupported(&windowsDevice->getStreamParams(), nullptr, windowsDevice->getDeviceInfo().defaultSampleRate);
    if (err != paFormatIsSupported) {
        std::cerr << "Format not supported for input on this device: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    std::cout << "Configuring input capture with " << windowsDevice->getStreamParams().channelCount << " channels." << std::endl;
    err = Pa_OpenStream(&stream_, &windowsDevice->getStreamParams(), nullptr, sampleRate_, 1024, paNoFlag, nullptr, nullptr);
    if (err != paNoError) {
        std::cerr << "Error opening input stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    return true;
}

bool WindowsAudioCapture::start_stream(WindowsAudioDevice *windowsDevice) {
    PaError err;

    err = Pa_StartStream(stream_);
    std::cout << "Stream started." << std::endl;
    if (err != paNoError) {
        std::cerr << "Error starting input stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    std::cout << "Audio capture started on device: " << windowsDevice->getDeviceInfo().name
              << " with " << windowsDevice->getStreamParams().channelCount << " channels " << sampleRate_ << " Hz." << std::endl;
    return true;
}

bool WindowsAudioCapture::read_stream(WindowsAudioDevice *windowsDevice, std::chrono::seconds duration) {
    PaError err;
    long totalFrames = static_cast<long>(duration.count()) * static_cast<long>(sampleRate_);
    long numFramesCaptured = 0;
    size_t numSamples = totalFrames * windowsDevice->getStreamParams().channelCount;
    size_t bytesPerSample = Pa_GetSampleSize(windowsDevice->getStreamParams().sampleFormat);
    // Allocate buffer for recording
    if (bytesPerSample == paSampleFormatNotSupported) {
        std::cerr << "Sample format not supported." << std::endl;
        return false;
    }
    capturedData_.resize(numSamples * bytesPerSample);
    std::cout << "Recording..." << std::endl;
    while (numFramesCaptured < totalFrames) {
        long framesToRead = 1024;
        if (numFramesCaptured + framesToRead > totalFrames) {
            framesToRead = totalFrames - numFramesCaptured;
        }
        void* bufferPtr = capturedData_.data() + numFramesCaptured * windowsDevice->getStreamParams().channelCount * bytesPerSample;
        err = Pa_ReadStream(stream_, bufferPtr, framesToRead);
        if (err != paNoError) {
            std::cerr << "Error reading from stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        numFramesCaptured += framesToRead;
    }
    std::cout << "Finished recording." << std::endl;
    return true;
}

bool WindowsAudioCapture::start(AAudioDevice *device, std::chrono::seconds duration) {
    WindowsAudioDevice windowsDevice(*device);

    if (windowsDevice.getHostAPIInfo().type != paWASAPI) {
        std::cerr << "Error: The selected device is not using the WASAPI host API." << std::endl;
        return false;
    }
    if (windowsDevice.getDeviceType() == DeviceType::Input || windowsDevice.getDeviceType() == DeviceType::LoopBack) {
        sampleRate_ = windowsDevice.getDeviceInfo().defaultSampleRate;
        if (!open_stream(&windowsDevice)) {
            return false;
        }
        if (!start_stream(&windowsDevice)) {
            Pa_CloseStream(stream_);
            return false;
        }
        if (!read_stream(&windowsDevice, duration)) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            return false;
        }
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
    } else {
        std::cerr << "Unsupported device type." << std::endl;
        return false;
    }
    return true;
}

bool WindowsAudioCapture::close_stream() {
    PaError err;
    err = Pa_StopStream(stream_);
    if (err != paNoError) {
        std::cerr << "Error stopping input stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    Pa_CloseStream(stream_);
    return true;
}

std::vector<uint8_t> WindowsAudioCapture::getCapturedData() const {
    return capturedData_;
}

double WindowsAudioCapture::getSampleRate() const {
    return sampleRate_;
}
