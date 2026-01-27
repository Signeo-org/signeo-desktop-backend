#!/usr/bin/env bash
# =============================================================================
# Signeo Backend - Unified Build Script
# =============================================================================
# Usage:
#   ./build.sh                  # Dev build
#   ./build.sh -prod            # Release build
#   ./build.sh --gpu            # GPU build
#   ./build.sh -prod --gpu --clean  # Clean GPU release
# =============================================================================

set -e

# Parse arguments
MODE="dev"
GPU=false; CLEAN=false; TIDY=false; FORMAT=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -dev|--dev) MODE="dev"; shift ;;
        -prod|--prod) MODE="prod"; shift ;;
        --gpu|-gpu) GPU=true; shift ;;
        --clean|-clean) CLEAN=true; shift ;;
        --tidy|-tidy) TIDY=true; shift ;;
        --format|-format) FORMAT=true; shift ;;
        *) shift ;;
    esac
done

# Path resolution
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

if [ "$GPU" = true ]; then
    BUILD_DIR="$PROJECT_ROOT/build_gpu"
else
    BUILD_DIR="$PROJECT_ROOT/build"
fi

# Generator
GENERATOR="-G Ninja"
command -v ninja &>/dev/null || GENERATOR=""

# =============================================================================
# Development Build
# =============================================================================
if [ "$MODE" = "dev" ]; then
    echo -e "\n🔧 Backend - Development Build\n"
    echo "  Build: $BUILD_DIR"
    
    # Clean
    [ "$CLEAN" = true ] && rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    
    # Configure
    echo -e "\n[1/2] Configuring..."
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" $GENERATOR \
        -DENABLE_GPU=$([ "$GPU" = true ] && echo ON || echo OFF) \
        -DENABLE_CLANG_TIDY=$([ "$TIDY" = true ] && echo ON || echo OFF) \
        -DENABLE_CLANG_FORMAT=$([ "$FORMAT" = true ] && echo ON || echo OFF)
    
    # Build
    echo -e "\n[2/2] Building..."
    cmake --build "$BUILD_DIR"
    
    echo -e "\n✅ Build Successful!"

# =============================================================================
# Production Build
# =============================================================================
elif [ "$MODE" = "prod" ]; then
    if [ "$GPU" = true ]; then
        PACKAGE_NAME="Signeo-Backend-Linux-GPU"
    else
        PACKAGE_NAME="Signeo-Backend-Linux"
    fi
    DIST_DIR="$PROJECT_ROOT/dist"
    INSTALL_PREFIX="$DIST_DIR/$PACKAGE_NAME"
    
    echo -e "\n📦 Backend - Production Build\n"
    echo "  Package: $PACKAGE_NAME"
    
    # Clean
    if [ "$CLEAN" = true ] || [ ! -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
    
    # Configure (Release)
    echo -e "\n[1/4] Configuring (Release)..."
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" $GENERATOR \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_GPU=$([ "$GPU" = true ] && echo ON || echo OFF)
    
    # Build
    echo -e "\n[2/4] Building..."
    cmake --build "$BUILD_DIR" --config Release
    
    # Install
    echo -e "\n[3/4] Installing..."
    rm -rf "$DIST_DIR"
    cmake --install "$BUILD_DIR" --config Release --prefix "$INSTALL_PREFIX" --component Application
    
    # Archive
    echo -e "\n[4/4] Packaging..."
    cd "$DIST_DIR"
    tar -czf "$PACKAGE_NAME.tar.gz" "$PACKAGE_NAME"
    
    echo -e "\n✅ Package: $DIST_DIR/$PACKAGE_NAME.tar.gz"
fi
