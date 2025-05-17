#include "AudioDeviceManager.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    AudioDeviceManager device_manager;
    if (!device_manager.init()) {
        std::cerr << "Error: Failed to init device manager." << std::endl;
        return 1;
    }
    if (!device_manager.record_device(std::chrono::seconds(5))) {
        std::cerr << "Error: Failed to record audio." << std::endl;
        return 1;
    }
    if (!device_manager.playback_device()) {
        std::cerr << "Error: Failed to playback audio." << std::endl;
        return 1;
    }
    return 0;
}
