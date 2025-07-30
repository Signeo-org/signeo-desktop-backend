#include "AAudioDevice.hpp"

#ifdef _WIN32
#include "WindowsAudioDevice.hpp"
#endif

AAudioDevice::AAudioDevice(int deviceId) {
    id_ = -1;
    deviceType_ = DeviceType::None;
    deviceInfo_ = Pa_GetDeviceInfo(deviceId);

    if (deviceInfo_ == nullptr) {
        std::cerr << "Error: Invalid device ID (" << deviceId << ")." << std::endl;
        return;
    }
    hostApiInfo_ = Pa_GetHostApiInfo(deviceInfo_->hostApi);
    if (hostApiInfo_ == nullptr) {
        std::cerr << "Error: Failed to retrieve Host API info for device ID (" << deviceId << ")." << std::endl;
        return;
    }
    id_ = deviceId;
    if (deviceInfo_->maxInputChannels > 0) {
        deviceType_ = DeviceType::Input;
    } else if (deviceInfo_->maxOutputChannels > 0) {
        deviceType_ = DeviceType::Output;
    } else if (deviceType_ == DeviceType::None) {
        std::cerr << "Error: Unsupported device type for device ID (" << deviceId << ")." << std::endl;
    }
}

AAudioDevice::~AAudioDevice() {}

int AAudioDevice::getID() const {
    return id_;
}

DeviceType AAudioDevice::getDeviceType() const {
    return deviceType_;
}

PaDeviceInfo AAudioDevice::getDeviceInfo() const {
    return *deviceInfo_;
}

PaHostApiInfo AAudioDevice::getHostAPIInfo() const {
    return *hostApiInfo_;
}

std::unique_ptr<AAudioDevice> AAudioDevice::createInstance(int deviceId) {
#ifdef _WIN32
    return std::make_unique<WindowsAudioDevice>(deviceId);
#else
    // Add support for other platforms as needed
    return nullptr;
#endif
}
