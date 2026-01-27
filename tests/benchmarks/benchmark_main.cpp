/**
 * @file benchmark_main.cpp
 * @brief Comprehensive benchmarking suite for Signeo Backend
 *
 * Measures performance across critical subsystems:
 * 1. Audio Processing Pipeline (Latency & Throughput)
 * 2. Real-Time Factor (RTF) analysis
 * 3. Memory usage profiling (Windows specific)
 * 4. Thread contention and locking overhead
 * 5. Word Error Rate (WER) accuracy validation
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "audio/audio_processor.hpp"
#include "audio/resampler.hpp"
#include "core/metrics_collector.hpp"
#include "output/log_output.hpp"
#include "utils/thread_safe_queue.hpp"
#include "utils/wer_calculator.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <psapi.h>
#endif

// ============================================================================
// Benchmark Recorder
// ============================================================================

struct BenchmarkRecord {
    std::string name;
    std::string category;
    std::map<std::string, std::string> metrics;
};

class BenchmarkRecorder {
public:
    static BenchmarkRecorder& instance() {
        static BenchmarkRecorder instance;
        return instance;
    }

    void add(const BenchmarkRecord& record) {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back(record);
    }

    void dump_xml(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open " << filename << " for writing\n";
            return;
        }

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<testsuites>\n";
        file << "  <testsuite name=\"Benchmarks\" tests=\"" << records_.size() << "\">\n";

        for (const auto& rec : records_) {
            file << "    <testcase name=\"" << escape_xml(rec.name) << "\" classname=\"" << escape_xml(rec.category)
                 << "\">\n";
            file << "      <properties>\n";
            for (const auto& [key, val] : rec.metrics) {
                file << "        <property name=\"" << escape_xml(key) << "\" value=\"" << escape_xml(val) << "\"/>\n";
            }
            file << "      </properties>\n";
            file << "    </testcase>\n";
        }

        file << "  </testsuite>\n";
        file << "</testsuites>\n";
    }

private:
    std::vector<BenchmarkRecord> records_;
    std::mutex mutex_;

    std::string escape_xml(const std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for (char c : data) {
            switch (c) {
                case '&':
                    buffer.append("&amp;");
                    break;
                case '\"':
                    buffer.append("&quot;");
                    break;
                case '\'':
                    buffer.append("&apos;");
                    break;
                case '<':
                    buffer.append("&lt;");
                    break;
                case '>':
                    buffer.append("&gt;");
                    break;
                default:
                    buffer.push_back(c);
                    break;
            }
        }
        return buffer;
    }
};

// ============================================================================
// Benchmark Utility Class
// ============================================================================

/**
 * @brief Generic micro-benchmarking framework
 *
 * Provides high-resolution timing, statistical analysis (mean, stddev, percentiles),
 * and consistent reporting for performance tests.
 */
class Benchmark {
public:
    /**
     * @brief Construct a new Benchmark object
     * @param name Display name for the benchmark scenario
     */
    Benchmark(const std::string& name) : name_(name) {}

    /**
     * @brief Run the benchmark function multiple times
     *
     * @tparam Func Function type (lambda or functor)
     * @param func The code block to measure
     * @param iterations Number of times to run (default: 1000)
     */
    template <typename Func>
    void run(Func&& func, int iterations = 1000) {
        std::vector<double> durations;
        durations.reserve(iterations);

        // Warmup
        for (int i = 0; i < 10; ++i)
            func();

        auto start_total = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            func();
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::micro> elapsed = end - start;
            durations.push_back(elapsed.count());
        }

        auto end_total = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> total_ms = end_total - start_total;

        // Statistics
        double sum = std::accumulate(durations.begin(), durations.end(), 0.0);
        double mean = sum / iterations;

        double sq_sum = std::inner_product(durations.begin(), durations.end(), durations.begin(), 0.0);
        double stdev = std::sqrt(sq_sum / iterations - mean * mean);

        // Percentiles
        std::sort(durations.begin(), durations.end());
        double p50 = durations[iterations / 2];
        double p95 = durations[static_cast<int>(iterations * 0.95)];
        double p99 = durations[static_cast<int>(iterations * 0.99)];

        print_results(iterations, total_ms.count(), mean, stdev, p50, p95, p99);
    }

private:
    std::string name_;

    void print_results(int iterations, double total_ms, double mean_us, double stdev_us, double p50, double p95,
                       double p99) {
        // Record to XML
        BenchmarkRecord rec;
        rec.name = name_;
        rec.category = "MicroBenchmark";
        rec.metrics["iterations"] = std::to_string(iterations);
        rec.metrics["total_time_ms"] = std::to_string(total_ms);
        rec.metrics["mean_latency_us"] = std::to_string(mean_us);
        rec.metrics["stdev_us"] = std::to_string(stdev_us);
        rec.metrics["p50_us"] = std::to_string(p50);
        rec.metrics["p95_us"] = std::to_string(p95);
        rec.metrics["p99_us"] = std::to_string(p99);
        rec.metrics["throughput_ops_sec"] = std::to_string(1000000.0 / mean_us);
        BenchmarkRecorder::instance().add(rec);

        std::cout << "\n[BENCH] " << name_ << "\n";
        std::cout << "  Iterations:    " << iterations << "\n";
        std::cout << "  Total Time:    " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
        std::cout << "  Mean Latency:  " << mean_us << " us\n";
        std::cout << "  Std Dev:       " << stdev_us << " us\n";
        std::cout << "  P50 Latency:   " << p50 << " us\n";
        std::cout << "  P95 Latency:   " << p95 << " us\n";
        std::cout << "  P99 Latency:   " << p99 << " us\n";
        std::cout << "  Throughput:    " << std::fixed << std::setprecision(0) << (1000000.0 / mean_us) << " ops/sec\n";
        std::cout << "----------------------------------------\n";
    }
};

// ============================================================================
// Memory Profiler (Windows)
// ============================================================================

/**
 * @brief Platform-specific memory usage profiler
 *
 * Currently implemented for Windows using PSAPI.
 * Tracks Working Set (RSS) and Private Bytes to detect leaks or high consumption.
 */
class MemoryProfiler {
public:
    struct MemoryStats {
        size_t working_set_kb = 0;       ///< Current physical memory usage
        size_t peak_working_set_kb = 0;  ///< Max physical memory usage
        size_t private_bytes_kb = 0;     ///< Committed memory (excluding shared)
    };

    /**
     * @brief Capture current memory usage statistics
     * @return MemoryStats struct with KB values
     */
    static MemoryStats get_current() {
        MemoryStats stats;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            stats.working_set_kb = pmc.WorkingSetSize / 1024;
            stats.peak_working_set_kb = pmc.PeakWorkingSetSize / 1024;
            stats.private_bytes_kb = pmc.PrivateUsage / 1024;
        }
#endif
        return stats;
    }

    static void print_stats(const std::string& label, const MemoryStats& stats) {
        std::cout << "\n[MEMORY] " << label << "\n";
        std::cout << "  Working Set:      " << stats.working_set_kb << " KB\n";
        std::cout << "  Peak Working Set: " << stats.peak_working_set_kb << " KB\n";
        std::cout << "  Private Bytes:    " << stats.private_bytes_kb << " KB\n";
        std::cout << "----------------------------------------\n";
    }
};

// ============================================================================
// Thread Contention Analyzer
// ============================================================================

/**
 * @brief Analyzes multithreaded synchronization overhead
 *
 * Compares performance of different synchronization primitives:
 * - std::mutex (heavy locking)
 * - std::atomic (lock-free)
 * - utils::ThreadSafeQueue (condition variable based)
 */
class ThreadContentionBenchmark {
public:
    static void run_contention_test() {
        std::cout << "\n[BENCH] Thread Contention Analysis\n";

        constexpr int NUM_THREADS = 8;
        constexpr int OPERATIONS_PER_THREAD = 10000;

        // Test 1: Mutex contention
        {
            std::mutex mtx;
            std::atomic<int> counter{0};
            std::vector<std::thread> threads;

            auto start = std::chrono::high_resolution_clock::now();

            for (int t = 0; t < NUM_THREADS; ++t) {
                threads.emplace_back([&]() {
                    for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                        std::lock_guard<std::mutex> lock(mtx);
                        counter++;
                    }
                });
            }

            for (auto& t : threads)
                t.join();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

            std::cout << "  Mutex Contention (" << NUM_THREADS << " threads, " << OPERATIONS_PER_THREAD
                      << " ops each):\n";
            std::cout << "    Total time:       " << std::fixed << std::setprecision(2) << duration_ms << " ms\n";
            std::cout << "    Ops/sec:          " << std::fixed << std::setprecision(0)
                      << (NUM_THREADS * OPERATIONS_PER_THREAD / (duration_ms / 1000.0)) << "\n";
        }

        // Test 2: Atomic contention
        {
            std::atomic<int> counter{0};
            std::vector<std::thread> threads;

            auto start = std::chrono::high_resolution_clock::now();

            for (int t = 0; t < NUM_THREADS; ++t) {
                threads.emplace_back([&]() {
                    for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }

            for (auto& t : threads)
                t.join();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

            std::cout << "  Atomic Contention (" << NUM_THREADS << " threads, " << OPERATIONS_PER_THREAD
                      << " ops each):\n";
            std::cout << "    Total time:       " << duration_ms << " ms\n";
            std::cout << "    Ops/sec:          " << std::fixed << std::setprecision(0)
                      << (NUM_THREADS * OPERATIONS_PER_THREAD / (duration_ms / 1000.0)) << "\n";
        }

        // Test 3: ThreadSafeQueue throughput
        {
            utils::ThreadSafeQueue<int> queue;
            std::atomic<int> consumed{0};
            std::vector<std::thread> producers;
            std::thread consumer;

            auto start = std::chrono::high_resolution_clock::now();

            // Start consumer
            consumer = std::thread([&]() {
                while (consumed < NUM_THREADS * OPERATIONS_PER_THREAD) {
                    auto val = queue.pop();
                    if (val.has_value()) {
                        consumed++;
                    }
                }
            });

            // Start producers
            for (int t = 0; t < NUM_THREADS; ++t) {
                producers.emplace_back([&]() {
                    for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                        queue.push(i);
                    }
                });
            }

            for (auto& p : producers)
                p.join();

            // Wait for consumer with timeout
            for (int wait = 0; wait < 100 && consumed < NUM_THREADS * OPERATIONS_PER_THREAD; ++wait) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            queue.stop();
            consumer.join();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

            std::cout << "  ThreadSafeQueue (" << NUM_THREADS << " producers, 1 consumer, " << OPERATIONS_PER_THREAD
                      << " items each):\n";
            std::cout << "    Total time:       " << duration_ms << " ms\n";
            std::cout << "    Items/sec:        " << std::fixed << std::setprecision(0)
                      << (consumed.load() / (duration_ms / 1000.0)) << "\n";

            BenchmarkRecord rec;
            rec.name = "ThreadSafeQueue Throughput";
            rec.category = "ThreadContention";
            rec.metrics["producers"] = std::to_string(NUM_THREADS);
            rec.metrics["items_per_thread"] = std::to_string(OPERATIONS_PER_THREAD);
            rec.metrics["total_time_ms"] = std::to_string(duration_ms);
            rec.metrics["items_per_sec"] = std::to_string(consumed.load() / (duration_ms / 1000.0));
            BenchmarkRecorder::instance().add(rec);
        }

        std::cout << "----------------------------------------\n";
    }
};

// ============================================================================
// RTF (Real-Time Factor) Benchmark
// ============================================================================

void benchmark_rtf() {
    std::cout << "\n[BENCH] Real-Time Factor (RTF) Analysis\n";

    // Simulate processing of 1 second of audio
    // RTF = processing_time / audio_duration
    // RTF < 1.0 means real-time capable

    auto proc_res = audio::AudioProcessor::create(48000, 2, 16000);
    auto& processor = *proc_res.value();

    for (int audio_duration_ms : {10, 100, 500, 1000}) {
        int samples_in = (48000 * 2 * audio_duration_ms) / 1000;  // stereo 48kHz
        std::vector<float> input(samples_in, 0.5f);

        constexpr int RUNS = 100;
        double total_processing_us = 0;

        for (int i = 0; i < RUNS; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            auto out = processor.process(input);
            (void)out;
            auto end = std::chrono::high_resolution_clock::now();
            total_processing_us += std::chrono::duration<double, std::micro>(end - start).count();
        }

        double avg_processing_ms = (total_processing_us / RUNS) / 1000.0;
        double rtf = avg_processing_ms / audio_duration_ms;

        std::cout << "  " << audio_duration_ms << "ms audio chunk: ";
        std::cout << "proc=" << std::fixed << std::setprecision(3) << avg_processing_ms << "ms, ";
        std::cout << "RTF=" << std::setprecision(4) << rtf;
        std::cout << (rtf < 1.0 ? " [OK] REALTIME" : " [SLOW] TOO SLOW") << "\n";

        BenchmarkRecord rec;
        rec.name = "RTF Analysis " + std::to_string(audio_duration_ms) + "ms chunk";
        rec.category = "RTF";
        rec.metrics["audio_duration_ms"] = std::to_string(audio_duration_ms);
        rec.metrics["processing_time_ms"] = std::to_string(avg_processing_ms);
        rec.metrics["rtf"] = std::to_string(rtf);
        rec.metrics["realtime_status"] = (rtf < 1.0 ? "OK" : "SLOW");
        BenchmarkRecorder::instance().add(rec);
    }
    std::cout << "----------------------------------------\n";
}

// ============================================================================
// Throughput Benchmark (simulated chars/sec)
// ============================================================================

void benchmark_throughput() {
    std::cout << "\n[BENCH] Throughput Analysis (Simulated)\n";

    // Simulate transcription throughput based on audio processing
    // Assumption: ~100ms of audio produces ~10-20 characters on average

    auto proc_res = audio::AudioProcessor::create(48000, 2, 16000);
    if (!proc_res) {
        std::cerr << "Failed to create processor for throughput bench\n";
        return;
    }
    auto& processor = *proc_res.value();

    constexpr int CHUNKS = 100;
    constexpr int CHUNK_DURATION_MS = 100;
    constexpr double AVG_CHARS_PER_100MS = 15.0;  // Realistic speech rate

    std::vector<float> input((48000 * 2 * CHUNK_DURATION_MS) / 1000, 0.5f);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < CHUNKS; ++i) {
        auto out = processor.process(input);
        (void)out;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    double simulated_audio_duration_s = (CHUNKS * CHUNK_DURATION_MS) / 1000.0;
    double simulated_chars = CHUNKS * AVG_CHARS_PER_100MS;
    double chars_per_sec = simulated_chars / (elapsed_ms / 1000.0);

    std::cout << "  Audio Processed:   " << simulated_audio_duration_s << " seconds\n";
    std::cout << "  Processing Time:   " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
    std::cout << "  Simulated Chars:   " << static_cast<int>(simulated_chars) << "\n";
    std::cout << "  Throughput:        " << std::fixed << std::setprecision(0) << chars_per_sec << " chars/sec\n";
    std::cout << "  Speed Factor:      " << std::setprecision(2) << (simulated_audio_duration_s / (elapsed_ms / 1000.0))
              << "x realtime\n";

    BenchmarkRecord rec;
    rec.name = "Throughput Analysis";
    rec.category = "Throughput";
    rec.metrics["audio_processed_s"] = std::to_string(simulated_audio_duration_s);
    rec.metrics["processing_time_ms"] = std::to_string(elapsed_ms);
    rec.metrics["simulated_chars"] = std::to_string(simulated_chars);
    rec.metrics["throughput_chars_sec"] = std::to_string(chars_per_sec);
    rec.metrics["speed_factor"] = std::to_string(simulated_audio_duration_s / (elapsed_ms / 1000.0));
    BenchmarkRecorder::instance().add(rec);
    std::cout << "----------------------------------------\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    // Initialize logging but suppress info/debug messages to avoid interleaved output
    core::init_logging("", false);          // Console disabled for benchmarks
    spdlog::set_level(spdlog::level::err);  // Only show errors

    std::string xml_output = "benchmark_results.xml";

    std::cout << "+==============================================================+\n";
    std::cout << "|              SIGNEO BACKEND BENCHMARK SUITE                  |\n";
    std::cout << "+==============================================================+\n";

    // Initial memory snapshot
    auto mem_start = MemoryProfiler::get_current();
    MemoryProfiler::print_stats("Initial State", mem_start);

    // 1. Audio Processing Latency Benchmarks
    std::cout << "\n========== AUDIO PROCESSING LATENCY ==========\n";

    {
        Benchmark bench("Resampling 48kHz -> 16kHz (10ms chunk)");
        auto proc_res = audio::AudioProcessor::create(48000, 1, 16000);
        auto& processor = *proc_res.value();
        std::vector<float> input(480);  // 10ms at 48kHz

        bench.run(
            [&]() {
                auto out = processor.process(input);
                (void)out;
            },
            10000);
    }

    {
        Benchmark bench("Downmix Stereo -> Mono (10ms chunk)");
        auto proc_res = audio::AudioProcessor::create(48000, 2, 48000);
        auto& processor = *proc_res.value();
        std::vector<float> input(960);  // 10ms at 48kHz Stereo

        bench.run(
            [&]() {
                auto out = processor.process(input);
                (void)out;
            },
            10000);
    }

    {
        Benchmark bench("Full Audio Pipeline (Stereo 48k -> Mono 16k, 100ms)");
        auto proc_res = audio::AudioProcessor::create(48000, 2, 16000);
        auto& processor = *proc_res.value();
        std::vector<float> input(9600);  // 100ms

        bench.run(
            [&]() {
                auto out = processor.process(input);
                (void)out;
            },
            1000);
    }

    // 2. RTF Analysis
    std::cout << "\n========== REAL-TIME FACTOR (RTF) ==========\n";
    benchmark_rtf();

    // 3. Throughput Analysis
    std::cout << "\n========== THROUGHPUT ==========\n";
    benchmark_throughput();

    // 4. WER Accuracy Benchmark
    std::cout << "\n========== WER ACCURACY ==========\n";
    std::cout << "[BENCH] Word Error Rate Examples\n";

    // Test cases: (reference, hypothesis, expected scenario)
    struct WerTestCase {
        std::string name;
        std::string reference;
        std::string hypothesis;
    };

    std::vector<WerTestCase> wer_tests = {
        {"Perfect Match", "the quick brown fox jumps over the lazy dog", "the quick brown fox jumps over the lazy dog"},
        {"Minor Errors (typical STT)", "the quick brown fox jumps over the lazy dog",
         "the quick brown fox jumps over lazy dog"},
        {"Moderate Errors", "hello world how are you doing today", "hello word how are you do today"},
        {"Significant Errors", "speech recognition is very accurate now", "speech wreck ignition is vary accurate"},
        {"Homophones", "I would like to eat there today", "I wood like too eat their today"}};

    for (const auto& test : wer_tests) {
        auto result = utils::WerCalculator::calculate(test.reference, test.hypothesis);
        std::cout << "  " << test.name << ":\n";
        std::cout << "    WER: " << std::fixed << std::setprecision(1) << result.wer_percentage() << "%";
        std::cout << " (S:" << result.substitutions << " D:" << result.deletions << " I:" << result.insertions << ")\n";

        BenchmarkRecord rec;
        rec.name = "WER: " + test.name;
        rec.category = "Accuracy";
        rec.metrics["wer_percentage"] = std::to_string(result.wer_percentage());
        rec.metrics["substitutions"] = std::to_string(result.substitutions);
        rec.metrics["deletions"] = std::to_string(result.deletions);
        rec.metrics["insertions"] = std::to_string(result.insertions);
        BenchmarkRecorder::instance().add(rec);
    }
    std::cout << "----------------------------------------\n";

    // 5. Thread Contention
    std::cout << "\n========== THREAD CONTENTION ==========\n";
    ThreadContentionBenchmark::run_contention_test();

    // Final memory snapshot
    auto mem_end = MemoryProfiler::get_current();
    MemoryProfiler::print_stats("After Benchmarks", mem_end);

    // Memory delta
    std::cout << "\n[MEMORY] Delta\n";
    std::cout << "  Working Set Change: "
              << (static_cast<long long>(mem_end.working_set_kb) - static_cast<long long>(mem_start.working_set_kb))
              << " KB\n";
    std::cout << "  Peak Working Set:   " << mem_end.peak_working_set_kb << " KB\n";
    std::cout << "----------------------------------------\n";

    std::cout << "\n+==============================================================+\n";
    std::cout << "|                   BENCHMARKS COMPLETE                        |\n";
    std::cout << "+==============================================================+\n";

    if (!xml_output.empty()) {
        std::cout << "[INFO] Writing XML report to: " << xml_output << "\n";
        BenchmarkRecorder::instance().dump_xml(xml_output);
    }

    return 0;
}
