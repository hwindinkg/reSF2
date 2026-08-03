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

// H02: the tactic->model file map must not invent file names — the list.xml
// Model attr is the ONLY legitimate source (LIVE_GAME_EVIDENCE Q1). The
// 47-entry hardcoded map (HARDCODE_AUDIT H02) says
// WandererStaff->weapon_staff.xml and Shuriken->weapon_knives.xml, but the
// real items ship Model="weapon_wanderer_staff" / "ranged_shurikens". RED
// on HEAD: the fallback resolver returns the invented map names.
static void test_h02_weapon_model_from_list() {
    std::printf("\n=== H02: weapon model file from list.xml Model, not the map ===\n");
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "dojo";
    config.hermetic = true;

    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) {
        resf2::test::check(false, "H02: runner init");
        return;
    }

    // Own the sample weapons (list.xml Model attrs ship on disk). The J/U
    // cycle starts at Fists (index 0), so J lands on the owned staff.
    runner.game().host_add_item("WEAPON_WANDERER_STAFF");
    runner.game().host_add_item("RANGED_SHURIKENS");

    const std::string staff = runner.game().host_get_weapon_tactic_model_file("WandererStaff");
    const std::string shuri = runner.game().host_get_weapon_tactic_model_file("Shuriken");
    std::fprintf(stderr, "  [H02] WandererStaff->'%s' Shuriken->'%s'\n",
                 staff.c_str(), shuri.c_str());
    resf2::test::check_eq(staff, std::string("weapon_wanderer_staff.xml"),
                          "H02: WandererStaff resolves list.xml Model (not map weapon_staff.xml)");
    resf2::test::check_eq(shuri, std::string("ranged_shurikens.xml"),
                          "H02: Shuriken resolves list.xml Model (not map weapon_knives.xml)");

    // End-to-end: cycling to the owned staff loads a real weapon model.
    runner.run_frames(20);
    runner.tap_key(resf2::platform::Key::J, 2);  // cycle forward
    runner.run_frames(10);
    std::fprintf(stderr, "  [H02] after J cycle: weapon nodes=%zu\n",
                 runner.game().host_get_player_weapon_node_count());
    resf2::test::check(runner.game().host_get_player_weapon_node_count() > 0,
                       "H02: the cycled owned weapon loads a real model");
}

// H09: the top-panel HUD stats must come from the LOADED SAVE (users.xml
// Level= / Money=), not the invented constants 7 / 72450 / 9
// (HARDCODE_AUDIT H09, game_clean.hpp:5455). The device users.xml ships
// Level="2" Money="129"; the engine's save path is assets/user.xml
// (gitignored, engine-written). RED on HEAD: the HUD showed 7 / 72450.
static void test_h09_hud_from_save() {
    std::printf("\n=== H09: HUD stats from the loaded save ===\n");
    const std::string save_dst = "assets/user.xml";
    std::ifstream src("reverse/data/users.xml", std::ios::binary);
    if (!src) {
        resf2::test::check(false, "H09: device users.xml readable");
        return;
    }
    {
        std::ofstream dst(save_dst, std::ios::binary | std::ios::trunc);
        dst << src.rdbuf();
    }

    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "dojo";
    config.hermetic = false;  // on_init loads the save (assets/user.xml)

    resf2::test::HeadlessTestRunner runner(config);
    const bool ok = runner.init();
    std::remove(save_dst.c_str());  // leave no trace in the repo
    if (!ok) {
        resf2::test::check(false, "H09: runner init");
        return;
    }
    const int lvl = runner.game().host_get_hud_level();
    const int gold = runner.game().host_get_hud_gold();
    std::fprintf(stderr, "  [H09] hud level=%d gold=%d (device save: Level=2 Money=129)\n",
                 lvl, gold);
    resf2::test::check_eq(lvl, 2, "H09: HUD level equals the loaded save Level (2)");
    resf2::test::check_eq(gold, 129, "H09: HUD gold equals the loaded save Money (129)");
    resf2::test::check(lvl != 7 && gold != 72450,
                       "H09: HUD no longer shows the invented constants 7/72450");
}

// H10: projectile damage/visuals come from the real magic item data
// (list.xml MagicDamage / Model attrs), not the invented palette
// (HARDCODE_AUDIT H10: {255,100,50} dmg 20 ...). Real device values:
// MAGIC_FIRE_BALL MagicDamage="322" Model="magic_fireball";
// MAGIC_ENERGY_BALL "372"; MAGIC_LIGHTNING_ARROW "609" (Model magic_fireball).
// The magic model files (magic_fireball.xml etc.) ship in the dump; colors
// are only a clearly-logged fallback (the original renders magic_* effect
// sequences per moves.xml — [HEURISTIC-TODO]).
static void test_h10_projectile_from_list() {
    std::printf("\n=== H10: projectile damage from list.xml MagicDamage ===\n");
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "dojo";
    config.hermetic = true;

    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) {
        resf2::test::check(false, "H10: runner init");
        return;
    }

    runner.game().host_add_item("MAGIC_FIRE_BALL");
    runner.game().host_add_item("MAGIC_ENERGY_BALL");
    runner.game().host_equip_item("MAGIC_FIRE_BALL");

    // Subtype spellings are the LIST.XML ones (SubType="EnergyBall",
    // SubType="IceBall") — the engine spawns with the equipped item's
    // subtype (sync_equipped_weapon), so list spelling is authoritative.
    const auto fb = runner.game().host_fire_projectile("FireBall");
    const auto eb = runner.game().host_fire_projectile("EnergyBall");
    const auto la = runner.game().host_fire_projectile("LightningArrow");
    std::fprintf(stderr, "  [H10] FireBall dmg=%.0f model='%s' | Energyball dmg=%.0f model='%s' | LightningArrow dmg=%.0f model='%s'\n",
                 fb.damage, fb.model_file.c_str(),
                 eb.damage, eb.model_file.c_str(),
                 la.damage, la.model_file.c_str());
    resf2::test::check_near(fb.damage, 322.0, 0.5,
                            "H10: FireBall damage = list.xml MagicDamage (322)");
    resf2::test::check_eq(fb.model_file, std::string("magic_fireball.xml"),
                          "H10: FireBall model = list.xml Model (magic_fireball.xml)");
    resf2::test::check_near(eb.damage, 372.0, 0.5,
                            "H10: Energyball damage = list.xml MagicDamage (372)");
    resf2::test::check_eq(eb.model_file, std::string("magic_energy_ball.xml"),
                          "H10: Energyball model = list.xml Model (magic_energy_ball.xml)");
    resf2::test::check_near(la.damage, 609.0, 0.5,
                            "H10: LightningArrow damage = list.xml MagicDamage (609)");
    resf2::test::check_eq(la.model_file, std::string("magic_fireball.xml"),
                          "H10: LightningArrow model = list.xml Model (magic_fireball.xml)");
    resf2::test::check(fb.damage != 20.0 && eb.damage != 25.0 && la.damage != 30.0,
                       "H10: no invented per-type damage values (20/25/30)");
}

// H05: the enemy's animations come from his weapon/model — a sword loadout
// attacks with the sword's moves.xml 1key move (SwordsSlash ->
// swords_slash.bin), not the hardcoded fists "high_punch"
// (HARDCODE_AUDIT H05, game.cpp:1980/2154/2166). RED on HEAD: the attack
// resolver did not exist and the executor played high_punch for every
// enemy.
static void test_h05_enemy_anims_from_weapon() {
    std::printf("\n=== H05: enemy anims from the loadout weapon ===\n");
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "battle";
    config.hermetic = true;

    // Sword loadout: the resolver must produce the sword's attack anim.
    {
        resf2::test::HeadlessTestRunner runner(config);
        if (!init_battle_runner(runner, "Man_Swords")) {
            resf2::test::check(false, "H05: sword runner init");
            return;
        }
        const std::string attack = runner.game().host_get_enemy_attack_anim();
        const std::string idle = runner.game().host_get_enemy_idle_anim();
        std::fprintf(stderr, "  [H05] sword enemy: attack='%s' idle='%s'\n",
                     attack.c_str(), idle.c_str());
        resf2::test::check_eq(attack, std::string("swords_slash"),
                              "H05: the sword loadout's attack anim is swords_slash");
        resf2::test::check(idle.find("fists") == std::string::npos,
                           "H05: the sword loadout's idle is NOT a fists anim");
        resf2::test::check_eq(idle, std::string("swords_stance_idle"),
                              "H05: the sword loadout's idle is swords_stance_idle");

        // End-to-end: once the enemy attacks, his animation must be the
        // sword move, never the hardcoded fists "high_punch".
        runner.run_frames(170);  // battle intro (stance_2)
        runner.tap_key(resf2::platform::Key::D, 2);
        runner.run_frames(10);
        std::string attack_anim_seen;
        const int kMaxFrames = 1200;  // 20 s — the AI attacks periodically
        for (int i = 0; i < kMaxFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_enemy_attacking()) {
                attack_anim_seen = runner.game().host_get_enemy_anim();
                break;
            }
        }
        std::fprintf(stderr, "  [H05] sword enemy attack anim played: '%s'\n",
                     attack_anim_seen.c_str());
        resf2::test::check(!attack_anim_seen.empty(),
                           "H05: the sword enemy attacked within the window");
        resf2::test::check(attack_anim_seen != "high_punch" &&
                               attack_anim_seen.find("fists") == std::string::npos,
                           "H05: the sword enemy plays a sword anim, not fists anims");
        resf2::test::check_eq(attack_anim_seen, std::string("swords_slash"),
                              "H05: the played attack anim is the real swords_slash");
    }

    // Fists loadout: the resolver keeps the real fist attack (high_punch).
    {
        resf2::test::HeadlessTestRunner runner(config);
        if (!init_battle_runner(runner, "Dojo_Disciple")) {
            resf2::test::check(false, "H05: fists runner init");
            return;
        }
        const std::string attack = runner.game().host_get_enemy_attack_anim();
        const std::string idle = runner.game().host_get_enemy_idle_anim();
        std::fprintf(stderr, "  [H05] fists enemy: attack='%s' idle='%s'\n",
                     attack.c_str(), idle.c_str());
        resf2::test::check_eq(attack, std::string("high_punch"),
                              "H05: the fists loadout keeps high_punch (real)");
        resf2::test::check_eq(idle, std::string("fists1_stance_idle"),
                              "H05: the fists loadout idles on fists1_stance_idle");
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

    // ---- H02 ----
    test_h02_weapon_model_from_list();

    // ---- H05 ----
    test_h05_enemy_anims_from_weapon();

    // ---- H06 ----
    test_h06_enemy_weapon_from_loadout();

    // ---- H09 ----
    test_h09_hud_from_save();

    // ---- H10 ----
    test_h10_projectile_from_list();

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
