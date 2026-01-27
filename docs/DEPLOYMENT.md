# Deployment Guide

This guide covers how to deploy and configure the Real-Time Audio-to-Subtitles application.

## 🖥️ System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Windows 10/11, Linux, macOS | Windows 11 |
| **CPU** | 4 cores | 8+ cores |
| **RAM** | 4 GB | 8+ GB |
| **Disk** | 500 MB | 1+ GB |
| **Audio** | Any microphone | USB or loopback device |

### Dependencies (automatically bundled in release)

- ONNX Runtime 1.15+ (Silero VAD inference)
- whisper.cpp (speech-to-text)
- PortAudio (audio capture)
- SpeexDSP (resampling)

## 📦 Installation

### Option 1: Pre-built Release (Recommended)

1. Download the latest release from the [Releases](https://github.com/your-repo/releases) page.
2. Extract the ZIP to your preferred location.
3. Run `signeo-core.exe --ui`

### Option 2: Build from Source

```powershell
# Clone the repository
git clone --recursive https://github.com/your-repo/signeo-core.git
cd signeo-core

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run
.\build\Release\signeo-core.exe --ui
```

## ⚙️ Configuration

Configuration is loaded in priority order:
1. **CLI Arguments** (highest priority)
2. **Environment Variables**
3. **Config File** (`config.ini`)
4. **Defaults** (lowest priority)

### Configuration File (`config.ini`)

Create a `config.ini` in the working directory:

```ini
# Audio Settings
device = -1              # -1 = default device, or index from --list-devices
sample_rate = 16000      # Internal processing sample rate

# STT Settings
model = models/ggml-base.bin   # Whisper model path
language = en                   # ISO 639-1 language code
n_threads = 4                   # CPU threads for inference
use_gpu = true                  # Enable GPU acceleration if available

# VAD Settings
vad_model = models/silero_vad.onnx  # VAD model path
vad_threshold = 0.5                  # Speech detection threshold (0.0-1.0)
vad_min_silence_ms = 300             # Minimum silence for segment split
vad_speech_pad_ms = 30               # Padding around speech segments

# Logging
log_level = info         # trace, debug, info, warn, error
log_file = subtitler.log # Optional log file path
```

### Environment Variables

All settings can be set via environment variables with the `SUBTITLER_` prefix:

| Variable | Description | Default |
|----------|-------------|---------|
| `SUBTITLER_DEVICE` | Audio device index (-1 for default) | -1 |
| `SUBTITLER_MODEL` | Path to Whisper model | `models/ggml-base.bin` |
| `SUBTITLER_LANGUAGE` | Language code (en, es, fr, de, etc.) | `en` |
| `SUBTITLER_THREADS` | Number of CPU threads | 4 |
| `SUBTITLER_USE_GPU` | Enable GPU acceleration | true |
| `SUBTITLER_VAD_MODEL` | Path to VAD ONNX model | `models/silero_vad.onnx` |
| `SUBTITLER_VAD_THRESHOLD` | VAD sensitivity (0.0-1.0) | 0.5 |
| `SUBTITLER_LOG_LEVEL` | Logging verbosity | info |

### CLI Arguments

```powershell
.\signeo-core.exe --help

Usage: signeo-core [OPTIONS]

Options:
  --ui                  Enable Terminal UI (required for interactive use)
  --list-devices        List available audio devices and exit
  --device <INDEX>      Audio device index (-1 for default)
  --model <PATH>        Path to Whisper model file
  --language <CODE>     Language code (en, es, fr, de, zh, ja, etc.)
  --threads <N>         Number of CPU threads for inference
  --vad-threshold <F>   VAD speech detection threshold (0.0-1.0)
  --log-level <LEVEL>   Log level: trace, debug, info, warn, error
  -h, --help            Show this help message
```

## 🎤 Audio Devices

### List Available Devices

```powershell
.\signeo-core.exe --list-devices
```

Example output:
```
[0] Microphone (Realtek Audio) - Default
[1] Stereo Mix (Realtek Audio) - Loopback
[2] USB Microphone
```

### Using System Audio (Loopback)

To transcribe audio from your speakers (e.g., Zoom, YouTube, etc.):

```powershell
# Use the loopback device index from --list-devices
.\signeo-core.exe --ui --device 1
```

## 📁 Required Files

### Directory Structure
Ensure your deployment folder looks like this:
```
Signeo-Backend/
├── signeo-core.exe  # Main Application
├── onnxruntime.dll         # Required Runtime
├── config.ini              # Configuration
├── models/                 # Models Directory
│   ├── ggml-base.bin
│   └── silero_vad.onnx
└── README.md
```

### Downloading Models

**Whisper Models:**
```powershell
# Download from Hugging Face
Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin" -OutFile "models/ggml-base.bin"
```

**VAD Model:**
```powershell
# Silero VAD v4
Invoke-WebRequest -Uri "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx" -OutFile "models/silero_vad.onnx"
```

## 🔧 Troubleshooting

### "Model file not found"

Ensure the model paths in `config.ini` or CLI arguments point to valid files:
```powershell
Test-Path "models/ggml-base.bin"  # Should return True
```

### "Audio device not found"

Run `--list-devices` and use a valid device index:
```powershell
.\signeo-core.exe --list-devices
```

### High CPU Usage

- Reduce threads: `--threads 2`
- Use a smaller model: `ggml-tiny.bin` instead of `ggml-base.bin`
- Increase VAD threshold: `--vad-threshold 0.7`

### No Audio Detected

- Check microphone permissions in Windows Settings
- Verify the device is not muted
- Lower VAD threshold: `--vad-threshold 0.3`

## 🧪 Verification

Run the included tests to verify your installation:

```powershell
# Unit tests
.\unit_tests.exe

# Integration tests
.\integration_tests.exe

# Benchmarks
.\benchmarks.exe
```

Expected results:
- Unit tests: 79/79 passed
- Integration tests: 7/7 passed
- RTF < 0.1 (100x+ realtime)

## 📜 License

MIT License. See [LICENSE](LICENSE) for details.
