// tests/integration/test_hardcode_fidelity.cpp
//
// HARDCODE-FIDELITY WAVE (HARDCODE_AUDIT.md, commit 2cf966e): the engine
// must READ ASSET FILES, not invent substitutes that didn't exist in the
// original. Each HIGH item ships as a RED-first probe: the assertion pins
// the real asset value, so a regression back to the invented constant
// fails the build.
//
// Data provenance (all from reverse/data, the device dump):
//   list.xml           item Model attr (= file base name), MagicDamage
//   stages.xml         warrior <Template> <Items> = the enemy loadout
//   moves.xml          real animation names / FileName attrs
//   users.xml          the save: Level / Money
//   models/*.xml       magic projectile models (magic_fireball.xml etc.)
//
// Runs the headless harness (real Game), so the probes hit the engine's
// actual load paths.

#include "../headless_test_runner.hpp"
#include "../check.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

static void suppress_stdout() {
    // Re-point stdout at NUL so load-path printf noise stays out of ctest
    // logs; the tests that need stdout capture reopen it around init().
    std::freopen("NUL", "w", stdout);
}

// Battle harness shared by the combat probes: queue a battle against a
// stages.xml warrior template (runner must already be constructed).
static void configure_battle(resf2::test::HeadlessTestRunner& runner,
                             const std::string& enemy_name) {
    scene::SceneHost::BattleInfo info;
    info.enemy_name = enemy_name;
    info.rounds = 1;
    info.round_time_s = 99;
    runner.game().host_set_battle_info(info);
    runner.game().host_set_battle_mode(true);
    runner.game().host_set_show_enemy(true);
}

// Construct + init a battle-scene runner and queue the given enemy.
static resf2::test::HeadlessTestRunner* init_battle_runner(
    resf2::test::HeadlessTestRunner& runner, const std::string& enemy_name) {
    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: battle runner init() returned false\n");
        return nullptr;
    }
    configure_battle(runner, enemy_name);
    return &runner;
}

// H06: the enemy weapon comes from the battle setup — the stages.xml
// warrior template's <Items> (WEAPON_SWORDS) -> list.xml Model attr
// (weapon_swords.xml). HARDCODE_AUDIT H06: game.cpp loaded
// "weapon_knuckles.xml" for EVERY battle; a sword loadout must load the
// sword's model. RED on HEAD: the load log names weapon_knuckles.xml even
// for Man_Swords.
static void test_h06_enemy_weapon_from_loadout() {
    std::printf("\n=== H06: enemy weapon from the stages.xml loadout ===\n");
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "battle";
    config.hermetic = true;
    resf2::test::HeadlessTestRunner runner(config);
    if (!init_battle_runner(runner, "Man_Swords")) {
        std::fprintf(stderr, "FAIL: H06 runner unusable\n");
        return;
    }

    const std::string file = runner.game().host_get_enemy_weapon_file();
    std::fprintf(stderr, "  [H06] enemy weapon file='%s' nodes=%zu\n",
                 file.c_str(), runner.game().host_get_enemy_weapon_node_count());
    resf2::test::check_eq(file, std::string("weapon_swords.xml"),
                          "H06: the sword loadout resolves ITS weapon model file");
    resf2::test::check(runner.game().host_get_enemy_weapon_node_count() > 0,
                       "H06: the sword weapon model loads with >0 nodes");

    // A FISTS loadout (Dojo_Disciple: <Item Name=\"Fists\"/>) carries NO
    // weapon — the disciple is unarmed; loading knuckles for him would be
    // the old every-battle substitute.
    resf2::test::HeadlessTestConfig fists_cfg = config;
    resf2::test::HeadlessTestRunner fists(fists_cfg);
    if (init_battle_runner(fists, "Dojo_Disciple")) {
        std::fprintf(stderr, "  [H06] fists enemy: file='%s' nodes=%zu\n",
                     fists.game().host_get_enemy_weapon_file().c_str(),
                     fists.game().host_get_enemy_weapon_node_count());
        resf2::test::check(fists.game().host_get_enemy_weapon_file().empty(),
                           "H06: the fists loadout resolves NO weapon model file");
        resf2::test::check_eq(fists.game().host_get_enemy_weapon_node_count(),
                              std::size_t(0),
                              "H06: the unarmed disciple loads no weapon model");
    } else {
        resf2::test::check(false, "H06: fists runner init");
    }
}

// H07: the invented "fists_idle" alias (HARDCODE_AUDIT I03/H07,
// asset_manager.cpp:845) is gone from the catalog, and the enemy idle
// resolves the REAL catalog stance idle (fists1_stance_idle.bin is real;
// "fists_idle" is not a moves.xml name). RED on HEAD: the alias existed,
// so fists_idle WAS in the catalog after load.
static void test_h07_idle_alias(const resf2::test::HeadlessTestRunner& runner) {
    const resf2::game::Game& g = runner.game();
    const bool has_fists_idle = g.host_has_animation("fists_idle");
    const bool has_real_idle = g.host_has_animation("fists1_stance_idle");
    const std::string idle = g.host_get_enemy_idle_anim();
    std::fprintf(stderr, "  [H07] fists_idle=%d fists1_stance_idle=%d enemy_idle='%s'\n",
                 (int)has_fists_idle, (int)has_real_idle, idle.c_str());
    resf2::test::check(!has_fists_idle,
                       "H07: no invented 'fists_idle' animation in the catalog");
    resf2::test::check(has_real_idle,
                       "H07: the real 'fists1_stance_idle' animation is in the catalog");
    resf2::test::check_eq(idle, std::string("fists1_stance_idle"),
                          "H07: the enemy idle resolves the real catalog stance idle (fists)");
}

}  // namespace

int main() {
    std::printf("=== Hardcode-Fidelity Wave (HARDCODE_AUDIT HIGH items) ===\n");
    std::fflush(stdout);

    // ---- H06 ----
    test_h06_enemy_weapon_from_loadout();

    // ---- H07 ----
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "dojo";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) {
            std::fprintf(stderr, "FAIL: H07 init() returned false\n");
            return resf2::test::summary() ? 1 : 1;
        }
        runner.run_frames(40);  // Loading scene triggers load_animations()
        test_h07_idle_alias(runner);
    }

    return resf2::test::summary();
}
