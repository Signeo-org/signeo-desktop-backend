#include "utils/config_file.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef _WIN32
// #include <windows.h>
// #include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace config {

// ConfigFile implementation is already in namespace config, but needs proper class scope
bool ConfigFile::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    spdlog::debug("Loading config from: {}", path);

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;

        // Trim whitespace
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Find '=' separator
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            spdlog::warn("Config line {}: Invalid format (missing '='): {}", line_num, line);
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        // Remove quotes if present
        if (value.size() >= 2) {
            if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }

        values_[key] = value;
        spdlog::debug("Config: {} = {}", key, value);
    }

    loaded_ = true;
    return true;
}

std::string ConfigFile::get(const std::string& key, const std::string& default_value) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : default_value;
}

int ConfigFile::get_int(const std::string& key, int default_value) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

float ConfigFile::get_float(const std::string& key, float default_value) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }
    try {
        return std::stof(it->second);
    } catch (...) {
        return default_value;
    }
}

bool ConfigFile::get_bool(const std::string& key, bool default_value) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }

    std::string val = it->second;
    std::ranges::transform(val, val.begin(), [](unsigned char c) { return std::tolower(c); });

    return val == "true" || val == "yes" || val == "1" || val == "on";
}

std::string ConfigFile::get_default_path() { return "config.ini"; }

std::string ConfigFile::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ConfigFile::get_home_dir() {
#ifdef _WIN32
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, "USERPROFILE") == 0 && buf != nullptr) {
        std::string home(buf);
        free(buf);
        return home;
    }
    return "";
#else
    const char* home = std::getenv("HOME");
    if (home)
        return std::string(home);

    struct passwd* pw = getpwuid(getuid());
    return pw ? std::string(pw->pw_dir) : "";
#endif
}

bool ConfigFile::has(const std::string& key) const { return values_.find(key) != values_.end(); }

bool ConfigFile::is_loaded() const { return loaded_; }

} // namespace config
