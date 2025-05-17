#ifndef WINDOWS_AUDIO_CAPTURE_HPP
#define WINDOWS_AUDIO_CAPTURE_HPP

#include "AudioCapture.hpp"
#include "WindowsAudioDevice.hpp"
#include <portaudio.h>
#include <vector>
#include <memory>

class WindowsAudioCapture : public AudioCapture {
    public:
        WindowsAudioCapture();
        ~WindowsAudioCapture() override;

        bool start(AAudioDevice *device, std::chrono::seconds duration) override;
        bool open_stream(WindowsAudioDevice *windowsDevice);
        bool read_stream(WindowsAudioDevice *windowsDevice, std::chrono::seconds duration);
        bool start_stream(WindowsAudioDevice *windowsDevice);
        bool start_input(WindowsAudioDevice *windowsDevice);
        bool close_stream();
        std::vector<uint8_t> getCapturedData() const override;
        double getSampleRate() const;
    protected:
    private:
        PaStream *stream_;
        std::vector<uint8_t> capturedData_;
        double sampleRate_;
};

#endif // WINDOWS_AUDIO_CAPTURE_HPP
