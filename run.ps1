# # run.ps1

# # ----------------------------
# # 1) Clone or Update PortAudio
# # ----------------------------
# if (Test-Path -Path "./external/portaudio") {
#     Write-Host "Updating portaudio (git pull)..."
#     Set-Location -Path "./external/portaudio"
#     git pull
#     Set-Location -Path "../.."
# } else {
#     Write-Host "Cloning portaudio..."
#     git clone https://github.com/PortAudio/portaudio.git external/portaudio
# }

# # --------------------------------
# # 2) Clone or Update whisper.cpp
# # --------------------------------
# if (Test-Path -Path "./external/whisper.cpp") {
#     Write-Host "Updating whisper.cpp (git pull)..."
#     Set-Location -Path "./external/whisper.cpp"
#     git pull
#     Set-Location -Path "../.."
# } else {
#     Write-Host "Cloning whisper.cpp..."
#     git clone https://github.com/ggerganov/whisper.cpp.git external/whisper.cpp
# }

# ----------------------------------
# 3) Create / Enter Build Directory
# ----------------------------------
if (-not (Test-Path -Path "./build")) {
    New-Item -ItemType Directory -Path "./build" | Out-Null
}

Set-Location -Path "./build"

# --------------------------------
# 4) Generate the Build Files
#    - Force BUILD_SHARED_LIBS=ON
#    - Choose Release configuration
# --------------------------------
Write-Host "Configuring CMake..."
cmake -D BUILD_SHARED_LIBS=ON -D CMAKE_BUILD_TYPE=Release ..

# -------------------------------------------------
# 5) Build the Project (Release mode on Windows)
# -------------------------------------------------
Write-Host "Building project..."
cmake --build . --config Release

# ------------------------------------------
# 6) Return to root and run the .exe
# ------------------------------------------
Set-Location -Path ".."

# Write-Host "Running executable..."
./build/Release/AudioCapturePlayback.exe
