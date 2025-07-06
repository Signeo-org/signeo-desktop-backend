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
Write-Host "Configuring CMake with CUDA support..."
cmake -D BUILD_SHARED_LIBS=OFF -D CMAKE_BUILD_TYPE=Release -D WHISPER_CUDA=ON -D GGML_CUDA=ON ..

# -------------------------------------------------
# 4) Build the Project (Release mode on Windows)
# -------------------------------------------------
Write-Host "Building project..."
cmake --build . --config Release

# ------------------------------------------
# 5) Return to root and run the .exe
# ------------------------------------------
Set-Location -Path ".."

# ------------------------------------------
# 6) Download the model if it doesn't exist
# ------------------------------------------
if (-not (Test-Path -Path "./models/ggml-base.bin")) {
    $url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin?download=true"
    try {
        Start-BitsTransfer -Source $url -Destination "./models/ggml-base.bin"
    }
    catch {
        Write-Warning "Failed to download the model. Please check your internet connection or the URL."
        exit 1
    }
}
else {
    Write-Host "Model already exists, skipping download."
}
