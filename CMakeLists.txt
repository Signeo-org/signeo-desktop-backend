cmake_minimum_required(VERSION 3.15)
project(AudioTranscriptionTool)

# ------------------------------------------------------------------
# 0) Compiler settings
# ------------------------------------------------------------------

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default build type to "Release" if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# MSVC runtime
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Force static libs globally
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build all libs as shared libraries" FORCE)

# ------------------------------------------------------------------
# 1) Configure PortAudio
# ------------------------------------------------------------------

set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON  CACHE BOOL "" FORCE)

# Windows-specific host APIs
if(WIN32)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_ASIO  ON CACHE BOOL "" FORCE)
    set(PA_USE_DS    ON CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
    set(PA_USE_WMME  ON CACHE BOOL "" FORCE)
endif()

# Disable PortAudio examples/tests
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------------
# 2) Configure Whisper.cpp
# ------------------------------------------------------------------

set(WHISPER_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------------
# 3) Add Subdirectories (Dependencies)
# ------------------------------------------------------------------

add_subdirectory(external/whisper.cpp external/whisper.cpp/build)
add_subdirectory(external/portaudio  external/portaudio/build)

# ------------------------------------------------------------------
# 4) Define Sources
# ------------------------------------------------------------------

set(SOURCES
    src/main.cpp
    # src/AudioCapture.cpp
    # src/AudioPlayback.cpp
    # src/AAudioDevice.cpp
    # src/AudioDeviceManager.cpp
)

# Platform-specific files (if needed in future)
# if(WIN32)
#     set(PLATFORM_SOURCES
#         platform/windows/src/WindowsAudioCapture.cpp
#         platform/windows/src/WindowsAudioDevice.cpp
#         platform/windows/src/WindowsAudioPlayback.cpp
#     )
#     include_directories(${PROJECT_SOURCE_DIR}/platform/windows/include)
# endif()

set(ALL_SOURCES ${SOURCES} ${PLATFORM_SOURCES})

# include_directories(
#     ${PROJECT_SOURCE_DIR}/include/
# )

# ------------------------------------------------------------------
# 5) Executable
# ------------------------------------------------------------------

add_executable(AudioTranscriptionTool ${ALL_SOURCES})

# ------------------------------------------------------------------
# 6) Link Libraries
# ------------------------------------------------------------------

target_link_libraries(AudioTranscriptionTool
    PRIVATE
        portaudio
        whisper
)

# ------------------------------------------------------------------
# 7) Post-build: Copy models + executable
# ------------------------------------------------------------------

# Copy model files (*.bin) to build output
file(GLOB MODEL_FILES "${CMAKE_SOURCE_DIR}/models/*.bin")

add_custom_command(TARGET AudioTranscriptionTool POST_BUILD
    # Create "models" folder near executable
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MODEL_FILES}
            "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMENT "Copying model files"
)

# Also copy everything into build/Release/ for consistency
add_custom_command(TARGET AudioTranscriptionTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/Release"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:AudioTranscriptionTool>
            "${CMAKE_BINARY_DIR}/Release/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/models"
            "${CMAKE_BINARY_DIR}/Release/models"
    COMMENT "Copying build outputs to Release/"
)
