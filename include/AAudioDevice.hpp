#ifndef AAUDIODEVICE_HPP
#define AAUDIODEVICE_HPP

#include "IAudioDevice.hpp"

#ifdef _WIN32
#include "Pa_win_wasapi.h"
#endif

class AAudioDevice : public IAudioDevice {
    public:
        AAudioDevice(int deviceId);
        ~AAudioDevice();
        DeviceType getDeviceType() const;
        PaDeviceInfo getDeviceInfo() const;
        PaHostApiInfo getHostAPIInfo() const;
        int getID() const;

        // Factory method for creating platform-specific instances
        static std::unique_ptr<AAudioDevice> createInstance(int deviceId);
    protected:
    private:
};

#endif // AAUDIODEVICE_HPP
