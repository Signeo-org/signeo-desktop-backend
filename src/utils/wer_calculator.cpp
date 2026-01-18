#include "utils/wer_calculator.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace utils {

WerResult WerCalculator::calculate(const std::string& reference, const std::string& hypothesis) {
    auto ref_words = tokenize(normalize(reference));
    auto hyp_words = tokenize(normalize(hypothesis));

    return calculate_wer(ref_words, hyp_words);
}

WerResult WerCalculator::calculate_wer(const std::vector<std::string>& reference,
                                       const std::vector<std::string>& hypothesis) {
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
    size_t m = reference.size();
    size_t n = hypothesis.size();

    // DP matrix: dp[i][j] = min edits to transform ref[0..i) to hyp[0..j)
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    // Initialize base cases
    for (size_t i = 0; i <= m; ++i) {
        dp[i][0] = static_cast<int>(i); // Deletions
    }
    for (size_t j = 0; j <= n; ++j) {
        dp[0][j] = static_cast<int>(j); // Insertions
    }

    // Fill DP matrix
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (reference[i - 1] == hypothesis[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1]; // Match
            } else {
                dp[i][j] = 1 + std::min({
                                   dp[i - 1][j],    // Deletion
                                   dp[i][j - 1],    // Insertion
                                   dp[i - 1][j - 1] // Substitution
                               });
            }
        }
    }

    // Traceback to classify errors
    size_t i = m;
    size_t j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && reference[i - 1] == hypothesis[j - 1]) {
            // Match - no error
            --i;
            --j;
        } else if (i > 0 && j > 0 && dp[i][j] == dp[i - 1][j - 1] + 1) {
            // Substitution
            result.substitutions++;
            --i;
            --j;
        } else if (j > 0 && dp[i][j] == dp[i][j - 1] + 1) {
            // Insertion
            result.insertions++;
            --j;
        } else if (i > 0 && dp[i][j] == dp[i - 1][j] + 1) {
            // Deletion
            result.deletions++;
            --i;
        } else {
            // Shouldn't happen, but safety break
            break;
        }
    }

    // Calculate WER
    result.wer = static_cast<double>(result.total_errors()) / static_cast<double>(result.reference_words);

    return result;
}

std::string WerCalculator::normalize(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    bool last_was_space = true; // Trim leading
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            last_was_space = false;
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
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

std::vector<std::string> WerCalculator::tokenize(const std::string& text) {
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
int WerResult::total_errors() const { return substitutions + deletions + insertions; }

double WerResult::wer_percentage() const { return wer * 100.0; }

} // namespace utils
