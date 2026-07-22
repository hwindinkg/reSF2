// engine/game/save.cpp
//
// JSON-based save/load implementation for reSF2.
// Writes and reads JSON manually (matching the existing codebase style).
// Handles errors gracefully: returns false + logs on corrupt/missing data.

#define _CRT_SECURE_NO_WARNINGS

#include "save.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace resf2::save {

namespace fs = std::filesystem;

// ============================================================
// Helpers
// ============================================================

// Write a JSON-escaped string value.
static void write_json_string(std::ostream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:   os << c;      break;
        }
    }
    os << '"';
}

// Write a JSON array of strings: ["a", "b", "c"]
static void write_json_string_array(std::ostream& os, const std::vector<std::string>& arr) {
    os << '[';
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i) os << ',';
        os << '\n';
        os << "    ";
        write_json_string(os, arr[i]);
    }
    if (!arr.empty()) os << '\n';
    os << "  ]";
}

// Find a JSON string value by key. Returns the unquoted value on success,
// or empty string if not found.
static std::string find_json_string(const std::string& json, const std::string& key) {
    auto pos = json.find('"' + key + '"');
    if (pos == std::string::npos) return {};
    auto colon = json.find(':', pos + key.size() + 2);
    if (colon == std::string::npos) return {};
    // Scan past whitespace to find the opening quote
    auto val_start = json.find_first_of('"', colon);
    if (val_start == std::string::npos) return {};
    auto val_end = json.find('"', val_start + 1);
    if (val_end == std::string::npos) return {};
    // Unescape simple escapes
    std::string raw = json.substr(val_start + 1, val_end - val_start - 1);
    std::string result;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            switch (raw[i + 1]) {
                case '"': result += '"'; ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'n': result += '\n'; ++i; break;
                case 'r': result += '\r'; ++i; break;
                case 't': result += '\t'; ++i; break;
                default: result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}

// Find a JSON integer value by key. Returns 0 if not found.
static int find_json_int(const std::string& json, const std::string& key) {
    auto pos = json.find('"' + key + '"');
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos + key.size() + 2);
    if (colon == std::string::npos) return 0;
    auto val_start = json.find_first_of("-0123456789", colon);
    if (val_start == std::string::npos) return 0;
    auto val_end = json.find_first_not_of("0123456789", val_start);
    // Handle negative numbers
    if (val_start > colon && json[val_start] == '-') {
        val_start = json.find_first_of("-0123456789", colon);
    }
    std::string num_str = json.substr(val_start, val_end - val_start);
    if (num_str.empty()) return 0;
    try {
        return std::stoi(num_str);
    } catch (...) {
        return 0;
    }
}

// Parse a JSON string array: ["a", "b", "c"]
// Returns the list of unquoted values.
static std::vector<std::string> find_json_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    auto pos = json.find('"' + key + '"');
    if (pos == std::string::npos) return result;
    auto colon = json.find(':', pos + key.size() + 2);
    if (colon == std::string::npos) return result;
    auto arr_start = json.find('[', colon);
    if (arr_start == std::string::npos) return result;
    auto arr_end = json.find(']', arr_start);
    if (arr_end == std::string::npos) return result;
    auto arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
    size_t q = 0;
    while ((q = arr.find('"', q)) != std::string::npos) {
        auto q2 = arr.find('"', q + 1);
        if (q2 == std::string::npos) break;
        result.push_back(arr.substr(q + 1, q2 - q - 1));
        q = q2 + 1;
    }
    return result;
}

// ============================================================
// SaveManager implementation
// ============================================================

SaveManager::SaveManager() {
    save_path_ = default_save_path();
}

void SaveManager::set_save_path(const std::string& path) {
    save_path_ = path;
}

std::string SaveManager::get_save_path() const {
    return save_path_;
}

bool SaveManager::save(const SaveData& data, const std::string& path) {
    try {
        std::string out_path = path.empty() ? save_path_ : path;
        std::ofstream f(out_path);
        if (!f) {
            std::printf("[save] ERROR: could not open %s for writing\n", out_path.c_str());
            return false;
        }
        write_json(f, data);
        std::printf("[save] saved to %s (v%d, %d gold, %d/%d w/l, %zu levels)\n",
                    out_path.c_str(), data.version, data.currency,
                    data.wins, data.losses, data.completed_levels.size());
        return true;
    } catch (const std::exception& e) {
        std::printf("[save] ERROR: exception during save: %s\n", e.what());
        return false;
    }
}

bool SaveManager::load(const std::string& path, SaveData& data) {
    try {
        std::string in_path = path.empty() ? save_path_ : path;
        if (!fs::exists(in_path)) {
            std::printf("[save] no save file at %s\n", in_path.c_str());
            return false;
        }
        std::ifstream f(in_path);
        if (!f) {
            std::printf("[save] ERROR: could not open %s for reading\n", in_path.c_str());
            return false;
        }
        std::string json((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        if (!parse_json(json, data)) {
            std::printf("[save] ERROR: failed to parse save file %s\n", in_path.c_str());
            return false;
        }
        std::printf("[save] loaded from %s (v%d, %d gold, %d/%d w/l, %zu levels)\n",
                    in_path.c_str(), data.version, data.currency,
                    data.wins, data.losses, data.completed_levels.size());
        return true;
    } catch (const std::exception& e) {
        std::printf("[save] ERROR: exception during load: %s\n", e.what());
        return false;
    }
}

bool SaveManager::load(SaveData& data) {
    return load("", data);
}

bool SaveManager::save_exists(const std::string& path) {
    std::string check_path = path.empty() ? save_path_ : path;
    return fs::exists(check_path);
}

// ============================================================
// JSON serialization
// ============================================================

void SaveManager::write_json(std::ostream& os, const SaveData& data) {
    os << "{\n";
    os << "  \"version\": " << data.version << ",\n";
    os << "  \"currency\": " << data.currency << ",\n";
    os << "  \"level\": " << data.level << ",\n";
    os << "  \"wins\": " << data.wins << ",\n";
    os << "  \"losses\": " << data.losses << ",\n";
    os << "  \"current_level\": ";
    write_json_string(os, data.current_level);
    os << ",\n";
    os << "  \"completed_levels\": ";
    write_json_string_array(os, data.completed_levels);
    os << ",\n";
    os << "  \"owned_items\": ";
    write_json_string_array(os, data.owned_items);
    os << ",\n";
    os << "  \"equipped_weapon\": ";
    write_json_string(os, data.equipped_weapon);
    os << ",\n";
    os << "  \"equipped_armor\": ";
    write_json_string(os, data.equipped_armor);
    os << ",\n";
    os << "  \"equipped_helmet\": ";
    write_json_string(os, data.equipped_helmet);
    os << ",\n";
    os << "  \"equipped_ranged\": ";
    write_json_string(os, data.equipped_ranged);
    os << ",\n";
    os << "  \"equipped_magic\": ";
    write_json_string(os, data.equipped_magic);
    os << "\n";
    os << "}\n";
}

bool SaveManager::parse_json(const std::string& json, SaveData& data) {
    if (json.empty()) {
        std::printf("[save] empty JSON\n");
        return false;
    }
    // Minimal structural validation
    if (json.find('{') == std::string::npos || json.find('}') == std::string::npos) {
        std::printf("[save] invalid JSON: missing braces\n");
        return false;
    }

    // Parse version (optional — default 1 if missing)
    int version = find_json_int(json, "version");
    if (version < 1 || version > 1) {
        std::printf("[save] unsupported save version: %d\n", version);
        return false;
    }
    data.version = version;

    // Parse simple fields
    data.currency = find_json_int(json, "currency");
    int level = find_json_int(json, "level");
    if (level > 0) data.level = level;
    data.wins = find_json_int(json, "wins");
    data.losses = find_json_int(json, "losses");

    // Parse string fields
    data.current_level = find_json_string(json, "current_level");

    // Parse arrays
    data.completed_levels = find_json_string_array(json, "completed_levels");
    data.owned_items      = find_json_string_array(json, "owned_items");

    // Parse equipment fields
    data.equipped_weapon = find_json_string(json, "equipped_weapon");
    data.equipped_armor  = find_json_string(json, "equipped_armor");
    data.equipped_helmet = find_json_string(json, "equipped_helmet");
    data.equipped_ranged = find_json_string(json, "equipped_ranged");
    data.equipped_magic  = find_json_string(json, "equipped_magic");

    return true;
}

// ============================================================
// Default save path resolution
// ============================================================

std::string SaveManager::default_save_path() {
    // Prefer platform-specific app data directory
    const char* appdata = nullptr;

#ifdef _WIN32
    appdata = std::getenv("APPDATA");
    if (appdata) {
        fs::path dir = fs::path(appdata) / "reSF2";
        try {
            fs::create_directories(dir);
        } catch (...) {}
        return (dir / "save.json").string();
    }
    // Fallback: LOCALAPPDATA
    appdata = std::getenv("LOCALAPPDATA");
    if (appdata) {
        fs::path dir = fs::path(appdata) / "reSF2";
        try {
            fs::create_directories(dir);
        } catch (...) {}
        return (dir / "save.json").string();
    }
#elif defined(__linux__) || defined(__APPLE__)
    appdata = std::getenv("XDG_DATA_HOME");
    if (appdata) {
        fs::path dir = fs::path(appdata) / "reSF2";
        try { fs::create_directories(dir); } catch (...) {}
        return (dir / "save.json").string();
    }
    const char* home = std::getenv("HOME");
    if (home) {
        fs::path dir = fs::path(home) / ".local" / "share" / "reSF2";
        try { fs::create_directories(dir); } catch (...) {}
        return (dir / "save.json").string();
    }
#endif

    // Last resort: OS temp directory (backward compatible with old location)
    try {
        auto temp = fs::temp_directory_path() / "resf2_save.json";
        return temp.string();
    } catch (...) {
        return "resf2_save.json";
    }
}

}  // namespace resf2::save
