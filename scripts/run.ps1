# --------------------------------------
# 1) Initialize and Update Submodules
# --------------------------------------
Write-Host "Updating git submodules..."
git submodule update --init --recursive

# ----------------------------------
# 2) Create / Enter Build Directory
# ----------------------------------
if (-not (Test-Path -Path "./build")) {
    New-Item -ItemType Directory -Path "./build" | Out-Null
}

Set-Location -Path "./build"

# --------------------------------
# 3) Generate the Build Files
#    - Force BUILD_SHARED_LIBS=ON
#    - Choose Release configuration
# --------------------------------
Write-Host "Configuring CMake..."
cmake -D BUILD_SHARED_LIBS=ON -D CMAKE_BUILD_TYPE=Release ..

# -------------------------------------------------
# 4) Build the Project (Release mode on Windows)
# -------------------------------------------------
Write-Host "Building project..."
cmake --build . --config Release

# ------------------------------------------
# 5) Return to root and run the .exe
# ------------------------------------------
Set-Location -Path ".."
# Write-Host "Running executable..."
./build/Release/AudioCapturePlayback.exe
