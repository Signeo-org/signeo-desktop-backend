cmake_minimum_required(VERSION 3.15)
project(AudioTranscriptionTool)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# MSVC runtime configuration
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Build all libs statically
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build all libs as shared libraries" FORCE)

# PortAudio options for Windows
if(WIN32)
    set(PA_USE_WASAPI ON CACHE BOOL "" FORCE)
    set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
    set(PA_USE_DS ON CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS ON CACHE BOOL "" FORCE)
    set(PA_USE_WMME ON CACHE BOOL "" FORCE)
endif()

# Disable PortAudio examples/tests
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Whisper.cpp build settings
set(WHISPER_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Add subdirectories - your submodules here
add_subdirectory(external/whisper.cpp external/whisper.cpp/build)
add_subdirectory(external/portaudio external/portaudio/build)

# Manually specify ONNX Runtime include and libs
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/external/onnxruntime-win-x64-1.17.3")

include_directories("${ONNXRUNTIME_ROOT}/include")
link_directories("${ONNXRUNTIME_ROOT}/lib")

# Define your source files
set(SOURCES
    src/main.cpp
)

add_executable(AudioTranscriptionTool ${SOURCES})

# Link libraries (order can matter)
target_link_libraries(AudioTranscriptionTool
    PRIVATE
        portaudio
        whisper
        onnxruntime.lib    # Ensure exact lib name matches your folder (e.g. onnxruntime.lib or onnxruntime.dll.lib)
)

# Post build: copy models folder to output directory
file(GLOB MODEL_FILES "${CMAKE_SOURCE_DIR}/models/*.bin")

add_custom_command(TARGET AudioTranscriptionTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MODEL_FILES}
        "$<TARGET_FILE_DIR:AudioTranscriptionTool>/models"
    COMMENT "Copying model files"
)

# Also copy executables and models to build/Release for Visual Studio users
add_custom_command(TARGET AudioTranscriptionTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Release"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:AudioTranscriptionTool> "${CMAKE_BINARY_DIR}/Release/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${CMAKE_BINARY_DIR}/Release/models"
    COMMENT "Copying build outputs to Release/"
)
