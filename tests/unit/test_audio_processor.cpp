#include <gtest/gtest.h>
#include "audio/audio_processor.hpp"
#include <vector>
#include <cmath>
#include <numeric>

// ============================================================================
// Public API Tests (process)
// ============================================================================

TEST(AudioProcessorTest, ProcessStereoToMonoResample) {
    // Input: 48kHz Stereo -> Output: 16kHz Mono
    auto result = audio::AudioProcessor::create(48000, 2, 16000);
    ASSERT_TRUE(result.has_value());
    auto& processor = *result.value();
    
    // Create 0.1s of stereo @ 48kHz (4800 frames * 2 channels = 9600 samples)
    std::vector<float> stereo_input(9600);
    for (size_t i = 0; i < stereo_input.size(); i += 2) {
        stereo_input[i] = 1.0f;      // Left
        stereo_input[i+1] = 1.0f;    // Right
    }
    
    // Process
    auto output = processor.process(stereo_input);
    
    // Expected output: ~0.1s @ 16kHz (1600 samples)
    EXPECT_NEAR(output.size(), 1600u, 50);
    
    // Check value (average of 1.0 and 1.0 is 1.0)
    // Resampling might introduce slight artifacts, but should be close
    float sum = std::accumulate(output.begin(), output.end(), 0.0f);
    float avg = sum / output.size();
    EXPECT_NEAR(avg, 1.0f, 0.05f);
}

TEST(AudioProcessorTest, ProcessMonoResampleOnly) {
    // Input: 48kHz Mono -> Output: 16kHz Mono
    auto result = audio::AudioProcessor::create(48000, 1, 16000);
    ASSERT_TRUE(result.has_value());
    auto& processor = *result.value();
    
    std::vector<float> mono_input(4800, 1.0f); // 0.1s
    
    auto output = processor.process(mono_input);
    
    EXPECT_NEAR(output.size(), 1600u, 20);
    
    float sum = std::accumulate(output.begin(), output.end(), 0.0f);
    float avg = sum / output.size();
    EXPECT_NEAR(avg, 1.0f, 0.05f);
}

TEST(AudioProcessorTest, ProcessPassthrough) {
    // Input: 16kHz Mono -> Output: 16kHz Mono (No change)
    auto result = audio::AudioProcessor::create(16000, 1, 16000);
    ASSERT_TRUE(result.has_value());
    auto& processor = *result.value();
    
    std::vector<float> input(1600, 0.5f);
    
    auto output = processor.process(input);
    
    EXPECT_EQ(output.size(), input.size());
    EXPECT_NEAR(output[0], 0.5f, 0.001f);
}

TEST(AudioProcessorTest, ProcessEmptyInput) {
    auto result = audio::AudioProcessor::create(48000, 2, 16000);
    ASSERT_TRUE(result.has_value());
    auto& processor = *result.value();
    std::vector<float> empty;
    
    auto output = processor.process(empty);
    
    EXPECT_TRUE(output.empty());
}

// ============================================================================
// Internal Logic Verification (Indirect)
// ============================================================================

TEST(AudioProcessorTest, DownmixAndResampleLogic) {
    // Input: 32kHz Stereo -> Output: 16kHz Mono
    auto result = audio::AudioProcessor::create(32000, 2, 16000);
    ASSERT_TRUE(result.has_value());
    auto& processor = *result.value();
    
    // L = 1.0, R = -1.0 -> Mono = 0.0
    std::vector<float> input(6400); // 0.1s
    for (size_t i = 0; i < input.size(); i += 2) {
        input[i] = 1.0f;
        input[i+1] = -1.0f;
    }
    
    auto output = processor.process(input);
    
    // Output should be near 0 (cancellation)
    float sum = 0.0f;
    for (float v : output) sum += std::abs(v);
    float avg_abs = sum / output.size();
    
    EXPECT_NEAR(avg_abs, 0.0f, 0.01f);
}

