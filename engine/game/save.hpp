// engine/game/save.hpp
//
// Save system for reSF2: SaveData struct and SaveManager class.
// Provides JSON-based save/load with version field for forward compatibility.
// Replaces the inline JSON save/load that was in game.hpp.

#pragma once

#include <string>
#include <vector>

namespace resf2::save {

// ---------- Save data structure ----------
//
// Represents the full player state that gets persisted to disk.
// Version field enables forward-compatible migration in future releases.

struct SaveData {
    int version = 1;                        // schema version for forward compat
    int currency = 1000;                    // player gold
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
};

// ---------- Save manager ----------
//
// Handles the actual disk I/O for save files. The save format is JSON,
// written manually (matching the existing codebase style — no JSON library).
//
// Save path resolution:
//   Default: <platform app data dir>/resf2_save.json
//   Fallback (if app data dir unavailable): <temp dir>/resf2_save.json
//   Override: user-supplied path

class SaveManager {
public:
    SaveManager();

    // Set a custom save path (overrides default resolution).
    void set_save_path(const std::string& path);

    // Get the resolved save file path.
    std::string get_save_path() const;

    // Save data to disk. Returns true on success.
    // If path is empty, uses the default/configured save path.
    bool save(const SaveData& data, const std::string& path = "");

    // Load data from disk. Returns true on success.
    // On failure, the data parameter is left unchanged.
    bool load(const std::string& path, SaveData& data);

    // Convenience overload: uses configured/default path.
    bool load(SaveData& data);

    // Check if a save file exists at the given path.
    // If path is empty, checks the configured/default path.
    bool save_exists(const std::string& path = "");

private:
    std::string save_path_;

    // Write SaveData as JSON to the output stream.
    static void write_json(std::ostream& os, const SaveData& data);

    // Parse JSON string into SaveData. Returns true on success.
    static bool parse_json(const std::string& json, SaveData& data);

    // Resolve the default save path (platform app data dir or temp dir).
    static std::string default_save_path();
};

}  // namespace resf2::save
