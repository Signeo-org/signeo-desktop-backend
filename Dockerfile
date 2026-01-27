# ==========================================
# Arguments (Pre-Build)
# ==========================================
ARG BASE_IMAGE=ubuntu:24.04

# ==========================================
# Stage 1: Builder
# ==========================================
FROM ${BASE_IMAGE} AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Build options
ARG BUILD_TYPE=Release
ARG ENABLE_COVERAGE=OFF
ARG GGML_CUDA=OFF

# Install build dependencies
# - build-essential: GCC/G++
# - cmake: Build system
# - git: For fetching dependencies (FetchContent)
# - ninja-build: Fast build tool
# - libasound2-dev: ALSA development headers (for PortAudio)
# - wget: To download models
# - autoconf, automake, libtool, pkg-config: For SpeexDSP
# - lcov, gcovr: For code coverage
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    libasound2-dev \
    wget \
    autoconf \
    automake \
    libtool \
    pkg-config \
    lcov \
    gcovr \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY . .

# Configure and Build
# - DCMAKE_BUILD_TYPE: Controlled by ARG (Release/Debug)
# - DENABLE_COVERAGE: Controlled by ARG (ON/OFF)
# - DGGML_CUDA: Controlled by ARG (ON/OFF)
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DENABLE_COVERAGE=${ENABLE_COVERAGE} \
    -DGGML_CUDA=${GGML_CUDA}

RUN cmake --build build --target signeo-core unit_tests integration_tests benchmarks

# ==========================================
# Stage 2: Runtime
# ==========================================
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libasound2t64 \
    libgomp1 \
    python3 \
    lcov \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy executables from builder
COPY --from=builder /app/build/signeo-core .
COPY --from=builder /app/build/unit_tests .
COPY --from=builder /app/build/integration_tests .
COPY --from=builder /app/build/benchmarks .
COPY tests/fixtures ./tests/fixtures

# Copy ONNX Runtime libraries
COPY --from=builder /app/build/libonnxruntime.so.1.23.2 /usr/lib/
RUN ln -s /usr/lib/libonnxruntime.so.1.23.2 /usr/lib/libonnxruntime.so && \
    ln -s /usr/lib/libonnxruntime.so.1.23.2 /usr/lib/libonnxruntime.so.1

# Copy resources
COPY --from=builder /app/build/models ./models
COPY --from=builder /app/config.ini .

# Entrypoint
CMD ["./signeo-core"]
