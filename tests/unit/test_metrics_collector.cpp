/**
 * @file test_metrics_collector.cpp
 * @brief Unit tests for the MetricsCollector
 * 
 * Verifies APM metric collection logic, including latency tracking,
 * throughput calculation, and thread safety.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include "core/metrics_collector.hpp"

/**
 * @brief Test fixture for MetricsCollector
 * Ensures a clean state before each test case.
 */
class MetricsCollectorTest : public ::testing::Test {
protected:
    core::MetricsCollector mc;

    void SetUp() override {
        mc.reset();
    }
};

TEST_F(MetricsCollectorTest, InitialState) {
    EXPECT_DOUBLE_EQ(mc.get_vad_latency_ms(), 0.0);
    EXPECT_DOUBLE_EQ(mc.get_inference_latency_ms(), 0.0);
    EXPECT_DOUBLE_EQ(mc.get_pipeline_latency_ms(), 0.0);
    EXPECT_DOUBLE_EQ(mc.get_audio_fill_rate(), 0.0);
}

TEST_F(MetricsCollectorTest, VadLatencyTracking) {
    mc.on_vad_processing_start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    mc.on_vad_processing_end();
    
    double latency = mc.get_vad_latency_ms();
    EXPECT_GE(latency, 9.0);  // At least 9ms (allowing some variance)
    EXPECT_LE(latency, 50.0); // But not more than 50ms
}

TEST_F(MetricsCollectorTest, InferenceLatencyTracking) {
    mc.on_inference_start();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    mc.on_inference_end(10); // 10 tokens
    
    double latency = mc.get_inference_latency_ms();
    EXPECT_GE(latency, 14.0);
    EXPECT_LE(latency, 60.0);
}

TEST_F(MetricsCollectorTest, PipelineLatencyRecording) {
    mc.record_pipeline_latency(123.45);
    EXPECT_DOUBLE_EQ(mc.get_pipeline_latency_ms(), 123.45);
    
    mc.record_pipeline_latency(0.0);
    EXPECT_DOUBLE_EQ(mc.get_pipeline_latency_ms(), 0.0);
}

TEST_F(MetricsCollectorTest, AudioChunkTracking) {
    // Simulate audio chunks
    for (int i = 0; i < 10; i++) {
        mc.on_audio_chunk(512);  // 512 samples per chunk
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    double fill_rate = mc.get_audio_fill_rate();
    EXPECT_GT(fill_rate, 0.0); // Should have some fill rate
}

TEST_F(MetricsCollectorTest, ThroughputCalculation) {
    // Simulate inference with tokens
    mc.on_inference_start();
    mc.on_inference_end(100); // 100 tokens
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    double throughput = mc.get_throughput_char_per_sec();
    EXPECT_GT(throughput, 0.0); // Should have some throughput
}

TEST_F(MetricsCollectorTest, Reset) {
    // Set some values
    mc.on_vad_processing_start();
    mc.on_vad_processing_end();
    mc.on_audio_chunk(1024);
    mc.on_inference_start();
    mc.on_inference_end(50);
    
    // Reset
    mc.reset();
    
    EXPECT_DOUBLE_EQ(mc.get_vad_latency_ms(), 0.0);
    EXPECT_DOUBLE_EQ(mc.get_inference_latency_ms(), 0.0);
    EXPECT_DOUBLE_EQ(mc.get_audio_fill_rate(), 0.0);
}

TEST_F(MetricsCollectorTest, ThreadSafety) {
    std::vector<std::thread> threads;
    
    // Spawn multiple threads updating metrics
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; j++) {
                mc.on_audio_chunk(256);
                mc.on_vad_processing_start();
                mc.on_vad_processing_end();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should not crash, values should be reasonable
    EXPECT_GE(mc.get_vad_latency_ms(), 0.0);
    EXPECT_GE(mc.get_audio_fill_rate(), 0.0);
}
