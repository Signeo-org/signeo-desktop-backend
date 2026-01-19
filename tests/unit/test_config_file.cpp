#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "utils/config_file.hpp"

class ConfigFileTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir;
    std::string test_file;

    void SetUp() override {
        // Unique temp dir
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        temp_dir = std::filesystem::temp_directory_path() / ("signeo_tests_" + std::to_string(now));
        std::filesystem::create_directories(temp_dir);
        test_file = (temp_dir / "test_config.ini").string();
    }

    void TearDown() override {
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
    }

    void create_config(const std::string& content) {
        std::ofstream f(test_file);
        f << content;
        f.close();
    }
};

TEST_F(ConfigFileTest, LoadAndParseBasic) {
    create_config(R"(
        # This is a comment
        model = models/test.bin
        threads = 8
        use_gpu = true
        pi = 3.14159
        invalid_int = abc
    )");

    config::ConfigFile cfg;
    ASSERT_TRUE(cfg.load(test_file));

    // String
    EXPECT_EQ(cfg.get("model", "default"), "models/test.bin");
    EXPECT_EQ(cfg.get("missing", "default"), "default");

    // Int
    EXPECT_EQ(cfg.get_int("threads", 4), 8);
    EXPECT_EQ(cfg.get_int("missing_int", 10), 10);
    EXPECT_EQ(cfg.get_int("invalid_int", 99), 99);  // Should fallback

    // Bool
    EXPECT_TRUE(cfg.get_bool("use_gpu", false));
    EXPECT_FALSE(cfg.get_bool("missing_bool", false));

    // Float
    EXPECT_FLOAT_EQ(cfg.get_float("pi", 0.0f), 3.14159f);
    EXPECT_FLOAT_EQ(cfg.get_float("missing_float", 1.5f), 1.5f);
}

TEST_F(ConfigFileTest, EmptyLinesAndComments) {
    create_config(R"(

        val = 1
        
        # comment line
        key = value   
    )");

    config::ConfigFile cfg;
    ASSERT_TRUE(cfg.load(test_file));
    EXPECT_EQ(cfg.get_int("val", 0), 1);
    EXPECT_EQ(cfg.get("key"), "value");  // Trimming check
}
