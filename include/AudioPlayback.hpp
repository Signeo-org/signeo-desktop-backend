#ifndef AUDIOPLAYBACK_HPP
#define AUDIOPLAYBACK_HPP

#include "AAudioDevice.hpp"
#include <vector>
#include <memory>

class AudioPlayback {
    public:
        virtual ~AudioPlayback() = default;
        virtual bool start(AAudioDevice *device, std::vector<uint8_t>& data, double sampleRate) = 0;

        static std::unique_ptr<AudioPlayback> createInstance();
    protected:
    private:
};

#endif // AUDIOPLAYBACK_HPP
