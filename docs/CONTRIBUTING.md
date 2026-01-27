# Contributing to Real-Time Audio-to-Subtitles

Thank you for your interest in contributing! This project aims to provide a high-performance, real-time transcription tool for everyone.

## 🛠️ Development Setup

1. **Prerequisites**:
   - CMake 3.20+
   - C++23 Compiler (MSVC 19.30+, GCC 13+, Clang 14+)
   - Python 3 (optional, for scripts)

2. **Clone & Configure**:
   ```bash
   git clone --recursive https://github.com/your-repo/signeo-core.git
   cd signeo-core
   cmake -B build
   ```

3. **Build & Run**:
   ```bash
   cmake --build build
   ./build/Debug/signeo-core --ui
   ```

## 🧪 Testing

We use **Google Test**. Ensure all tests pass before submitting a PR.

```bash
# Unit tests
./build/Debug/unit_tests

# Integration tests
./build/Debug/integration_tests

# Benchmarks
./build/Debug/benchmarks
```

### Test Coverage

Generate an HTML coverage report:

```powershell
# Windows
.\coverage.ps1

# View report
start coverage\report\index.html
```

### Test Categories

| Suite | Focus Area |
|-------|------------|
| `unit_tests` | Individual components (RingBuffer, Queue, Config, Logging) |
| `integration_tests` | Full pipeline, device hot-swap, stress tests |
| `benchmarks` | RTF, throughput, memory profiling, thread contention |

## 📐 Coding Style

- **Standard**: C++23
- **Formatting**: Clang-Format (Google style)
- **Naming**: `snake_case` for variables/functions, `PascalCase` for classes.
- **Headers**: `#pragma once` guard.
- **Safety**: Prefer `std::unique_ptr` over raw pointers. Always use RAII for resources (PortAudio, etc.).
- **Config**: Use `AppConfig` for tunable parameters, avoid magic numbers.
- **Architecture**: Keep configs decoupled from logic classes. Prefer namespace-level structs for DTOs.

## 📁 Project Structure

- `src/core`: Application lifecycle and orchestration (Application, logging)
- `src/audio`: Audio capture (PortAudio) and processing (resampling, ringbuffer)
- `src/vad`: Voice Activity Detection (Silero ONNX)
- `src/stt`: Speech-to-Text (Whisper.cpp)
- `src/ui`: Terminal UI (FTXUI)
- `tests`: Unit, Integration, and Benchmark suites

## 📝 Pull Requests

1. Fork the repo and create your branch from `main`.
2. Ensure you have added tests for any new features.
3. Update documentation if behavior changes.
4. Verify code coverage (`./coverage.ps1` on Windows).

Happy coding!
