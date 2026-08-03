// engine/game/boot_configs.hpp
//
// Wave 8 — boot-time config set of the ORIGINAL (LIVE_BOOT_TRACE §2/§4 #5):
// perks.xml (11.26 s), forge.xml / CharacterProgress.xml / Achievements.xml
// (12.02-12.55 s), quests.xml (17.67 s), config_cdn.xml (18.37 s). The RE
// engine loaded none of them; they must parse without error and expose the
// real data even if the game logic does not consume them yet.
//
// purchased.xml is runtime-generated (absent from the APK and from the
// device pull) — its absence is tolerated by design (HEURISTIC-TODO until a
// copy lands).

#pragma once

#include <string>
#include <vector>

namespace resf2::game {

struct ForgeConfig {
    size_t aspects = 0;          // <Aspect> entries (AspectScale)
    int first_aspect_value = 0;  // first <Aspect Value=..>
    size_t price_rows = 0;       // <Price> upgrade-cost rows
};

struct PerksConfig {
    size_t perks = 0;            // <Perk> entries
    bool has_double_sweep = false;  // PERK_DOUBLE_SWEEP (users.xml <Perks>)
};

struct AchievementsConfig {
    size_t counters = 0;         // <Counter> groups
    size_t achievements = 0;     // <Achievement> entries
    std::string first_counter;   // first <Counter Name=..>
};

struct CharacterProgressConfig {
    size_t thresholds = 0;       // <Threshold> entries
    int first_exp = 0;           // Level-1 threshold Exp
    int max_level = 0;           // highest threshold level
};

struct QuestConfig {
    size_t quests = 0;           // <Quest> entries
    std::string first_quest;     // first <Quest Name=..>
    std::string first_priority;  // its Priority attribute
};

struct CdnConfig {
    size_t platform_items = 0;   // <item> under <platform>
    size_t version_items = 0;    // <item> under <versions>
    size_t total_items = 0;      // every <item> in the file
    std::string android_name;    // <item PlatformID="2" Name=..>
};

// All boot-time configs parsed from the original asset tree. `events`
// records every file that parsed, in load order — the boot-order probe
// (tests/integration/test_parser_fidelity.cpp item 3) compares it to the
// LIVE_BOOT_TRACE chronology.
struct BootConfigs {
    ForgeConfig forge;
    PerksConfig perks;
    AchievementsConfig achievements;
    CharacterProgressConfig progress;
    QuestConfig quests;
    CdnConfig cdn;
    // true when the run was clean even though purchased.xml is absent
    // (runtime-generated file, not shipped).
    bool purchased_tolerated = false;
    std::vector<std::string> events;
};

// Parse the boot config set from asset_root (searched at <root>/ and
// <root>/assets/). Returns true when every SHIPPED config parsed without
// error; purchased.xml is optional. On success `events` lists the files in
// the order the original opens them (perks, forge, CharacterProgress,
// Achievements — the pre-save boot configs; quests/config_cdn are loaded
// later by the engine and are NOT appended here).
bool load_boot_configs(const std::string& asset_root, BootConfigs& out);

// quests.xml / config_cdn.xml — loaded by the engine AFTER the save load
// (original: 17.67 s / 18.37 s, i.e. inside init_location after stages.xml).
// Standalone so the fidelity test can pin the real data without a full boot.
bool load_quests_config(const std::string& asset_root, QuestConfig& out);
bool load_cdn_config(const std::string& asset_root, CdnConfig& out);

}  // namespace resf2::game
