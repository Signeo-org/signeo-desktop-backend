#!/usr/bin/env bash
set -e

# set working directory to the script's location
cd "$(dirname "$0")"
cd ../   # go to backend root

# ---------------------------------------------
# 1) Detect OS and set CUDA flags
# ---------------------------------------------
OS="$(uname)"
if [[ "$OS" == "Darwin" ]]; then
    echo "macOS detected → building without CUDA"
    CUDA_FLAGS="-D WHISPER_CUDA=OFF -D GGML_CUDA=OFF"
else
    echo "Non-macOS detected → building with CUDA"
    CUDA_FLAGS="-D WHISPER_CUDA=ON -D GGML_CUDA=ON"
fi

# ---------------------------------------------
# 2) Update Submodules
# ---------------------------------------------
echo "Updating git submodules..."
git submodule update --init --recursive

# ---------------------------------------------
# 3) Download Model if Missing
# ---------------------------------------------
MODEL_PATH="./models/ggml-base.bin"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin?download=true"

if [ ! -f "$MODEL_PATH" ]; then
    echo "Downloading model..."
    mkdir -p ./models
    if ! curl -L -o "$MODEL_PATH" "$MODEL_URL"; then
        echo "[ERROR] Failed to download the model. Check your internet connection or the URL."
        exit 1
    fi
else
    echo "Model already exists, skipping download."
fi

# ---------------------------------------------
# 4) Create / Enter Build Directory
# ---------------------------------------------
mkdir -p build
cd build

# ---------------------------------------------
# 5) Configure CMake
# ---------------------------------------------
echo "Configuring CMake..."
cmake -D BUILD_SHARED_LIBS=OFF -D CMAKE_BUILD_TYPE=Release $CUDA_FLAGS ..

# ---------------------------------------------
# 6) Build Project
# ---------------------------------------------
echo "Building project..."
cmake --build .

# ---------------------------------------------
# 7) Only create Release folder and copy files if it does not exist
# ---------------------------------------------
if [ ! -d "Release" ]; then
    echo "Creating Release folder and copying build outputs..."
    mkdir Release

    # Copy binary
    if ls AudioTranscriptionTool* 1> /dev/null 2>&1; then
        cp AudioTranscriptionTool* Release/
    else
        echo "[WARNING] No AudioTranscriptionTool binary found!"
    fi

    # Copy models
    if [ -d "../models" ]; then
        mkdir -p Release/models
        cp ../models/*.bin Release/models/ 2>/dev/null || echo "[INFO] No model files found to copy."
    fi
else
    echo "Release folder already exists → skipping copy."
fi

# ---------------------------------------------
# 8) Return to root
# ---------------------------------------------
cd ..
