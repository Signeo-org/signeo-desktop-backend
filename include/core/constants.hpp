#pragma once

/**
 * @file constants.h
 * @brief Centralized compile-time constants for the application
 *
 * Provides type-safe, documented constants organized by module namespace.
 * All constants are inline constexpr for compile-time optimization.
 */

#include <cstdint>

namespace core {

/// @brief Audio processing constants
namespace audio_constants {
/// Standard sample rate for VAD/STT processing (16kHz for Whisper)
inline constexpr int SAMPLE_RATE = 16000;

/// Frame size in samples (512 samples = 32ms at 16kHz)
inline constexpr int FRAME_SIZE = 512;

/// Frame duration in milliseconds
inline constexpr int FRAME_DURATION_MS = 32;

/// Minimum audio length for transcription (ms)
inline constexpr int MIN_AUDIO_LENGTH_MS = 200;

/// Maximum audio buffer size (30 seconds)
inline constexpr int MAX_BUFFER_MS = 30000;
}  // namespace audio_constants

/// @brief VAD (Voice Activity Detection) constants
namespace vad_constants {
/// Default speech probability threshold
inline constexpr float DEFAULT_THRESHOLD = 0.5f;

/// Default RMS energy gate threshold
inline constexpr float DEFAULT_ENERGY_THRESHOLD = 0.001f;

/// Default EMA smoothing alpha (0.1=stable, 0.5=responsive)
inline constexpr float DEFAULT_SMOOTHING_ALPHA = 0.3f;

/// Default hangover frames to extend after speech ends
inline constexpr int DEFAULT_HANGOVER_FRAMES = 8;

/// Default pre-roll frames to include before trigger
inline constexpr int DEFAULT_PRE_ROLL_FRAMES = 6;
}  // namespace vad_constants

/// @brief STT (Speech-to-Text) constants
namespace stt_constants {
/// Default number of inference threads
inline constexpr int DEFAULT_THREADS = 4;

/// Transcription step interval (ms)
inline constexpr int DEFAULT_STEP_MS = 2000;

/// Audio context to keep between transcriptions (ms)
inline constexpr int DEFAULT_KEEP_MS = 500;

/// Maximum audio window size (ms)
inline constexpr int MAX_WINDOW_MS = 10000;
}  // namespace stt_constants

/// @brief Application version information
namespace version {
inline constexpr int MAJOR = 1;
inline constexpr int MINOR = 0;
inline constexpr int PATCH = 0;
inline constexpr const char* STRING = "1.0.0";
}  // namespace version

}  // namespace core
