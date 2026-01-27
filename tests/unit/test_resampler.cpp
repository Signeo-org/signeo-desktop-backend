#include <gtest/gtest.h>

#include "audio/resampler.hpp"
#include "output/log_output.hpp"

// Fixture that ensures spdlog is initialized
class ResamplerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reinitialize spdlog in case it was shutdown by previous tests
        try {
            if (!spdlog::default_logger()) {
                core::init_logging("", false);
            }
        } catch (...) {
            core::init_logging("", false);
        }
    }
};

TEST_F(ResamplerTest, Initialization) {
    // Valid
    auto res = audio::AudioResampler::create(audio::AudioResampler::Config(16000, 48000));
    EXPECT_TRUE(res.has_value());

    // Invalid (negative rate) should return error, not throw
    auto res_invalid = audio::AudioResampler::create(audio::AudioResampler::Config(-1, 16000));
    EXPECT_FALSE(res_invalid.has_value());
    // Optional: check error message if needed
}

TEST_F(ResamplerTest, ProcessNormal) {
    auto res = audio::AudioResampler::create(audio::AudioResampler::Config(16000, 32000));  // 1:2 ratio
    ASSERT_TRUE(res.has_value());
    auto& resampler = *res.value();

    std::vector<float> input(160, 1.0f);  // 10ms
    auto output = resampler.process(input);

    // Output should be approx 320 samples
    EXPECT_NEAR(output.size(), 320, 1);
    EXPECT_FALSE(output.empty());
}

TEST_F(ResamplerTest, ProcessEmpty) {
    auto res = audio::AudioResampler::create(audio::AudioResampler::Config(16000, 16000));
    ASSERT_TRUE(res.has_value());
    auto& resampler = *res.value();

    std::vector<float> input;
    auto output = resampler.process(input);
    EXPECT_TRUE(output.empty());
}

TEST_F(ResamplerTest, Reset) {
    auto res = audio::AudioResampler::create(audio::AudioResampler::Config(16000, 16000));
    ASSERT_TRUE(res.has_value());
    auto& resampler = *res.value();

    // Just verify it doesn't crash
    resampler.reset();
}

TEST_F(ResamplerTest, ExpectedSize) {
    auto res = audio::AudioResampler::create(audio::AudioResampler::Config(16000, 32000));
    ASSERT_TRUE(res.has_value());
    auto& resampler = *res.value();

    size_t expected = resampler.expected_output_size(100);
    EXPECT_GE(expected, 200);
}
