# Real-Time Audio-to-Subtitles

A production-grade, cross-platform C++ application for real-time speech-to-text transcription. Captures audio from microphone or system loopback, processes it locally using advanced Voice Activity Detection (Silero VAD) and Speech-to-Text (Whisper), and displays subtitles with a rich Terminal UI.

## ✨ Features

- **Real-Time Transcription**: Low-latency sliding window transcription using `whisper.cpp`
- **Advanced VAD**: Neural Voice Activity Detection (Silero) with 6-stage pipeline
- **Rich TUI**: Beautiful terminal interface built with FTXUI (audio gauges, VAD metrics, CPU usage)
- **System Audio Capture**: WASAPI Loopback for transcribing meetings/videos
- **Hot-Swappable Devices**: Switch audio input devices at runtime
- **Fully Configurable**: 25+ options via CLI, environment variables, and config file
- **Production-Grade**: Multi-threaded architecture, robust error handling, structured logging

## 🚀 Quick Start

### Prerequisites

- **CMake** 3.20+
- **C++ Compiler** (MSVC 19.30+, GCC 11+, Clang 14+) - C++23 required

### Build & Run

```powershell
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run with TUI
.\build\Release\realtime-subtitler.exe --ui
```

### TUI Controls

| Key | Action |
|-----|--------|
| `←` / `→` | Switch between tabs (Logs, Devices, Audio, VAD, STT, System, Help) |
| `↑` / `↓` | Navigate within tab |
| `Enter` | Select/Confirm |
| `Escape` | Exit application |

---

## ⚙️ Configuration

Configuration priority: **CLI > Environment > Config File > Defaults**

### Quick Reference

| Flag | Description | Default |
|------|-------------|---------|
| `--ui` | Enable Terminal UI | false |
| `--device <N>` | Audio device index | -1 (default) |
| `--model <PATH>` | Whisper model path | `models/ggml-base.bin` |
| `--language <CODE>` | Language (en, es, fr, etc.) | `en` |
| `--threads <N>` | CPU threads | 4 |
| `--vad-threshold <F>` | Speech sensitivity (0.0-1.0) | 0.5 |
| `--log-level <LEVEL>` | trace/debug/info/warn/error | `info` |
| `--list-devices` | List audio devices and exit | - |
| `--help` | Show all options | - |

### Environment Variables

All options available via `SUBTITLER_*` prefix:

```powershell
$env:SUBTITLER_LANGUAGE = "es"
$env:SUBTITLER_VAD_THRESHOLD = "0.6"
$env:SUBTITLER_THREADS = "8"
```

### Config File (`config.ini`)

```ini
device = -1
model = models/ggml-base.bin
language = en
n_threads = 4
use_gpu = true
vad_threshold = 0.5
log_level = info
```

> See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for complete 25+ configuration options.

---

## 🏗️ Architecture

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Audio     │───▶│     VAD     │───▶│     STT     │───▶│    TUI      │
│   Capture   │    │   Thread    │    │   Thread    │    │   Render    │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
```

| Thread | Technology | Role |
|--------|------------|------|
| **Audio** | PortAudio, SpeexDSP | Capture, resample |
| **VAD** | ONNX Runtime, Silero | Speech detection |
| **STT** | whisper.cpp | Transcription |
| **Main** | FTXUI | UI rendering |

---

## 🧪 Testing

```powershell
# Unit tests (80+ tests)
.\build\unit_tests.exe

# Integration tests
.\build\integration_tests.exe

# Benchmarks
.\build\benchmarks.exe
```

| Suite | Tests | Coverage |
|-------|-------|----------|
| **Unit** | 80+ | RingBuffer, Queue, Config, Logging, Metrics, WER |
| **Integration** | 7 | Pipeline, Config, Device hot-swap |
| **Benchmarks** | 5 | RTF, Throughput, Memory, Contention, WER |

---

## 📖 Documentation

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Complete system design, all CLI/ENV options, module reference |
| [DEPLOYMENT.md](docs/DEPLOYMENT.md) | Installation guide, troubleshooting |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Development setup, coding standards |

---

## 📜 License

MIT License. See [LICENSE](LICENSE) for details.
