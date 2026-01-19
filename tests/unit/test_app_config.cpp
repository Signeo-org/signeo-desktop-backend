#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "config/app_config.hpp"

// Helper to create safe argv
std::pair<int, std::vector<char*>> create_argv(const std::vector<std::string>& args) {
    static std::vector<std::string> kept_args;  // Keep strings alive
    kept_args = args;

    static std::vector<char*> argv;
    argv.clear();
    for (auto& arg : kept_args) {
        argv.push_back(const_cast<char*>(arg.data()));  // Safe because we don't modify
    }
    return {static_cast<int>(argv.size()), argv};
}

TEST(AppConfigTest, ParseDefaults) {
    std::vector<std::string> args = {"app"};
    auto [argc, argv] = create_argv(args);

    AppConfig config = AppConfig::parse(argc, argv.data());

    EXPECT_EQ(config.device_index, -1);
    EXPECT_EQ(config.n_threads, 4);
    EXPECT_FALSE(config.verbose);
}

TEST(AppConfigTest, ParseCLIArgs) {
    std::vector<std::string> args = {"app", "--device", "2", "--threads", "8", "--verbose"};
    auto [argc, argv] = create_argv(args);

    AppConfig config = AppConfig::parse(argc, argv.data());

    EXPECT_EQ(config.device_index, 2);
    EXPECT_EQ(config.n_threads, 8);
    EXPECT_TRUE(config.verbose);
}

TEST(AppConfigTest, ParseVADParams) {
    std::vector<std::string> args = {"app", "--vad-threshold", "0.75", "--vad-hangover", "10"};
    auto [argc, argv] = create_argv(args);

    AppConfig config = AppConfig::parse(argc, argv.data());

    EXPECT_FLOAT_EQ(config.vad_threshold, 0.75f);
    EXPECT_EQ(config.vad_hangover_frames, 10);
}

TEST(AppConfigTest, ParseSTTParams) {
    std::vector<std::string> args = {"app", "--stt-step", "3000", "--no-stt-dedup"};
    auto [argc, argv] = create_argv(args);

    AppConfig config = AppConfig::parse(argc, argv.data());

    EXPECT_EQ(config.stt_step_ms, 3000);
    EXPECT_FALSE(config.stt_token_dedup);  // Should be false due to --no-stt-dedup
}
