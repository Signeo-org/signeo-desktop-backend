#pragma once

/**
 * @file wer_calculator.hpp
 * @brief Word Error Rate (WER) calculator for transcription accuracy
 *
 * WER = (Substitutions + Deletions + Insertions) / Total Reference Words
 * Uses Levenshtein distance at the word level.
 */

#include <string>
#include <vector>

namespace utils {

/**
 * @brief Result of WER calculation with detailed breakdown
 */
struct WerResult {
    double wer = 0.0;         // Word Error Rate (0.0 = perfect, 1.0 = 100% errors)
    int substitutions = 0;    // Words replaced
    int deletions = 0;        // Words missing from hypothesis
    int insertions = 0;       // Extra words in hypothesis
    int reference_words = 0;  // Total words in reference
    int hypothesis_words = 0; // Total words in hypothesis

    // Helper to get error counts
    int total_errors() const;

    // WER as percentage
    double wer_percentage() const;
};

/**
 * @brief Word Error Rate calculator
 *
 * Computes WER between a reference transcription and a hypothesis (predicted) transcription
 * using the Levenshtein distance algorithm at the word level.
 */
class WerCalculator {
public:
    /**
     * @brief Calculate WER between reference and hypothesis
     * @param reference Ground truth transcription
     * @param hypothesis Predicted transcription
     * @return WerResult with WER value and error breakdown
     */
    static WerResult calculate(const std::string& reference, const std::string& hypothesis);

    /**
     * @brief Calculate WER from pre-tokenized word vectors
     */
    static WerResult calculate_wer(const std::vector<std::string>& reference,
                                   const std::vector<std::string>& hypothesis);

    /**
     * @brief Normalize text for comparison
     * - Lowercase
     * - Remove punctuation
     * - Collapse whitespace
     */
    static std::string normalize(const std::string& text);

    /**
     * @brief Tokenize text into words
     */
    static std::vector<std::string> tokenize(const std::string& text);
};

} // namespace utils
