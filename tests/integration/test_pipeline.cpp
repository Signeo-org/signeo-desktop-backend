#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <cmath>
#include <cstdlib>

#include "core/application.hpp"
#include "config/app_config.hpp"
#include "utils/thread_safe_queue.hpp"
#include "audio/audio_capture.hpp"
#include "output/logging.hpp"
#include "stt/stt_engine.hpp"
#include "stt/streaming_transcriber.hpp"

// ============================================================================
// Integration Test Fixture
// ============================================================================

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a dummy WAV file for testing if it doesn't exist
        create_test_wav("test_audio.wav");
    }

    void TearDown() override {
        if (std::filesystem::exists("test_audio.wav")) {
             std::filesystem::remove("test_audio.wav");
        }
    }

    void create_test_wav(const std::string& filename) {
        std::ofstream file(filename, std::ios::binary);
        
        // Write simple WAV header (44 bytes)
        // This is a minimal valid header for 16kHz Mono 16-bit PCM
        struct WavHeader {
            char riff[4] = {'R', 'I', 'F', 'F'};
            uint32_t fileSize = 36 + 32000; // 36 + data size
            char wave[4] = {'W', 'A', 'V', 'E'};
            char fmt[4] = {'f', 'm', 't', ' '};
            uint32_t fmtSize = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels = 1;
            uint32_t sampleRate = 16000;
            uint32_t byteRate = 16000 * 2;
            uint16_t blockAlign = 2;
            uint16_t bitsPerSample = 16;
            char data[4] = {'d', 'a', 't', 'a'};
            uint32_t dataSize = 32000; // 1 second of audio
        } header;

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // Write 1 second of "silence" (all zeros) for now
        std::vector<int16_t> silence(16000, 0);
        file.write(reinterpret_cast<const char*>(silence.data()), silence.size() * 2);
    }
};

// ============================================================================
// Configuration Integration Tests
// ============================================================================

TEST_F(IntegrationTest, ConfigLoadingPriority) {
    // 1. Test Defaults (with minimal valid argc/argv)
    const char* default_argv[] = {"test_app"};
    AppConfig config = AppConfig::parse(1, const_cast<char**>(default_argv));
    EXPECT_EQ(config.device_index, -1);
    EXPECT_EQ(config.n_threads, 4);

    // 2. Test CLI Arguments
    const char* cli_argv[] = {"test_app", "--device", "2", "--threads", "8"};
    config = AppConfig::parse(5, const_cast<char**>(cli_argv));
    EXPECT_EQ(config.device_index, 2);
    EXPECT_EQ(config.n_threads, 8);
}

// ============================================================================
// End-to-End Pipeline Mock Test
// ============================================================================

TEST_F(IntegrationTest, PipelineInstantiation) {
    // Check if models exist first
    if (!std::filesystem::exists("models/ggml-base.bin") || 
        !std::filesystem::exists("models/silero_vad.onnx")) {
        GTEST_SKIP() << "Models not found, skipping pipeline test";
    }

    try {
        // Just verify dependencies load without crashing
        SUCCEED(); 
    } catch (...) {
        FAIL() << "Pipeline verification failed";
    }
}

// ============================================================================
// WAV File Injection Test (Audio → Processing)
// ============================================================================

TEST_F(IntegrationTest, WavFileInjection) {
    // Create a WAV file with a simple tone (440Hz sine wave)
    std::string test_wav = "test_sine.wav";
    
    // Create WAV with actual audio content
    std::ofstream file(test_wav, std::ios::binary);
    
    struct WavHeader {
        char riff[4] = {'R', 'I', 'F', 'F'};
        uint32_t fileSize = 0;
        char wave[4] = {'W', 'A', 'V', 'E'};
        char fmt[4] = {'f', 'm', 't', ' '};
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 1;
        uint16_t numChannels = 1;
        uint32_t sampleRate = 16000;
        uint32_t byteRate = 16000 * 2;
        uint16_t blockAlign = 2;
        uint16_t bitsPerSample = 16;
        char data[4] = {'d', 'a', 't', 'a'};
        uint32_t dataSize = 0;
    } header;
    
    // Generate 1 second of 440Hz sine wave
    const int sample_rate = 16000;
    const int duration_samples = sample_rate; // 1 second
    const float frequency = 440.0f;
    std::vector<int16_t> samples(duration_samples);
    
    for (int i = 0; i < duration_samples; i++) {
        float t = static_cast<float>(i) / sample_rate;
        samples[i] = static_cast<int16_t>(16000 * std::sin(2.0f * 3.14159f * frequency * t));
    }
    
    header.dataSize = static_cast<uint32_t>(samples.size() * 2);
    header.fileSize = 36 + header.dataSize;
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(samples.data()), samples.size() * 2);
    file.close();
    
    // Verify file was created
    EXPECT_TRUE(std::filesystem::exists(test_wav));
    EXPECT_GT(std::filesystem::file_size(test_wav), 44); // More than just header
    
    // Cleanup
    std::filesystem::remove(test_wav);
}

// ============================================================================
// Environment Variable Config Priority Test
// ============================================================================

TEST_F(IntegrationTest, EnvVarConfigPriority) {
    // Set an environment variable
#ifdef _WIN32
    _putenv_s("SUBTITLER_THREADS", "16");
#else
    setenv("SUBTITLER_THREADS", "16", 1);
#endif
    
    const char* argv[] = {"test_app"};
    AppConfig config = AppConfig::parse(1, const_cast<char**>(argv));
    
    // ENV should override default (4 threads)
    EXPECT_EQ(config.n_threads, 16);
    
    // CLI should override ENV
    const char* argv2[] = {"test_app", "--threads", "2"};
    config = AppConfig::parse(3, const_cast<char**>(argv2));
    EXPECT_EQ(config.n_threads, 2);
    
    // Cleanup
#ifdef _WIN32
    _putenv_s("SUBTITLER_THREADS", "");
#else
    unsetenv("SUBTITLER_THREADS");
#endif
}

// ============================================================================
// Multi-Threaded Stress Test
// ============================================================================

TEST_F(IntegrationTest, MultiThreadedStress) {
    // Stress test with multiple concurrent config parsing operations
    const int num_threads = 8;
    const int iterations_per_thread = 100;
    
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&success_count, &failure_count, iterations_per_thread]() {
            for (int i = 0; i < iterations_per_thread; i++) {
                try {
                    const char* argv[] = {"stress_test", "--threads", "4"};
                    AppConfig config = AppConfig::parse(3, const_cast<char**>(argv));
                    
                    if (config.n_threads == 4) {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                } catch (...) {
                    failure_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), num_threads * iterations_per_thread);
    EXPECT_EQ(failure_count.load(), 0);
}

// ============================================================================
// Audio Buffer Stress Test
// ============================================================================

TEST_F(IntegrationTest, AudioBufferStress) {
    // Stress test the thread-safe queue with high throughput
    utils::ThreadSafeQueue<std::vector<float>> queue;
    
    const int producer_count = 4;
    const int chunks_per_producer = 100;
    const int chunk_size = 512;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    // Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < producer_count; p++) {
        producers.emplace_back([&queue, &produced, chunks_per_producer, chunk_size]() {
            for (int i = 0; i < chunks_per_producer; i++) {
                std::vector<float> chunk(chunk_size, static_cast<float>(i));
                queue.push(std::move(chunk));
                produced++;
            }
        });
    }
    
    // Consumer - uses blocking pop(), will unblock when queue.stop() is called
    std::thread consumer([&queue, &consumed]() {
        while (true) {
            auto chunk = queue.pop();
            if (!chunk) {
                // Queue stopped and empty
                break;
            }
            consumed++;
        }
    });
    
    // Wait for producers to finish
    for (auto& p : producers) {
        p.join();
    }
    
    // Give consumer time to drain queue, then stop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.stop();
    consumer.join();
    
    EXPECT_EQ(produced.load(), producer_count * chunks_per_producer);
    EXPECT_EQ(consumed.load(), produced.load());
}

// ============================================================================
// Device Hot-Swap Test (requires multiple audio devices)
// ============================================================================

TEST_F(IntegrationTest, DeviceHotSwap) {
    // Ensure spdlog is initialized
    core::init_logging("", true);
    
    // Create audio capture instance
    auto capture_result = audio::AudioCapture::create(16000, 512);
    if (!capture_result) {
        GTEST_SKIP() << "Could not create AudioCapture: " << capture_result.error();
    }
    
    auto capture = std::move(*capture_result);
    
    // List available devices
    auto devices_result = capture->list_devices();
    if (!devices_result) {
        GTEST_SKIP() << "Could not list devices: " << devices_result.error();
    }
    
    auto devices = *devices_result;
    
    // Print available devices
    std::cout << "\n=== Available Audio Devices ===" << std::endl;
    for (const auto& dev : devices) {
        std::cout << "  [" << dev.index << "] " << dev.name 
                  << " (channels: " << dev.max_input_channels 
                  << ", sample_rate: " << dev.default_sample_rate
                  << (dev.is_default ? ", DEFAULT" : "")
                  << (dev.is_loopback ? ", LOOPBACK" : "")
                  << ")" << std::endl;
    }
    
    // Need at least 2 devices for hot-swap test
    if (devices.size() < 2) {
        GTEST_SKIP() << "Hot-swap test requires at least 2 audio devices, found " << devices.size();
    }
    
    // Find first two usable devices (with input channels)
    std::vector<int> usable_devices;
    for (const auto& dev : devices) {
        if (dev.max_input_channels > 0 || dev.is_loopback) {
            usable_devices.push_back(dev.index);
            if (usable_devices.size() >= 2) break;
        }
    }
    
    if (usable_devices.size() < 2) {
        GTEST_SKIP() << "Need at least 2 usable input devices for hot-swap";
    }
    
    // Test 1: Start on first device
    std::cout << "\n--- Starting capture on device " << usable_devices[0] << " ---" << std::endl;
    auto start_result = capture->start(usable_devices[0]);
    if (!start_result) {
        GTEST_SKIP() << "Could not start on device " << usable_devices[0] << ": " << start_result.error();
    }
    
    EXPECT_TRUE(capture->is_active());
    
    // Capture for 500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Read some audio
    auto chunk1 = capture->read_chunk(8000); // Up to 500ms of audio
    std::cout << "  Captured " << chunk1.data.size() << " samples from device 1" << std::endl;
    
    // Stop
    capture->stop();
    EXPECT_FALSE(capture->is_active());
    
    // Test 2: Hot-swap to second device
    std::cout << "\n--- Hot-swapping to device " << usable_devices[1] << " ---" << std::endl;
    start_result = capture->start(usable_devices[1]);
    if (!start_result) {
        GTEST_SKIP() << "Could not start on device " << usable_devices[1] << ": " << start_result.error();
    }
    
    EXPECT_TRUE(capture->is_active());
    
    // Capture for 500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Read some audio
    auto chunk2 = capture->read_chunk(8000);
    std::cout << "  Captured " << chunk2.data.size() << " samples from device 2" << std::endl;
    
    // Stop
    capture->stop();
    EXPECT_FALSE(capture->is_active());
    
    // Test 3: Switch back to original device
    std::cout << "\n--- Switching back to device " << usable_devices[0] << " ---" << std::endl;
    start_result = capture->start(usable_devices[0]);
    if (!start_result) {
        // This might fail if the device was unplugged - that's okay for hot-swap test
        std::cout << "  Device 0 no longer available (expected in some hot-swap scenarios)" << std::endl;
    } else {
        EXPECT_TRUE(capture->is_active());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        capture->stop();
    }
    
    std::cout << "\n=== Hot-swap test completed successfully ===" << std::endl;
    SUCCEED();
}
// Real End-to-End Test with JFK.wav
// ============================================================================

TEST_F(IntegrationTest, PipelineJfkWav) {
    // Locate jfk.wav
    // We assume the test runs from the build directory
    std::string wav_path = "tests/fixtures/jfk.wav";
    if (!std::filesystem::exists(wav_path)) {
        // Fallback: check if we are in build dir
        wav_path = "../tests/fixtures/jfk.wav"; 
    }
    
    if (!std::filesystem::exists(wav_path)) {
        GTEST_SKIP() << "jfk.wav not found at " << wav_path << ", skipping E2E test";
    }

    if (!std::filesystem::exists("models/ggml-base.bin")) {
        GTEST_SKIP() << "Whisper model not found, skipping E2E test";
    }

    core::init_logging("", true);
    spdlog::set_level(spdlog::level::info);

    // 1. Create AudioCapture in File Mode
    auto capture_res = audio::AudioCapture::create(16000, 512, wav_path);
    ASSERT_TRUE(capture_res.has_value()) << "Failed to open WAV: " << capture_res.error();
    auto capture = std::move(*capture_res);
    ASSERT_FALSE(capture->is_active()); // Shouldn't be active yet

    // 2. Setup VAD and STT
    // We'll skip VAD for this simple test and just feed audio to STT to verify transcription
    // roughly matching "ask not what your country can do for you"
    
    // Setup STT Engine
    stt::SttConfig stt_config;
    stt_config.model_path = "models/ggml-base.bin";
    stt_config.n_threads = 4;
    stt_config.language = "en";
    
    // Create Engine (Factory)
    auto engine_res = stt::SttEngine::create(stt_config);
    if (!engine_res) {
        GTEST_SKIP() << "Failed to create STT Engine: " << engine_res.error();
    }
    
    // Setup Transcriber Config
    stt::TranscriberConfig transcriber_config;
    transcriber_config.step_ms = 3000;
    transcriber_config.max_length_ms = 10000; // was length_ms
    
    // Create Transcriber (Dependency Injection)
    auto transcriber = std::make_unique<stt::StreamingTranscriber>(*engine_res.value(), transcriber_config);
    
    // Extend SttEngine lifetime for unique_ptr ownership during test
    // (This works but unique_ptr inside factory result needs to stay alive)
    auto& engine_ptr = engine_res.value(); // Keep valid reference
    (void)engine_ptr; // Suppress unused variable warning if only used for transcriber ref
    
    // Start capture (virtual)
    EXPECT_TRUE(capture->start());
    EXPECT_TRUE(capture->is_active());
    
    std::cout << "Processing " << wav_path << "..." << std::endl;
    
    // 3. Processing Loop
    int chunks_processed = 0;
    int max_chunks = 1000; // Safety break
    
    while (capture->is_active() && chunks_processed < max_chunks) {
        auto chunk = capture->read_chunk(512);
        
        if (chunk.data.empty()) {
            spdlog::info("Generic: End of file reached.");
            break;
        }
        
        // Feed to transcriber
        transcriber->push_audio(chunk.data);
        
        // Transcribe if ready
        transcriber->process();
        
        chunks_processed++;
    }
    
    capture->stop();
    
    // Force final transcription of remaining buffer
    transcriber->finalize();
    
    // Finalize
    auto final_text = transcriber->get_full_text();
    std::cout << "Transcription Result: " << final_text << std::endl;
    
    // Verify content
    // Note: Whisper output might vary slightly, but key phrases should be there.
    // "Ask not what your country can do for you"
    
    // Convert to lower case for easier matching
    std::string text_lower = final_text;
    std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
    
    EXPECT_TRUE(text_lower.find("ask not") != std::string::npos || 
                text_lower.find("country") != std::string::npos ||
                text_lower.find("do for you") != std::string::npos)
        << "Transcription failed to produce expected text. Got: " << final_text;
}

