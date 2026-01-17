/**
 * @file metrics_collector.cpp
 * @brief Implementation of APM metrics collection
 */

#include "core/metrics_collector.hpp"

namespace core {

void MetricsCollector::on_audio_chunk(size_t samples) {
    total_audio_samples_ += samples;

    // Calculate buffer fill rate (samples/second) over a sliding window
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = now - last_audio_chunk_time_;

    if (elapsed.count() > 0.0) {
        // Exponential moving average for smoother rate calculation
        double instant_rate = static_cast<double>(samples) / elapsed.count();
        double alpha = 0.1; // Smoothing factor
        audio_fill_rate_ = alpha * instant_rate + (1.0 - alpha) * audio_fill_rate_.load();
    }
    last_audio_chunk_time_ = now;
}

void MetricsCollector::on_vad_processing_start() { vad_start_time_ = std::chrono::steady_clock::now(); }

void MetricsCollector::on_vad_processing_end() {
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> diff = end - vad_start_time_;
    last_vad_latency_ms_ = diff.count();
}

void MetricsCollector::on_inference_start() { inference_start_time_ = std::chrono::steady_clock::now(); }

void MetricsCollector::on_inference_end(int tokens_generated) {
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> diff = end - inference_start_time_;
    last_inference_latency_ms_ = diff.count();

    if (tokens_generated > 0) {
        total_tokens_ += tokens_generated;
    }
}

double MetricsCollector::get_current_rtf() const { return current_rtf_; }

double MetricsCollector::get_inference_latency_ms() const { return last_inference_latency_ms_; }

double MetricsCollector::get_pipeline_latency_ms() const { return last_pipeline_latency_ms_; }

void MetricsCollector::record_pipeline_latency(double ms) { last_pipeline_latency_ms_ = ms; }

double MetricsCollector::get_vad_latency_ms() const { return last_vad_latency_ms_; }

double MetricsCollector::get_audio_fill_rate() const { return audio_fill_rate_; }

double MetricsCollector::get_throughput_char_per_sec() const {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = now - start_time_;
    if (elapsed.count() < 0.001)
        return 0.0;

    // Assuming 1 token ~ 4 chars for approximation, or just return tokens/sec?
    // Method name says "char_per_sec".
    // Let's assume average 4 chars per token.
    return (static_cast<double>(total_tokens_) * 4.0) / elapsed.count();
}

void MetricsCollector::reset() {
    last_vad_latency_ms_ = 0;
    last_inference_latency_ms_ = 0;
    audio_fill_rate_ = 0;
    total_audio_samples_ = 0;
    total_tokens_ = 0;
}

} // namespace core
