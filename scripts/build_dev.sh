#!/bin/bash
# Builds the project (CPU by default, GPU optional) using generic flags

# Determine directories
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
ENABLE_GPU="OFF"

# Parse arguments
ENABLE_TIDY="OFF"
ENABLE_FORMAT="OFF"

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --gpu|-gpu) ENABLE_GPU="ON"; BUILD_DIR="$PROJECT_ROOT/build_gpu" ;;
        --clean|-clean) rm -rf "$BUILD_DIR" ;;
        --tidy|-tidy) ENABLE_TIDY="ON" ;;
        --format|-format) ENABLE_FORMAT="ON" ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

echo -e "\033[0;36mBuilding Project...\033[0m"
echo "GPU Support:  $ENABLE_GPU"
echo "Clang-Tidy:   $ENABLE_TIDY"
echo "Clang-Format: $ENABLE_FORMAT"
echo "Project Root: $PROJECT_ROOT"
echo "Build Dir:    $BUILD_DIR"

# Ensure Build Directory Exists
mkdir -p "$BUILD_DIR"

# 1. Configure CMake
echo -e "\n\033[0;36m[1/2] Configuring CMake...\033[0m"

# Check if Ninja is available
GENERATOR="-G Ninja"
if ! command -v ninja &> /dev/null; then
    GENERATOR=""
    echo "Ninja not found, using default generator."
fi

CMAKE_ARGS=("-S" "$PROJECT_ROOT" "-B" "$BUILD_DIR" $GENERATOR "-DENABLE_GPU=$ENABLE_GPU" "-DENABLE_CLANG_TIDY=$ENABLE_TIDY" "-DENABLE_CLANG_FORMAT=$ENABLE_FORMAT")

cmake "${CMAKE_ARGS[@]}"

if [ $? -ne 0 ]; then
    echo -e "\033[0;31mCMake configuration failed.\033[0m"
    exit 1
fi

# 2. Build
echo -e "\n\033[0;36m[2/2] Building...\033[0m"
cmake --build "$BUILD_DIR"

if [ $? -eq 0 ]; then
    echo -e "\n\033[0;32mBuild Successful!\033[0m"
else
    echo -e "\n\033[0;31mBuild failed.\033[0m"
    exit 1
fi
