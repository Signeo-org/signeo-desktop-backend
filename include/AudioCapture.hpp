#ifndef AUDIOCAPTURE_HPP
#define AUDIOCAPTURE_HPP

#include "AAudioDevice.hpp"
#include <vector>
#include <memory>
#include <chrono>

class AudioCapture {
    public:
        virtual ~AudioCapture() = default;

        virtual bool start(AAudioDevice *device, std::chrono::seconds duration) = 0;
        virtual std::vector<uint8_t> getCapturedData() const = 0;
        virtual double getSampleRate() const = 0;

        // Factory method for creating platform-specific instances
        static std::unique_ptr<AudioCapture> createInstance();
    protected:
    private:
};

#endif // AUDIOCAPTURE_HPP
