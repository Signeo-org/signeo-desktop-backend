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
auto ConfigFile::load(const std::string& path) -> bool {
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto ConfigFile::get(const std::string& key, const std::string& default_value) const -> std::string {
    auto iter = values_.find(key);
    return iter != values_.end() ? iter->second : default_value;
}

auto ConfigFile::get_int(const std::string& key, int default_value) const -> int {
    auto iter = values_.find(key);
    if (iter == values_.end()) {
        return default_value;
    }
    try {
        return std::stoi(iter->second);
    } catch (...) {
        return default_value;
    }
}

auto ConfigFile::get_float(const std::string& key, float default_value) const -> float {
    auto iter = values_.find(key);
    if (iter == values_.end()) {
        return default_value;
    }
    try {
        return std::stof(iter->second);
    } catch (...) {
        return default_value;
    }
}

auto ConfigFile::get_bool(const std::string& key, bool default_value) const -> bool {
    auto iter = values_.find(key);
    if (iter == values_.end()) {
        return default_value;
    }

    std::string val = iter->second;
    std::ranges::transform(val, val.begin(), [](unsigned char chr) { return std::tolower(chr); });

    return val == "true" || val == "yes" || val == "1" || val == "on";
}

auto ConfigFile::get_default_path() -> std::string {
    return "config.ini";
}

auto ConfigFile::trim(const std::string& str) -> std::string {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

auto ConfigFile::get_home_dir() -> std::string {
#ifdef _WIN32
    char* buf = nullptr;
    size_t buf_size = 0;
    if (_dupenv_s(&buf, &buf_size, "USERPROFILE") == 0 && buf != nullptr) {
        std::string home(buf);
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
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

auto ConfigFile::has(const std::string& key) const -> bool {
    return values_.contains(key);
}

auto ConfigFile::is_loaded() const -> bool {
    return loaded_;
}

}  // namespace config
