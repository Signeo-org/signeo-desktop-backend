#include "WindowsAudioDevice.hpp" 

WindowsAudioDevice::WindowsAudioDevice(int deviceId) : AAudioDevice(deviceId) {
    PaError err;
    wasapiInfo_.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo_.hostApiType = hostApiInfo_->type;
    wasapiInfo_.version = 1;
    wasapiInfo_.flags = paWinWasapiAutoConvert;

    int isLoopback = PaWasapi_IsLoopback(deviceId);
    if (isLoopback == 1) {
        deviceType_ = DeviceType::LoopBack;
    } else if (isLoopback < 0) {
        std::cerr << "Error checking if device " << deviceId << " is loopback: " << Pa_GetErrorText(isLoopback) << std::endl;
    }
    if (getDeviceType() == DeviceType::Input || getDeviceType() == DeviceType::LoopBack) {
        streamParams_.device = getID();
        streamParams_.hostApiSpecificStreamInfo = &wasapiInfo_;
        streamParams_.channelCount = deviceInfo_->maxInputChannels;
        streamParams_.sampleFormat = paInt16;
        streamParams_.suggestedLatency = deviceInfo_->defaultLowInputLatency;
        streamParams_.hostApiSpecificStreamInfo = nullptr;
        // std::cout << "Input capture configured on device: " << getDeviceInfo().name << " with " << inputParams_.channelCount << " channels." << std::endl;
    } else if (getDeviceType() == DeviceType::Output) {
        streamParams_.device = getID();
        streamParams_.hostApiSpecificStreamInfo = &wasapiInfo_;
        streamParams_.channelCount = deviceInfo_->maxOutputChannels;
        streamParams_.sampleFormat = paInt16;
        streamParams_.suggestedLatency = deviceInfo_->defaultLowOutputLatency;
        streamParams_.hostApiSpecificStreamInfo = nullptr;
        // std::cout << "Output capture configured on device: " << getDeviceInfo().name << " with " << outputParams_.channelCount << " channels." << std::endl;
    } else {
        std::cerr << "Unsupported device type." << std::endl;
    }
}

WindowsAudioDevice::WindowsAudioDevice(const AAudioDevice &device) : WindowsAudioDevice(device.getID()) {}

WindowsAudioDevice::~WindowsAudioDevice() {}

PaWasapiStreamInfo WindowsAudioDevice::getWasapiInfo() const {
    return wasapiInfo_;
}

PaStreamParameters WindowsAudioDevice::getStreamParams() const {
    return streamParams_;
}
