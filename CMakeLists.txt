cmake_minimum_required(VERSION 3.10)
project(AudioCapturePlayback)

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
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build all libs as shared libraries" FORCE)

# ------------------------------------------------------------------
# 2) Configure PortAudio
# ------------------------------------------------------------------

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
# 3) Add Subdirectories for Dependencies
#    (Adjust paths to where you cloned them)
# ------------------------------------------------------------------
add_subdirectory(external/whisper.cpp external/whisper.cpp/build)
add_subdirectory(external/portaudio external/portaudio/build)

# ------------------------------------------------------------------
# 4) Define Your Executable
# ------------------------------------------------------------------
set(MY_SOURCES
    main.cpp
    # Add other .cpp/.h if needed
)

add_executable(AudioCapturePlayback ${MY_SOURCES})

# ------------------------------------------------------------------
# 5) Link Against PortAudio + Whisper
# ------------------------------------------------------------------
target_link_libraries(AudioCapturePlayback
    PRIVATE
        portaudio
        whisper
)

# ------------------------------------------------------------------
# 6) Copy all necessary DLLs (and optional PDBs in Debug) on Windows
# ------------------------------------------------------------------
if(WIN32)

    # --- PortAudio ---
    add_custom_command(TARGET AudioCapturePlayback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:portaudio>                # e.g. portaudio.dll
            $<TARGET_FILE_DIR:AudioCapturePlayback> # e.g. build/Release/
    )

    # --- Whisper ---
    add_custom_command(TARGET AudioCapturePlayback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:whisper>                 
            $<TARGET_FILE_DIR:AudioCapturePlayback>
    )

    # --- ggml (core) ---
    if(TARGET ggml)
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ggml>
                $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
    endif()

    # --- ggml-base ---
    if(TARGET ggml-base)
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ggml-base>
                $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
    endif()

    # --- ggml-cpu ---
    if(TARGET ggml-cpu)
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ggml-cpu>
                $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
    endif()

    # --- Copy PDB files if building in Debug mode ---
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # PortAudio PDB
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:portaudio>
                $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
        # Whisper PDB
        add_custom_command(TARGET AudioCapturePlayback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_PDB_FILE:whisper>
                $<TARGET_FILE_DIR:AudioCapturePlayback>
        )
        # ggml PDB
        if(TARGET ggml)
            add_custom_command(TARGET AudioCapturePlayback POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:ggml>
                    $<TARGET_FILE_DIR:AudioCapturePlayback>
            )
        endif()
        # ggml-base PDB
        if(TARGET ggml-base)
            add_custom_command(TARGET AudioCapturePlayback POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:ggml-base>
                    $<TARGET_FILE_DIR:AudioCapturePlayback>
            )
        endif()
        # ggml-cpu PDB
        if(TARGET ggml-cpu)
            add_custom_command(TARGET AudioCapturePlayback POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_PDB_FILE:ggml-cpu>
                    $<TARGET_FILE_DIR:AudioCapturePlayback>
            )
        endif()
    endif()
endif()
