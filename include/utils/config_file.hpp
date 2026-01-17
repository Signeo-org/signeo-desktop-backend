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
    bool load(const std::string& path);

    /**
     * @brief Get string value
     */
    std::string get(const std::string& key, const std::string& default_value = "") const;

    /**
     * @brief Get integer value
     */
    int get_int(const std::string& key, int default_value = 0) const;

    /**
     * @brief Get float value
     */
    float get_float(const std::string& key, float default_value = 0.0f) const;

    /**
     * @brief Get boolean value (supports: true/false, yes/no, 1/0)
     */
    bool get_bool(const std::string& key, bool default_value = false) const;

    /**
     * @brief Check if a key exists
     */
    /**
     * @brief Check if a key exists
     */
    bool has(const std::string& key) const;

    /**
     * @brief Check if config was loaded
     */
    bool is_loaded() const;

    /**
     * @brief Get default config file path
     */
    static std::string get_default_path();

private:
    static std::string trim(const std::string& str);

    static std::string get_home_dir();

    std::map<std::string, std::string> values_;
    bool loaded_ = false;
};

} // namespace config
