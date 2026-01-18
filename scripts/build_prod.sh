#!/bin/bash
# Automated Release Packaging for Signeo Backend (Linux)

# Determine directories
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
DIST_DIR="$PROJECT_ROOT/dist"
PACKAGE_NAME="Signeo-Backend-Linux-x64"
ENABLE_GPU="OFF"

# Parse arguments
CLEAN="OFF"
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --gpu|-gpu) 
            ENABLE_GPU="ON" 
            BUILD_DIR="$PROJECT_ROOT/build_gpu"
            PACKAGE_NAME="Signeo-Backend-Linux-x64-GPU"
            ;;
        --clean|-clean) CLEAN="ON" ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

INSTALL_PREFIX="$DIST_DIR/$PACKAGE_NAME"

echo -e "\033[0;36m🚀 Starting Release Process for $PACKAGE_NAME...\033[0m"
echo -e "Project Root: $PROJECT_ROOT"

# 1. Clean
if [ "$CLEAN" == "ON" ] || [ ! -d "$BUILD_DIR" ]; then
    echo -e "\033[0;33m🧹 Cleaning build directory...\033[0m"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

# 2. Configure (Release)
echo -e "\n\033[0;36m🔧 Configuring (Release)...\033[0m"

# Check if Ninja is available
GENERATOR="-G Ninja"
if ! command -v ninja &> /dev/null; then
    GENERATOR=""
    echo "Ninja not found, using default generator."
fi

CMAKE_ARGS=("-S" "$PROJECT_ROOT" "-B" "$BUILD_DIR" $GENERATOR "-DCMAKE_BUILD_TYPE=Release" "-DENABLE_GPU=$ENABLE_GPU")

if [ "$ENABLE_GPU" == "ON" ]; then
    # Usually no need to force compilers on Linux, standard gcc/nvcc works
    CMAKE_ARGS+=("-DENABLE_CLANG_TIDY=OFF")
fi

cmake "${CMAKE_ARGS[@]}"

if [ $? -ne 0 ]; then
    echo -e "\033[0;31mConfiguration failed.\033[0m"
    exit 1
fi

# 3. Build
echo -e "\n\033[0;36m🔨 Building...\033[0m"
cmake --build "$BUILD_DIR" --config Release

if [ $? -ne 0 ]; then
    echo -e "\033[0;31mBuild failed.\033[0m"
    exit 1
fi

# 4. Install
echo -e "\n\033[0;36m📦 Installing to $INSTALL_PREFIX...\033[0m"
rm -rf "$DIST_DIR"
cmake --install "$BUILD_DIR" --config Release --prefix "$INSTALL_PREFIX" --component Application

# 5. Archive (Tarball for Linux)
echo -e "\n\033[0;36m🤐 Archiving...\033[0m"
ARCHIVE_PATH="$DIST_DIR/$PACKAGE_NAME.tar.gz"
cd "$DIST_DIR"
tar -czf "$PACKAGE_NAME.tar.gz" "$PACKAGE_NAME"

echo -e "\n\033[0;32m✅ Done! Package: $ARCHIVE_PATH\033[0m"
