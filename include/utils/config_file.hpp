#pragma once

/**
 * @file config_file.hpp
 * @brief Simple INI-style configuration file parser
 */

#include <spdlog/spdlog.h>

#include <map>
#include <string>

namespace config {

/**
 * @brief Simple INI-style configuration file parser
 *
 * Supports:
 *   key = value
 *   key=value
 *   # comments
 *   ; comments
 */
class ConfigFile {
public:
    ConfigFile() = default;

    /**
     * @brief Load configuration from file
     * @param path Path to config file
     * @return true if loaded successfully, false if file not found or error
     */
    auto load(const std::string& path) -> bool;

    /**
     * @brief Get string value
     */
    [[nodiscard]] auto get(const std::string& key, const std::string& default_value = "") const -> std::string;

    /**
     * @brief Get integer value
     */
    [[nodiscard]] auto get_int(const std::string& key, int default_value = 0) const -> int;

    /**
     * @brief Get float value
     */
    [[nodiscard]] auto get_float(const std::string& key, float default_value = 0.0F) const -> float;

    /**
     * @brief Get boolean value (supports: true/false, yes/no, 1/0)
     */
    [[nodiscard]] auto get_bool(const std::string& key, bool default_value = false) const -> bool;

    /**
     * @brief Check if a key exists
     */
    /**
     * @brief Check if a key exists
     */
    [[nodiscard]] auto has(const std::string& key) const -> bool;

    /**
     * @brief Check if config was loaded
     */
    [[nodiscard]] auto is_loaded() const -> bool;

    /**
     * @brief Get default config file path
     */
    static auto get_default_path() -> std::string;

private:
    static auto trim(const std::string& str) -> std::string;

    static auto get_home_dir() -> std::string;

    std::map<std::string, std::string> values_;
    bool loaded_ = false;
};

}  // namespace config
