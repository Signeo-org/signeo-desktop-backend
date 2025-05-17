#ifndef WINDOWSAUDIODEVICE_HPP
#define WINDOWSAUDIODEVICE_HPP

#include "AAudioDevice.hpp"

class WindowsAudioDevice : public AAudioDevice {
    public:
        WindowsAudioDevice(int deviceId);
        WindowsAudioDevice(const AAudioDevice &device);
        ~WindowsAudioDevice();
        PaWasapiStreamInfo getWasapiInfo() const;
        PaStreamParameters getStreamParams() const;
    protected:
    private:
        PaWasapiStreamInfo wasapiInfo_;
        PaStreamParameters streamParams_;
};

#endif // WINDOWSAUDIODEVICE_HPP
