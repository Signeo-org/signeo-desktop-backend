cmake_minimum_required(VERSION 3.10)
project(AudioCapturePlayback)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories for common headers
include_directories(${PROJECT_SOURCE_DIR}/include)

# Collect common source files
set(SOURCES
    src/main.cpp
    src/AudioCapture.cpp
    src/AudioPlayback.cpp
    src/AAudioDevice.cpp
    src/AudioDeviceManager.cpp
)

# Platform-specific source files
if(WIN32)
    set(PLATFORM_SOURCES
        platform/windows/src/WindowsAudioCapture.cpp
        platform/windows/src/WindowsAudioDevice.cpp
        platform/windows/src/WindowsAudioPlayback.cpp
    )
    include_directories(${PROJECT_SOURCE_DIR}/platform/windows/include)
endif()

# Combine all source files
set(ALL_SOURCES ${SOURCES} ${PLATFORM_SOURCES})

# Add executable target
add_executable(AudioCapturePlayback ${ALL_SOURCES})

# Add PortAudio as a subdirectory
set(PORTAUDIO_DIR "${CMAKE_SOURCE_DIR}/external/portaudio")
add_subdirectory(${PORTAUDIO_DIR} ${CMAKE_BINARY_DIR}/portaudio_build EXCLUDE_FROM_ALL)

# Configure PortAudio options (Windows-specific settings)
if(WIN32)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
    set(PA_USE_DS ON CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
    set(PA_USE_WMME ON CACHE BOOL "" FORCE)
endif()

# PortAudio build options
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED ON CACHE BOOL "" FORCE)

# Include PortAudio headers
include_directories(${PORTAUDIO_DIR}/include)

# Link PortAudio shared library
target_link_libraries(AudioCapturePlayback PRIVATE portaudio)

# Add preprocessor definitions
if(WIN32)
    target_compile_definitions(AudioCapturePlayback PRIVATE PA_USE_WASAPI)
endif()

# Windows-specific post-build steps
if(WIN32)
    # Copy the PortAudio DLL to the executable directory
    add_custom_command(TARGET AudioCapturePlayback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:portaudio>
        $<TARGET_FILE_DIR:AudioCapturePlayback>
    )

    # Copy PDB files if building in Debug mode
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_PDB_FILE:portaudio>
            $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
    endif()
endif()
