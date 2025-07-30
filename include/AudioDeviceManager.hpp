#ifndef AUDIODEVICEMANAGER_HPP
#define AUDIODEVICEMANAGER_HPP

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include "AAudioDevice.hpp"

#ifdef _WIN32
#include <pa_win_wasapi.h>
#include "WindowsAudioDevice.hpp"
#include "WindowsAudioCapture.hpp"
#include "WindowsAudioPlayback.hpp"
#endif

class AudioDeviceManager {
    public:
        AudioDeviceManager();
        ~AudioDeviceManager();
        bool init();
        bool record_device(std::chrono::seconds duration);
        bool playback_device();
    protected:
        PaHostApiTypeId selectedHostAPI_;
        AAudioDevice *selectedRecordingDevice_;
        AAudioDevice *selectedPlaybackDevice_;
        std::unique_ptr<AudioCapture> audioCapture_;
        std::unique_ptr<AudioPlayback> audioPlayback_;
        std::vector<std::unique_ptr<AAudioDevice>> recordingDevices_;
        std::vector<std::unique_ptr<AAudioDevice>> playbackDevices_;
    private:
        bool initDevices();
        void listAvailableHostAPIs(PaHostApiIndex numHostAPIs, PaHostApiIndex defaultHostAPIIndex);
        bool selectHostAPI();
        void listAvailableRecordingDevices();
        bool selectRecordingDevice();
        void listAvailablePlaybackDevices();
        bool selectPlaybackDevice();
};

#endif // AUDIODEVICEMANAGER_HPP
// 