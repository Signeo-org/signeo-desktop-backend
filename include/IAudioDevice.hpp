#ifndef IAUDIODEVICE_HPP
#define IAUDIODEVICE_HPP

#include <string>
#include <vector>
#include <memory>
#include <iostream>

#include <portaudio.h>

enum DeviceType {
    Input,
    Output,
    LoopBack,
    None
};

class IAudioDevice {
    public:
        IAudioDevice() = default;
        virtual ~IAudioDevice() = default;
        virtual DeviceType getDeviceType() const = 0;
        virtual PaDeviceInfo getDeviceInfo() const = 0;
        virtual PaHostApiInfo getHostAPIInfo() const = 0;
    protected:
        int id_ = -1;
        DeviceType deviceType_ = DeviceType::None;
        const PaDeviceInfo *deviceInfo_;
        const PaHostApiInfo *hostApiInfo_;
    private:
};

#endif // IAUDIODEVICE_HPP
