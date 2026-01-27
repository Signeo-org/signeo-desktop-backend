# Real-Time Audio-to-Subtitles Implementation Plan

## Project Overview
**Objective:** Build a cross-platform, locally-running, real-time audio-to-subtitle CLI application that captures audio from microphone and OS loopback, processes it through a speech-to-text pipeline, and displays subtitles in real-time via terminal output.

- **Target Platforms:** Windows, macOS, Linux
- **Primary Language:** C++17/20
- **Key Constraint:** 100% local processing, no external APIs
- **Expected Latency:** 200–400ms end-to-end

---

## Phase 1: Foundation & Architecture

### 1.1 Project Setup
**Tasks:**
- [ ] Initialize CMake-based C++ project structure
- [ ] Set up version control (Git) and CI/CD pipeline (GitHub Actions)
- [ ] Define directory structure:
    ```text
    signeo-core/
    ├── CMakeLists.txt
    ├── src/
    │   ├── main.cpp
    │   ├── audio/
    │   │   ├── audio_capture.h
    │   │   ├── audio_capture.cpp
    │   │   └── ringbuffer.h
    │   ├── vad/
    │   │   ├── vad_processor.h
    │   │   ├── vad_processor.cpp
    │   ├── stt/
    │   │   ├── whisper_engine.h
    │   │   ├── whisper_engine.cpp
    |   ├── output/
    |   |   ├── tui_renderer.h
    |   |   ├── tui_renderer.cpp
    │   └── utils/
    │       ├── logger.h
    │       ├── timestamp.h
    │       └── config.h
    ├── deps/      # External dependencies (CMake FetchContent)
    ├── models/    # Pre-downloaded ONNX/Whisper models
    ├── tests/
    ├── docs/
    └── build/
    ```

**Deliverables:**
- CMakeLists.txt with cross-platform support
- Basic project structure in place
- README with setup instructions

### 1.2 Dependency Integration
**Tasks:**
- [ ] **PortAudio:** Integrate via CMake (FetchContent or vcpkg)
    - Test microphone capture on all three platforms
    - Test loopback device enumeration
    - Create cross-platform audio device selector utility
    - v19.7.0
- [ ] **ONNX Runtime:** Set up for Silero VAD
    - Download pre-built ONNX Runtime binaries
    - Link against C++ API
    - Test ONNX model loading
    - v1.16.0
- [ ] **Whisper.cpp:** Clone and integrate
    - Use official `ggerganov/whisper.cpp` repository
    - Build as CMake subdirectory or external library
    - Verify model loading (base/small models)
    - v1.8.2
- [ ] **FTXUI:** Integrate via CMake (FetchContent)
    - Set up library for "flagship" Terminal User Interface (TUI)
    - Verify basic rendering (screen, dom components)
    - v6.1.9
- [ ] **Spdlog:** Add spdlog for structured logging
    - v1.16.0
- [ ] **CLI11:** Add CLI11 for command-line parsing
    - v2.6.1
- [ ] **Silero VAD:** Integrate via CMake (FetchContent or vcpkg)
    - Download pre-built Silero VAD binaries
    - Link against C++ API
    - Test Silero VAD model loading
    - v6.2.0
- [ ] **SpeexDSP:** Integrate via CMake (FetchContent)
    - Download official source (xiph/speexdsp)
    - Build static library with `resample.c`
    - Verify resampling quality (48kHz -> 16kHz)
    - v1.2.1

**Deliverables:**
- All dependencies resolved and linked
- Simple test programs for each dependency
- Cross-platform build validates on Windows, macOS, Linux

---

## Phase 2: Audio Capture Pipeline

### 2.1 Audio Input Module
**Tasks:**
- [ ] **PortAudio Wrapper (`audio_capture.cpp`):**
    ```cpp
    class AudioCapture {
    public:
        AudioCapture(int sample_rate = 16000, int channels = 1);
        void start();
        void stop();
        std::vector<float> read_chunk(size_t frames);
        std::vector<AudioDevice> list_devices();
    private:
        PaStream* stream_;
        Ringbuffer ringbuffer_; // Lock-free or mutex-protected
    };
    ```
- [ ] **Ringbuffer Implementation (`ringbuffer.h`):**
    - Lock-free circular buffer for audio samples
    - Safe for producer (PortAudio callback) and consumer (main thread)
    - Prevent buffer underruns/overruns with proper sizing
- [ ] **Device Enumeration:**
    - List available microphones and loopback devices
    - Auto-detect default loopback device per OS:
        - **Windows:** WASAPI Stereo Mix / VB-Audio Virtual Input
        - **macOS:** BlackHole / Soundflower
        - **Linux:** PulseAudio monitor_sink or ALSA loopback
- [ ] **Cross-Platform Audio Format Normalization:**
    - Ensure all input resampled to 16kHz mono (Whisper.cpp standard)
    - Handle float32 ↔ int16 conversion if needed

**Tests:**
- [ ] Unit test: ringbuffer push/pop correctness
- [ ] Integration test: capture audio from microphone, verify signal presence
- [ ] Integration test: detect loopback device on each platform

**Deliverables:**
- AudioCapture class fully functional on all platforms
- CLI utility: `./signeo-core --list-devices`
- Audio capture test that streams 10 seconds of audio to file for inspection

### 2.2 Audio Preprocessing
**Tasks:**
- [ ] **Gain Normalization:**
    - Auto-detect and normalize input levels
    - Prevent clipping/saturation
- [ ] **Noise Gate (optional but recommended):**
    - Simple RMS-based silence detection before VAD
    - Reduces false positives in VAD

**Deliverables:**
- Audio preprocessing module with configurable thresholds
- Test: verify gain normalization on sample audio

---

## Phase 3: Voice Activity Detection

### 3.1 Silero VAD Integration
**Tasks:**
- [ ] **Download Silero VAD ONNX Model:**
    - Get `silero_vad.onnx` from Silero project
    - Store in `models/` directory
    - Create model loader utility
- [ ] **VAD Processor (`vad_processor.cpp`):**
    ```cpp
    class VadProcessor {
    public:
        VadProcessor(const std::string& model_path);
        VADResult process(const std::vector<float>& audio_chunk);
        void reset(); // Reset state between utterances
    private:
        OrtSession* session_;
        float threshold_; // Configurable, default ~0.5
    };
    ```
- [ ] **Streaming State Management:**
    - VAD processes 512-sample chunks (16kHz = 32ms windows)
    - Maintain session state for continuous processing
    - Implement state reset on silence periods
- [ ] **Gating Logic:**
    - Only queue audio for STT when VAD confidence exceeds threshold
    - Implement hysteresis to avoid toggling on boundary samples

**Tests:**
- [ ] Unit test: VAD correctly identifies speech vs. silence on test audio
- [ ] Integration test: VAD reduces unnecessary STT processing by >50%

**Deliverables:**
- VadProcessor class fully integrated
- Metrics output: silence/speech duration, detection confidence

---

## Phase 4: Speech-to-Text Engine

### 4.1 Whisper.cpp Integration
**Tasks:**
- [ ] **Download Whisper Models:**
    - **Primary:** `ggml-base.bin` or `ggml-small.bin` available
    - **Cutting Edge:** Add support for **Distil-Whisper** (e.g., `distil-large-v3`) and **Turbo**  and **Quantized** models (e.g., `large-v3-turbo-q5_0`) for 2-3x speedup
    - Store in `models/` directory
    - Create model loader with integrity checks
- [ ] **Whisper Engine (`whisper_engine.cpp`):**
    ```cpp
    class WhisperEngine {
    public:
        WhisperEngine(const std::string& model_path, int n_threads = 4);
        TranscriptionResult process_chunk(const std::vector<float>& audio);
        TranscriptionResult finalize(); // Get final result
        void reset();
    private:
        WhisperContext* ctx_;
        std::vector<float> accumulated_audio_; // Buffer for processing
    };
    ```
- [ ] **Streaming & Partial Results:**
    - Accumulate audio chunks in internal buffer
    - Process in batches (e.g., every 1–2 seconds of audio)
    - Return partial transcriptions asynchronously
    - Mark segments as "final" when confidence is high
- [ ] **Hardware Acceleration (GPU & NPU):**
    - **macOS:** Enable CoreML support (Apple Neural Engine) via `whisper.cpp` flags
    - **Windows/Linux:** Enable OpenVINO (Intel) or CUDA (NVIDIA) where available
    - Fall back to standard CPU if unavailable
    - Benchmark latency improvements (NPU vs CPU)
- [ ] **Multilingual Support:**
    - Auto-detect language on first utterance
    - Allow manual language override via CLI flag
    - Test with Spanish, English, other languages

**Tests:**
- [ ] Unit test: Whisper model loads correctly
- [ ] Integration test: transcribe known audio sample, verify accuracy
- [ ] Performance test: measure latency for various model sizes
- [ ] Stress test: continuous audio over 5+ minutes, no memory leaks

**Deliverables:**
- WhisperEngine class with streaming support
- Model download utility: `./signeo-core --download-models`
- Benchmark: latency and accuracy metrics for base/small models

---

## Phase 5: Real-Time TUI Subtitle Renderer

### 5.1 TUI Output Module (FTXUI)
**Tasks:**
- [ ] **TUI Renderer (`tui_renderer.cpp`):**
    - Use **FTXUI** library to create a rich, "flagship" visual interface
    - Implement responsive layout:
        - **Header:** App status, active device, model info
        - **Main View:** Scrolling subtitle history with smooth rendering
        - **Footer:** Real-time metrics (latency, probability), controls help
    ```cpp
    class TuiRenderer {
    public:
        void render_frame(const AppState& state);
        void update_subtitle(const TranscriptionResult& result);
        void start_event_loop();
    private:
        ftxui::ScreenInteractive screen_;
        // FTXUI components...
    };
    ```
- [ ] **Modern Polish:**
    - Use borders, colors, and gauges for confident levels
    - Implement smooth scrolling for new text
    - **Aesthetics:** "Glassmorphism" simulation in TUI (if possible) or clean, modern borders
- [ ] **Output Formats (Backend):**
    - Plain text: `[HH:MM:SS] transcribed text` (logging)
    - JSON: `{"timestamp_ms": 1500, "text": "...", "confidence": 0.95, "is_final": true}`
    - WebVTT chunks: Prepare for future frontend integration
- [ ] **Buffering & Synchronization:**
    - Ensure timestamps align precisely with audio
    - Implement re-sync logic on drift detection

**Tests:**
- [ ] Unit test: TUI component layout logic
- [ ] Integration test: real-time updates without flickering
- [ ] Stress test: rapid subtitle updates (e.g., 100+ segments/min)

**Deliverables:**
- TuiRenderer class using FTXUI
- "Flagship" visual terminal interface
- Configurable verbosity and formatting options

---

## Phase 6: Main Application & Threading

### 6.1 Application Architecture
**Tasks:**
- [ ] **Multi-threaded Pipeline:**
    - **Main Thread**
    - ├─→ **Audio Capture Thread** (PortAudio callback)
    - ├─→ **VAD Processing Thread** (gated, low latency)
    - ├─→ **STT Worker Thread** (Whisper.cpp, may block)
    - └─→ **TUI Renderer Thread** (output)
- [ ] **Thread-Safe Communication:**
    - Use lock-free queues (if available) or mutex-protected deques
    - Audio → VAD: ringbuffer
    - VAD → STT: command queue
    - STT → Renderer: result queue
- [ ] **Main Application (`main.cpp`):**
    ```cpp
    int main(int argc, char* argv[]) {
        CLI11::App app{"Real-Time Subtitler"};
        std::string device = "default";
        std::string model = "base";
        int threads = 4;
        bool use_gpu = false;

        app.add_option("--device", device, "Audio device");
        app.add_option("--model", model, "Whisper model size");
        app.add_flag("--gpu", use_gpu, "Use GPU acceleration");
        // ... more options

        CLI11_PARSE(app, argc, argv);

        Application app_runner(device, model, threads, use_gpu);
        return app_runner.run();
    }
    ```
- [ ] **Graceful Shutdown:**
    - Handle Ctrl+C (SIGINT)
    - Flush remaining audio, finalize pending segments
    - Clean up resources (threads, models, streams)

**Tests:**
- [ ] Integration test: all threads start and stop cleanly
- [ ] Stress test: 10+ minutes of continuous audio processing
- [ ] Shutdown test: Ctrl+C during active transcription

**Deliverables:**
- Full application executable: `./signeo-core [options]`
- Help menu with all CLI options

---

## Phase 7: Application Performance Monitoring & Metrics

### 7.1 Performance Monitoring Infrastructure
**Tasks:**
- [ ] **Core APM Framework:**
    - Use lightweight, header-only metrics library or integrate OpenTelemetry C++ for production-grade monitoring
    - Create custom `MetricsCollector` class to track:
        - **Latency:** Per-stage timing (audio capture, VAD, STT, rendering)
        - **Resource Usage:** CPU %, memory, peak allocations
        - **Throughput:** Words per second, audio frames processed
        - **Error Rates:** VAD false positives, STT inference errors, buffer overflows
    ```cpp
    class MetricsCollector {
    public:
        MetricsCollector();
        void start_timer(const std::string& stage); // e.g., "stt_inference"
        void stop_timer(const std::string& stage);
        void record_metric(const std::string& name, double value);
        void record_counter(const std::string& name, int64_t count);
        // Retrieval
        MetricsSnapshot get_metrics();  // Current snapshot
        MetricsReport get_report();      // Statistical summary (mean, P50, P95, P99)
    };
    ```
- [ ] **Per-Stage Instrumentation:**
    - **Audio Capture:** Measure ringbuffer fullness, capture latency
    - **VAD Processing:** Inference time, speech vs. silence ratio
    - **STT Engine:** Model inference time, queue wait time, token count
    - **TUI Renderer:** Output formatting time, screen refresh latency
    - **Synchronization:** End-to-end audio→subtitle timestamp drift
- [ ] **Memory Profiling:**
    - Track allocations per stage using RAII wrappers or custom allocator
    - Log peak memory, sustained memory, fragmentation
    - Detect memory leaks over 1+ hour runs
- [ ] **CPU Profiling:**
    - Measure CPU usage per thread
    - Identify bottlenecks (e.g., STT inference dominates)
    - Track wake-ups and context switches
- [ ] **Metrics Output Formats:**
    - JSON export: `metrics.json` with timeseries data
    - CSV export: For spreadsheet analysis
    - Real-time stdout: Live metrics during execution (e.g., `[CPU: 45% | Mem: 234MB | Latency_p95: 287ms]`)
    - Optional CloudWatch/Prometheus export: For distributed systems

**Deliverables:**
- MetricsCollector class with thread-safe recording
- Per-stage timing instrumentation throughout pipeline
- CLI flags: `--enable-metrics`, `--metrics-output json|csv|stdout`
- JSON schema for metrics export
- Example metrics dashboard (static HTML or Grafana)

### 7.2 Configuration Management
**Tasks:**
- [ ] **CLI Options:**
    ```bash
    ./signeo-core \
      --device "Microphone" \
      --model base \
      --language auto \
      --vad-threshold 0.5 \
      --output-format json \
      --threads 4 \
      --verbose \
      --enable-metrics \
      --metrics-output json \
      --metrics-file metrics.json
    ```
- [ ] **Config File Support (optional):**
    - YAML or INI format
    - `~/.signeo-core/config.yaml`
- [ ] **Environment Variables:**
    - `SUBTITLER_MODEL_PATH`, `SUBTITLER_DEVICE`, `SUBTITLER_ENABLE_METRICS`, etc.

**Deliverables:**
- All CLI options documented including metrics-related flags
- Config file example included with metrics settings
- Metrics configuration schema

### 7.3 Performance Optimization
**Tasks (enabled by Phase 7.1 metrics):**
- [ ] **Latency Analysis:**
    - Use `MetricsCollector` to identify bottleneck stages
    - Profile with P50/P95/P99 percentiles (not just mean)
    - Measure:
        - **TTFB (Time-to-First-Bytes):** Audio start → first STT token
        - **TTFC (Time-to-First-Correct):** Audio start → first correct transcription
        - **Final Latency:** End-of-speech → final subtitle locked
- [ ] **Memory Management:**
    - Use `MetricsCollector` memory tracking
    - Profile memory usage over 1 hour continuous run
    - Implement object pooling for audio chunks if needed
    - Check for memory leaks with Valgrind/ASAN
    - Track peak memory and sustained memory separately
- [ ] **CPU Utilization:**
    - Measure CPU usage per thread via `MetricsCollector`
    - Identify bottleneck (typically STT inference)
    - Tune number of threads based on system capabilities
    - Consider thread affinity for optimal cache behavior
- [ ] **Model Optimization (optional):**
    - Quantize Whisper model if latency is still high
    - Profile inference time breakdown using `MetricsCollector`
    - A/B test model sizes (tiny, base, small) with latency vs. accuracy trade-off

**Deliverables:**
- Benchmark report: latency (P50/P95/P99), memory, CPU for typical workload
- Metrics export in JSON format with statistical breakdowns
- Optimization recommendations for future phases
- Comparative analysis: model size vs. latency/accuracy

---

## Phase 8: Testing & Performance Benchmarking

### 8.1 Unit Testing
**Tasks:**
- [ ] Set up Google Test (gtest) framework
- [ ] Write unit tests for:
    - Ringbuffer push/pop
    - Audio format conversion
    - Timestamp formatting
    - VAD confidence scoring
    - JSON serialization
    - NEW: MetricsCollector accuracy and thread safety

**Deliverables:**
- 80% code coverage for core modules
- All tests passing on CI/CD pipeline
- Unit tests for metrics collection

### 8.2 Performance Benchmarking Suite
**Tasks:**
- [ ] **Build a Benchmark Framework (`tests/benchmark/`):**
    ```cpp
    struct BenchmarkTestCase {
        std::string name;
        std::string audio_file; // Path to test audio
        std::string reference_transcript; // Ground truth
        int64_t video_timestamp_ms; // For sync testing
    };
    class BenchmarkRunner {
        void run_test(const BenchmarkTestCase& test);
        void generate_report(); // WER, latency, sync metrics
    };
    ```
- [ ] **Word Error Rate (WER) Calculation:**
    - Implement Levenshtein distance algorithm for word-level alignment
    - Calculate: `WER = (Substitutions + Deletions + Insertions) / Total Words`
    - Track breakdown: substitution%, deletion%, insertion% per test
    - Use library: `jiwer` (Python binding) or implement in C++
    ```cpp
    class WerCalculator {
        double calculate_wer(const std::string& reference,
                             const std::string& hypothesis);
        WerBreakdown get_breakdown(); // {substitutions, deletions, insertions}
    };
    ```
- [ ] **Latency Benchmarks:**
    - End-to-End Latency: Time from audio frame to rendered subtitle
    - Per-Stage Latency: VAD → STT → Rendering (P50/P95/P99)
    - Real-Time Factor (RTF): Processing time / audio duration (target <1.0)
    - TTFB/TTFC: Time to first byte/first correct transcription
- [ ] **Video Synchronization Testing:**
    - Create test videos with burned-in timestamps
    - Capture system output with screen recording
    - Use frame-by-frame comparison to measure subtitle-to-audio drift
    - Acceptable drift: ±100ms
    ```cpp
    struct SyncMeasurement {
        int64_t audio_timestamp_ms;
        int64_t subtitle_timestamp_ms;
        int64_t drift_ms; // Positive = subtitle lags
    };
    ```
- [ ] **Test Data & Fixtures:**
    - Audio Samples:
        - Clean English (1 min)
        - Noisy/background (1 min)
        - Multiple speakers (1 min)
        - Non-English language (Spanish, etc., 1 min)
        - Edge cases: very quiet, clipped, music
    - Reference Transcripts: Hand-corrected ground truth for WER calculation
    - Expected Metrics: WER <10%, latency <400ms p95, RTF <1.0
- [ ] **Automated Benchmark Harness:**
    - `./signeo-core --benchmark --benchmark-output=report.json`
    - Output JSON structure:
        ```json
        {
          "test_cases": [
            {
              "name": "clean_english",
              "wer": 0.045,
              "wer_breakdown": {"sub": 2, "del": 1, "ins": 0},
              "latency_ms": {"p50": 150, "p95": 287, "p99": 420},
              "rtf": 0.65,
              "sync_drift_ms": 45,
              "memory_peak_mb": 256,
              "cpu_avg_percent": 42
            }
          ],
          "summary": {...}
        }
        ```

**Deliverables:**
- BenchmarkRunner class with full benchmarking pipeline
- WerCalculator for transcription accuracy measurement
- Test audio fixtures (5–10 samples covering various scenarios)
- Benchmark JSON report schema
- Automated harness: `./signeo-core --benchmark`
- HTML/Markdown benchmark report generator
- Target metrics documented and validated

### 8.3 Integration Testing
**Tasks:**
- [ ] **End-to-End Tests:**
    - Real audio input → full pipeline → subtitle output
    - Test on all three platforms (Windows, macOS, Linux)
    - Verify metrics collection during E2E tests
- [ ] **Loopback Testing:**
    - Play pre-recorded audio through loopback device
    - Verify transcription accuracy and latency
    - Validate video sync against burned-in timestamps
- [ ] **Edge Cases:**
    - Very quiet audio (measure WER degradation)
    - Very loud/clipped audio (measure robustness)
    - Music/background noise (measure false-positive VAD)
    - Multiple speakers (if applicable)
    - Language switching (measure language detection latency)
- [ ] **Stress Tests:**
    - 1+ hour continuous processing (monitor memory leaks via metrics)
    - Rapid language switches
    - Device hot-plugging (microphone disconnect/reconnect)
    - Verify metrics stability under stress
- [ ] **Metrics Validation:**
    - Verify MetricsCollector timing accuracy (cross-check with external profiler)
    - Validate JSON export correctness
    - Test concurrent metric recording from multiple threads

**Deliverables:**
- Test suite with >15 integration test scenarios
- Test data (audio samples, reference transcripts) included
- Test report with pass/fail results and metrics summary
- Video sync validation report (drift measurements)

### 8.4 Cross-Platform Validation & Metrics Collection
**Tasks:**
- [ ] **Build and test on:**
    - Windows 10/11 (WASAPI) with metrics enabled
    - macOS 12+ (Core Audio) with metrics enabled
    - Ubuntu 20.04 LTS+ (ALSA/PulseAudio) with metrics enabled
- [ ] Verify loopback device detection on each platform
- [ ] Test with various audio devices (USB headsets, built-in mics, etc.)
- [ ] **Platform-Specific Metrics:**
    - Measure platform differences in latency, CPU usage, memory footprint
    - A/B compare results across Windows/macOS/Linux
    - Document platform-specific optimizations

**Deliverables:**
- Cross-platform test matrix with metrics (all pass)
- Platform-specific benchmark reports (latency, memory, CPU by OS)
- Known issues documented (if any)
- Comparative analysis: Windows vs. macOS vs. Linux performance

---

## Phase 9: Documentation, Benchmarking Report & Release

### 9.1 Documentation
**Tasks:**
- [ ] **README.md:**
    - Project overview
    - Installation instructions (all platforms)
    - Quick start guide
    - Feature list, including APM and benchmarking capabilities
- [ ] **User Guide:**
    - CLI options reference (including `--enable-metrics`, `--benchmark`)
    - Output format specification (JSON, WebVTT)
    - Metrics output format documentation
    - Troubleshooting guide (common issues)
    - Performance tuning tips based on metrics
- [ ] **Benchmark Report:**
    - Executive summary: Target vs. achieved metrics
    - **Detailed breakdown:**
        - Accuracy: WER by audio type (clean, noisy, multilingual)
        - Latency: P50/P95/P99 end-to-end, per-stage breakdown
        - Resource Usage: CPU %, memory (peak and sustained), RTF
        - Sync Validation: Video synchronization drift measurements
    - Platform comparison: Windows vs. macOS vs. Linux
    - Model comparison: tiny vs. base vs. small accuracy/latency trade-off
    - Recommendations for production deployment
- [ ] **APM & Metrics Documentation:**
    - How to enable and interpret metrics
    - JSON export schema with examples
    - How to integrate with monitoring systems (Prometheus, CloudWatch)
    - Custom metric extension guide
- [ ] **Developer Guide:**
    - Architecture overview (including metrics pipeline)
    - Module descriptions
    - How to add new features (e.g., translation, different STT engines)
    - How to extend `MetricsCollector` for custom metrics
    - Contribution guidelines
- [ ] **API Documentation:**
    - Doxygen comments for all classes/functions
    - Auto-generate HTML docs
    - Include `MetricsCollector` API documentation

**Deliverables:**
- Comprehensive documentation in Markdown and Doxygen
- Production-ready benchmark report (PDF + HTML)
- Metrics documentation with real-world examples
- Hosted on GitHub Pages (optional)

### 9.2 Release Preparation
**Tasks:**
- [ ] **Version Management:**
    - Use semantic versioning (v0.1.0 for initial release)
    - Create `CHANGELOG.md`
- [ ] **Packaging:**
    - Windows: MSVC binary + dependencies (zip or installer)
    - macOS: Universal binary (x86_64 + ARM64) + DMG
    - Linux: AppImage or snap package
    - Or: Pre-built binaries on GitHub Releases
- [ ] **Model Distribution:**
    - Host Whisper/Silero models on GitHub Releases or CDN
    - Provide download utility in app
- [ ] **GitHub Repository Setup:**
    - Initialize public GitHub repo
    - Add CI/CD workflows (GitHub Actions)
    - Create issue/PR templates
    - Add LICENSE (MIT or Apache 2.0 recommended)

**Deliverables:**
- v0.1.0 release with binaries for all platforms
- GitHub Pages documentation live
- Installation instructions working end-to-end

---

## Phase 10: Future Enhancements (Post-Launch)

### 10.1 Short-Term Roadmap (v0.2–v0.3)
- [ ] **Confidence Metrics:**
    - Display `[HIGH/MEDIUM/LOW]` per subtitle segment
    - Option to filter low-confidence results
- [ ] **Translation (optional):**
    - Integrate lightweight translation model (e.g., mBART, M2M-100)
    - Real-time translation to target language
- [ ] **Speaker Diarization (optional):**
    - Identify and label different speakers
    - Output format: `[Speaker 1]: Text...`
- [ ] **Subtitle Synchronization:**
    - Manual resync control
    - Auto-detect and correct drift
- [ ] **Export Formats:**
    - SRT file generation
    - VTT file generation
    - Bulk JSON export

### 10.2 Long-Term Roadmap (v1.0+)
- [ ] **Web Frontend (React/Vue):**
    - Real-time subtitle display in browser
    - WebSocket server for remote streaming
    - Recording and playback
- [ ] **Plugin System:**
    - Swap STT engines (e.g., local Faster-Whisper, cloud APIs)
    - Swap VAD engines
    - Custom post-processing filters
- [ ] **Mobile Apps (iOS/Android):**
    - Integrated real-time subtitles for live audio
    - Use TensorFlow Lite or ONNX Mobile for inference
- [ ] **Performance Improvements:**
    - Quantized/distilled models for lower latency
    - Batch processing optimizations
    - GPU support (CUDA, Metal, Vulkan)

---

## Implemented Improvements & Future Enhancements

### Phase 1: Foundation & Architecture

#### ✅ Implemented
- **CMake project structure** — Cross-platform build with FetchContent dependencies
- **Auto-dependency management** — spdlog, CLI11, FTXUI, PortAudio, Whisper.cpp, ONNX Runtime, SpeexDSP
- **Model auto-download** — CMake downloads models during configure (silero_vad.onnx, ggml-base.bin)
- **Graceful shutdown** — Ctrl+C signal handling with cascading cleanup
- **Windows console handler** — Proper CTRL_C_EVENT handling
- **Error cascading** — Proper exit codes and error logging
- **CLI11 integration** — Full command-line argument parsing (`--device`, `--model`, `--language`, `--threads`, `--gpu`, `--vad-threshold`, `--log-level`, `--verbose`, `--list-devices`, `--version`)
- **Environment variables** — `SUBTITLER_MODEL_PATH`, `SUBTITLER_DEVICE`, `SUBTITLER_LANGUAGE`, `SUBTITLER_THREADS`, `SUBTITLER_USE_GPU`
- **Config file support** — Auto-creates/loads `~/.signeo-core/config.ini` with INI format

#### 🔮 Future Enhancements
- [ ] **GitHub Actions CI/CD** — Automated cross-platform builds

---

### Phase 2: Audio Capture Pipeline

#### ✅ Implemented
- **Device-native audio capture** — Use device's native sample rate/channels instead of forcing 16kHz
- **AudioProcessor class** — Centralized downmix (stereo→mono) + resample (native→16kHz)
- **SpeexDSP resampler** — High-quality resampling with quality level 5
- **Dynamic fallbacks** — Channel count and latency fallbacks for problematic devices

#### 🔮 Future Enhancements
- [ ] **ASIO support** — Lower latency on Windows with ASIO-compatible devices
- [ ] **VB-Audio compatibility** — Better Voicemeeter integration
- [ ] **Loopback capture** — System audio capture (WASAPI loopback, PulseAudio monitor)

---

### Phase 3: Voice Activity Detection

#### ✅ Implemented
- **Silero VAD** — ONNX Runtime-based speech detection (0.97+ confidence)
- **512-sample frames** — 32ms windows at 16kHz
- **State management** — Proper VAD state reset between utterances

#### 🔮 Future Enhancements
- [ ] **Whisper Native VAD** — Use `whisper_vad_*` API (note: batch-only, not streaming)
- [ ] **Hybrid VAD Mode** — Periodically validate Silero decisions with Whisper VAD
- [x] **Adjustable thresholds** — CLI flags for VAD sensitivity (`--vad-threshold`, `--vad-energy-th`)
- [x] **Energy-based pre-filter** — RMS silence detection before VAD to reduce CPU
- [x] **EMA Probability Smoothing** — Reduces jitter with configurable alpha (`--vad-smoothing`)
- [x] **Hangover Mechanism** — Extends speech detection to prevent clipped endings (`--vad-hangover`)
- [x] **Pre-Roll Buffer** — Captures audio before speech trigger (`--vad-preroll`)
- [x] **Adaptive Thresholds** — Auto-adjusts to noise floor (`--vad-adaptive`)

---

### Phase 4: Speech-to-Text Engine

#### ✅ Implemented
- **SttEngine class** — Whisper.cpp wrapper with configurable settings
- **VAD-gated transcription** — Accumulate audio during speech, transcribe on speech-end
- **Auto model download** — CMake downloads `ggml-base.bin` during configure
- **Approach 3 (Single Final Inference)** — No deduplication needed

#### 🔮 Future Enhancements: Sliding Window + Real-Time Preview
```cpp
// Future: Real-time partial transcriptions during long utterances
struct SlidingWindowConfig {
    int step_ms = 2000;      // Process every 2 seconds
    int length_ms = 8000;    // 8 second context window
    int keep_ms = 500;       // 500ms overlap prevents word splits
};
```

- [ ] **Approach 1: Timestamp-Based Dedup** — Use Whisper word-level timestamps
  ```cpp
  for (auto& word : new_words) {
      if (word.t_start >= last_committed_timestamp) {
          output.push_back(word);
          last_committed_timestamp = word.t_end;
      }
  }
  ```
- [ ] **Approach 2: Suffix Matching** — Compare text overlap as fallback
- [ ] **Context continuity** — `prompt_tokens` between windows for coherent transcription
- [ ] **GPU acceleration** — CUDA/cuBLAS for 3-5x faster inference
- [ ] **Distil-Whisper models** — 2-3x faster with similar accuracy
- [ ] **Turbo/Quantized models** — `large-v3-turbo-q5_0` for speed optimization

---

### Phase 5: TUI Subtitle Renderer

#### 🔮 Planned Implementation
- [ ] **SubtitleRenderer class** — FTXUI-based terminal display
- [ ] **Live transcription display** — Real-time text updates
- [ ] **Subtitle history** — Scrolling previous lines (faded)
- [ ] **Status indicators** — VAD probability bar, STT status
- [ ] **Animation effects** — Smooth text appearance

---

### Model Recommendations

| Model | Size | Use Case | RTF (CPU) | RTF (GPU) |
|-------|------|----------|-----------|-----------|
| `ggml-base.bin` | 147MB | Best balance | ~2.5 | ~0.3 |
| `ggml-small.bin` | 466MB | Higher accuracy | ~4.0 | ~0.5 |
| `ggml-large-v3-turbo-q5_0` | ~1GB | Best accuracy | ~6.0 | ~0.8 |
| `distil-large-v3` | ~800MB | Fast + accurate | ~3.0 | ~0.4 |

---

## Development Timeline Summary

| Phase | Key Deliverables |
| :--- | :--- |
| **1. Foundation & Architecture** | Project setup, CMakeLists.txt |
| **2. Audio Capture** | PortAudio integration, device enumeration |
| **3. VAD** | Silero VAD processor, gating logic |
| **4. STT Engine** | Whisper.cpp streaming, model loading |
| **5. CLI Renderer** | Real-time subtitle output, formats |
| **6. Main App & Threading** | Multi-threaded pipeline, CLI |
| **7. APM & Config** | MetricsCollector, CLI options, profiling |
| **8. Testing & Benchmarking** | Unit + integration + performance benchmarks, WER/latency/sync validation |
| **9. Documentation & Release** | README, user guide, benchmark report, v0.1.0 release |
| **Total** | Production-ready MVP with comprehensive metrics |

## Resource Requirements

### Hardware (Development)
- **Laptop/Desktop:** Modern CPU (6+ cores CPU Intel core i7-10750H), 32GB+ RAM, nvidia rtx 2070 super, 1TB SSD M.2.
- **Microphone:** Standard USB and Bluetooth headset or built-in
- **Test Devices:** One machine per platform (Windows, macOS, Linux) recommended

### Software
- **C++ compiler:** MSVC (Windows), Clang (macOS), GCC (Linux)
- **CMake:** 3.20+
- **Git**
- **CI/CD:** GitHub Actions (free)

### External Libraries (Auto-Fetched via CMake)
- **PortAudio (19.7+)** - For audio capture
- **ONNX Runtime (1.15+)** - For ONNX model loading
- **Whisper.cpp (latest)** - For speech-to-text
- **spdlog (1.10+)** - For logging
- **FTXUI (Latest)** - For TUI
- **CLI11 (2.3+)** - For command-line parsing
- **SpeexDSP (1.2.1)** - For audio resampling
- **Silero VAD (6.2.0)** - For voice activity detection
- **Google Test (1.12+, for tests)** - For unit testing

### Models (Downloaded on Demand)
- `ggml-base.bin` (~140MB) or `ggml-small.bin` (~466MB)
- `silero_vad.onnx` (~3MB)

---

## Success Criteria

### ✅ Functional:
- [ ] Captures audio from microphone + loopback on all three platforms
- [ ] Produces real-time subtitles with <500ms latency (p95, best case)
- [ ] 100% local processing, no external API calls
- [ ] Handles continuous audio for 1+ hours without crashes
- [ ] APM/metrics collection with zero impact on latency

### ✅ Quality:
- [ ] Word error rate (WER) <10% on clean English audio
- [ ] WER properly calculated with Levenshtein distance alignment
- [ ] >80% test coverage
- [ ] All unit + integration + benchmark tests pass on CI/CD

### ✅ Accuracy & Latency:
- [ ] **Accuracy (WER by audio type):**
    - Clean English: <8%
    - Noisy/background: <12%
    - Multilingual: <15%
- [ ] **Latency (P95):**
    - TTFB: <100ms
    - Final transcript: <300ms
    - Real-Time Factor (RTF): <0.8
- [ ] **Video Sync:** Subtitle drift <±150ms over 1-hour video

### ✅ Usability:
- [ ] Single executable with sensible defaults
- [ ] Clear CLI help and documentation
- [ ] Works "out of the box" on all platforms
- [ ] Metrics enabled by default, JSON export on demand

### ✅ Performance:
- [ ] CPU usage <50% on modern 4-core CPU
- [ ] Memory usage <500MB sustained
- [ ] Peak memory <800MB
- [ ] Latency benchmarked and documented per stage
- [ ] Platform comparison metrics available

### ✅ Observability:
- [ ] `MetricsCollector` captures timing for all stages
- [ ] JSON metrics export with statistical breakdowns (P50/P95/P99)
- [ ] Benchmark report automatically generated
- [ ] Real-time metrics display in CLI (optional)

---

## Next Steps (Immediate Actions)

1. **Week 1:**
    - [ ] Create GitHub repo and initialize CMake project
    - [ ] Set up CI/CD pipeline (GitHub Actions)
    - [ ] Integrate PortAudio and validate audio capture
    - [ ] Plan `MetricsCollector` architecture (lightweight vs. OpenTelemetry)
2. **Week 2:**
    - [ ] Implement ringbuffer and audio preprocessing
    - [ ] Integrate Silero VAD
    - [ ] Add basic logging and `MetricsCollector` stub
3. **Week 3–4:**
    - [ ] Integrate Whisper.cpp
    - [ ] Implement streaming transcription logic
    - [ ] Create CLI output module
    - [ ] Begin instrumenting stages with `MetricsCollector`
4. **Week 5–6:**
    - [ ] Multi-threaded architecture with metrics integration
    - [ ] Full per-stage timing instrumentation
    - [ ] Memory/CPU profiling setup
    - [ ] CLI flags for metrics (`--enable-metrics`, `--metrics-output`)
5. **Week 7:**
    - [ ] Build benchmark framework with WER calculator
    - [ ] Create test audio fixtures and reference transcripts
    - [ ] Video sync testing infrastructure (frame-by-frame comparison)
    - [ ] Automated benchmark harness
6. **Week 8:**
    - [ ] Integration testing with metrics validation
    - [ ] Cross-platform benchmarking runs
    - [ ] Generate benchmark reports
    - [ ] Analyze and document results
7. **Week 9+:**
    - [ ] Documentation (README, user guide, APM docs, benchmark report)
    - [ ] Final optimization pass based on metrics
    - [ ] Release v0.1.0 with comprehensive benchmarking data

---

## APM & Benchmarking Best Practices

### Metrics Collection Strategy
**Lightweight vs. Comprehensive:**
- **Option A (Lightweight):** Custom `MetricsCollector` class with thread-safe queues and simple aggregation. Minimal overhead, easy to extend. **Recommended for MVP.**
- **Option B (Enterprise):** OpenTelemetry C++ client for production-grade distributed tracing, metrics export to Prometheus/CloudWatch. Add in v0.2+.

**Recommended Path:** Start with lightweight custom `MetricsCollector` (Phase 7.1), integrate OpenTelemetry in v0.2 if needed for cloud deployments.

### Performance Benchmarking Best Practices
**WER Calculation:**
- Use dynamic programming (Levenshtein distance) for word-level alignment
- Report: WER %, and breakdown (substitutions, deletions, insertions)
- Test on diverse audio: clean, noisy, accented, multilingual
- Reference: `jiwer` library (Python) or custom C++ implementation
- Document human baseline (typically 4–5% WER)

**Latency Measurement:**
- Measure at multiple milestones: capture start → TTFB → TTFC → final locked
- Report P50/P95/P99, not just mean (tail matters for UX)
- Real-Time Factor (RTF): `processing_time / audio_duration` (target <1.0)
- Endpointing latency: actual end-of-speech → VAD decision time
- Test with realistic audio frame sizes (20–100ms)

**Video Synchronization:**
- Use test videos with burned-in frame timecodes
- Measure frame-by-frame: when subtitle appears vs. when speaker says words
- Acceptable drift: ±50–100ms (human perceptual threshold)
- Monitor drift over time: should remain stable, not accumulate

**Resource Profiling:**
- **CPU:** Track per-thread, identify STT as primary bottleneck
- **Memory:** Separate peak (burst during processing) vs. sustained (idle state)
- **Throughput:** Words per second, tokens per second for STT model

---

## Benchmarking Harness Example Output

```json
{
  "timestamp": "2025-12-28T19:00:00Z",
  "system_info": {
    "os": "Ubuntu 22.04",
    "cpu": "Intel i7-8700K",
    "ram_gb": 16
  },
  "test_cases": [
    {
      "name": "clean_english_1min",
      "audio_file": "test_audio/clean_english.wav",
      "duration_sec": 60,
      "wer": 0.062,
      "wer_details": {
        "substitutions": 3,
        "deletions": 2,
        "insertions": 1,
        "total_words": 95
      },
      "latency_ms": {
        "p50": 145,
        "p95": 287,
        "p99": 420,
        "mean": 180
      },
      "latency_breakdown": {
        "vad_processing_ms": 5,
        "stt_inference_ms": 250,
        "cli_render_ms": 2
      },
      "rtf": 0.65,
      "sync_drift_ms": 45,
      "resource_usage": {
        "cpu_avg_percent": 42,
        "memory_peak_mb": 256,
        "memory_sustained_mb": 180
      }
    }
  ],
  "summary": {
    "avg_wer": 0.085,
    "avg_latency_p95_ms": 310,
    "avg_rtf": 0.68,
    "max_memory_mb": 512
  }
}
```

---

## Contact & Support
For questions during implementation:
- **Metrics & APM:** OpenTelemetry C++ docs, Prometheus client libraries
- **WER Calculation:** `jiwer` library (Python), or Levenshtein distance algorithms
- **Latency Measurement:** Speech-to-text latency best practices (see Gladia, Deepgram benchmarking guides)
- **Library Documentation:** PortAudio, Whisper.cpp, ONNX Runtime
- **Community:** Reddit r/LocalLLaMA, Whisper discussions, OpenTelemetry community

---

## Key Highlights Summary

### ✅ 10 Phased Approach
- **Phase 1:** CMake project setup & dependency integration
- **Phase 2–3:** Audio capture (PortAudio) + VAD (Silero)
- **Phase 4–5:** STT engine (Whisper.cpp) + CLI renderer
- **Phase 6:** Main application & multi-threaded pipeline
- **Phase 7 (NEW):** Application Performance Monitoring (APM) & metrics infrastructure
- **Phase 8 (EXPANDED):** Testing, performance benchmarking, WER/latency/sync validation
- **Phase 9:** Documentation, comprehensive benchmark report & release

### ✅ Specific Deliverables
- Code structure and class signatures
- Testing strategies (unit + integration + performance)
- Per-stage performance metrics to measure (latency, resource usage, throughput)
- WER calculator with Levenshtein distance alignment
- Video synchronization drift validation
- Automated benchmark harness with JSON report generation

### ✅ Timeline Breakdown
- Each phase has realistic duration estimates
- Clear success criteria including accuracy targets (WER <10%) and latency SLOs (P95 <300ms)
- Performance benchmarking and metrics collection strategies
- Next immediate actions

### ✅ Architecture Specifics
- Multi-threaded pipeline design with metrics integration
- Thread-safe communication patterns
- Memory management considerations
- `MetricsCollector` class for lightweight APM (latency, memory, CPU, throughput)
- Per-stage instrumentation (audio capture, VAD, STT, rendering)

### ✅ Cross-Platform Support
- Windows (WASAPI), macOS (Core Audio), Linux (ALSA/PulseAudio)
- Loopback device detection per platform
- Platform-specific benchmark comparisons

### ✅ Production-Grade Observability
- Real-time metrics display (CPU, memory, latency in CLI)
- JSON/CSV export for metrics and benchmark results
- HTML/Markdown benchmark report generator
- **Target metrics:** WER breakdown, TTFB/TTFC/final latency, RTF, sync drift, resource usage

### ✅ Comprehensive Performance Testing
- Word Error Rate (WER) testing on clean, noisy, and multilingual audio
- **Latency benchmarks:** TTFB, TTFC, final latency with P50/P95/P99 percentiles
- **Video sync validation:** Frame-by-frame drift measurement (target ±100ms)
- **Real-Time Factor (RTF):** Processing time vs. audio duration (target <0.8)
- **Resource profiling:** CPU, peak memory, sustained memory over 1+ hour runs
- Automated benchmark suite with 5–10 test fixtures and reference transcripts

### ✅ Future Roadmap
- **Short-term (v0.2–v0.3):** Confidence metrics, translation, diarization, export formats
- **Long-term (v1.0+):** Web frontend, plugin system, mobile apps, quantized models
- **Optional:** OpenTelemetry integration for cloud deployments (Phase 2)

> [!TIP]
> This document is production-ready with comprehensive APM and benchmarking infrastructure. You can use it as your actual project roadmap. Start with Phase 1 this week, and you'll have a working, fully-benchmarked MVP.

