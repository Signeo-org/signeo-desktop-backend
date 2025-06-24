cmake_minimum_required(VERSION 3.10)
project(AudioTranscriptionTool)

# ------------------------------------------------------------------
# 1) Configure Project Settings
# ------------------------------------------------------------------

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default build type to "Release" if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Force building all sub-libraries as shared (DLLs on Windows)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build all libs as shared libraries" FORCE)

# ------------------------------------------------------------------
# 2) Configure PortAudio
# ------------------------------------------------------------------

# Make lib static
set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON  CACHE BOOL "" FORCE)

# Windows-specific options for PortAudio host APIs
if(WIN32)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
    set(PA_USE_DS ON CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
    set(PA_USE_WMME ON CACHE BOOL "" FORCE)
endif()

# Disable PortAudio's examples/tests (optional, to reduce clutter)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------------
# 2) Configure Whisper.cpp
# ------------------------------------------------------------------

set(WHISPER_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------------
# 3) Add Subdirectories for Dependencies
# ------------------------------------------------------------------
add_subdirectory(external/whisper.cpp external/whisper.cpp/build)
add_subdirectory(external/portaudio external/portaudio/build)

# ------------------------------------------------------------------
# 4) Define Your Executable
# ------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    # Add other .cpp/.h if needed
)

add_executable(AudioTranscriptionTool ${SOURCES})

# ------------------------------------------------------------------
# 5) Link Against PortAudio + Whisper
# ------------------------------------------------------------------
target_link_libraries(AudioTranscriptionTool
    PRIVATE
        portaudio
        whisper
)

# --- Copy model files (*.bin) ---
file(GLOB MODEL_FILES "${CMAKE_SOURCE_DIR}/models/*.bin")
add_custom_command(TARGET AudioTranscriptionTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MODEL_FILES}
            "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMENT "Copying model files"
)
