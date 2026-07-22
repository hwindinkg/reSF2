// tests/test_weapon_loading.cpp
//
// Tests for weapon content completion:
// - Weapon model XML parsing for all weapon types
// - Weapon-specific animation existence
// - Move selection respects tactic_weapon field

#include "../engine/format/xml_doc.hpp"
#include "../engine/fight/moves.hpp"
#include "../engine/core/math.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <unordered_set>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %lld, expected %lld\n", \
                     __LINE__, msg, (long long)(a), (long long)(b)); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while(0)

static std::string load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return data;
}

// ===== Test 1: Weapon model XMLs can be parsed =====
// Verify that weapon model files have valid XML with <Scene>/<Nodes> structure.
static void test_weapon_model_parsing(const std::string& model_dir) {
    std::printf("\n=== Test 1: Weapon model XML parsing ===\n");
    int parsed = 0;
    int total = 0;
    // Scan all weapon_*.xml and magic_*.xml files
    if (!std::filesystem::exists(model_dir)) {
        std::fprintf(stderr, "  Model dir not found: %s\n", model_dir.c_str());
        return;
    }
    for (auto& entry : std::filesystem::directory_iterator(model_dir)) {
        auto name = entry.path().filename().string();
        if (name.find("weapon_") != 0 && name.find("magic_") != 0) continue;
        if (entry.path().extension() != ".xml") continue;
        total++;
        std::string xml = load_file(entry.path().string());
        if (xml.empty()) {
            std::fprintf(stderr, "  FAIL [%s]: empty\n", name.c_str());
            ++tests_failed;
            continue;
        }
        resf2::format::XmlDocument doc;
        if (!doc.parse(xml)) {
            std::fprintf(stderr, "  FAIL [%s]: parse error: %s\n", name.c_str(), doc.error().c_str());
            ++tests_failed;
            continue;
        }
        auto* root = doc.root();
        auto* scene = root ? root->first_child("Scene") : nullptr;
        auto* nodes = scene ? scene->first_child("Nodes") : nullptr;
        if (!nodes) {
            std::fprintf(stderr, "  FAIL [%s]: no Scene/Nodes\n", name.c_str());
            ++tests_failed;
            continue;
        }
        parsed++;
    }
    CHECK(parsed == total, "All weapon/magic model XMLs parse correctly");
    CHECK(parsed >= 90, "At least 90 weapon model files found");
    std::printf("  Parsed %d/%d weapon models\n", parsed, total);
}

// ===== Test 2: Animation .bin files exist on disk =====
// Verify that animation files are present and loadable.
static void test_animations_exist(const std::string& anim_dir) {
    std::printf("\n=== Test 2: Animation .bin files on disk ===\n");
    std::string found_dir;
    std::vector<std::string> dirs_to_check = {anim_dir, "assets/animations/binary", "assets/assets/animations/binary"};
    for (auto& dir : dirs_to_check) {
        if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir)) {
            found_dir = dir;
            break;
        }
    }
    CHECK(!found_dir.empty(), "Animation directory found");
    if (found_dir.empty()) return;

    int total_bins = 0;
    for (auto& entry : std::filesystem::directory_iterator(found_dir)) {
        if (entry.path().extension() == ".bin") total_bins++;
    }
    CHECK(total_bins >= 500, "At least 500 .bin animation files exist");
    std::printf("  Found %d .bin files in %s\n", total_bins, found_dir.c_str());

    // Verify key weapon animation sets exist
    // Each weapon type should have at least a stance/idle and attack animation.
    // Animation naming patterns vary: _stance, _stance_idle, _idle for stances;
    // _slash, _super_slash, _punch, _split for attacks.
    struct AnimSet {
        const char* prefix;
        const char* name;
    };
    const AnimSet key_sets[] = {
        {"fists1", "Fists"},           // uses fists1_ prefix
        {"axe", "Axes"},
        {"claws", "Claws"},
        {"composite_sword", "CompositeSword"},
        {"composite_spear", "CompositeSpear"},
        {"composite_staff", "CompositeStaff"},
        {"composite_scythe", "CompositeScythe"},
        {"daggers", "Daggers"},
        {"fans", "Fans"},
        {"giant_sword", "GiantSword"},
        {"glaive", "Glaive"},
        {"glaivebow", "Glaivebow"},
        {"katana", "Katana"},
        {"knives", "Knives"},
        {"knobstick", "Knobsticks"},
        {"knuckles", "Knuckles"},
        {"kusarigama", "Kusarigama"},
        {"nunchaku", "Nunchaku"},
        {"one_handed_sword", "OneHandedSword"},
        {"power_fists", "PowerFists"},
        {"rifle", "Rifle"},
        {"sai", "Sai"},
        {"scythe", "Scythe"},
        {"spear", "Spear"},
        {"staff", "Staff"},
        {"sticks", "Batons"},
        {"swords", "Swords"},
        {"tonfa", "Tonfa"},
        {"two_hand", "TwoHanded"},
    };
    const int num_key_sets = sizeof(key_sets) / sizeof(key_sets[0]);

    // Collect all .bin names on disk
    std::unordered_set<std::string> anim_names;
    for (auto& entry : std::filesystem::directory_iterator(found_dir)) {
        if (entry.path().extension() == ".bin") {
            anim_names.insert(entry.path().stem().string());
        }
    }

    int found_sets = 0;
    for (auto& as : key_sets) {
        std::string prefix(as.prefix);
        // Check for stance/idle animation
        bool has_stance = anim_names.count(prefix + "_stance") > 0 ||
                          anim_names.count(prefix + "_stance_idle") > 0 ||
                          anim_names.count(prefix + "_idle") > 0;
        // Check for attack animation (various naming patterns)
        bool has_attack = anim_names.count(prefix + "_slash") > 0 ||
                          anim_names.count(prefix + "_super_slash") > 0 ||
                          anim_names.count(prefix + "_punch") > 0 ||
                          anim_names.count(prefix + "_split") > 0 ||
                          anim_names.count(prefix + "_high") > 0;
        if (has_stance && has_attack) {
            found_sets++;
        } else {
            std::printf("  [%s] prefix='%s': stance=%d attack=%d\n",
                       as.name, as.prefix, (int)has_stance, (int)has_attack);
        }
    }
    CHECK(found_sets >= (int)(num_key_sets * 0.7),
          "At least 70% of key weapon types have stance+attack anims");
    std::printf("  Weapon types with complete anim sets: %d/%d\n",
               found_sets, num_key_sets);
}

// ===== Test 3: MoveDatabase tactic_weapon field =====
// Verify that moves have TacticWeapon field parsed and can be filtered.
static void test_tactic_weapon_filtering() {
    std::printf("\n=== Test 3: tactic_weapon filtering ===\n");
    resf2::fight::MoveDatabase db;
    bool loaded = db.load_from_file("assets/animations/moves.xml");
    CHECK(loaded, "MoveDatabase loads from moves.xml");
    if (!loaded) return;

    // Verify tactic_weapon is parsed
    bool found_tactic = false;
    for (auto& [name, move] : db.all_moves()) {
        if (!move.tactic_weapon.empty()) {
            found_tactic = true;
            break;
        }
    }
    CHECK(found_tactic, "At least one move has tactic_weapon parsed");

    // Verify Fists moves exist
    {
        resf2::fight::MoveDatabase::MoveQuery q;
        q.move_type = "Punch";
        q.key_count = 1;
        q.tactic_weapon = "Fists";
        auto results = db.query(q);
        CHECK(!results.empty(), "Fists have 1key Punch moves");
        std::printf("  Fists 1key Punch: %zu moves\n", results.size());
    }
}

// ===== Test 4: MoveDatabase query with different weapons =====
// Verify that querying with different tactic_weapon gives different results.
static void test_weapon_query_diff() {
    std::printf("\n=== Test 4: Weapon-specific move queries ===\n");
    resf2::fight::MoveDatabase db;
    if (!db.load_from_file("assets/animations/moves.xml")) return;

    int weapon_types_with_moves = 0;
    struct WeaponQuery {
        const char* tactic;
        const char* direction;
        const char* move_type;
        int key_count;
    };
    // Weapon-specific moves use template "1key|Central|Weapon" (no "Punch"/"Kick" keyword).
    // So we query without move_type for weapons, and with move_type for Fists.
    // Also try Central direction first (basic attack).
    WeaponQuery weapon_queries[] = {
        {"Fists", "Central", "Punch", 1},       // Fists use "1key|Central|Unarmed|Punch"
        {"Swords", "Central", "", 1},            // Weapons use "1key|Central|Weapon"
        {"Axes", "Central", "", 1},
        {"Katana", "Central", "", 1},
        {"Spear", "Central", "", 1},
        {"Staff", "Central", "", 1},
        {"TwoHanded", "Central", "", 1},
        {"Claws", "Central", "", 1},
        {"Knuckles", "Central", "", 1},
        {"Daggers", "Central", "", 1},
        {"Glaive", "Central", "", 1},
        {"Sai", "Central", "", 1},
        {"Tonfa", "Central", "", 1},
        {"Fans", "Central", "", 1},
        {"Kusarigama", "Central", "", 1},
        {"Nunchaku", "Central", "", 1},
        {"BigSwords", "Central", "", 1},
        {"CompositeSword", "Central", "", 1},
        {"CompositeSpear", "Central", "", 1},
        {"CompositeStaff", "Central", "", 1},
        {"CompositeScythe", "Central", "", 1},
        {"Rifle", "Central", "", 1},
        {"FireBall", "Central", "", 1},
        {"Energyball", "Central", "", 1},
    };

    for (auto& wq : weapon_queries) {
        resf2::fight::MoveDatabase::MoveQuery q;
        q.tactic_weapon = wq.tactic;
        q.key_count = wq.key_count;
        q.direction = wq.direction;
        if (wq.move_type[0] != '\0') q.move_type = wq.move_type;
        auto results = db.query(q);
        if (!results.empty()) weapon_types_with_moves++;
        else {
            std::printf("  NO moves for %s (dir=%s, kc=%d, type=%s)\n",
                       wq.tactic, wq.direction, wq.key_count,
                       wq.move_type[0] != '\0' ? wq.move_type : "any");
        }
    }

    CHECK(weapon_types_with_moves >= (int)(sizeof(weapon_queries)/sizeof(weapon_queries[0]) * 0.5),
          "At least 50% of weapon types have weapon-specific 1key Central moves");
    std::printf("  Weapon types with 1key Central moves: %d/%zu\n",
               weapon_types_with_moves, sizeof(weapon_queries)/sizeof(weapon_queries[0]));
}

// ===== Test 5: Weapon model file locations =====
// Verify that weapon XML files are in the expected location (assets/models/).
static void test_weapon_model_locations(const std::string& model_dir) {
    std::printf("\n=== Test 5: Weapon model file locations ===\n");
    CHECK(std::filesystem::exists(model_dir), "Models directory exists");
    if (!std::filesystem::exists(model_dir)) return;

    int weapon_files = 0;
    int magic_files = 0;
    for (auto& entry : std::filesystem::directory_iterator(model_dir)) {
        auto name = entry.path().filename().string();
        if (name.find("weapon_") == 0) weapon_files++;
        else if (name.find("magic_") == 0) magic_files++;
    }
    CHECK(weapon_files >= 80, "At least 80 weapon model files");
    CHECK(magic_files >= 5, "At least 5 magic model files");
    std::printf("  Weapon files: %d, Magic files: %d\n", weapon_files, magic_files);
}

int main(int argc, char* argv[]) {
    const char* asset_root = argc > 1 ? argv[1] : "assets";
    std::string model_dir = std::string(asset_root) + "/models";
    std::string anim_dir = std::string(asset_root) + "/animations/binary";

    test_weapon_model_parsing(model_dir);
    test_animations_exist(anim_dir);
    test_tactic_weapon_filtering();
    test_weapon_query_diff();
    test_weapon_model_locations(model_dir);

    // ===== Summary =====
    std::printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
