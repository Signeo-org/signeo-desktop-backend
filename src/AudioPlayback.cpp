#include "AudioPlayback.hpp"

#ifdef _WIN32
#include "WindowsAudioPlayback.hpp"
#endif

std::unique_ptr<AudioPlayback> AudioPlayback::createInstance() {
#ifdef _WIN32
    return std::make_unique<WindowsAudioPlayback>();
#else
    // Add support for other platforms as needed
    return nullptr;
#endif
}
