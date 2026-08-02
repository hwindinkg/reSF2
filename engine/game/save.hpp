// engine/game/save.hpp
//
// Save system for reSF2: SaveData struct and SaveManager class.
// Provides JSON-based save/load with version field for forward compatibility.
// Replaces the inline JSON save/load that was in game.hpp.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace resf2::save {

// ---------- Save data structure ----------
//
// Represents the full player state that gets persisted to disk.
// Version field enables forward-compatible migration in future releases.
//
// [ORIGINAL] The original game stores progress in usersDefault.xml (initial)
// and user.xml (after first save). The XML format uses <Warrior> attributes
// for player stats, <Battles> for zone/battle lock state, and <Items> for
// inventory. We preserve compatibility with both formats.

struct SaveData {
    int version = 1;                        // schema version for forward compat
    int currency = 0;                       // player gold [ORIGINAL] default 0 from usersDefault.xml
    int level = 1;                          // player character level
    int wins = 0;                           // total battle wins
    int losses = 0;                         // total battle losses
    std::vector<std::string> completed_levels;  // completed level IDs (zone/battle)
    std::vector<std::string> owned_items;       // item IDs the player owns
    std::string equipped_weapon;                // currently equipped weapon ID
    std::string equipped_armor;                 // currently equipped armor ID
    std::string equipped_helmet;                // currently equipped helmet ID
    std::string equipped_ranged;                // currently equipped ranged weapon ID
    std::string equipped_magic;                 // currently equipped magic item ID
    std::string current_level;                  // most recently played level

    // [ORIGINAL] From usersDefault.xml: Tutorial attribute drives the initial
    // Sensei tutorial sequence. Values: "MOVE", "BAG", "FIRST_FIGHT", "COMPLETE".
    std::string tutorial_state = "MOVE";

    // [ORIGINAL] From <Warrior Voice=> in usersDefault.xml / user.xml: the
    // player's voice gender. "Male" (usersDefault.xml default) selects the
    // m_pl_* sound set, "Female" the f_pl_* set.
    std::string voice = "Male";

    // [ORIGINAL] From usersDefault.xml <Battles>: each entry is "ZONE_N|BOSS_NAME|"
    // (unlocked) or "ZONE_N|BOSS_NAME_LOCKED|" (locked). Parsed into a map for
    // quick lookup by zone/battle name.
    // Key: "ZONE_N" → true if unlocked, false if locked.
    std::map<std::string, bool> zone_unlocked;
    // Key: "ZONE_N|BOSS_NAME" → true if unlocked (not _LOCKED suffix), false if locked.
    std::map<std::string, bool> battle_unlocked;

    // Audio settings [ORIGINAL] from <Sounds> in usersDefault.xml
    float sound_volume = 1.0f;
    float music_volume = 1.0f;
    bool sound_muted = false;
    bool music_muted = false;

    // Track which source file was loaded ("xml" or "json") for save format selection.
    std::string source_format;
};

// ---------- Save manager ----------
//
// Handles the actual disk I/O for save files. Supports two formats:
//   1. XML (original game format): usersDefault.xml / user.xml
//   2. JSON (legacy fallback): resf2_save.json
//
// Load order:
//   1. user.xml in assets directory (player's actual save)
//   2. usersDefault.xml in assets directory (initial save on first start)
//   3. resf2_save.json in app data / temp dir (legacy JSON fallback)
//
// Save always writes to user.xml in the assets directory.

class SaveManager {
public:
    SaveManager();

    // Set the assets root directory (used to find user.xml / usersDefault.xml).
    void set_asset_root(const std::string& root);

    // Set a custom save path (overrides default resolution).
    void set_save_path(const std::string& path);

    // Get the resolved save file path.
    std::string get_save_path() const;

    // Save data to disk. Returns true on success.
    // Writes in XML format to user.xml if asset_root is set, otherwise JSON.
    bool save(const SaveData& data, const std::string& path = "");

    // Load data from disk. Returns true on success.
    // Tries user.xml → usersDefault.xml → JSON fallback.
    bool load(const std::string& path, SaveData& data);

    // Convenience overload: uses configured/default path.
    bool load(SaveData& data);

    // Check if a save file exists at the given path.
    // If path is empty, checks the configured/default path.
    bool save_exists(const std::string& path = "");

private:
    std::string save_path_;
    std::string asset_root_;  // root directory for XML save files

    // Write SaveData as JSON to the output stream.
    static void write_json(std::ostream& os, const SaveData& data);

    // Parse JSON string into SaveData. Returns true on success.
    static bool parse_json(const std::string& json, SaveData& data);

    // Write SaveData as XML (original format) to the output stream.
    // [ORIGINAL] Matches the structure of usersDefault.xml.
    static void write_xml(std::ostream& os, const SaveData& data);

    // Parse XML string into SaveData. Returns true on success.
    // [ORIGINAL] Reads <Warrior> attributes, <Items>, <Battles>, <Sounds>.
    static bool parse_xml(const std::string& xml, SaveData& data);

    // Load a single file, auto-detecting format (XML or JSON).
    bool load_file(const std::string& path, SaveData& data);

    // Resolve the default save path (platform app data dir or temp dir).
    static std::string default_save_path();

    // Resolve the XML save path (user.xml in assets directory).
    std::string xml_save_path() const;

    // Resolve the default XML path (usersDefault.xml in assets directory).
    std::string xml_default_path() const;
};

}  // namespace resf2::save
