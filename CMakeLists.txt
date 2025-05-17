cmake_minimum_required(VERSION 3.5)
project(AudioCapture)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set consistent runtime library
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL") # or "MultiThreaded" for static

# Add include directory for PortAudio headers
include_directories("${CMAKE_SOURCE_DIR}/include")

# Add executable
add_executable(audio_capture "${CMAKE_SOURCE_DIR}/sources/main.cpp")

# Link PortAudio library
target_link_libraries(audio_capture PRIVATE "${CMAKE_SOURCE_DIR}/lib/portaudio_static_x64.lib")

# Link additional system libraries required by PortAudio
target_link_libraries(audio_capture PRIVATE winmm ole32 oleaut32 uuid setupapi)
