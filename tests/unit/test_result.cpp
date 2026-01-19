#include <gtest/gtest.h>

#include <string>

#include "core/result.hpp"
#include "output/logging.hpp"

using namespace core;

// ============================================================================
// Basic Result Tests
// ============================================================================

TEST(ResultTest, SuccessValue) {
    Result<int> result(42);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(*result, 42);  // operator*
}

TEST(ResultTest, ErrorValue) {
    Result<int> result(std::unexpected("error message"));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "error message");
}

TEST(ResultTest, StatusSuccess) {
    Status status;  // Default constructed = success

    EXPECT_TRUE(status.has_value());
}

TEST(ResultTest, StatusError) {
    Status status = make_error("operation failed");

    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error(), "operation failed");
}

// ============================================================================
// Manual Error Propagation Tests (TRY macro not tested due to MSVC limitations)
// ============================================================================

TEST(ResultTest, ManualErrorPropagation) {
    auto func = []() -> Result<int> {
        auto inner_result = Result<int>(std::unexpected("fail"));
        if (!inner_result) {
            return std::unexpected(inner_result.error());
        }
        return *inner_result + 1;
    };

    auto result = func();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "fail");
}

TEST(ResultTest, ManualSuccessPropagation) {
    auto func = []() -> Result<int> {
        auto inner_result = Result<int>(10);
        if (!inner_result) {
            return std::unexpected(inner_result.error());
        }
        return *inner_result + 5;
    };

    auto result = func();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 15);
}

// ============================================================================
// Error Propagation Chain Tests
// ============================================================================

TEST(ResultTest, ErrorPropagationChain) {
    auto level3 = []() -> Result<int> { return std::unexpected("level 3 error"); };

    auto level2 = [&]() -> Result<int> {
        auto res = level3();
        if (!res)
            return std::unexpected(res.error());
        return *res * 2;
    };

    auto level1 = [&]() -> Result<int> {
        auto res = level2();
        if (!res)
            return std::unexpected(res.error());
        return *res + 10;
    };

    auto result = level1();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), "level 3 error");
}

TEST(ResultTest, SuccessPropagationChain) {
    auto level3 = []() -> Result<int> { return 5; };

    auto level2 = [&]() -> Result<int> {
        auto res = level3();
        if (!res)
            return std::unexpected(res.error());
        return *res * 2;
    };

    auto level1 = [&]() -> Result<int> {
        auto res = level2();
        if (!res)
            return std::unexpected(res.error());
        return *res + 10;
    };

    auto result = level1();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 20);  // (5*2)+10
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST(ResultTest, MoveSemantics) {
    Result<std::string> result1("hello");

    Result<std::string> result2 = std::move(result1);

    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, "hello");
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(ResultTest, MakeErrorHelper) {
    Status error_status = make_error("test error");

    EXPECT_FALSE(error_status.has_value());
    EXPECT_EQ(error_status.error(), "test error");
}

TEST(ResultTest, LogErrorHelper) {
    // Ensure spdlog is initialized
    try {
        if (!spdlog::default_logger()) {
            core::init_logging("", false);
        }
    } catch (...) {
        core::init_logging("", false);
    }

    // log_error returns an unexpected
    Status error_status = log_error("logged error");

    EXPECT_FALSE(error_status.has_value());
    EXPECT_EQ(error_status.error(), "logged error");
}

// ============================================================================
// Real-World Usage Pattern Tests
// ============================================================================

TEST(ResultTest, FileOperationPattern) {
    auto open_file = [](const std::string& path) -> Result<int> {
        if (path.empty()) {
            return std::unexpected("empty path");
        }
        return 123;  // Mock file descriptor
    };

    auto read_file = [](int fd) -> Result<std::string> {
        if (fd < 0) {
            return std::unexpected("invalid fd");
        }
        return "file contents";
    };

    auto process_file = [&](const std::string& path) -> Result<std::string> {
        auto fd_res = open_file(path);
        if (!fd_res)
            return std::unexpected(fd_res.error());

        auto contents_res = read_file(*fd_res);
        if (!contents_res)
            return std::unexpected(contents_res.error());

        return *contents_res + " processed";
    };

    // Success case
    auto result1 = process_file("valid.txt");
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, "file contents processed");

    // Error case
    auto result2 = process_file("");
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), "empty path");
}
