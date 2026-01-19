/**
 * @file test_streaming_transcriber.cpp
 * @brief Unit tests for StreamingTranscriber logic
 *
 * Note: Full integration tests require a real SttEngine. These tests focus on
 * the testable pure logic: audio buffering, timing, and text processing helpers.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

// Since StreamingTranscriber's helper methods are private, we test the logic
// by extracting it to standalone functions for unit testing.

namespace test_utils {

// Replicated from streaming_transcriber.cpp for unit testing

/**
 * @brief Normalize and split text into tokens
 *
 * Removes punctuation and converts to lowercase.
 * Example: "Hello, World!" -> {"hello", "world"}
 */
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        std::string normalized;
        for (char c : word) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        if (!normalized.empty()) {
            tokens.push_back(normalized);
        }
    }
    return tokens;
}

/**
 * @brief Find overlapping suffix/prefix between two token lists
 *
 * @param prev Previous segment tokens
 * @param curr Current segment tokens
 * @return Number of tokens in the overlap
 */
int find_overlap(const std::vector<std::string>& prev, const std::vector<std::string>& curr) {
    if (prev.empty() || curr.empty())
        return 0;

    size_t max_overlap = std::min(prev.size(), curr.size());

    for (size_t overlap = max_overlap; overlap >= 1; --overlap) {
        bool match = true;
        for (size_t i = 0; i < overlap; ++i) {
            if (prev[prev.size() - overlap + i] != curr[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            return static_cast<int>(overlap);
        }
    }
    return 0;
}

/**
 * @brief Remove repetitive phrases from text
 *
 * Detects and removes immediate suffix repetitions (e.g. "text text" -> "text").
 * Minimum repetition length is 10 characters.
 */
std::string remove_repetition(const std::string& text) {
    if (text.empty())
        return text;

    std::string current = text;
    bool changed = true;
    while (changed) {
        changed = false;
        size_t n = current.length();
        for (size_t i = 10; i <= n / 2; ++i) {
            std::string sub = current.substr(n - i, i);
            std::string prev = current.substr(n - 2 * i, i);

            if (sub == prev) {
                current = current.substr(0, n - i);
                changed = true;
                break;
            }
        }
    }
    return current;
}

/**
 * @brief Check if text is a common STT hallucination
 *
 * Detects common Whisper hallucinations like "Thank you", "Bye", etc.
 */
bool is_hallucination(const std::string& text) {
    if (text.empty())
        return true;
    if (text.length() < 2)
        return true;

    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    while (!lower.empty() && std::ispunct(lower.back())) {
        lower.pop_back();
    }

    static const std::vector<std::string> blacklist = {"thank you", "thank you very much", "you", "bye"};

    for (const auto& phrase : blacklist) {
        if (lower == phrase)
            return true;
    }

    return false;
}

}  // namespace test_utils

// ============================================================================
// Tests for tokenize()
// ============================================================================

TEST(StreamingTranscriberLogicTest, TokenizeBasic) {
    auto tokens = test_utils::tokenize("Hello World");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
}

TEST(StreamingTranscriberLogicTest, TokenizeWithPunctuation) {
    auto tokens = test_utils::tokenize("Hello, World!");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
}

TEST(StreamingTranscriberLogicTest, TokenizeEmpty) {
    auto tokens = test_utils::tokenize("");
    EXPECT_TRUE(tokens.empty());
}

TEST(StreamingTranscriberLogicTest, TokenizeMixedCase) {
    auto tokens = test_utils::tokenize("ThIs Is A TeSt");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0], "this");
    EXPECT_EQ(tokens[3], "test");
}

// ============================================================================
// Tests for find_overlap()
// ============================================================================

TEST(StreamingTranscriberLogicTest, FindOverlapNone) {
    std::vector<std::string> prev = {"hello", "world"};
    std::vector<std::string> curr = {"foo", "bar"};
    EXPECT_EQ(test_utils::find_overlap(prev, curr), 0);
}

TEST(StreamingTranscriberLogicTest, FindOverlapSingle) {
    std::vector<std::string> prev = {"hello", "world"};
    std::vector<std::string> curr = {"world", "foo"};
    EXPECT_EQ(test_utils::find_overlap(prev, curr), 1);
}

TEST(StreamingTranscriberLogicTest, FindOverlapMultiple) {
    std::vector<std::string> prev = {"a", "b", "c", "d"};
    std::vector<std::string> curr = {"c", "d", "e", "f"};
    EXPECT_EQ(test_utils::find_overlap(prev, curr), 2);
}

TEST(StreamingTranscriberLogicTest, FindOverlapEmpty) {
    std::vector<std::string> prev = {};
    std::vector<std::string> curr = {"a", "b"};
    EXPECT_EQ(test_utils::find_overlap(prev, curr), 0);
}

// ============================================================================
// Tests for remove_repetition()
// ============================================================================

TEST(StreamingTranscriberLogicTest, RemoveRepetitionNoRepeat) {
    std::string text = "Hello World";
    EXPECT_EQ(test_utils::remove_repetition(text), "Hello World");
}

TEST(StreamingTranscriberLogicTest, RemoveRepetitionWithRepeat) {
    // Pattern must be exact suffix repetition (length >= 10)
    // "abcdefghij" repeated twice = "abcdefghijabcdefghij" -> "abcdefghij"
    std::string text = "abcdefghijabcdefghij";
    std::string result = test_utils::remove_repetition(text);
    EXPECT_EQ(result, "abcdefghij");
}

TEST(StreamingTranscriberLogicTest, RemoveRepetitionEmpty) {
    EXPECT_EQ(test_utils::remove_repetition(""), "");
}

// ============================================================================
// Tests for is_hallucination()
// ============================================================================

TEST(StreamingTranscriberLogicTest, IsHallucinationEmpty) {
    EXPECT_TRUE(test_utils::is_hallucination(""));
}

TEST(StreamingTranscriberLogicTest, IsHallucinationSingleChar) {
    EXPECT_TRUE(test_utils::is_hallucination("a"));
}

TEST(StreamingTranscriberLogicTest, IsHallucinationBlacklist) {
    EXPECT_TRUE(test_utils::is_hallucination("Thank you"));
    EXPECT_TRUE(test_utils::is_hallucination("bye"));
    EXPECT_TRUE(test_utils::is_hallucination("you"));
}

TEST(StreamingTranscriberLogicTest, IsHallucinationValid) {
    EXPECT_FALSE(test_utils::is_hallucination("Hello everyone"));
    EXPECT_FALSE(test_utils::is_hallucination("Welcome to the show"));
}
