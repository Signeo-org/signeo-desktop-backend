#include "AudioCapture.hpp"

#ifdef _WIN32
#include "WindowsAudioCapture.hpp"
#endif

std::unique_ptr<AudioCapture> AudioCapture::createInstance() {
#ifdef _WIN32
    return std::make_unique<WindowsAudioCapture>();
#else
    // Add support for other platforms as needed
    return nullptr;
#endif
}
