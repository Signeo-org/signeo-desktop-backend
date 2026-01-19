#include <gtest/gtest.h>

#include "utils/wer_calculator.hpp"

using namespace utils;

// ============================================================================
// WER Calculator Tests
// ============================================================================

TEST(WerCalculatorTest, PerfectMatch) {
    auto result = WerCalculator::calculate("hello world", "hello world");

    EXPECT_DOUBLE_EQ(result.wer, 0.0);
    EXPECT_EQ(result.substitutions, 0);
    EXPECT_EQ(result.deletions, 0);
    EXPECT_EQ(result.insertions, 0);
    EXPECT_EQ(result.reference_words, 2);
}

TEST(WerCalculatorTest, CompletelyDifferent) {
    auto result = WerCalculator::calculate("hello world", "foo bar");

    EXPECT_DOUBLE_EQ(result.wer, 1.0);  // 2 substitutions / 2 words = 100%
    EXPECT_EQ(result.substitutions, 2);
    EXPECT_EQ(result.deletions, 0);
    EXPECT_EQ(result.insertions, 0);
}

TEST(WerCalculatorTest, OneSubstitution) {
    auto result = WerCalculator::calculate("the quick brown fox", "the slow brown fox");

    EXPECT_DOUBLE_EQ(result.wer, 0.25);  // 1 sub / 4 words = 25%
    EXPECT_EQ(result.substitutions, 1);
    EXPECT_EQ(result.deletions, 0);
    EXPECT_EQ(result.insertions, 0);
}

TEST(WerCalculatorTest, OneDeletion) {
    auto result = WerCalculator::calculate("the quick brown fox", "the brown fox");

    EXPECT_DOUBLE_EQ(result.wer, 0.25);  // 1 del / 4 words = 25%
    EXPECT_EQ(result.substitutions, 0);
    EXPECT_EQ(result.deletions, 1);
    EXPECT_EQ(result.insertions, 0);
}

TEST(WerCalculatorTest, OneInsertion) {
    auto result = WerCalculator::calculate("the brown fox", "the quick brown fox");

    EXPECT_DOUBLE_EQ(result.wer, 1.0 / 3.0);  // 1 ins / 3 words ≈ 33%
    EXPECT_EQ(result.substitutions, 0);
    EXPECT_EQ(result.deletions, 0);
    EXPECT_EQ(result.insertions, 1);
}

TEST(WerCalculatorTest, MixedErrors) {
    // Reference: "the quick brown fox jumps over the lazy dog"
    // Hypothesis: "a quick brown cat jumps over lazy dogs"
    // Changes: "the"→"a" (sub), "fox"→"cat" (sub), "the" deleted, "dog"→"dogs" (sub)
    auto result = WerCalculator::calculate("the quick brown fox jumps over the lazy dog",
                                           "a quick brown cat jumps over lazy dogs");

    EXPECT_GT(result.wer, 0.0);
    EXPECT_LT(result.wer, 1.0);
    EXPECT_EQ(result.total_errors(), result.substitutions + result.deletions + result.insertions);
}

TEST(WerCalculatorTest, EmptyReference) {
    auto result = WerCalculator::calculate("", "hello world");

    // All hypothesis words are insertions, WER is capped at 1.0 for empty reference
    EXPECT_DOUBLE_EQ(result.wer, 1.0);
    EXPECT_EQ(result.insertions, 2);
}

TEST(WerCalculatorTest, EmptyHypothesis) {
    auto result = WerCalculator::calculate("hello world", "");

    EXPECT_DOUBLE_EQ(result.wer, 1.0);  // 2 deletions / 2 words = 100%
    EXPECT_EQ(result.deletions, 2);
}

TEST(WerCalculatorTest, BothEmpty) {
    auto result = WerCalculator::calculate("", "");

    EXPECT_DOUBLE_EQ(result.wer, 0.0);
    EXPECT_EQ(result.total_errors(), 0);
}

TEST(WerCalculatorTest, CaseInsensitive) {
    auto result = WerCalculator::calculate("Hello World", "hello world");

    EXPECT_DOUBLE_EQ(result.wer, 0.0);
}

TEST(WerCalculatorTest, PunctuationIgnored) {
    auto result = WerCalculator::calculate("Hello, world!", "hello world");

    EXPECT_DOUBLE_EQ(result.wer, 0.0);
}

TEST(WerCalculatorTest, ExtraWhitespace) {
    auto result = WerCalculator::calculate("  hello   world  ", "hello world");

    EXPECT_DOUBLE_EQ(result.wer, 0.0);
}

// ============================================================================
// Normalization Tests
// ============================================================================

TEST(WerCalculatorTest, Normalize) {
    EXPECT_EQ(WerCalculator::normalize("Hello, World!"), "hello world");
    EXPECT_EQ(WerCalculator::normalize("  extra   spaces  "), "extra spaces");
    EXPECT_EQ(WerCalculator::normalize("UPPERCASE"), "uppercase");
    EXPECT_EQ(WerCalculator::normalize("test123"), "test123");
}

// ============================================================================
// Tokenization Tests
// ============================================================================

TEST(WerCalculatorTest, Tokenize) {
    auto tokens = WerCalculator::tokenize("hello world test");

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "test");
}

TEST(WerCalculatorTest, TokenizeEmpty) {
    auto tokens = WerCalculator::tokenize("");
    EXPECT_TRUE(tokens.empty());
}

// ============================================================================
// Real-World Examples
// ============================================================================

TEST(WerCalculatorTest, RealisticTranscription) {
    // Simulating a typical STT output vs reference
    std::string reference = "The quick brown fox jumps over the lazy dog";
    std::string hypothesis = "The quick brown fox jumps over the lazy dog";  // Perfect

    auto result = WerCalculator::calculate(reference, hypothesis);
    EXPECT_DOUBLE_EQ(result.wer, 0.0);
}

TEST(WerCalculatorTest, TypicalSttErrors) {
    // Common STT errors: homophones, similar sounds
    std::string reference = "I would like to eat today";
    std::string hypothesis = "I would like to eat today";  // Perfect match

    auto result = WerCalculator::calculate(reference, hypothesis);
    EXPECT_DOUBLE_EQ(result.wer, 0.0);
    EXPECT_EQ(result.wer_percentage(), 0.0);
}
