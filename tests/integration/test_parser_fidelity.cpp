// tests/integration/test_parser_fidelity.cpp
//
// Wave 8 — golden parser-fidelity suite. Every assertion here is a value the
// ORIGINAL engine produces from the REAL shipped/device files; the reference
// data is the live-device evidence in reverse/analysis/LIVE_BOOT_TRACE.md
// (commit 2d5c955, Redmi 6A 684006127d29, v1.9.21):
//
//   * users.xml  — reverse/data/users.xml (pulled from the device, 2026-08-03)
//   * packs.xml  — reverse/data/packs.xml (same pull)
//   * assets/*   — authentic original asset tree (byte-identical to the
//                  device pulls: list.xml/stages.xml/moves.xml MD5-match)
//
// Items under test (LIVE_BOOT_TRACE §4 discrepancy table):
//   1. HIGH  Save format: original reads/writes users.xml + hash + backup;
//            the <Warrior> carries 66 attributes, <Items> slots with
//            UpgradeLevel/DeliveryTime/DeliveryUpgradeLevel/AcquireType,
//            <Battles> with Locked/Hidden/ReplayCount, <Currencies>,
//            <Versions>.
//   2. HIGH  Pack discovery: original discovers packs from packs.xml
//            (Name/Url/Version); the RE engine dir-scans for *.dz instead.
//   3. MED   Boot order: original parses moves.xml (12.56 s) BEFORE the save
//            (15.82 s); list.xml LATE (15.84 s, after save); then stages.xml
//            (16.8) -> quests.xml (17.67) -> packs.xml (18.36) ->
//            config_cdn.xml (18.37). Boot configs (perks 11.26, forge/
//            CharacterProgress/Achievements 12.02-12.55) come BEFORE moves.
//   4. MED   Boot config set: forge/perks/CharacterProgress/Achievements/
//            quests/config_cdn must parse at boot without error and expose
//            the real data. (purchased.xml is runtime-generated, absent from
//            the pull — HEURISTIC-TODO, tolerated as optional.)
//   5. MED   Tactics: the trace shows the original reads BOTH
//            tacticSettings.xml (11.26) AND tactics/*.atf (20.94). The XML
//            path is therefore NOT a divergence; the .atf loader is deferred
//            (no .atf in reverse/data — HEURISTIC-TODO).
//
// RED on HEAD (2026-08-03): SaveData drops every item slot + battle
// attribute + 59 of the 66 <Warrior> attributes; DzRegistry has no packs.xml
// path; boot loads list.xml before the save and moves.xml after it; none of
// the boot configs are parsed; the write never produces users_backup.xml.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../headless_test_runner.hpp"
#include "engine/game/boot_configs.hpp"
#include "engine/game/save.hpp"
#include "engine/game/tactic_settings.hpp"
#include "engine/reverse/dz_reader.hpp"

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %lld, expected %lld\n", \
                     __LINE__, msg, (long long)(a), (long long)(b)); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while (0)

#define CHECK_STREQ(a, b, msg) do { \
    std::string _sa((a)), _sb((b)); \
    if (_sa != _sb) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got '%s', expected '%s'\n", \
                     __LINE__, msg, _sa.c_str(), _sb.c_str()); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while (0)

static std::string load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return data;
}

static bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// ---------------------------------------------------------------------------
// The 66 <Warrior> attributes of the device users.xml REAL Warrior element
// (line 5; the stub <Warrior ID="1" IsFake="True" /> on line 4 carries only
// ID+IsFake and is skipped — the original resolves the real user). Verbatim
// attribute names; a 1:1 parser must read them all and a save writer must
// emit them all.
// ---------------------------------------------------------------------------
static const char* const kDeviceWarriorAttrs[] = {
    "ID", "FirstName", "Avatar", "Voice", "Money", "Bonus",
    "Strength", "Stamina", "Level", "Experience", "Power", "PowerSyncTime",
    "Difficulty", "LastLotteryEnterTime", "LastLotteryPlayTime",
    "LotteryDaysMax", "LotteryDays", "RateTime", "Skeleton", "Armor", "Helm",
    "Weapon", "Ranged", "Magic", "ShowUpgrades", "ArenaRating", "ArenaRank",
    "Tutorial", "Tactic", "CurrentZone", "ServerUserID", "AskedForDumps",
    "InstallID", "IndexSlider", "CoinIcon", "PaidBonus", "PaidMoney",
    "DenominationDigits", "LotteryLevel", "LotteryExperience",
    "LastDailyTimeOffset", "LastEnergyTimeOffset", "LastDumpTime",
    "LotteryPlayedToday", "FightID", "MapFocus", "RaidMapFocus",
    "RaidTutorialStep", "Language", "ShowForge", "TrySocialLogin",
    "DailyProgress", "DailyPlayTime", "PeriodicPlayTime",
    "StarterPackTimerEndTime", "GPlusAutoLogin", "GPlusFiledLogins",
    "LastShopItem", "MapMaskColor", "ForgeTutorialMaterialsGiven",
    "FacebookLiked", "EclipseMode", "RaidToggleIsVisible",
    "RaidTutorialGemsTaken", "RaidTop100Focus", "FightIDS",
};
static constexpr size_t kDeviceWarriorAttrCount =
    sizeof(kDeviceWarriorAttrs) / sizeof(kDeviceWarriorAttrs[0]);

// Boot order the ORIGINAL exhibits (LIVE_BOOT_TRACE §2/§4):
// perks 11.26 -> forge/CharacterProgress/Achievements 12.02-12.55 ->
// moves.xml 12.56 -> save 15.82 -> list.xml 15.84 -> stages.xml 16.8 ->
// quests.xml 17.67 -> packs.xml 18.36 -> config_cdn.xml 18.37.
static const char* const kOriginalBootOrder[] = {
    "perks.xml", "forge.xml", "CharacterProgress.xml", "Achievements.xml",
    "moves.xml", "save", "list.xml", "stages.xml", "quests.xml",
    "packs.xml", "config_cdn.xml",
};
static constexpr size_t kOriginalBootOrderSize =
    sizeof(kOriginalBootOrder) / sizeof(kOriginalBootOrder[0]);

// ===========================================================================
// Item 1 (HIGH): save format
// ===========================================================================
static int test_save_format(const std::string& repo_root) {
    std::printf("\n--- Item 1: save format (users.xml, device pull) ---\n");
    const std::string device_save = repo_root + "/reverse/data/users.xml";
    CHECK(file_exists(device_save), "1: device users.xml present in reverse/data");

    resf2::save::SaveManager mgr;
    resf2::save::SaveData d;
    CHECK(mgr.load(device_save, d), "1: engine SaveManager parses the real device users.xml");
    if (tests_failed && !d.currency && !d.level && d.items.empty()) {
        // Hard parse failure; the rest would be noise.
        return tests_failed;
    }

    // Typed values, verbatim from the device file.
    CHECK_EQ(d.currency, 129, "1: Money=\"129\" -> currency int 129");
    CHECK_EQ(d.level, 2, "1: Level=\"2\" -> level 2");
    CHECK_STREQ(d.current_level, "ZONE_1", "1: CurrentZone=\"ZONE_1\" (string)");
    CHECK_STREQ(d.tutorial_state, "END", "1: Tutorial=\"END\" (enum value)");
    CHECK_STREQ(d.voice, "Male", "1: Voice=\"Male\"");
    CHECK_STREQ(d.equipped_weapon, "WEAPON_KNIVES", "1: Weapon=\"WEAPON_KNIVES\"");
    CHECK_STREQ(d.equipped_armor, "ARMOR_ROBE", "1: Armor=\"ARMOR_ROBE\"");
    CHECK_STREQ(d.equipped_helmet, "Head", "1: Helm=\"Head\"");
    CHECK_STREQ(d.equipped_ranged, "NoRanged", "1: Ranged=\"NoRanged\"");
    CHECK_STREQ(d.equipped_magic, "NoMagic", "1: Magic=\"NoMagic\"");

    // Items with the original slot attributes.
    CHECK_EQ((long long)d.items.size(), 7LL, "1: <Items> has 7 entries");
    if (d.items.size() == 7) {
        CHECK_STREQ(d.items[0].name, "Body", "1: item[0] Body");
        CHECK_EQ(d.items[0].equipped, 0, "1: Body Equipped=\"0\"");
        CHECK_STREQ(d.items[5].name, "WEAPON_KNIVES", "1: item[5] WEAPON_KNIVES");
        const auto& knife = d.items[5];
        CHECK_EQ(knife.equipped, 1, "1: WEAPON_KNIVES Equipped=\"1\"");
        CHECK_EQ(knife.count, 1, "1: WEAPON_KNIVES Count=\"1\"");
        CHECK_EQ(knife.upgrade_level, 100, "1: WEAPON_KNIVES UpgradeLevel=\"100\"");
        CHECK_EQ((long long)knife.delivery_time, -1LL, "1: WEAPON_KNIVES DeliveryTime=\"-1\"");
        CHECK_EQ(knife.delivery_upgrade_level, -1, "1: WEAPON_KNIVES DeliveryUpgradeLevel=\"-1\"");
        CHECK_STREQ(knife.acquire_type, "Item", "1: WEAPON_KNIVES AcquireType=\"Item\"");
        CHECK_EQ(d.items[6].upgrade_level, 200, "1: ARMOR_ROBE UpgradeLevel=\"200\"");
    }

    // Battles with the original attribute set.
    CHECK_EQ((long long)d.battles.size(), 7LL, "1: <Battles> has 7 entries (6 bosses + tournament)");
    if (d.battles.size() == 7) {
        CHECK_STREQ(d.battles[0].name, "ZONE_1|BOSS_LYNX|", "1: battle[0] ZONE_1|BOSS_LYNX|");
        CHECK_EQ(d.battles[0].locked, 0, "1: ZONE_1|BOSS_LYNX| Locked=\"0\"");
        CHECK_STREQ(d.battles[1].name, "ZONE_2|BOSS_HERMIT_LOCKED|",
                    "1: battle[1] ZONE_2|BOSS_HERMIT_LOCKED| (name carries the lock)");
        CHECK_STREQ(d.battles[6].name, "ZONE_1|Tournament|", "1: battle[6] ZONE_1|Tournament|");
        CHECK_EQ(d.battles[6].hidden, 0, "1: tournament Hidden=\"0\"");
        CHECK_EQ(d.battles[6].replay_count, 0, "1: tournament ReplayCount=\"0\"");
    }

    // Currencies + Versions sections.
    CHECK_EQ(d.currencies.forge_material1, 0, "1: Currencies ForgeMaterial1=\"0\"");
    CHECK_EQ(d.currencies.forge_material2, 0, "1: Currencies ForgeMaterial2=\"0\"");
    CHECK_EQ(d.currencies.forge_material3, 0, "1: Currencies ForgeMaterial3=\"0\"");
    CHECK_EQ(d.currencies.ascension_ticket, 0, "1: Currencies AscensionTicket=\"0\"");
    CHECK_STREQ(d.xml_version, "1.9.21", "1: <Version Value=\"1.9.21\">");
    CHECK_STREQ(d.data_version, "1.9.21.0", "1: <DataVersion Value=\"1.9.21.0\">");

    // Full attribute set: every device attribute name must be captured.
    CHECK_EQ((long long)d.warrior_attrs.size(), (long long)kDeviceWarriorAttrCount,
             "1: all 66 <Warrior> attributes captured");
    for (size_t i = 0; i < kDeviceWarriorAttrCount; ++i) {
        bool found = false;
        std::string value;
        for (const auto& [name, val] : d.warrior_attrs) {
            if (name == kDeviceWarriorAttrs[i]) { found = true; value = val; break; }
        }
        if (!found) {
            std::fprintf(stderr, "  FAIL [line %d]: 1: missing <Warrior> attribute '%s'\n",
                         __LINE__, kDeviceWarriorAttrs[i]);
            ++tests_failed;
        } else {
            ++tests_passed;
            std::printf("  PASS: 1: <Warrior> attribute '%s' captured\n", kDeviceWarriorAttrs[i]);
        }
        if (std::string(kDeviceWarriorAttrs[i]) == "InstallID")
            CHECK_STREQ(value, "1253114496", "1: InstallID=\"1253114496\"");
        if (std::string(kDeviceWarriorAttrs[i]) == "Language")
            CHECK_STREQ(value, "rus", "1: Language=\"rus\"");
        if (std::string(kDeviceWarriorAttrs[i]) == "FightIDS")
            CHECK_STREQ(value, "ZONE_1|Tournament|2", "1: FightIDS=\"ZONE_1|Tournament|2\"");
        if (std::string(kDeviceWarriorAttrs[i]) == "MapMaskColor")
            CHECK_STREQ(value, "0xffffff00", "1: MapMaskColor=\"0xffffff00\"");
    }

    // Round-trip: parse(device) -> write_xml -> parse -> identical values.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto tmp = fs::temp_directory_path(ec) / "resf2_fidelity_save";
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        resf2::save::SaveManager w;
        w.set_asset_root(tmp.string());
        CHECK(w.save(d), "1: write round-trip file (XML)");
        resf2::save::SaveData d2;
        CHECK(w.load((tmp / "user.xml").string(), d2), "1: read back the written XML");
        CHECK_EQ(d2.currency, d.currency, "1: round-trip currency identical");
        CHECK_EQ(d2.level, d.level, "1: round-trip level identical");
        CHECK_STREQ(d2.current_level, d.current_level, "1: round-trip zone identical");
        CHECK_STREQ(d2.tutorial_state, d.tutorial_state, "1: round-trip tutorial identical");
        CHECK_STREQ(d2.voice, d.voice, "1: round-trip voice identical");
        CHECK_EQ((long long)d2.items.size(), (long long)d.items.size(),
                 "1: round-trip item count identical");
        if (d2.items.size() == d.items.size() && !d2.items.empty())
            CHECK_EQ(d2.items[5].upgrade_level, d.items[5].upgrade_level,
                     "1: round-trip WEAPON_KNIVES UpgradeLevel identical");
        CHECK_EQ((long long)d2.battles.size(), (long long)d.battles.size(),
                 "1: round-trip battle count identical");
        if (d2.battles.size() == d.battles.size() && !d2.battles.empty())
            CHECK_EQ(d2.battles[6].replay_count, d.battles[6].replay_count,
                     "1: round-trip tournament ReplayCount identical");
        CHECK_EQ(d2.currencies.forge_material1, d.currencies.forge_material1,
                 "1: round-trip currencies identical");
        CHECK_STREQ(d2.xml_version, d.xml_version, "1: round-trip version identical");
        CHECK_STREQ(d2.data_version, d.data_version, "1: round-trip data version identical");
        CHECK_EQ((long long)d2.warrior_attrs.size(), (long long)d.warrior_attrs.size(),
                 "1: round-trip full attribute set preserved");

        // Schema: the written file must expose every device attribute name.
        const std::string written = load_file((tmp / "user.xml").string());
        bool all_present = true;
        for (size_t i = 0; i < kDeviceWarriorAttrCount && all_present; ++i) {
            const std::string needle = std::string(kDeviceWarriorAttrs[i]) + "=\"";
            if (written.find(needle) == std::string::npos) all_present = false;
        }
        CHECK(all_present, "1: written XML contains all 66 device <Warrior> attributes");

        // Backup convention: original writes users_backup.xml before each
        // overwrite (LIVE_BOOT_TRACE 15.46/21.38: users.xml + users_backup.xml).
        resf2::save::SaveData v1 = d, v2 = d;
        v1.currency = 100;
        v2.currency = 200;
        CHECK(w.save(v1), "1: save #1 (currency 100)");
        CHECK(w.save(v2), "1: save #2 (currency 200)");
        CHECK(file_exists((tmp / "users_backup.xml").string()),
              "1: users_backup.xml exists after second save");
        const std::string backup = load_file((tmp / "users_backup.xml").string());
        CHECK(backup.find("Money=\"100\"") != std::string::npos,
              "1: users_backup.xml holds the PREVIOUS save (Money=\"100\")");
        const std::string current = load_file((tmp / "user.xml").string());
        CHECK(current.find("Money=\"200\"") != std::string::npos,
              "1: user.xml holds the NEW save (Money=\"200\")");
        fs::remove_all(tmp, ec);
    }

    return tests_failed;
}

// ===========================================================================
// Item 2 (HIGH): pack discovery via packs.xml
// ===========================================================================
static int test_packs(const std::string& repo_root) {
    std::printf("\n--- Item 2: pack discovery (packs.xml, device pull) ---\n");
    const std::string packs_path = repo_root + "/reverse/data/packs.xml";
    CHECK(file_exists(packs_path), "2: device packs.xml present in reverse/data");

    auto& dz = resf2::dz::DzRegistry::instance();
    CHECK(dz.load_packs_xml(repo_root), "2: DzRegistry reads packs.xml");
    const auto& packs = dz.pack_list();
    CHECK_EQ((long long)packs.size(), 1LL, "2: device packs.xml declares exactly 1 pack");
    if (packs.size() == 1) {
        CHECK_STREQ(packs[0].name, "files", "2: pack[0] Name=\"files\"");
        CHECK_STREQ(packs[0].url, "assets/files.dz", "2: pack[0] Url=\"assets/files.dz\"");
        CHECK_STREQ(packs[0].version, "1.9.21", "2: pack[0] Version=\"1.9.21\"");
    }
    // The pack list, in packs.xml order, is what the engine must discover
    // (LIVE_BOOT_TRACE §4 #2: "never reads packs.xml" was the RE defect).
    const std::string device_text = load_file(packs_path);
    CHECK(device_text.find("<Pack Name=\"files\" Url=\"assets/files.dz\" Version=\"1.9.21\" />")
              != std::string::npos,
          "2: reference packs.xml entry matches the device file verbatim");
    return tests_failed;
}

// ===========================================================================
// Item 3 (MED): boot order
// ===========================================================================
static int test_boot_order(const std::string& repo_root) {
    std::printf("\n--- Item 3: boot order (LIVE_BOOT_TRACE chronology) ---\n");
    resf2::test::HeadlessTestConfig cfg;
    cfg.asset_root = repo_root;
    cfg.width = 1440;
    cfg.height = 720;
    cfg.hermetic = false;  // the probe must observe the real save load
    resf2::test::HeadlessTestRunner runner(cfg);
    if (!runner.init()) {
        CHECK(false, "3: headless boot initialised");
        return tests_failed;
    }

    const bool done = runner.run_until(
        [&]() { return runner.game().boot_events().size() >= kOriginalBootOrderSize; },
        600);
    const auto& events = runner.game().boot_events();
    std::fprintf(stderr, "  [3] boot events observed (%zu): ", events.size());
    for (const auto& e : events) std::fprintf(stderr, "%s ", e.c_str());
    std::fprintf(stderr, "\n");

    CHECK(done, "3: full boot sequence reached in 600 frames");
    CHECK_EQ((long long)events.size(), (long long)kOriginalBootOrderSize,
             "3: exactly the original loader sequence is observed");
    for (size_t i = 0; i < events.size() && i < kOriginalBootOrderSize; ++i) {
        CHECK_STREQ(events[i], kOriginalBootOrder[i], "3: boot order step");
    }
    return tests_failed;
}

// ===========================================================================
// Item 4 (MED): boot config set — real data exposed
// ===========================================================================
static int test_boot_configs(const std::string& repo_root) {
    std::printf("\n--- Item 4: boot configs parse + expose real data ---\n");
    resf2::game::BootConfigs cfg;
    CHECK(resf2::game::load_boot_configs(repo_root, cfg), "4: all boot configs parse without error");

    CHECK_EQ((long long)cfg.forge.aspects, 52LL, "4: forge.xml has 52 <Aspect> entries");
    CHECK_EQ(cfg.forge.first_aspect_value, 55, "4: forge first <Aspect Value=\"55\">");

    CHECK_EQ((long long)cfg.perks.perks, 142LL, "4: perks.xml has 142 <Perk> entries (1 of 143 is inside a comment)");
    CHECK(cfg.perks.has_double_sweep, "4: PERK_DOUBLE_SWEEP present (users.xml <Perks> references it)");

    CHECK_EQ((long long)cfg.achievements.counters, 88LL, "4: Achievements.xml has 88 <Counter> groups (1 is commented out)");
    CHECK_EQ((long long)cfg.achievements.achievements, 124LL,
             "4: Achievements.xml has 124 <Achievement> entries (6 commented out)");
    CHECK_STREQ(cfg.achievements.first_counter, "PerfectRound",
                "4: first achievement counter is PerfectRound");

    CHECK_EQ((long long)cfg.progress.thresholds, 52LL, "4: CharacterProgress.xml has 52 <Threshold> entries");
    CHECK_EQ(cfg.progress.first_exp, 150, "4: Level 1 threshold Exp=\"150\"");

    // quests.xml (17.67 s) and config_cdn.xml (18.37 s) load AFTER the save
    // in the engine; the fidelity test pins them through their own loaders.
    resf2::game::QuestConfig quests;
    CHECK(resf2::game::load_quests_config(repo_root, quests), "4: quests.xml parses");
    CHECK_EQ((long long)quests.quests, 498LL, "4: quests.xml has 498 <Quest> entries (6 of the 504 raw tags are commented out)");
    CHECK_STREQ(quests.first_quest, "SetBackVersionCheck",
                "4: first quest is SetBackVersionCheck (Priority=99998)");
    CHECK_STREQ(quests.first_priority, "99998", "4: first quest Priority=\"99998\"");

    resf2::game::CdnConfig cdn;
    CHECK(resf2::game::load_cdn_config(repo_root, cdn), "4: config_cdn.xml parses");
    CHECK_EQ((long long)cdn.platform_items, 5LL, "4: config_cdn.xml has 5 platform entries");
    CHECK_STREQ(cdn.android_name, "Android", "4: PlatformID=2 is Android");
    CHECK_EQ((long long)cdn.total_items, 519LL, "4: config_cdn.xml has 519 <item> total");

    // purchased.xml is runtime-generated (not shipped, absent from the pull);
    // the loader must tolerate its absence — HEURISTIC-TODO until the file
    // lands (device 684006127d29 not reachable this session).
    CHECK(cfg.purchased_tolerated, "4: missing purchased.xml is tolerated (HEURISTIC-TODO)");
    return tests_failed;
}

// ===========================================================================
// Item 5 (MED): tactics source
// ===========================================================================
static int test_tactics(const std::string& repo_root) {
    std::printf("\n--- Item 5: tacticSettings.xml (trace: read at 11.26, AND .atf at 20.94) ---\n");
    // The original reads BOTH tacticSettings.xml (boot, 11.26) and
    // tactics/*.atf (dojo, 20.94) — LIVE_BOOT_TRACE §2. The XML path is
    // therefore correct in the RE engine; the missing .atf loader is
    // deferred (no .atf in reverse/data — HEURISTIC-TODO).
    resf2::game::TacticSettings t;
    CHECK(t.load(repo_root), "5: engine parses the real tacticSettings.xml");
    CHECK(t.loaded(), "5: tactic settings loaded flag");
    CHECK(t.count() > 0, "5: tactic settings expose table entries");
    return tests_failed;
}

int main(int argc, char** argv) {
    const std::string repo_root = argc > 1 ? argv[1] : ".";

    test_save_format(repo_root);
    test_packs(repo_root);
    test_boot_order(repo_root);
    test_boot_configs(repo_root);
    test_tactics(repo_root);

    std::printf("\n=== Parser-fidelity: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
