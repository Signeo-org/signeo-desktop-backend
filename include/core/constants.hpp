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

/// Time conversion
inline constexpr float MILLISECONDS_PER_SECOND = 1000.0f;

/// Audio Capture / Processing
inline constexpr int MAX_INPUT_CHANNELS = 8;
inline constexpr int RESAMPLER_QUALITY = 5;
inline constexpr int RING_BUFFER_DURATION_SEC = 10;
inline constexpr int BITS_PER_SAMPLE = 16;
inline constexpr float PCM_TO_FLOAT_SCALE = 32768.0f;
inline constexpr float GAIN_THRESHOLD = 0.001f;
inline constexpr int RESAMPLER_BUFFER_MARGIN = 16;
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
inline constexpr int DEFAULT_HANGOVER_FRAMES = 20;

/// Default pre-roll frames to include before trigger
inline constexpr int DEFAULT_PRE_ROLL_FRAMES = 6;

/// Default adaptive threshold settings
inline constexpr bool DEFAULT_ADAPTIVE = true;
inline constexpr float DEFAULT_ADAPTIVE_MIN = 0.35f;
inline constexpr float DEFAULT_ADAPTIVE_MAX = 0.6f;
inline constexpr float DEFAULT_ADAPTIVE_ALPHA = 0.95f;

// Internal / Algorithm Constants
inline constexpr int INTERNAL_STATE_SIZE = 128;
inline constexpr int INTERNAL_PREROLL_MULTIPLIER = 128;
inline constexpr float INTERNAL_ADAPTIVE_OFFSET = 0.25f;
inline constexpr float INTERNAL_NOISE_FLOOR = 0.1f;
inline constexpr int INTERNAL_CONTEXT_SAMPLES = 64; 
}  // namespace vad_constants

/// @brief STT (Speech-to-Text) constants
namespace stt_constants {
/// Default model path
inline constexpr const char* DEFAULT_MODEL_PATH = "models/ggml-small.en.bin";

/// Default language
inline constexpr const char* DEFAULT_LANGUAGE = "en";

/// Default number of inference threads (fallback if hardware_concurrency fails)
inline constexpr int DEFAULT_THREADS_FALLBACK = 4;

/// Transcription step interval (ms)
inline constexpr int DEFAULT_STEP_MS = 2000;

/// Audio context to keep between transcriptions (ms)
inline constexpr int DEFAULT_KEEP_MS = 1000;

/// Maximum audio window size (ms)
inline constexpr int MAX_WINDOW_MS = 10000;

/// Deduplication and hallucination settings
inline constexpr bool DEFAULT_DEDUP = true;
inline constexpr int DEFAULT_MIN_REPETITION = 10;
inline constexpr int DEFAULT_HALLUCINATION_LEN = 2;

// Inference parameters (Greedy / Live Optimization)
inline constexpr int DEFAULT_BEAM_SIZE = 1;         // 1=greedy
inline constexpr float DEFAULT_TEMPERATURE = 0.0f;
inline constexpr float DEFAULT_TEMPERATURE_INC = 0.0f;
inline constexpr bool DEFAULT_NO_FALLBACK = true;
inline constexpr int DEFAULT_MAX_TOKENS = 0;        // 0=no limit
inline constexpr int DEFAULT_AUDIO_CTX = 0;         // 0=full context
inline constexpr bool DEFAULT_NO_CONTEXT = true;    // Independent chunks for live
inline constexpr float DEFAULT_NO_SPEECH_THOLD = 0.6f;
inline constexpr float DEFAULT_ENTROPY_THOLD = 2.4f;
inline constexpr float DEFAULT_LOGPROB_THOLD = -1.0f;
inline constexpr bool DEFAULT_SUPPRESS_BLANK = true;
inline constexpr bool DEFAULT_SUPPRESS_NST = true;
inline constexpr bool DEFAULT_TIMESTAMP_MERGE = true;

// Output settings
inline constexpr bool DEFAULT_PRINT_PROGRESS = false;
inline constexpr bool DEFAULT_PRINT_TIMESTAMPS = false;
inline constexpr bool DEFAULT_SINGLE_SEGMENT = true;
}  // namespace stt_constants

/// @brief Application runtime constants
namespace app_constants {
// CLI / Config Validation Ranges
inline constexpr int MIN_THREADS = 1;
inline constexpr int MAX_THREADS = 32;
inline constexpr float MIN_VAD_PROB = 0.0f;
inline constexpr float MAX_VAD_PROB = 1.0f;
inline constexpr float MIN_VAD_SMOOTHING = 0.05f;
inline constexpr float MAX_VAD_SMOOTHING = 0.9f;
inline constexpr int MAX_VAD_HANGOVER = 60;
inline constexpr int MAX_VAD_PREROLL = 20;
inline constexpr float MIN_VAD_ADAPTIVE_ALPHA = 0.01f;
inline constexpr float MAX_VAD_ADAPTIVE_ALPHA = 0.99f;

inline constexpr int MIN_STT_STEP = 500;
inline constexpr int MAX_STT_STEP = 10000;
inline constexpr int MAX_STT_KEEP = 5000;
inline constexpr int MIN_STT_MAXLEN = 2000;
inline constexpr int MAX_STT_MAXLEN = 30000;

inline constexpr int MIN_STT_REP = 4;
inline constexpr int MAX_STT_REP = 50;
inline constexpr int MIN_STT_HAL = 1;
inline constexpr int MAX_STT_HAL = 10;

// Runtime Timing
inline constexpr int MAIN_LOOP_SLEEP_MS = 100;
inline constexpr int AUDIO_WAIT_MS = 5;
inline constexpr float GAIN_EPSILON = 0.01f;

// Thread Metrics
inline constexpr uint64_t NANOSECONDS_PER_MILLISECOND = 1000000;
inline constexpr double WINDOWS_TIME_UNIT = 10000.0;
inline constexpr double PERCENT_MULTIPLIER = 100.0;
} // namespace app_constants


/// @brief Application version information
namespace version {
inline constexpr int MAJOR = 1;
inline constexpr int MINOR = 0;
inline constexpr int PATCH = 0;
inline constexpr const char* STRING = "1.0.0";
}  // namespace version

}  // namespace core
