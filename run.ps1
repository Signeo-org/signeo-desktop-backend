
# Check if there is a external folder
if (Test-Path -Path "./external/portaudio") {
    # Update the portaudio library
    Set-Location -Path "./external/portaudio"
    git pull
    Set-Location -Path "../.."
} else {
    # Clone the portaudio library
    git clone https://github.com/PortAudio/portaudio.git external/portaudio
}

# Check if there is a bulid folder
if (-not (Test-Path -Path "./build")) {
    New-Item -ItemType Directory -Path "./build"
}

# Navigate to the build directory
Set-Location -Path "./build"

# Generate the build files
cmake ..

# Compile the project
cmake --build . --config Release

# Back to the root directory
Set-Location -Path ".."

# Run the resulting executable
./build/Release/AudioCapturePlayback.exe
