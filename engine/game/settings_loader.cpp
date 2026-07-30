#include "settings_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace resf2::game {

static std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string attr(const std::string& tag, const char* key) {
    const std::string needle = std::string(key) + "=\"";
    auto p = tag.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    auto e = tag.find('"', p);
    if (e == std::string::npos) return {};
    return tag.substr(p, e - p);
}

std::string normalize_localization_path(const std::string& value) {
    fs::path p(value);
    if (p.has_filename()) return p.stem().string();
    return value;
}

UserSettingsLanguage load_user_settings_language(const std::string& asset_root) {
    for (const auto& base : {fs::path(asset_root) / "assets" / "files" / "assets",
                             fs::path(asset_root) / "assets" / "files",
                             fs::path(asset_root) / "assets",
                             fs::path(asset_root)}) {
        const fs::path p = base / "userSettings.xml";
        if (!fs::exists(p)) continue;
        const std::string xml = read_text(p);
        auto lang_pos = xml.find("<Language ");
        if (lang_pos == std::string::npos) return {};
        auto end = xml.find('>', lang_pos);
        if (end == std::string::npos) return {};
        const std::string tag = xml.substr(lang_pos, end - lang_pos);
        UserSettingsLanguage out;
        out.value = attr(tag, "Value");
        const auto num = attr(tag, "Number");
        if (!num.empty()) out.number = std::atoi(num.c_str());
        return out;
    }
    return {};
}

} // namespace resf2::game
