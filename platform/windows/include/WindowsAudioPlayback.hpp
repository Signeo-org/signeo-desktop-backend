#ifndef WINDOWS_AUDIO_PLAYBACK_HPP
#define WINDOWS_AUDIO_PLAYBACK_HPP

#include "AudioPlayback.hpp"
#include <portaudio.h>
#include <vector>
#include <memory>

#include "WindowsAudioDevice.hpp"

class WindowsAudioPlayback : public AudioPlayback {
    public:
        WindowsAudioPlayback();
        ~WindowsAudioPlayback() override;
        
        bool start(AAudioDevice *device, std::vector<uint8_t>& data, double sampleRate) override;
    protected:
    private:
        PaStream *stream_;
};

#endif // WINDOWS_AUDIO_PLAYBACK_HPP
