cmake_minimum_required(VERSION 3.20)

# Suppress CMake policy warnings from third-party dependencies
if(POLICY CMP0194)
    cmake_policy(SET CMP0194 NEW)  # MSVC is not an assembler for ASM (CMake 4.2+)
endif()

# Suppress deprecation warnings from older CMakeLists in dependencies
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

project(signeo-core VERSION 2.0.0 LANGUAGES CXX C)

if(WIN32)
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Static Analysis (Clang-Tidy) ---
option(ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "Clang-Tidy requested but not found.")
    endif()
endif()

# --- Code Formatting (Clang-Format) ---
option(ENABLE_CLANG_FORMAT "Enable code formatting with clang-format" OFF)

if(ENABLE_CLANG_FORMAT)
    find_program(CLANG_FORMAT_EXE NAMES "clang-format")
    if(CLANG_FORMAT_EXE)
        message(STATUS "Clang-Format found: ${CLANG_FORMAT_EXE}")
        
        # Collect all source files
        file(GLOB_RECURSE ALL_SOURCES
            RELATIVE "${CMAKE_SOURCE_DIR}"
            "src/*.cpp" "src/*.hpp" "include/*.hpp" "tests/*.cpp"
        )
        
        # Exclude build/ and external/ directories from formatting if they were accidentally picked up
        # (GLOB_RECURSE with relative path usually respects source dir, but be careful of generated files)
        
        # 1. Format Target (Apply changes)
        add_custom_target(format
            COMMAND "${CLANG_FORMAT_EXE}" -i -style=file ${ALL_SOURCES}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Running clang-format on all source files..."
        )

        # 2. Check Format Target (Dry run, fail on changes)
        add_custom_target(check-format
            COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror -style=file ${ALL_SOURCES}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Checking code formatting..."
        )
    else()
        message(WARNING "Clang-Format requested but not found.")
    endif()
endif()

# --- Versioning ---
set(GIT_COMMIT_HASH "release") # Static for release builds

configure_file(
    ${CMAKE_SOURCE_DIR}/src/core/version.hpp.in
    ${CMAKE_BINARY_DIR}/include/core/version.hpp
)

message(STATUS "Version: ${PROJECT_VERSION}")

# --- Dependencies ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Force static libraries" FORCE)
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# 1. spdlog (Logging)
message(STATUS "Fetching spdlog...")
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
)
FetchContent_MakeAvailable(spdlog)

# 2. CLI11 (Command Line Parsing)
message(STATUS "Fetching CLI11...")
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.1
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
)
FetchContent_MakeAvailable(CLI11)

# 3. FTXUI (Terminal UI)
message(STATUS "Fetching FTXUI...")
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v6.1.9
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
)
FetchContent_MakeAvailable(ftxui)

# 4. Whisper.cpp (Speech-to-Text)
message(STATUS "Fetching whisper.cpp...")

# Unified GPU Option
option(ENABLE_GPU "Enable GPU support (CUDA for Whisper & ONNX Runtime)" OFF)

if(ENABLE_GPU)
    set(GGML_CUDA ON CACHE BOOL "Enable CUDA support" FORCE)
    message(STATUS "GPU Support ENABLED: CUDA will be used for Whisper.cpp and ONNX Runtime")
else()
    set(GGML_CUDA OFF CACHE BOOL "Enable CUDA support" FORCE)
    message(STATUS "GPU Support DISABLED: CPU-only build")
endif()

if(GGML_CUDA)
    include(CheckLanguage)
    
    # For MSVC with newer VS versions, we need to configure CUDA settings before detection
    if(MSVC)
        # Find nvcc in common CUDA Toolkit locations or PATH
        find_program(NVCC_EXECUTABLE nvcc
            PATHS
                "$ENV{CUDA_PATH}/bin"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin"
            NO_DEFAULT_PATH
        )
        if(NOT NVCC_EXECUTABLE)
            find_program(NVCC_EXECUTABLE nvcc)
        endif()
        
        if(NVCC_EXECUTABLE)
            set(CMAKE_CUDA_COMPILER "${NVCC_EXECUTABLE}" CACHE FILEPATH "CUDA compiler" FORCE)
            set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}" CACHE FILEPATH "CUDA Host Compiler" FORCE)
            set(CMAKE_CUDA_FLAGS "-allow-unsupported-compiler" CACHE STRING "CUDA Flags" FORCE)
            message(STATUS "Found nvcc: ${NVCC_EXECUTABLE}")
        endif()
    endif()

    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        message(STATUS "CUDA compiler found: ${CMAKE_CUDA_COMPILER}")
    else()
        message(WARNING "CUDA requested but not found. Falling back to CPU for Whisper.")
        set(GGML_CUDA OFF CACHE BOOL "" FORCE)
    endif()
else()
    message(STATUS "CUDA disabled for Whisper.")
endif()

FetchContent_Declare(
    whisper_cpp
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG v1.8.3
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Whisper.cpp options
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Set policy default for whisper.cpp to suppress CMP0194 warning
set(CMAKE_POLICY_DEFAULT_CMP0194 NEW CACHE STRING "" FORCE)
FetchContent_MakeAvailable(whisper_cpp)

# Suppress warnings in whisper.cpp (External)
if(TARGET whisper AND MSVC)
    target_compile_options(whisper PRIVATE /wd4244 /wd4267)
endif()

# 5. PortAudio (Audio Capture)
# Note: PortAudio often requires system libraries (ALSA/Jack on Linux).
# On Windows, it usually builds fine with WASAPI/DSound.
if(WIN32)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_ASIO OFF CACHE BOOL "" FORCE)
    set(PA_USE_DS OFF CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS OFF CACHE BOOL "" FORCE)
    set(PA_USE_WMME OFF CACHE BOOL "" FORCE)
endif()

set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON  CACHE BOOL "" FORCE)

# Disable PortAudio examples/tests
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

message(STATUS "Fetching PortAudio...")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
    portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG master
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(portaudio)

# 6. SpeexDSP (Audio Resampling)
message(STATUS "Fetching SpeexDSP...")
FetchContent_Declare(
    speexdsp
    GIT_REPOSITORY https://github.com/xiph/speexdsp.git
    GIT_TAG SpeexDSP-1.2.1
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Manually configure SpeexDSP (Windows vs Linux)
# FetchContent_MakeAvailable(speexdsp) -> Downloads only
FetchContent_GetProperties(speexdsp)
if(NOT speexdsp_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(speexdsp)
    cmake_policy(POP)
endif()

if(WIN32)
    # Windows: Manual Source Build (as previously defined)
    set(SPEEXDSP_SOURCES
        "${speexdsp_SOURCE_DIR}/libspeexdsp/preprocess.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/jitter.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/mdf.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/fftwrap.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/filterbank.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/resample.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/buffer.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/scal.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/kiss_fft.c"
        "${speexdsp_SOURCE_DIR}/libspeexdsp/kiss_fftr.c"
    )

    add_library(speexdsp STATIC ${SPEEXDSP_SOURCES})

    target_include_directories(speexdsp PUBLIC
        "${speexdsp_SOURCE_DIR}/include"
    )

    target_compile_definitions(speexdsp PRIVATE
        -DFLOATING_POINT
        -DUSE_SMALLFT
        -DUSE_ALLOCA
        -D_CRT_SECURE_NO_WARNINGS
        -DEXPORT=
    )

    if(MSVC)
        target_compile_options(speexdsp PRIVATE /wd4244 /wd4267 /wd4018)
    endif()

elseif(UNIX)
    # Linux: Use Autotools (configure && make)
    message(STATUS "Configuring SpeexDSP with Autotools...")
    
    find_program(MAKE_EXE make REQUIRED)
    
    set(SPEEXDSP_INSTALL_DIR "${CMAKE_BINARY_DIR}/speexdsp_install")
    file(MAKE_DIRECTORY ${SPEEXDSP_INSTALL_DIR})
    file(MAKE_DIRECTORY "${SPEEXDSP_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${SPEEXDSP_INSTALL_DIR}/lib")
    
    # We use a custom command to run autogen/configure/make
    # Note: We run ./autogen.sh first because we are fetching from git
    add_custom_command(
        OUTPUT "${SPEEXDSP_INSTALL_DIR}/lib/libspeexdsp.a"
        COMMAND chmod +x autogen.sh
        COMMAND ./autogen.sh
        COMMAND ./configure --prefix=${SPEEXDSP_INSTALL_DIR} --enable-static --disable-shared --with-pic
        COMMAND ${MAKE_EXE} -j
        COMMAND ${MAKE_EXE} install
        WORKING_DIRECTORY ${speexdsp_SOURCE_DIR}
        COMMENT "Building SpeexDSP with Autotools"
        VERBATIM
    )
    
    add_custom_target(speexdsp_build DEPENDS "${SPEEXDSP_INSTALL_DIR}/lib/libspeexdsp.a")
    
    # Use INTERFACE library to manage dependencies and include paths
    add_library(speexdsp INTERFACE)
    target_include_directories(speexdsp INTERFACE "${SPEEXDSP_INSTALL_DIR}/include")
    target_link_libraries(speexdsp INTERFACE "${SPEEXDSP_INSTALL_DIR}/lib/libspeexdsp.a")
    add_dependencies(speexdsp speexdsp_build)

endif()


# 6. ONNX Runtime (Prebuilt Binary - v1.23.2)
message(STATUS "Fetching ONNX Runtime v1.23.2 (Binary)...")

if(WIN32)
    if(ENABLE_GPU)
        message(STATUS "Selecting ONNX Runtime GPU (CUDA) build for Windows")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-gpu-1.23.2.zip")
    else()
        message(STATUS "Selecting ONNX Runtime CPU build for Windows")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip")
    endif()

    set(ONNX_LIB_REL_PATH "lib/onnxruntime.lib")
    set(ONNX_BIN_REL_PATH "lib/onnxruntime.dll")
    if(ENABLE_GPU)
         # GPU version has extra provider DLLs that we might want to track or install
         set(ONNX_PROVIDERS_CUDA_BIN_REL_PATH "lib/onnxruntime_providers_cuda.dll")
         set(ONNX_PROVIDERS_SHARED_BIN_REL_PATH "lib/onnxruntime_providers_shared.dll")
    endif()

elseif(UNIX)
    if(ENABLE_GPU)
        message(STATUS "Selecting ONNX Runtime GPU (CUDA) build for Linux")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-linux-x64-gpu-1.23.2.tgz")
    else()
         message(STATUS "Selecting ONNX Runtime CPU build for Linux")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-linux-x64-1.23.2.tgz")
    endif()

    set(ONNX_LIB_REL_PATH "lib/libonnxruntime.so.1.23.2")
    set(ONNX_BIN_REL_PATH "lib/libonnxruntime.so.1.23.2")
    set(ONNX_SO_LINK "lib/libonnxruntime.so") 
else()
    message(FATAL_ERROR "Unsupported platform for ONNX Runtime")
endif()

FetchContent_Declare(
    onnxruntime_bin
    URL ${ONNX_URL}
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime_bin)

# Create Imported Target for ONNX Runtime (Unconditional)
message(STATUS "ONNX Runtime Source Dir: ${onnxruntime_bin_SOURCE_DIR}")
add_library(onnxruntime SHARED IMPORTED)

if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}"
        IMPORTED_IMPLIB "${onnxruntime_bin_SOURCE_DIR}/${ONNX_LIB_REL_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_bin_SOURCE_DIR}/include"
    )
else()
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_bin_SOURCE_DIR}/${ONNX_LIB_REL_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_bin_SOURCE_DIR}/include"
    )
endif()

# 7. Silero VAD (v6.2)
message(STATUS "Fetching Silero VAD v6.2...")
FetchContent_Declare(
    silero_vad
    GIT_REPOSITORY https://github.com/snakers4/silero-vad.git
    GIT_TAG v6.2
    GIT_PROGRESS TRUE
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(silero_vad)

# 8. Whisper Models
# Base model (default, good for CPU)
set(WHISPER_MODEL_BASE "ggml-small.en.bin")
set(WHISPER_MODEL_BASE_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${WHISPER_MODEL_BASE}")
set(WHISPER_MODEL_BASE_PATH "${CMAKE_SOURCE_DIR}/models/${WHISPER_MODEL_BASE}")

if(NOT EXISTS "${WHISPER_MODEL_BASE_PATH}")
    message(STATUS "Downloading Whisper model: ${WHISPER_MODEL_BASE}...")
    file(DOWNLOAD 
        "${WHISPER_MODEL_BASE_URL}" 
        "${WHISPER_MODEL_BASE_PATH}"
        SHOW_PROGRESS
        STATUS WHISPER_DOWNLOAD_STATUS
    )
    list(GET WHISPER_DOWNLOAD_STATUS 0 WHISPER_DOWNLOAD_ERROR)
    if(WHISPER_DOWNLOAD_ERROR)
        message(FATAL_ERROR "Failed to download Whisper model: ${WHISPER_DOWNLOAD_STATUS}")
    endif()
    message(STATUS "Whisper model downloaded: ${WHISPER_MODEL_BASE_PATH}")
else()
    message(STATUS "Whisper model already exists: ${WHISPER_MODEL_BASE_PATH}")
endif()

# Note: Large-v3-turbo model (~1.6GB) removed - ggml-small.en.bin is sufficient for real-time STT

# --- Core Library (shared code between executable and tests) ---
add_library(signeo-core-lib STATIC
    src/audio/audio_capture.cpp
    src/audio/audio_processor.cpp
    src/audio/ringbuffer.cpp
    src/audio/resampler.cpp
    src/vad/vad_processor.cpp
    src/stt/stt_engine.cpp
    src/stt/streaming_transcriber.cpp
    src/ui/tui_renderer.cpp
    src/ui/tui_settings.cpp
    src/output/log_output.cpp
    src/output/json_output.cpp
    src/config/app_config.cpp
    src/core/application.cpp
    src/core/metrics_collector.cpp
    src/utils/signal_handler.cpp
    src/utils/thread_metrics.cpp
    src/utils/wer_calculator.cpp
    src/utils/config_file.cpp
    src/ipc/ipc_handler.cpp
)

target_include_directories(signeo-core-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(signeo-core-lib PUBLIC
    spdlog::spdlog
    CLI11::CLI11
    ftxui::screen
    ftxui::dom
    ftxui::component
    whisper
    portaudio
    onnxruntime
    speexdsp
)

if(ENABLE_CLANG_TIDY AND CLANG_TIDY_EXE)
    if(MSVC)
        set_target_properties(signeo-core-lib PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--extra-arg=/EHsc")
    else()
        set_target_properties(signeo-core-lib PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
endif()

# --- Main Executable ---
add_executable(signeo-core
    src/main.cpp
)

# Include directories
target_include_directories(signeo-core PRIVATE
    src
    ${CMAKE_BINARY_DIR}/src
)

# Link Libraries (core library brings in all other dependencies)
target_link_libraries(signeo-core PRIVATE
    signeo-core-lib
)

if(ENABLE_CLANG_TIDY AND CLANG_TIDY_EXE)
    if(MSVC)
        set_target_properties(signeo-core PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--extra-arg=/EHsc")
    else()
        set_target_properties(signeo-core PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(signeo-core PRIVATE /W4)
else()
    target_compile_options(signeo-core PRIVATE -Wall -Wextra)
endif()

# --- Post-Build Operations ---

# 1. Copy ONNX Runtime Library to executable directory
if(WIN32)
    # Copy main DLL
    if(EXISTS "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}")
        add_custom_command(TARGET signeo-core POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}"
            $<TARGET_FILE_DIR:signeo-core>
            COMMENT "Copying onnxruntime.dll to executable directory"
        )
    endif()

    # Copy CUDA Provider DLLs if GPU enabled
    if(ENABLE_GPU)
        if(EXISTS "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_CUDA_BIN_REL_PATH}")
            add_custom_command(TARGET signeo-core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_CUDA_BIN_REL_PATH}"
                $<TARGET_FILE_DIR:signeo-core>
                COMMENT "Copying onnxruntime_providers_cuda.dll"
            )
        endif()
        if(EXISTS "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_SHARED_BIN_REL_PATH}")
            add_custom_command(TARGET signeo-core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_SHARED_BIN_REL_PATH}"
                $<TARGET_FILE_DIR:signeo-core>
                COMMENT "Copying onnxruntime_providers_shared.dll"
            )
        endif()
    endif()

elseif(UNIX)
    # Copy .so file
    if(EXISTS "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}")
        add_custom_command(TARGET signeo-core POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}"
            "$<TARGET_FILE_DIR:signeo-core>/libonnxruntime.so.1.23.2"
            COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libonnxruntime.so.1.23.2"
            "$<TARGET_FILE_DIR:signeo-core>/libonnxruntime.so"
            COMMENT "Copying libonnxruntime.so to executable directory"
        )
    endif()
endif()

# 2. Copy Silero VAD Model to source models/ directory
add_custom_command(TARGET signeo-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${silero_vad_SOURCE_DIR}/src/silero_vad/data/silero_vad.onnx"
    "${CMAKE_SOURCE_DIR}/models/silero_vad.onnx"
    COMMENT "Copying Silero VAD model to source models/"
)

# 3. Copy models directory to executable directory (for VSCode debugging)
add_custom_command(TARGET signeo-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/models"
    "$<TARGET_FILE_DIR:signeo-core>/models"
    COMMENT "Copying models to executable directory for VSCode/CMake Tools"
)

# 4. Copy default config.ini to executable directory
if(EXISTS "${CMAKE_SOURCE_DIR}/config.ini")
    add_custom_command(TARGET signeo-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:signeo-core>/config.ini"
        COMMENT "Copying config.ini to executable directory"
    )
endif()

# 5. Copy example.env to executable directory
if(EXISTS "${CMAKE_SOURCE_DIR}/example.env")
    add_custom_command(TARGET signeo-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/example.env"
        "$<TARGET_FILE_DIR:signeo-core>/example.env"
        COMMENT "Copying example.env to executable directory"
    )
endif()

# ============================================================================
# Testing Infrastructure
# ============================================================================

option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

if(BUILD_TESTS)
    message(STATUS "Fetching Google Test...")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_PROGRESS TRUE
        USES_TERMINAL_DOWNLOAD TRUE
    )
    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Disable GTest installation (prevents it from showing up in CPack)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)
    

    # Unit Tests
    
    add_executable(unit_tests
        tests/unit/test_app_config.cpp
        tests/unit/test_audio_processor.cpp
        tests/unit/test_config_file.cpp
        tests/unit/test_log_output.cpp
        tests/unit/test_metrics_collector.cpp
        tests/unit/test_resampler.cpp
        tests/unit/test_result.cpp
        tests/unit/test_ring_buffer.cpp
        tests/unit/test_signal_handler.cpp
        tests/unit/test_streaming_transcriber.cpp
        tests/unit/test_thread_metrics.cpp
        tests/unit/test_thread_safe_queue.cpp
        tests/unit/test_wer_calculator.cpp
    )
    
    target_link_libraries(unit_tests PRIVATE
        gtest_main
        signeo-core-lib  # Link against core library
    )
    
    gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 60)
    
    message(STATUS "Tests enabled. Run with: ctest or ./build/unit_tests")

     # Integration Tests
    add_executable(integration_tests
        tests/integration/test_pipeline.cpp
    )
    
    target_link_libraries(integration_tests PRIVATE
        gtest_main
        signeo-core-lib
    )
    
    gtest_discover_tests(integration_tests DISCOVERY_TIMEOUT 60)

    # Benchmarks
    set(BENCHMARK_SRC "tests/benchmarks/benchmark_main.cpp")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${BENCHMARK_SRC}")
        message(STATUS "Found benchmark source: ${BENCHMARK_SRC}")
        add_executable(benchmarks ${BENCHMARK_SRC})
        target_link_libraries(benchmarks PRIVATE signeo-core-lib)
    else()
        message(WARNING "Benchmark source NOT found: ${BENCHMARK_SRC}")
    endif()

    # Enable code coverage if requested (APPLY TO ALL TARGETS)
    if(ENABLE_COVERAGE)
        if(MSVC)
            message(STATUS "Code coverage enabled (use OpenCppCoverage manually)")
        else()
            message(STATUS "Code coverage enabled (gcov/lcov)")
            
            # Atomic updates for multi-threaded coverage
            add_compile_options(-fprofile-update=atomic)
            
            # 1. Apply to Core Library (to instrument source code)
            # Use PUBLIC for link options so executables (main app, tests) inherit --coverage (links gcov)
            target_compile_options(signeo-core-lib PRIVATE --coverage)
            target_link_options(signeo-core-lib PUBLIC --coverage)
            
            # 2. Apply to Tests & Benchmarks
            foreach(TEST_TARGET unit_tests integration_tests benchmarks)
                if(TARGET ${TEST_TARGET})
                    target_compile_options(${TEST_TARGET} PRIVATE --coverage)
                    target_link_options(${TEST_TARGET} PRIVATE --coverage)
                endif()
            endforeach()
        endif()
    endif()
endif()

# ============================================================================
# Packaging
# ============================================================================

# Install rules
# Install rules
install(TARGETS signeo-core
    DESTINATION .
    COMPONENT Application
)

# Install Dependencies (DLLs / Shared Libs)
if(WIN32)
    install(FILES "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}"
        DESTINATION .
        COMPONENT Application
    )
    if(ENABLE_GPU)
        install(FILES 
            "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_CUDA_BIN_REL_PATH}"
            "${onnxruntime_bin_SOURCE_DIR}/${ONNX_PROVIDERS_SHARED_BIN_REL_PATH}"
            DESTINATION .
            COMPONENT Application
        )
    endif()
    
elseif(UNIX)
    install(FILES "${onnxruntime_bin_SOURCE_DIR}/${ONNX_BIN_REL_PATH}"
        DESTINATION lib
        COMPONENT Application
    )
    # Also install the symlink logic if CPack supports it, otherwise usually just the .so.version is enough if loader invokes it directly, but usually we need the symlink.
    # For Docker, we will handle copying manually or via install.
endif()

# Install Resources (only required models, exclude large turbo/base models)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/models
    DESTINATION .
    COMPONENT Application
    FILES_MATCHING
    PATTERN "ggml-small.en.bin"
    PATTERN "silero_vad.onnx"
    PATTERN "ggml-large-v3-turbo*" EXCLUDE
    PATTERN "ggml-base*" EXCLUDE
)

install(FILES ${CMAKE_SOURCE_DIR}/config.ini
    DESTINATION .
    COMPONENT Application
)

install(FILES ${CMAKE_SOURCE_DIR}/example.env
    DESTINATION .
    COMPONENT Application
    OPTIONAL
)

# Install Documentation
install(FILES 
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/docs/DEPLOYMENT.md
    ${CMAKE_SOURCE_DIR}/docs/CONTRIBUTING.md
    ${CMAKE_SOURCE_DIR}/docs/ARCHITECTURE.md
    DESTINATION .
    COMPONENT Application
)

include(CPack)

# Set package information
set(CPACK_PACKAGE_NAME "signeo-core")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-time audio to subtitles application")
set(CPACK_PACKAGE_VENDOR "Signeo")

# For Windows, use ZIP and NSIS generators (NSIS requires NSIS to be installed)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    # NSIS specific settings
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "
        CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CPACK_PACKAGE_NAME}.lnk' '$INSTDIR\\\\bin\\\\${CPACK_PACKAGE_NAME}.exe'
        CreateShortCut '$DESKTOP\\\\${CPACK_PACKAGE_NAME}.lnk' '$INSTDIR\\\\bin\\\\${CPACK_PACKAGE_NAME}.exe'
    ")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")
endif()
