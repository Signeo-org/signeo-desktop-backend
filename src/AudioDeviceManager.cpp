#include "AudioDeviceManager.hpp"

AudioDeviceManager::AudioDeviceManager() {
    selectedHostAPI_ = paInDevelopment;
    selectedRecordingDevice_ = nullptr;
    selectedPlaybackDevice_ = nullptr;
    audioCapture_ = nullptr;
    audioPlayback_ = nullptr;
}

AudioDeviceManager::~AudioDeviceManager() {
    // Terminate PortAudio
    Pa_Terminate();
}

bool AudioDeviceManager::initDevices() {
    PaDeviceIndex numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        std::cerr << "ERROR: Pa_GetDeviceCount returned " << numDevices << std::endl;
        return false;
    }
    for (PaDeviceIndex i = 0; i < numDevices; i++) {
        std::unique_ptr<AAudioDevice> device = AAudioDevice::createInstance(i);
        if (device && device->getHostAPIInfo().type == selectedHostAPI_) {
            if (device->getDeviceInfo().maxInputChannels > 0) {
                recordingDevices_.push_back(std::move(device));
            } else if (device->getDeviceInfo().maxOutputChannels > 0) {
                playbackDevices_.push_back(std::move(device));
            } else {
                std::cerr << "Error: Failed to create device instance for device ID " << i << std::endl;
            }
        }
    }
    return true;
}

bool AudioDeviceManager::init() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Error initializing PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    audioCapture_ = AudioCapture::createInstance();
    audioPlayback_ = AudioPlayback::createInstance();
    std::cout << "------------------------------------------------" << std::endl;
    if (!selectHostAPI())
        return false;
    if (!initDevices())
        return false;
    std::cout << "------------------------------------------------" << std::endl;
    if (!selectRecordingDevice())
        return false;
    std::cout << "------------------------------------------------" << std::endl;
    if (!selectPlaybackDevice())
        return false;
    std::cout << "------------------------------------------------" << std::endl;
    return true;
}

void AudioDeviceManager::listAvailableHostAPIs(PaHostApiIndex numHostAPIs, PaHostApiIndex defaultHostAPIIndex) {
    // List available host APIs
    std::cout << "Available host APIs:" << std::endl;
    for (PaHostApiIndex i = 0; i < numHostAPIs; i++) {
        const PaHostApiInfo* hostAPIInfo = Pa_GetHostApiInfo(i);
        if (hostAPIInfo) {
            std::cout << "[" << i << "] " << hostAPIInfo->name;
            if (i == defaultHostAPIIndex) {
                std::cout << " [Default]";
            }
            std::cout << std::endl;
        } else {
            std::cerr << "Warning: Null host API info encountered at index " << i << "." << std::endl;
        }
    }
}

bool AudioDeviceManager::selectHostAPI() {
    // Get the number of available host APIs
    PaHostApiIndex numHostAPIs = Pa_GetHostApiCount();
    if (numHostAPIs < 0) {
        std::cerr << "ERROR: Pa_GetHostApiCount returned " << numHostAPIs << std::endl;
        return false;
    }

    // List available host APIs
    PaHostApiIndex defaultHostAPIIndex = Pa_GetDefaultHostApi();
    listAvailableHostAPIs(numHostAPIs, defaultHostAPIIndex);

    // Prompt user for selection
    std::cout << "Enter host API index to use (press Enter to use default): ";
    std::string input;
    std::getline(std::cin, input); // Read the entire input line

    PaHostApiIndex hostAPIIndex;
    if (input.empty()) {
        // Use the default host API if input is empty
        hostAPIIndex = defaultHostAPIIndex;
    } else {
        // Validate the user-provided index
        try {
            hostAPIIndex = std::stoi(input);
            if (hostAPIIndex < 0 || hostAPIIndex > numHostAPIs) {
                throw std::out_of_range("Index out of range");
            }
        } catch (...) {
            std::cerr << "Invalid input. Using default host API instead." << std::endl;
            hostAPIIndex = defaultHostAPIIndex;
        }
    }

    // Retrieve the selected host API info
    const PaHostApiInfo* selectedHostAPIInfo = Pa_GetHostApiInfo(hostAPIIndex);
    if (selectedHostAPIInfo) {
        std::cout << "Selected Host API: " << selectedHostAPIInfo->name << std::endl;
        selectedHostAPI_ = selectedHostAPIInfo->type;
    } else {
        std::cerr << "Error: Could not retrieve host API info for the selected index." << std::endl;
        selectedHostAPI_ = paInDevelopment;
    }
    return true;
}

void AudioDeviceManager::listAvailableRecordingDevices() {
    if (recordingDevices_.empty()) {
        std::cerr << "No available record audio devices found." << std::endl;
        return;
    }

    std::cout << "Available record audio devices:" << std::endl;
    for (size_t i = 0; i < recordingDevices_.size(); ++i) {
        const std::unique_ptr<AAudioDevice>& device = recordingDevices_[i]; // Reference to the unique_ptr
        if (device) { // Ensure the pointer is not null
            std::cout << "[" << i << "] " << device->getDeviceInfo().name << " (" << device->getHostAPIInfo().name << ")";
            if (device->getID() == device->getHostAPIInfo().defaultInputDevice)
                std::cout << " [Default Input]";
            if (device->getID() == device->getHostAPIInfo().defaultOutputDevice)
                std::cout << " [Default Output]";
            // if (device->getDeviceInfo().maxInputChannels > 0)
            //     std::cout << " [Input Channels: " << device->getDeviceInfo().maxInputChannels << "]";
            // if (device->getDeviceInfo().maxOutputChannels > 0)
            //     std::cout << " [Output Channels: " << device->getDeviceInfo().maxOutputChannels << "]";
            std::cout << std::endl;
        } else {
            std::cerr << "Error: Null device encountered at index " << i << "." << std::endl;
        }
    }
}

void AudioDeviceManager::listAvailablePlaybackDevices(){
    if (playbackDevices_.empty()) {
        std::cerr << "No available playback audio devices found." << std::endl;
        return;
    }

    std::cout << "Available playback audio devices:" << std::endl;
    for (size_t i = 0; i < playbackDevices_.size(); ++i) {
        const std::unique_ptr<AAudioDevice>& device = playbackDevices_[i]; // Reference to the unique_ptr
        if (device) { // Ensure the pointer is not null
            std::cout << "[" << i << "] " << device->getDeviceInfo().name << " (" << device->getHostAPIInfo().name << ")";
            if (device->getID() == device->getHostAPIInfo().defaultInputDevice)
                std::cout << " [Default Input]";
            if (device->getID() == device->getHostAPIInfo().defaultOutputDevice)
                std::cout << " [Default Output]";
            // if (device->getDeviceInfo().maxInputChannels > 0)
            //     std::cout << " [Input Channels: " << device->getDeviceInfo().maxInputChannels << "]";
            // if (device->getDeviceInfo().maxOutputChannels > 0)
            //     std::cout << " [Output Channels: " << device->getDeviceInfo().maxOutputChannels << "]";
            std::cout << std::endl;
        } else {
            std::cerr << "Error: Null device encountered at index " << i << "." << std::endl;
        }
    }
}


bool AudioDeviceManager::selectRecordingDevice() {
    int deviceID;
    std::string input; // Use a string to handle empty input

    listAvailableRecordingDevices(); // List available recording devices
    std::cout << "Enter device ID to capture audio from (press Enter for default input device): ";
    std::getline(std::cin, input); // Read the entire line of input

    if (input.empty()) { // Check if input is empty
        for (int i = 0; i < recordingDevices_.size(); ++i) {
            if (recordingDevices_[i] &&
                recordingDevices_[i]->getID() == recordingDevices_[i]->getHostAPIInfo().defaultInputDevice) {
                selectedRecordingDevice_ = recordingDevices_[i].get();
                std::cout << "Selected recording device: " << selectedRecordingDevice_->getDeviceInfo().name << std::endl;
                return true;
            }
        }
        std::cerr << "No default input device found." << std::endl;
        return false;
    } else {
        // Convert input to integer
        try {
            deviceID = std::stoi(input);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input. Please enter a valid device ID." << std::endl;
            return false;
        }

        if (deviceID < 0 || deviceID >= static_cast<int>(recordingDevices_.size())) {
            std::cerr << "Invalid device ID." << std::endl;
            return false;
        }
    }
    selectedRecordingDevice_ = recordingDevices_[deviceID].get();
    std::cout << "Selected recording device: " << selectedRecordingDevice_->getDeviceInfo().name << std::endl;
    return true;
}

bool AudioDeviceManager::selectPlaybackDevice() {
    int deviceID;
    std::string input; // Use a string to handle empty input

    listAvailablePlaybackDevices(); // List available playback devices
    std::cout << "Enter device ID to playback audio from (press Enter for default output device): ";
    std::getline(std::cin, input); // Read the entire line of input

    if (input.empty()) { // Check if input is empty
        for (int i = 0; i < playbackDevices_.size(); ++i) {
            if (playbackDevices_[i] &&
                playbackDevices_[i]->getID() == playbackDevices_[i]->getHostAPIInfo().defaultOutputDevice) {
                selectedPlaybackDevice_ = playbackDevices_[i].get();
                std::cout << "Selected playback device: " << selectedPlaybackDevice_->getDeviceInfo().name << std::endl;
                return true;
            }
        }
        std::cerr << "No default output device found." << std::endl;
        return false;
    } else {
        try {
            deviceID = std::stoi(input);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid input. Please enter a valid device ID." << std::endl;
            return false;
        }

        if (deviceID < 0 || deviceID >= static_cast<int>(playbackDevices_.size())) {
            std::cerr << "Invalid device ID." << std::endl;
            return false;
        }
    }
    selectedPlaybackDevice_ = playbackDevices_[deviceID].get();
    std::cout << "Selected playback device: " << selectedPlaybackDevice_->getDeviceInfo().name << std::endl;
    return true;
}

bool AudioDeviceManager::record_device(std::chrono::seconds duration)
{
    std::cout << "Selected general device ID: " << selectedRecordingDevice_->getID() << std::endl;
    std::cout << "Device name: " << selectedRecordingDevice_->getDeviceInfo().name << std::endl;
    std::cout << "Device type: " << selectedRecordingDevice_->getDeviceType() << std::endl;
    std::cout << "Max input channels: " << selectedRecordingDevice_->getDeviceInfo().maxInputChannels << std::endl;
    std::cout << "Max output channels: " << selectedRecordingDevice_->getDeviceInfo().maxOutputChannels << std::endl;
    std::cout << "Sample Rate: " << selectedRecordingDevice_->getDeviceInfo().defaultSampleRate << std::endl;

    if (!audioCapture_->start(selectedRecordingDevice_, duration)) {
        std::cerr << "Failed to start audio capture." << std::endl;
        return false;
    }
    std::cout << "Recording audio from device: " << selectedRecordingDevice_->getDeviceInfo().name 
              << " for " << duration.count() << " seconds..." << std::endl;
    if (audioCapture_->getCapturedData().empty()) {
        std::cerr << "Captured data is empty. Recording might have failed." << std::endl;
        return false;
    }
    std::cout << "Captured " << audioCapture_->getCapturedData().size() << " samples." << std::endl;
    return true;
}

bool AudioDeviceManager::playback_device()
{
    std::cout << "Selected general device ID: " << selectedPlaybackDevice_->getID() << std::endl;
    std::cout << "Device name: " << selectedPlaybackDevice_->getDeviceInfo().name << std::endl;
    std::cout << "Device type: " << selectedPlaybackDevice_->getDeviceType() << std::endl;
    std::cout << "Max input channels: " << selectedPlaybackDevice_->getDeviceInfo().maxInputChannels << std::endl;
    std::cout << "Max output channels: " << selectedPlaybackDevice_->getDeviceInfo().maxOutputChannels << std::endl;
    std::cout << "Sample Rate: " << selectedPlaybackDevice_->getDeviceInfo().defaultSampleRate << std::endl;

    std::vector<uint8_t> capturedData = audioCapture_->getCapturedData();
    if (capturedData.empty()) {
        std::cerr << "No captured data to play." << std::endl;
        return false;
    }
    if (!audioPlayback_->start(selectedPlaybackDevice_, capturedData, selectedRecordingDevice_->getDeviceInfo().defaultSampleRate)) {
        std::cerr << "Failed to start audio capture." << std::endl;
        return false;
    }
    std::cout << "Playing back recorded audio..." << std::endl;
    return true;
}
