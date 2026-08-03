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

#include "engine/format/xml_doc.hpp"

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

void SaveManager::set_asset_root(const std::string& root) {
    asset_root_ = root;
}

void SaveManager::set_save_path(const std::string& path) {
    save_path_ = path;
}

std::string SaveManager::get_save_path() const {
    return save_path_;
}

bool SaveManager::save(const SaveData& data, const std::string& path) {
    try {
        // [ORIGINAL] If asset_root is set, save in XML format to user.xml
        // (matching the original game's save format from usersDefault.xml).
        std::string out_path = path.empty() ? xml_save_path() : path;
        bool use_xml = !asset_root_.empty() && path.empty();
        if (path.empty() && asset_root_.empty()) {
            out_path = save_path_;  // fall back to JSON path
            use_xml = false;
        }

        // [Wave 8] Original save convention (LIVE_BOOT_TRACE 15.46/21.38):
        // before overwriting users.xml the previous save is copied to
        // users_backup.xml, so a corrupt write never loses the last good
        // state. The original also writes *.hash files; the hash algorithm
        // is not yet recovered (device offline) — HEURISTIC-TODO.
        // NOTE: must run BEFORE ofstream truncates the target.
        if (use_xml) {
            const std::string backup_path = xml_backup_path();
            if (!backup_path.empty() && fs::exists(out_path)) {
                std::error_code ec;
                fs::copy_file(out_path, backup_path, fs::copy_options::overwrite_existing, ec);
            }
        }

        std::ofstream f(out_path);
        if (!f) {
            std::printf("[save] ERROR: could not open %s for writing\n", out_path.c_str());
            return false;
        }
        if (use_xml) {
            write_xml(f, data);
        } else {
            write_json(f, data);
        }
        std::printf("[save] saved to %s (%s v%d, %d gold, %d/%d w/l, %zu levels)\n",
                    out_path.c_str(), use_xml ? "XML" : "JSON", data.version, data.currency,
                    data.wins, data.losses, data.completed_levels.size());
        return true;
    } catch (const std::exception& e) {
        std::printf("[save] ERROR: exception during save: %s\n", e.what());
        return false;
    }
}

bool SaveManager::load(const std::string& path, SaveData& data) {
    try {
        // If a specific path was given, use it directly
        if (!path.empty()) {
            return load_file(path, data);
        }

        // [ORIGINAL] Load order matches the original game:
        // 1. user.xml in assets (player's actual save)
        // 2. usersDefault.xml in assets (initial save on first start)
        // 3. resf2_save.json in app data / temp dir (legacy JSON fallback)
        std::string xml_path = xml_save_path();
        if (!xml_path.empty() && fs::exists(xml_path)) {
            std::printf("[save] loading from user.xml: %s\n", xml_path.c_str());
            if (load_file(xml_path, data)) {
                data.source_format = "xml";
                return true;
            }
        }

        std::string default_xml = xml_default_path();
        if (!default_xml.empty() && fs::exists(default_xml)) {
            std::printf("[save] loading from usersDefault.xml: %s\n", default_xml.c_str());
            if (load_file(default_xml, data)) {
                data.source_format = "xml";
                return true;
            }
        }

        // Legacy JSON fallback
        if (fs::exists(save_path_)) {
            std::printf("[save] loading from JSON: %s\n", save_path_.c_str());
            if (load_file(save_path_, data)) {
                data.source_format = "json";
                return true;
            }
        }

        std::printf("[save] no save file found\n");
        return false;
    } catch (const std::exception& e) {
        std::printf("[save] ERROR: exception during load: %s\n", e.what());
        return false;
    }
}

bool SaveManager::load_file(const std::string& in_path, SaveData& data) {
    try {
        if (!fs::exists(in_path)) {
            std::printf("[save] no save file at %s\n", in_path.c_str());
            return false;
        }
        std::ifstream f(in_path);
        if (!f) {
            std::printf("[save] ERROR: could not open %s for reading\n", in_path.c_str());
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

        // Detect format by content
        bool is_xml = content.find("<?xml") != std::string::npos ||
                      content.find("<Warrior") != std::string::npos ||
                      content.find("<Warriors") != std::string::npos;

        if (is_xml) {
            if (!parse_xml(content, data)) {
                std::printf("[save] ERROR: failed to parse XML save file %s\n", in_path.c_str());
                return false;
            }
        } else {
            if (!parse_json(content, data)) {
                std::printf("[save] ERROR: failed to parse JSON save file %s\n", in_path.c_str());
                return false;
            }
        }
        std::printf("[save] loaded from %s (%d gold, %d/%d w/l, %zu levels)\n",
                    in_path.c_str(), data.currency,
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

std::string SaveManager::xml_save_path() const {
    if (asset_root_.empty()) return "";
    // [ORIGINAL] user.xml is the player's save, written next to assets/
    auto p = fs::path(asset_root_) / "user.xml";
    return p.string();
}

std::string SaveManager::xml_backup_path() const {
    if (asset_root_.empty()) return "";
    // [ORIGINAL] users_backup.xml holds the previous save (same folder).
    auto p = fs::path(asset_root_) / "users_backup.xml";
    return p.string();
}

std::string SaveManager::xml_default_path() const {
    if (asset_root_.empty()) return "";
    // [ORIGINAL] usersDefault.xml is the initial save shipped with the game.
    // Try both the root and assets/ subdirectory.
    auto p1 = fs::path(asset_root_) / "usersDefault.xml";
    if (fs::exists(p1)) return p1.string();
    auto p2 = fs::path(asset_root_) / "assets" / "usersDefault.xml";
    if (fs::exists(p2)) return p2.string();
    return p1.string();  // return first candidate even if missing
}

// ============================================================
// XML serialization [ORIGINAL]
// ============================================================

namespace {

// Scan a tag region for every name="value" pair, in file order. The region
// must START AFTER the tag name (e.g. `ID="1" IsFake="True" ...`), so the tag
// name itself is never captured as an attribute.
void parse_attrs_into(const std::string& region,
                      std::vector<std::pair<std::string, std::string>>& out) {
    size_t pos = 0;
    while (pos < region.size()) {
        size_t start = std::string::npos;
        for (size_t i = pos; i < region.size(); ++i) {
            const char c = region[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
                start = i;
                break;
            }
        }
        if (start == std::string::npos) break;
        size_t name_end = start;
        while (name_end < region.size()) {
            const char c = region[name_end];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_')
                ++name_end;
            else
                break;
        }
        const std::string name = region.substr(start, name_end - start);
        const size_t eq = region.find('=', name_end);
        if (eq == std::string::npos || eq > name_end + 8) { pos = name_end; continue; }
        const size_t q = region.find('"', eq);
        if (q == std::string::npos) { pos = name_end; continue; }
        const size_t q2 = region.find('"', q + 1);
        if (q2 == std::string::npos) { pos = name_end; continue; }
        out.emplace_back(name, region.substr(q + 1, q2 - q - 1));
        pos = q2 + 1;
    }
}

}  // namespace

void SaveManager::write_xml(std::ostream& os, const SaveData& data) {
    os << "<?xml version=\"1.0\"?>\n";
    os << "<CurrentUser ID=\"1\" Token=\"1\" UseNewHash=\"1\" />\n";
    os << "<Warriors>\n";
    os << "  <Warrior ID=\"1\"\n";

    // [Wave 8] The <Warrior> attribute set. The captured device attribute set
    // is emitted in file order with the LIVE typed fields overriding the core
    // attributes; a fresh save (no capture) gets the usersDefault.xml default
    // set. Either way the core attributes are guaranteed present.
    auto emit_attr = [&](const char* name, const std::string& value) {
        os << "  " << name << "=\"" << value << "\"\n";
    };
    std::vector<std::pair<std::string, std::string>> attrs = data.warrior_attrs;
    if (attrs.empty()) {
        attrs = {
            {"FirstName", "NAME_SHADOW"}, {"Avatar", "avatar_hero"},
            {"Voice", data.voice}, {"Money", std::to_string(data.currency)},
            {"Bonus", "9"}, {"Strength", "3"}, {"Stamina", "3"},
            {"Level", std::to_string(data.level)}, {"Experience", "0"},
            {"Power", "5"}, {"PowerSyncTime", "0"}, {"Difficulty", "50"},
            {"LastLotteryEnterTime", "0"}, {"LastLotteryPlayTime", "0"},
            {"LotteryDaysMax", "6"}, {"LotteryDays", "0"}, {"RateTime", "0"},
            {"Skeleton", "Skeleton"},
            {"Armor", data.equipped_armor.empty() ? "Body" : data.equipped_armor},
            {"Helm", data.equipped_helmet.empty() ? "Head" : data.equipped_helmet},
            {"Weapon", data.equipped_weapon.empty() ? "Fists" : data.equipped_weapon},
            {"Ranged", data.equipped_ranged.empty() ? "NoRanged" : data.equipped_ranged},
            {"Magic", data.equipped_magic.empty() ? "NoMagic" : data.equipped_magic},
            {"ShowUpgrades", "0"}, {"ArenaRating", "0"}, {"ArenaRank", "0"},
            {"Tutorial", data.tutorial_state}, {"Tactic", "Player"},
            {"CurrentZone", data.current_level},
        };
    }
    // The core attributes always reflect live state: override if captured,
    // append if the capture lacked them.
    {
        auto set_core = [&](const char* name, const std::string& value) {
            for (auto& [key, val] : attrs)
                if (key == name) { val = value; return; }
            attrs.emplace_back(name, value);
        };
        set_core("Voice", data.voice);
        set_core("Money", std::to_string(data.currency));
        set_core("Level", std::to_string(data.level));
        set_core("Tutorial", data.tutorial_state);
        set_core("CurrentZone", data.current_level);
        set_core("Armor", data.equipped_armor.empty() ? "Body" : data.equipped_armor);
        set_core("Helm", data.equipped_helmet.empty() ? "Head" : data.equipped_helmet);
        set_core("Weapon", data.equipped_weapon.empty() ? "Fists" : data.equipped_weapon);
        set_core("Ranged", data.equipped_ranged.empty() ? "NoRanged" : data.equipped_ranged);
        set_core("Magic", data.equipped_magic.empty() ? "NoMagic" : data.equipped_magic);
    }
    for (const auto& [name, value] : attrs) {
        // The tag literal already carries ID="1"; a captured ID attribute
        // must not duplicate it.
        if (name == "ID") continue;
        emit_attr(name.c_str(), value);
    }
    os << ">\n";

    // Items — full slots when captured, name-only form otherwise.
    os << "    <Items>\n";
    if (!data.items.empty()) {
        for (const auto& it : data.items) {
            os << "      <Item Name=\"" << it.name << "\" Equipped=\"" << it.equipped
               << "\" Count=\"" << it.count << "\" UpgradeLevel=\"" << it.upgrade_level
               << "\" DeliveryTime=\"" << it.delivery_time << "\" DeliveryUpgradeLevel=\""
               << it.delivery_upgrade_level << "\" AcquireType=\"" << it.acquire_type
               << "\" />\n";
        }
    } else {
        for (const auto& item : data.owned_items) {
            bool equipped = (item == data.equipped_weapon || item == data.equipped_armor ||
                             item == data.equipped_helmet || item == data.equipped_ranged ||
                             item == data.equipped_magic);
            os << "      <Item Name=\"" << item << "\" Equipped=\"" << (equipped ? "1" : "0")
               << "\" Count=\"1\" />\n";
        }
        if (data.owned_items.empty()) {
            os << "      <Item Name=\"Body\" Equipped=\"1\" Count=\"1\" />\n";
            os << "      <Item Name=\"Head\" Equipped=\"1\" Count=\"1\" />\n";
            os << "      <Item Name=\"Fists\" Equipped=\"1\" Count=\"1\" />\n";
            os << "      <Item Name=\"NoRanged\" Equipped=\"1\" Count=\"1\" />\n";
            os << "      <Item Name=\"NoMagic\" Equipped=\"1\" Count=\"1\" />\n";
        }
    }
    os << "    </Items>\n";

    // Battles — verbatim rows when captured (Locked/Hidden/ReplayCount),
    // zone/battle lock maps otherwise.
    os << "    <Battles>\n";
    if (!data.battles.empty()) {
        for (const auto& b : data.battles) {
            os << "      <Battle Name=\"" << b.name << "\" Locked=\"" << b.locked
               << "\" Hidden=\"" << b.hidden << "\" ReplayCount=\"" << b.replay_count
               << "\" />\n";
        }
    } else if (!data.battle_unlocked.empty()) {
        for (const auto& [key, unlocked] : data.battle_unlocked) {
            std::string suffix = unlocked ? "" : "_LOCKED";
            os << "      <Battle Name=\"" << key << "|" << suffix << "|\" />\n";
        }
    } else {
        os << "      <Battle Name=\"ZONE_1|BOSS_LYNX|\" />\n";
        os << "      <Battle Name=\"ZONE_2|BOSS_HERMIT_LOCKED|\" />\n";
        os << "      <Battle Name=\"ZONE_3|BOSS_BUTCHER_LOCKED|\" />\n";
        os << "      <Battle Name=\"ZONE_4|BOSS_WASP_LOCKED|\" />\n";
        os << "      <Battle Name=\"ZONE_5|BOSS_HUNTRESS_LOCKED|\" />\n";
        os << "      <Battle Name=\"ZONE_6|BOSS_SAMURAI_LOCKED|\" />\n";
    }
    os << "    </Battles>\n";

    // Sounds
    os << "    <Sounds>\n";
    os << "      <Sound Value=\"" << data.sound_volume << "\" Mute=\"" << (data.sound_muted ? "1" : "0") << "\" />\n";
    os << "      <Music Value=\"" << data.music_volume << "\" Mute=\"" << (data.music_muted ? "1" : "0") << "\" />\n";
    os << "    </Sounds>\n";

    os << "    <Currencies ForgeMaterial1=\"" << data.currencies.forge_material1
       << "\" ForgeMaterial2=\"" << data.currencies.forge_material2
       << "\" ForgeMaterial3=\"" << data.currencies.forge_material3
       << "\" AscensionTicket=\"" << data.currencies.ascension_ticket << "\"/>\n";
    os << "    <Resistances Resistance_2=\"0\"/>\n";
    os << "  </Warrior>\n";
    os << "</Warriors>\n";
    os << "<Versions>\n";
    os << "  <Version Value=\"" << data.xml_version << "\"/>\n";
    os << "  <DataVersion Value=\"" << data.data_version << "\"/>\n";
    os << "</Versions>\n";
}

bool SaveManager::parse_xml(const std::string& xml, SaveData& data) {
    if (xml.empty()) {
        std::printf("[save] empty XML\n");
        return false;
    }

    resf2::format::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::printf("[save] XML parse error: %s\n", doc.error().c_str());
        return false;
    }

    // [ORIGINAL] The document has multiple root-level elements (CurrentUser,
    // Warriors, Versions). XmlDocument::root() returns the first one, so we
    // need to iterate the document's internal buffer. Instead, we use a
    // simple approach: find the <Warrior> element directly.

    // Find <Warrior> element by scanning for it in the raw XML
    // (The XmlDocument parser wraps everything under a single root, but our
    // XML has multiple root-level elements. We parse it manually.)

    // Parse Warrior attributes from the LAST <Warrior ...> tag. The device
    // users.xml carries a stub <Warrior ID="1" IsFake="True" /> FIRST and the
    // real user (FirstName/Money/...) second; the original resolves the real
    // one. Picking the first match would read ID/IsFake only and drop the
    // whole save (latent until the device file was captured).
    size_t warrior_pos = std::string::npos;
    {
        size_t scan = 0;
        size_t found;
        while ((found = xml.find("<Warrior ", scan)) != std::string::npos) {
            warrior_pos = found;
            scan = found + 1;
        }
    }
    if (warrior_pos == std::string::npos) {
        std::printf("[save] no <Warrior> element found in XML\n");
        return false;
    }

    // Helper: extract attribute value from a tag region
    auto get_attr = [&](const std::string& region, const std::string& attr_name) -> std::string {
        std::string search = attr_name + "=\"";
        auto pos = region.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        auto end = region.find('"', pos);
        if (end == std::string::npos) return "";
        return region.substr(pos, end - pos);
    };

    // Extract the full Warrior tag (from "<Warrior " to ">")
    auto tag_end = xml.find('>', warrior_pos);
    if (tag_end == std::string::npos) return false;
    std::string warrior_tag = xml.substr(warrior_pos, tag_end - warrior_pos + 1);

    // [Wave 8] Capture the FULL <Warrior> attribute set verbatim, in file
    // order (all 66 attributes of the device users.xml real Warrior). The
    // tag name itself is skipped by scanning from the first space AFTER the
    // '<' (the tag may carry leading whitespace, e.g. the engine's own
    // two-space-indented writes).
    data.warrior_attrs.clear();
    {
        const size_t lt = warrior_tag.find('<');
        const size_t first_space = warrior_tag.find(' ', lt);
        if (first_space != std::string::npos)
            parse_attrs_into(warrior_tag.substr(first_space + 1), data.warrior_attrs);
    }

    // Player stats [ORIGINAL] from <Warrior> attributes
    data.currency = std::atoi(get_attr(warrior_tag, "Money").c_str());
    data.level = std::atoi(get_attr(warrior_tag, "Level").c_str());
    if (data.level < 1) data.level = 1;
    data.current_level = get_attr(warrior_tag, "CurrentZone");
    data.tutorial_state = get_attr(warrior_tag, "Tutorial");
    if (data.tutorial_state.empty()) data.tutorial_state = "MOVE";

    // [S1] <Warrior Voice=> picks the player's m_pl_*/f_pl_* sound set.
    // usersDefault.xml defaults to "Male"; keep that when absent.
    data.voice = get_attr(warrior_tag, "Voice");
    if (data.voice.empty()) data.voice = "Male";

    data.equipped_weapon = get_attr(warrior_tag, "Weapon");
    data.equipped_armor = get_attr(warrior_tag, "Armor");
    data.equipped_helmet = get_attr(warrior_tag, "Helm");
    data.equipped_ranged = get_attr(warrior_tag, "Ranged");
    data.equipped_magic = get_attr(warrior_tag, "Magic");

    // Parse <Items> for inventory — full slots, in file order.
    data.owned_items.clear();
    data.items.clear();
    auto items_start = xml.find("<Items>", warrior_pos);
    if (items_start != std::string::npos) {
        auto items_end = xml.find("</Items>", items_start);
        if (items_end != std::string::npos) {
            std::string items_region = xml.substr(items_start, items_end - items_start);
            size_t search_pos = 0;
            while ((search_pos = items_region.find("<Item ", search_pos)) != std::string::npos) {
                auto item_end = items_region.find("/>", search_pos);
                if (item_end == std::string::npos) break;
                std::string item_tag = items_region.substr(search_pos, item_end - search_pos);
                std::string name = get_attr(item_tag, "Name");
                if (!name.empty()) {
                    data.owned_items.push_back(name);
                    ItemEntry e;
                    e.name = name;
                    e.equipped = std::atoi(get_attr(item_tag, "Equipped").c_str());
                    e.count = std::atoi(get_attr(item_tag, "Count").c_str());
                    e.upgrade_level = std::atoi(get_attr(item_tag, "UpgradeLevel").c_str());
                    e.delivery_time = std::atoll(get_attr(item_tag, "DeliveryTime").c_str());
                    e.delivery_upgrade_level =
                        std::atoi(get_attr(item_tag, "DeliveryUpgradeLevel").c_str());
                    e.acquire_type = get_attr(item_tag, "AcquireType");
                    if (e.acquire_type.empty()) e.acquire_type = "Item";
                    data.items.push_back(std::move(e));
                }
                search_pos = item_end + 2;
            }
        }
    }

    // Parse <Battles> for zone/battle lock state — verbatim rows too.
    data.battle_unlocked.clear();
    data.zone_unlocked.clear();
    data.battles.clear();
    auto battles_start = xml.find("<Battles>", warrior_pos);
    if (battles_start != std::string::npos) {
        auto battles_end = xml.find("</Battles>", battles_start);
        if (battles_end != std::string::npos) {
            std::string battles_region = xml.substr(battles_start, battles_end - battles_start);
            size_t search_pos = 0;
            while ((search_pos = battles_region.find("<Battle ", search_pos)) != std::string::npos) {
                auto battle_end = battles_region.find("/>", search_pos);
                if (battle_end == std::string::npos) break;
                std::string battle_tag = battles_region.substr(search_pos, battle_end - search_pos);
                std::string name = get_attr(battle_tag, "Name");
                if (!name.empty()) {
                    // [Wave 8] Verbatim row (Locked/Hidden/ReplayCount).
                    BattleEntry b;
                    b.name = name;
                    b.locked = std::atoi(get_attr(battle_tag, "Locked").c_str());
                    b.hidden = std::atoi(get_attr(battle_tag, "Hidden").c_str());
                    b.replay_count = std::atoi(get_attr(battle_tag, "ReplayCount").c_str());
                    data.battles.push_back(std::move(b));

                    // [ORIGINAL] Format: "ZONE_N|BOSS_NAME|" or "ZONE_N|BOSS_NAME_LOCKED|"
                    // Parse zone and battle from the name
                    auto pipe1 = name.find('|');
                    if (pipe1 != std::string::npos) {
                        std::string zone = name.substr(0, pipe1);
                        std::string rest = name.substr(pipe1 + 1);
                        auto pipe2 = rest.find('|');
                        std::string boss = (pipe2 != std::string::npos) ? rest.substr(0, pipe2) : rest;

                        // Check if locked: name ends with "_LOCKED" before the trailing pipe
                        bool locked = false;
                        if (boss.size() > 7 && boss.substr(boss.size() - 7) == "_LOCKED") {
                            locked = true;
                            boss = boss.substr(0, boss.size() - 7);
                        }

                        std::string key = zone + "|" + boss;
                        data.battle_unlocked[key] = !locked;
                        data.zone_unlocked[zone] = !locked;
                    }
                }
                search_pos = battle_end + 2;
            }
        }
    }

    // [Wave 8] <Currencies> forge materials + ascension ticket.
    data.currencies = Currencies{};
    {
        auto cur_start = xml.find("<Currencies", warrior_pos);
        if (cur_start != std::string::npos) {
            auto cur_end = xml.find('>', cur_start);
            if (cur_end != std::string::npos) {
                std::string cur_tag = xml.substr(cur_start, cur_end - cur_start + 1);
                data.currencies.forge_material1 =
                    std::atoi(get_attr(cur_tag, "ForgeMaterial1").c_str());
                data.currencies.forge_material2 =
                    std::atoi(get_attr(cur_tag, "ForgeMaterial2").c_str());
                data.currencies.forge_material3 =
                    std::atoi(get_attr(cur_tag, "ForgeMaterial3").c_str());
                data.currencies.ascension_ticket =
                    std::atoi(get_attr(cur_tag, "AscensionTicket").c_str());
            }
        }
    }

    // [Wave 8] <Versions> footer.
    {
        auto ver_pos = xml.find("<Version ");
        if (ver_pos != std::string::npos) {
            auto ver_end = xml.find('>', ver_pos);
            if (ver_end != std::string::npos) {
                std::string ver_tag = xml.substr(ver_pos, ver_end - ver_pos + 1);
                std::string v = get_attr(ver_tag, "Value");
                if (!v.empty()) data.xml_version = v;
            }
        }
        auto dver_pos = xml.find("<DataVersion ");
        if (dver_pos != std::string::npos) {
            auto dver_end = xml.find('>', dver_pos);
            if (dver_end != std::string::npos) {
                std::string dver_tag = xml.substr(dver_pos, dver_end - dver_pos + 1);
                std::string v = get_attr(dver_tag, "Value");
                if (!v.empty()) data.data_version = v;
            }
        }
    }

    // Parse <Sounds> for audio settings
    auto sounds_start = xml.find("<Sounds>", warrior_pos);
    if (sounds_start != std::string::npos) {
        auto sounds_end = xml.find("</Sounds>", sounds_start);
        if (sounds_end != std::string::npos) {
            std::string sounds_region = xml.substr(sounds_start, sounds_end - sounds_start);
            // <Sound Value="1.0" Mute="0" />
            auto sound_pos = sounds_region.find("<Sound ");
            if (sound_pos != std::string::npos) {
                auto sound_end = sounds_region.find("/>", sound_pos);
                if (sound_end != std::string::npos) {
                    std::string sound_tag = sounds_region.substr(sound_pos, sound_end - sound_pos);
                    std::string val = get_attr(sound_tag, "Value");
                    if (!val.empty()) data.sound_volume = std::strtof(val.c_str(), nullptr);
                    std::string mute = get_attr(sound_tag, "Mute");
                    data.sound_muted = (mute == "1");
                }
            }
            // <Music Value="1.0" Mute="0" />
            auto music_pos = sounds_region.find("<Music ");
            if (music_pos != std::string::npos) {
                auto music_end = sounds_region.find("/>", music_pos);
                if (music_end != std::string::npos) {
                    std::string music_tag = sounds_region.substr(music_pos, music_end - music_pos);
                    std::string val = get_attr(music_tag, "Value");
                    if (!val.empty()) data.music_volume = std::strtof(val.c_str(), nullptr);
                    std::string mute = get_attr(music_tag, "Mute");
                    data.music_muted = (mute == "1");
                }
            }
        }
    }

    std::printf("[save] parsed XML: currency=%d level=%d zone=%s tutorial=%s items=%zu battles=%zu\n",
                data.currency, data.level, data.current_level.c_str(),
                data.tutorial_state.c_str(), data.owned_items.size(),
                data.battle_unlocked.size());
    return true;
}

}  // namespace resf2::save
