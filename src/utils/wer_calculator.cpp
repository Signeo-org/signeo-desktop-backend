#include "utils/wer_calculator.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace utils {

auto WerCalculator::calculate(const std::string& reference, const std::string& hypothesis) -> WerResult {
    auto ref_words = tokenize(normalize(reference));
    auto hyp_words = tokenize(normalize(hypothesis));

    return calculate_wer(ref_words, hyp_words);
}

auto WerCalculator::calculate_wer(const std::vector<std::string>& reference, const std::vector<std::string>& hypothesis)
    -> WerResult {
    WerResult result;
    result.reference_words = static_cast<int>(reference.size());
    result.hypothesis_words = static_cast<int>(hypothesis.size());

    if (reference.empty()) {
        result.insertions = static_cast<int>(hypothesis.size());
        result.wer = hypothesis.empty() ? 0.0 : 1.0;
        return result;
    }

    if (hypothesis.empty()) {
        result.deletions = static_cast<int>(reference.size());
        result.wer = 1.0;
        return result;
    }

    // Levenshtein distance with traceback for error classification
    size_t ref_len = reference.size();
    size_t hyp_len = hypothesis.size();

    // DP matrix: distance[i][j] = min edits to transform ref[0..i) to hyp[0..j)
    std::vector<std::vector<int>> distance(ref_len + 1, std::vector<int>(hyp_len + 1, 0));

    // Initialize base cases
    for (size_t row = 0; row <= ref_len; ++row) {
        distance[row][0] = static_cast<int>(row);  // Deletions
    }
    for (size_t col = 0; col <= hyp_len; ++col) {
        distance[0][col] = static_cast<int>(col);  // Insertions
    }

    // Fill DP matrix
    for (size_t row = 1; row <= ref_len; ++row) {
        for (size_t col = 1; col <= hyp_len; ++col) {
            if (reference[row - 1] == hypothesis[col - 1]) {
                distance[row][col] = distance[row - 1][col - 1];  // Match
            } else {
                distance[row][col] = 1 + std::min({
                                   distance[row - 1][col],     // Deletion
                                   distance[row][col - 1],     // Insertion
                                   distance[row - 1][col - 1]  // Substitution
                               });
            }
        }
    }

    // Traceback to classify errors
    size_t row = ref_len;
    size_t col = hyp_len;
    while (row > 0 || col > 0) {
        if (row > 0 && col > 0 && reference[row - 1] == hypothesis[col - 1]) {
            // Match - no error
            --row;
            --col;
        } else if (row > 0 && col > 0 && distance[row][col] == distance[row - 1][col - 1] + 1) {
            // Substitution
            result.substitutions++;
            --row;
            --col;
        } else if (col > 0 && distance[row][col] == distance[row][col - 1] + 1) {
            // Insertion
            result.insertions++;
            --col;
        } else if (row > 0 && distance[row][col] == distance[row - 1][col] + 1) {
            // Deletion
            result.deletions++;
            --row;
        } else {
            // Shouldn't happen, but safety break
            break;
        }
    }

    // Calculate WER
    result.wer = static_cast<double>(result.total_errors()) / static_cast<double>(result.reference_words);

    return result;
}

auto WerCalculator::normalize(const std::string& text) -> std::string {
    std::string result;
    result.reserve(text.size());

    bool last_was_space = true;  // Trim leading
    for (char chr : text) {
        if (std::isalnum(static_cast<unsigned char>(chr)) != 0) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
            last_was_space = false;
        } else if (std::isspace(static_cast<unsigned char>(chr)) != 0) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        }
        // Skip punctuation
    }

    // Trim trailing space
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

auto WerCalculator::tokenize(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        if (!word.empty()) {
            words.push_back(word);
        }
    }
    return words;
}

// WerResult implementation
auto WerResult::total_errors() const -> int {
    return substitutions + deletions + insertions;
}

auto WerResult::wer_percentage() const -> double {
    return wer * 100.0;
}

}  // namespace utils
