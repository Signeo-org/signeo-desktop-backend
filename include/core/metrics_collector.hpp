#pragma once

/**
 * @file metrics_collector.hpp
 * @brief Application Performance Monitoring (APM) metrics collection
 *
 * Thread-safe singleton for collecting real-time performance metrics
 * across the audio processing pipeline.
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>

namespace core {

/**
 * @brief Singleton metrics collector for performance monitoring
 *
 * Collects:
 * - Audio processing latency (VAD, inference, pipeline)
 * - Real-Time Factor (RTF)
 * - Throughput (chars/sec, tokens/sec)
 * - Audio fill rate
 *
 * Thread-safe for concurrent access from audio, VAD, and STT threads.
 */
class MetricsCollector {
public:
    MetricsCollector() = default;

    // Non-copyable (metrics should be unique per application instance)
    MetricsCollector(const MetricsCollector&) = delete;
    auto operator=(const MetricsCollector&) -> MetricsCollector& = delete;
    MetricsCollector(MetricsCollector&&) = delete;
    auto operator=(MetricsCollector&&) -> MetricsCollector& = delete;
    ~MetricsCollector() = default;

    // =========================================================================
    // Audio Pipeline Event Tracking
    // =========================================================================

    /**
     * @brief Record audio chunk received from capture
     * @param samples Number of samples in the chunk
     */
    void on_audio_chunk(size_t samples);

    /**
     * @brief Mark start of VAD processing
     */
    void on_vad_processing_start();

    /**
     * @brief Mark end of VAD processing
     */
    void on_vad_processing_end();

    // =========================================================================
    // STT Metrics
    // =========================================================================

    /**
     * @brief Mark start of STT inference
     */
    void on_inference_start();

    /**
     * @brief Mark end of STT inference
     * @param tokens_generated Number of tokens produced
     */
    void on_inference_end(int tokens_generated);

    // =========================================================================
    // Metric Getters (Thread-safe)
    // =========================================================================

    /**
     * @brief Get Real-Time Factor (processing_time / audio_duration)
     * @return RTF value (< 1.0 means faster than realtime)
     */
    [[nodiscard]] auto get_current_rtf() const -> double;

    /**
     * @brief Get last STT inference latency
     * @return Latency in milliseconds
     */
    [[nodiscard]] auto get_inference_latency_ms() const -> double;

    /**
     * @brief Get last VAD processing latency
     * @return Latency in milliseconds
     */
    [[nodiscard]] auto get_vad_latency_ms() const -> double;

    /**
     * @brief Get end-to-end pipeline latency
     * @return Latency in milliseconds
     */
    [[nodiscard]] auto get_pipeline_latency_ms() const -> double;

    /**
     * @brief Get transcription throughput
     * @return Characters per second (approximate)
     */
    [[nodiscard]] auto get_throughput_char_per_sec() const -> double;

    /**
     * @brief Get audio input fill rate
     * @return Samples per second being processed
     */
    [[nodiscard]] auto get_audio_fill_rate() const -> double;

    /**
     * @brief Record pipeline latency (end-to-end)
     * @param latency_ms Latency in milliseconds
     */
    void record_pipeline_latency(double latency_ms);

    /**
     * @brief Reset all collected metrics
     */
    void reset();

private:
    static constexpr double kSmoothingAlpha = 0.1;
    static constexpr double kMinElapsedSec = 0.001;
    static constexpr double kCharsPerToken = 4.0;

    // Timestamps
    std::chrono::steady_clock::time_point vad_start_time_;
    std::chrono::steady_clock::time_point inference_start_time_;

    // Thread-safe atomic metrics
    std::atomic<double> last_vad_latency_ms_{0.0};
    std::atomic<double> last_inference_latency_ms_{0.0};
    std::atomic<double> last_pipeline_latency_ms_{0.0};
    std::atomic<double> current_rtf_{1.0};

    // Throughput tracking
    std::atomic<size_t> total_tokens_{0};
    std::atomic<size_t> total_audio_samples_{0};
    std::atomic<double> audio_fill_rate_{0.0};

    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_audio_chunk_time_ = std::chrono::steady_clock::now();
};

}  // namespace core
