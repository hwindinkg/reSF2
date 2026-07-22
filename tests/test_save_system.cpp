// test_save_system.cpp
//
// Tests for the save/load system and PlayerProfile.
//
// Covers:
//   - SaveData roundtrip (write → read → verify)
//   - Data integrity (currency, level, wins, losses, inventory)
//   - PlayerProfile mutation
//   - Edge cases (empty save, corrupt save, missing file)

#include "../engine/game/save.hpp"
#include "../engine/game/player.hpp"

#include <cstdio>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace save = resf2::save;
namespace player = resf2::player;
namespace fs = std::filesystem;

// ============================================================
// Test framework (matches existing test style)
// ============================================================

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    std::printf("  TEST %d: %s ... ", test_count, name); \
    bool _ok = true;

#define END_TEST \
    if (_ok) { pass_count++; std::printf("PASS\n"); } \
    else { std::printf("FAIL\n"); } \
} while(0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("\n    FAIL at line %d: %s\n", __LINE__, #cond); \
        _ok = false; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::printf("\n    FAIL at line %d: expected '%s' == '%s', got %d != %d\n", \
                    __LINE__, #a, #b, (int)_a, (int)_b); \
        _ok = false; \
    } \
} while(0)

// ============================================================
// Helpers
// ============================================================

// Generate a unique temp file path for each test.
static std::string temp_save_path() {
    static int counter = 0;
    counter++;
    return (fs::temp_directory_path() / ("resf2_test_save_" + std::to_string(counter) + ".json")).string();
}

// Clean up a temp file.
static void cleanup(const std::string& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

// Write raw content to a file (for corrupt save tests).
static void write_raw(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

// ============================================================
// Tests
// ============================================================

void test_save_roundtrip() {
    TEST("save roundtrip: write then read matches");
    auto path = temp_save_path();

    save::SaveData original;
    original.currency = 5000;
    original.level = 3;
    original.wins = 12;
    original.losses = 4;
    original.current_level = "ZONE_3/FightClub";
    original.completed_levels = {"ZONE_1/FirstFight", "ZONE_2/SecondFight"};
    original.owned_items = {"weapon_fists", "armor_leather", "helmet_basic"};
    original.equipped_weapon = "weapon_fists";
    original.equipped_armor = "armor_leather";

    save::SaveManager mgr;
    CHECK(mgr.save(original, path));
    CHECK(mgr.save_exists(path));

    save::SaveData loaded;
    CHECK(mgr.load(path, loaded));

    CHECK_EQ(loaded.version, 1);
    CHECK_EQ(loaded.currency, 5000);
    CHECK_EQ(loaded.level, 3);
    CHECK_EQ(loaded.wins, 12);
    CHECK_EQ(loaded.losses, 4);
    CHECK(loaded.current_level == "ZONE_3/FightClub");
    CHECK_EQ(loaded.completed_levels.size(), (size_t)2);
    CHECK(loaded.completed_levels[0] == "ZONE_1/FirstFight");
    CHECK(loaded.completed_levels[1] == "ZONE_2/SecondFight");
    CHECK_EQ(loaded.owned_items.size(), (size_t)3);
    CHECK(loaded.owned_items[0] == "weapon_fists");
    CHECK(loaded.owned_items[1] == "armor_leather");
    CHECK(loaded.equipped_weapon == "weapon_fists");
    CHECK(loaded.equipped_armor == "armor_leather");
    CHECK(loaded.equipped_helmet.empty());
    CHECK(loaded.equipped_ranged.empty());
    CHECK(loaded.equipped_magic.empty());

    cleanup(path);
    END_TEST;
}

void test_save_empty_fields() {
    TEST("save roundtrip: empty optional fields");
    auto path = temp_save_path();

    save::SaveData original;
    original.currency = 1000;
    original.level = 1;

    save::SaveManager mgr;
    CHECK(mgr.save(original, path));

    save::SaveData loaded;
    CHECK(mgr.load(path, loaded));

    CHECK_EQ(loaded.currency, 1000);
    CHECK_EQ(loaded.level, 1);
    CHECK_EQ(loaded.wins, 0);
    CHECK_EQ(loaded.losses, 0);
    CHECK(loaded.current_level.empty());
    CHECK(loaded.completed_levels.empty());
    CHECK(loaded.owned_items.empty());
    CHECK(loaded.equipped_weapon.empty());
    CHECK(loaded.equipped_armor.empty());
    CHECK(loaded.equipped_helmet.empty());
    CHECK(loaded.equipped_ranged.empty());
    CHECK(loaded.equipped_magic.empty());

    cleanup(path);
    END_TEST;
}

void test_load_nonexistent() {
    TEST("load missing file returns false");
    auto path = temp_save_path();

    // Ensure file does not exist
    cleanup(path);

    save::SaveManager mgr;
    save::SaveData data;
    CHECK(!mgr.load(path, data));
    CHECK(!mgr.save_exists(path));

    cleanup(path);
    END_TEST;
}

void test_corrupt_save() {
    TEST("load corrupt save returns false");
    auto path = temp_save_path();

    write_raw(path, "{this is not valid json");
    save::SaveManager mgr;
    save::SaveData data;
    data.currency = 9999;  // should be unchanged on failure
    CHECK(!mgr.load(path, data));
    CHECK_EQ(data.currency, 9999);

    cleanup(path);

    // Test with empty file
    auto path2 = temp_save_path();
    write_raw(path2, "");
    save::SaveData data2;
    data2.currency = 7777;
    CHECK(!mgr.load(path2, data2));
    CHECK_EQ(data2.currency, 7777);

    cleanup(path2);
    END_TEST;
}

void test_missing_braces() {
    TEST("load save with missing braces returns false");
    auto path = temp_save_path();

    write_raw(path, "\"version\": 1");
    save::SaveManager mgr;
    save::SaveData data;
    CHECK(!mgr.load(path, data));

    cleanup(path);
    END_TEST;
}

void test_save_default_path() {
    TEST("save manager resolves default path");
    save::SaveManager mgr;
    std::string path = mgr.get_save_path();
    CHECK(!path.empty());
    std::printf("\n    default save path: %s\n", path.c_str());
    END_TEST;
}

// ============================================================
// PlayerProfile tests
// ============================================================

void test_player_basics() {
    TEST("PlayerProfile basic state");
    player::PlayerProfile p;
    CHECK_EQ(p.currency(), 1000);
    CHECK_EQ(p.level(), 1);
    CHECK_EQ(p.wins(), 0);
    CHECK_EQ(p.losses(), 0);
    CHECK(p.completed_levels().empty());
    CHECK(p.owned_items().empty());
    END_TEST;
}

void test_player_currency() {
    TEST("PlayerProfile add/spend currency");
    player::PlayerProfile p;
    p.add_currency(500);
    CHECK_EQ(p.currency(), 1500);
    CHECK(p.spend_currency(300));
    CHECK_EQ(p.currency(), 1200);
    CHECK(!p.spend_currency(2000));  // not enough
    CHECK_EQ(p.currency(), 1200);    // unchanged
    END_TEST;
}

void test_player_wins_losses() {
    TEST("PlayerProfile wins/losses and level");
    player::PlayerProfile p;
    CHECK_EQ(p.level(), 1);

    for (int i = 0; i < 5; i++) {
        p.add_win();
        // Level up after every 5 wins
        if (i < 4) CHECK_EQ(p.level(), 1);
        else CHECK_EQ(p.level(), 2);
    }
    // 5 wins, 1 loss → level 2
    CHECK_EQ(p.wins(), 5);
    CHECK_EQ(p.level(), 2);

    p.add_loss();
    CHECK_EQ(p.losses(), 1);
    END_TEST;
}

void test_player_levels() {
    TEST("PlayerProfile complete and check levels");
    player::PlayerProfile p;
    CHECK(!p.is_level_completed("ZONE_1/Battle1"));

    p.complete_level("ZONE_1/Battle1");
    CHECK(p.is_level_completed("ZONE_1/Battle1"));
    CHECK(!p.is_level_completed("ZONE_1/Battle2"));

    // Completing same level again is no-op
    p.complete_level("ZONE_1/Battle1");
    CHECK_EQ(p.completed_levels().size(), (size_t)1);

    p.complete_level("ZONE_2/Battle1");
    CHECK_EQ(p.completed_levels().size(), (size_t)2);
    END_TEST;
}

void test_player_inventory() {
    TEST("PlayerProfile add/has items");
    player::PlayerProfile p;
    CHECK(!p.has_item("weapon_sword"));

    p.add_item("weapon_sword");
    CHECK(p.has_item("weapon_sword"));
    CHECK(!p.has_item("armor_plate"));

    // Adding same item is no-op
    p.add_item("weapon_sword");
    CHECK_EQ(p.owned_items().size(), (size_t)1);

    // Empty string is ignored
    p.add_item("");
    CHECK_EQ(p.owned_items().size(), (size_t)1);
    END_TEST;
}

void test_player_equip() {
    TEST("PlayerProfile equip items");
    player::PlayerProfile p;
    p.add_item("weapon_sword");
    p.add_item("armor_plate");

    // Can't equip unowned item
    CHECK(!p.equip_item("weapon", "weapon_axe"));

    // Equip owned items
    CHECK(p.equip_item("weapon", "weapon_sword"));
    CHECK(p.equipped_weapon() == "weapon_sword");

    CHECK(p.equip_item("armor", "armor_plate"));
    CHECK(p.equipped_armor() == "armor_plate");

    // Unequip (empty string)
    CHECK(p.equip_item("weapon", ""));
    CHECK(p.equipped_weapon().empty());

    // Unknown slot
    CHECK(!p.equip_item("invalid_slot", "weapon_sword"));
    END_TEST;
}

void test_player_to_save_data() {
    TEST("PlayerProfile to_save_data roundtrip");
    player::PlayerProfile p;
    p.add_currency(2000);
    for (int i = 0; i < 3; i++) p.add_win();
    p.complete_level("ZONE_1/Demo");
    p.add_item("weapon_fists");
    p.equip_item("weapon", "weapon_fists");
    p.set_current_level("ZONE_1/Demo");

    auto data = p.to_save_data();
    CHECK_EQ(data.currency, 3000);
    CHECK_EQ(data.wins, 3);
    CHECK_EQ(data.losses, 0);
    CHECK_EQ(data.level, 1);
    CHECK_EQ(data.completed_levels.size(), (size_t)1);
    CHECK(data.completed_levels[0] == "ZONE_1/Demo");
    CHECK_EQ(data.owned_items.size(), (size_t)1);
    CHECK(data.owned_items[0] == "weapon_fists");
    CHECK(data.equipped_weapon == "weapon_fists");

    // Roundtrip: from_save_data
    auto p2 = player::PlayerProfile::from_save_data(data);
    CHECK_EQ(p2.currency(), 3000);
    CHECK_EQ(p2.wins(), 3);
    CHECK(p2.is_level_completed("ZONE_1/Demo"));
    CHECK(p2.has_item("weapon_fists"));
    CHECK(p2.equipped_weapon() == "weapon_fists");
    END_TEST;
}

void test_save_load_profile_roundtrip() {
    TEST("SaveManager load/save with full PlayerProfile data");
    auto path = temp_save_path();

    player::PlayerProfile original;
    original.add_currency(500);
    original.add_win();
    original.add_win();
    original.add_loss();
    original.complete_level("ZONE_1/Fight1");
    original.add_item("weapon_knuckles");
    original.add_item("armor_light");
    original.equip_item("weapon", "weapon_knuckles");
    original.set_current_level("ZONE_1/Fight1");

    save::SaveManager mgr;
    CHECK(mgr.save(original.to_save_data(), path));

    save::SaveData loaded_data;
    CHECK(mgr.load(path, loaded_data));

    auto loaded = player::PlayerProfile::from_save_data(loaded_data);
    CHECK_EQ(loaded.currency(), 1500);
    CHECK_EQ(loaded.wins(), 2);
    CHECK_EQ(loaded.losses(), 1);
    CHECK(loaded.is_level_completed("ZONE_1/Fight1"));
    CHECK(loaded.has_item("weapon_knuckles"));
    CHECK(loaded.has_item("armor_light"));
    CHECK(loaded.equipped_weapon() == "weapon_knuckles");
    CHECK(loaded.current_level() == "ZONE_1/Fight1");

    cleanup(path);
    END_TEST;
}

void test_ten_cycles() {
    TEST("10 save/load cycles without data loss");
    auto path = temp_save_path();

    player::PlayerProfile p;
    p.add_currency(100);
    p.add_win();
    p.add_item("weapon_test");
    p.complete_level("CycleTest");
    p.set_current_level("CycleTest");

    save::SaveManager mgr;
    for (int cycle = 0; cycle < 10; cycle++) {
        // Mutate
        p.add_currency(50);
        if (cycle % 2 == 0) p.add_win();
        else p.add_loss();
        p.complete_level("Cycle_" + std::to_string(cycle));

        // Save
        CHECK(mgr.save(p.to_save_data(), path));

        // Reload
        save::SaveData data;
        CHECK(mgr.load(path, data));
        p = player::PlayerProfile::from_save_data(data);
    }

    CHECK_EQ(p.currency(), 1100 + 50 * 10);  // start 1000 + 100 + 10*50 = 1600
    CHECK_EQ(p.wins(), 6);   // 1 initial + 5 from even cycles
    CHECK_EQ(p.losses(), 5); // 5 from odd cycles
    CHECK_EQ(p.completed_levels().size(), (size_t)11);  // 1 + 10
    CHECK(p.is_level_completed("Cycle_9"));

    cleanup(path);
    END_TEST;
}

void test_save_manager_custom_path() {
    TEST("SaveManager custom path");
    auto path = temp_save_path();

    save::SaveManager mgr;
    mgr.set_save_path(path);
    CHECK(mgr.get_save_path() == path);

    save::SaveData data;
    data.currency = 777;
    CHECK(mgr.save(data));  // uses configured path

    save::SaveData loaded;
    CHECK(mgr.load(loaded));
    CHECK_EQ(loaded.currency, 777);

    cleanup(path);
    END_TEST;
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("=== Save System Tests ===\n\n");

    std::printf("--- SaveManager (save/load) ---\n");
    test_save_roundtrip();
    test_save_empty_fields();
    test_load_nonexistent();
    test_corrupt_save();
    test_missing_braces();
    test_save_default_path();
    test_save_manager_custom_path();

    std::printf("\n--- PlayerProfile ---\n");
    test_player_basics();
    test_player_currency();
    test_player_wins_losses();
    test_player_levels();
    test_player_inventory();
    test_player_equip();
    test_player_to_save_data();
    test_save_load_profile_roundtrip();
    test_ten_cycles();

    std::printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
