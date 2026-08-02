// tests/integration/test_soak_parser_defects.cpp
//
// Soak-fix Wave 7a (reverse/analysis/SOAK_TRIAGE.md §8 parser-level defects,
// re-soak 2): behavioral tests for P1, P2, P3, P7 — written from the
// player's perspective, each asserting what the player SEES:
//
//   P1: equipping WEAPON_KNIVES must resolve the model via the list.xml
//       `Model` attribute ("weapon_knives"), not a name-mangled guess
//       ("weapon_knive.xml" — the soak log's NOT FOUND line). The weapon
//       must load real geometry (>0 nodes) with no "model NOT FOUND" log.
//   P2: battle hit detection must test the attacker's attack edges against
//       the ENEMY FIGHTER's model edges (body.xml/head.xml capsules), never
//       the punching bag. The bag exists only in the dojo; in a battle no
//       `bag_edge=` collision may fire, and damage to the enemy must come
//       from a fighter hit. Dojo bag behavior stays intact.
//   P3: equipped armor/helm (users.xml Armor="ARMOR_ROBE" Helm="Head",
//       list.xml Model= attr) must load their model files (armor_robe.xml,
//       head_kenji.xml) and the render path must consume them.
//   P7: holding S once must duck exactly once; the held key must not
//       auto-repeat the move (soak: "[MOVE] Duck" ×16 with no key events).
//
// RED on HEAD (2026-08-02): P1 weapon_knive.xml NOT FOUND / 0 nodes,
// P2 bag_edge collisions fire in battles, P3 armor model never loads,
// P7 duck restarts every animation cycle while S is held.

#include "../headless_test_runner.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;
namespace scn = resf2::scene;

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

static resf2::test::HeadlessTestRunner make_battle_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = "battle";
    return resf2::test::HeadlessTestRunner(config);
}

// Manual frame: poll, update, render, advance — the same pattern tap_key
// uses. poll_events() clears the just-pressed edges, so injected input must
// land after it and before on_update().
static void frame(resf2::test::HeadlessTestRunner& r) {
    r.game().on_update(r.platform(), 16);
    r.game().on_render(r.platform());
    r.platform().advance_time_ms(16);
}

// Press edge + one update frame with the key held.
static void edge_down(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_down(k);
    frame(r);
}

// Release edge + one update frame with the key released.
static void edge_up(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_up(k);
    frame(r);
}

// Every scenario starts with the battle intro: the start-stance animation
// must run to completion before the A6 hold can be broken.
static void warm_up(resf2::test::HeadlessTestRunner& r) {
    r.run_frames(330);              // intro stance animation runs to completion
    r.tap_key(plat::Key::D, 2);     // first input ends the A6 hold
    for (int i = 0; i < 80; ++i) {  // settle into stance_idle
        r.run_frames(1);
        if (!r.game().host_get_start_stance() &&
            r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
    r.run_frames(10);
}

// Step toward the bag (enemy spawn) until within ~120 world units of it.
static void walk_to_bag(resf2::test::HeadlessTestRunner& r) {
    for (int i = 0; i < 8; ++i) {
        const float bag_x = r.game().host_get_enemy_pos_x();
        const float px = r.game().host_get_player_pos_x();
        const float dist = std::fabs(bag_x - px);
        if (dist < 120.0f) return;
        edge_down(r, bag_x > px ? plat::Key::D : plat::Key::A);
        r.run_frames(40);
        edge_up(r, bag_x > px ? plat::Key::D : plat::Key::A);
    }
}

// One punch (O): fires the move, plays through the attack interval.
static void punch(resf2::test::HeadlessTestRunner& r) {
    edge_down(r, plat::Key::O);
    r.run_frames(2);
    edge_up(r, plat::Key::O);
    r.run_frames(50);
}

// ---------- P1: weapon model filename from the list.xml Model attr ----------

static void test_p1_weapon_model() {
    std::printf("\n=== P1: WEAPON_KNIVES resolves weapon_knives.xml ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P1 init() returned false\n"); ++tests_failed; return; }
    warm_up(runner);

    const char* cap = "test_soak_p1_stdout.tmp";
    std::freopen(cap, "w", stdout);
    runner.game().host_add_currency(1000);
    const bool bought = runner.game().host_buy_item("WEAPON_KNIVES");
    const bool equipped = runner.game().host_equip_item("WEAPON_KNIVES");
    runner.run_frames(10);
    std::fflush(stdout);
    suppress_stdout();

    std::fprintf(stderr, "  [P1] bought=%d equipped=%d equipped_weapon='%s'\n",
                 (int)bought, (int)equipped,
                 runner.game().host_get_equipped("weapon").c_str());

    const std::size_t nodes = runner.game().host_get_player_weapon_node_count();
    std::fprintf(stderr, "  [P1] player weapon model nodes: %zu\n", nodes);
    CHECK(nodes > 0, "P1: equipping WEAPON_KNIVES loads the weapon model (>0 nodes)");

    bool not_found_logged = false;
    {
        std::ifstream f(cap);
        std::string line;
        while (std::getline(f, line))
            if (line.find("model NOT FOUND") != std::string::npos) {
                std::fprintf(stderr, "  [P1] %s\n", line.c_str());
                not_found_logged = true;
            }
    }
    std::remove(cap);
    CHECK(!not_found_logged,
          "P1: no 'model NOT FOUND' log when equipping WEAPON_KNIVES");
}

// ---------- P2: battle hit detection targets the enemy fighter ----------

static void test_p2_battle_hit_target() {
    std::printf("\n=== P2: battle hits the ENEMY fighter, not the bag ===\n");
    {
        resf2::test::HeadlessTestRunner runner = make_battle_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: P2 battle init() failed\n"); ++tests_failed; return; }
        scn::SceneHost::BattleInfo info;
        info.enemy_name = "enemy";
        info.rounds = 1;
        info.round_time_s = 60;
        info.reward_gold = 100;
        info.reward_xp = 50;
        runner.game().host_set_battle_info(info);
        runner.game().host_set_battle_mode(true);
        runner.game().host_set_show_enemy(true);
        runner.run_frames(100);  // battle intro

        const float hp0 = runner.enemy_health_frac();
        const char* cap = "test_soak_p2_stdout.tmp";
        std::freopen(cap, "w", stdout);
        // Walk toward the enemy and punch periodically (same driving as
        // test_full_battle: hold forward most frames, punch every 30).
        for (int i = 0; i < 1100; ++i) {
            if (i % 60 < 45) {
                runner.platform().poll_events();
                runner.platform().inject_key_down(plat::Key::ArrowRight);
                frame(runner);
            } else {
                runner.platform().poll_events();
                runner.platform().inject_key_up(plat::Key::ArrowRight);
                frame(runner);
            }
            if (i % 30 == 0) {
                edge_down(runner, plat::Key::O);
                runner.run_frames(2);
                edge_up(runner, plat::Key::O);
            }
        }
        std::fflush(stdout);
        suppress_stdout();

        const float hp1 = runner.enemy_health_frac();
        std::fprintf(stderr, "  [P2] enemy hp %.3f -> %.3f\n", hp0, hp1);

        int bag_hits = 0, enemy_hits = 0;
        {
            std::ifstream f(cap);
            std::string line;
            while (std::getline(f, line)) {
                if (line.find("bag_edge=") != std::string::npos) {
                    if (bag_hits < 3)
                        std::fprintf(stderr, "  [P2] bag collision: %s\n", line.c_str());
                    ++bag_hits;
                }
                if (line.find("[COMBAT] Player hit enemy:") != std::string::npos) {
                    if (enemy_hits < 3)
                        std::fprintf(stderr, "  [P2] enemy hit: %s\n", line.c_str());
                    ++enemy_hits;
                }
            }
        }
        std::remove(cap);
        std::fprintf(stderr, "  [P2] bag_edge hits=%d, enemy damage lines=%d\n",
                     bag_hits, enemy_hits);
        CHECK(bag_hits == 0,
              "P2: no bag_edge collisions in a battle vs the enemy fighter");
        CHECK(enemy_hits > 0,
              "P2: the enemy fighter takes damage from player hits in battle");
        CHECK(hp1 < hp0 - 0.01f,
              "P2: the enemy's health drops during the battle");
    }

    // Dojo bag behavior must stay intact: the bag is the dojo's target.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: P2 dojo init() failed\n"); ++tests_failed; return; }
        warm_up(runner);
        runner.game().host_set_tutorial_state("BAG");
        runner.game().host_reset_menu_state();
        runner.run_frames(40);
        walk_to_bag(runner);
        const int hits0 = runner.game().host_get_tutorial_bag_hits();
        punch(runner);
        const int hits1 = runner.game().host_get_tutorial_bag_hits();
        float peak = 0.0f;
        for (int i = 0; i < 120; ++i) {
            runner.run_frames(1);
            const float d = runner.game().host_get_bag_displacement();
            if (d > peak) peak = d;
        }
        std::fprintf(stderr, "  [P2/dojo] bag hits %d -> %d, peak displacement %.2f\n",
                     hits0, hits1, peak);
        CHECK(hits1 > hits0, "P2/dojo: a punch registers a hit on the bag");
        CHECK(peak > 5.0f, "P2/dojo: the bag visibly displaces (dojo behavior intact)");
    }
}

// ---------- P3: equipped armor/helm models load and render ----------

static void test_p3_armor_helm() {
    std::printf("\n=== P3: equipped armor/helm models attach to the fighter ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P3 init() returned false\n"); ++tests_failed; return; }
    warm_up(runner);

    // users.xml reference: Armor="ARMOR_ROBE" Helm="Head". The shop cannot
    // sell them here (ARMOR_ROBE is level-gated, helms are ShopHide), so the
    // items land in the inventory through the host_add_item test seam and
    // are then equipped through the real equip path. HEAD_KENJI is a real
    // helm item (list.xml Model="head_kenji").
    runner.game().host_add_item("ARMOR_ROBE");
    const bool equipped_armor = runner.game().host_equip_item("ARMOR_ROBE");
    runner.game().host_add_item("HEAD_KENJI");
    const bool has_helm_item = runner.game().host_has_item("HEAD_KENJI");
    const bool equipped_helm = runner.game().host_equip_item("HEAD_KENJI");
    runner.run_frames(10);

    std::fprintf(stderr, "  [P3] armor equipped=%d | helm item=%d equipped=%d\n",
                 (int)equipped_armor, (int)has_helm_item, (int)equipped_helm);

    const std::size_t armor_nodes = runner.game().host_get_armor_model_node_count();
    const std::size_t armor_caps = runner.game().host_get_armor_model_capsule_count();
    const std::size_t helm_nodes = runner.game().host_get_helm_model_node_count();
    std::fprintf(stderr, "  [P3] armor model: %zu nodes, %zu capsules; helm model: %zu nodes\n",
                 armor_nodes, armor_caps, helm_nodes);
    CHECK(armor_nodes > 0, "P3: ARMOR_ROBE loads armor_robe.xml (>0 nodes)");
    CHECK(armor_caps > 0, "P3: the armor model carries capsules (edge attach)");
    CHECK(helm_nodes > 0, "P3: the helm model loads (>0 nodes)");

    // The render path must consume the armor model (player body render).
    runner.run_frames(20);
    const int drawn = runner.game().host_get_armor_capsules_drawn();
    std::fprintf(stderr, "  [P3] armor capsules drawn by the renderer: %d\n", drawn);
    CHECK(drawn > 0, "P3: the render path draws the equipped armor over the body");
}

// ---------- P7: held S ducks once, no auto-repeat ----------

static void test_p7_duck_auto_repeat() {
    std::printf("\n=== P7: holding S ducks once and holds; punch-out works ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P7 init() returned false\n"); ++tests_failed; return; }
    warm_up(runner);
    // The soak's input theft ("[MOVE] Punch dir=Down -> Duck") happens with a
    // weapon equipped: the fists-only down punch (LowPunch) is locked out, so
    // the empty-move_type allowance leaves Duck as the only "Punch" candidate.
    runner.game().host_add_item("WEAPON_KNIVES");
    runner.game().host_equip_item("WEAPON_KNIVES");
    runner.run_frames(10);

    const char* cap = "test_soak_p7_stdout.tmp";
    std::freopen(cap, "w", stdout);
    // Hold S for ~2.4 s (150 frames) without any other input. run_frames()
    // polls every frame, so the just-pressed edge clears after the first
    // update — this is how the real game sees a held key.
    runner.platform().poll_events();
    runner.platform().inject_key_down(plat::Key::S);
    frame(runner);
    for (int i = 0; i < 149; ++i) runner.run_frames(1);
    std::fflush(stdout);

    int duck_found = 0, move_duck = 0;
    {
        std::ifstream f(cap);
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("[DUCK] found:") != std::string::npos) ++duck_found;
            if (line.find("[MOVE] Duck") != std::string::npos) ++move_duck;
        }
    }
    std::fprintf(stderr, "  [P7] [DUCK] found x%d, [MOVE] Duck x%d over 150 held frames\n",
                 duck_found, move_duck);
    CHECK(move_duck == 1, "P7: one S press fires the Duck move exactly once");

    const std::string held_anim = runner.game().host_get_player_anim();
    const int held_state = runner.game().host_get_player_move_state();
    std::fprintf(stderr, "  [P7] end of hold: anim='%s' move_state=%d\n",
                 held_anim.c_str(), held_state);
    CHECK(duck_found == 1,
          "P7: the held-key continuation fires exactly once (no auto-repeat)");
    CHECK(held_anim == "duck",
          "P7: holding S keeps the fighter ducking (crouch holds, not a one-shot)");

    // Punch while holding S: the input must NOT be stolen by Duck. The
    // theft fires when the real down-punch is unavailable — a second O tap
    // during the first attack's recovery is self-rejected (same move cannot
    // chain), and with knives the fists-only LowPunch is weapon-locked, so
    // the empty-move_type allowance leaves Duck as the only "Punch"
    // candidate (soak: "[MOVE] Punch dir=Down -> Duck").
    {
        std::freopen(cap, "w", stdout);
        edge_down(runner, plat::Key::O);
        runner.run_frames(2);
        edge_up(runner, plat::Key::O);
        runner.run_frames(60);   // past Uninterrupt (End=16) + attack interval
        edge_down(runner, plat::Key::O);
        runner.run_frames(2);
        edge_up(runner, plat::Key::O);
        runner.run_frames(25);
        std::fflush(stdout);
        suppress_stdout();
        bool stolen = false;
        {
            std::ifstream f(cap);
            std::string line;
            while (std::getline(f, line))
                if (line.find("-> Duck") != std::string::npos) {
                    std::fprintf(stderr, "  [P7] theft: %s\n", line.c_str());
                    stolen = true;
                }
        }
        CHECK(!stolen,
              "P7: a punch while holding S never selects Duck (no input theft)");
    }
    std::remove(cap);

    // After release the fighter stands back up.
    runner.platform().poll_events();
    runner.platform().inject_key_up(plat::Key::S);
    frame(runner);
    for (int i = 0; i < 60; ++i) {
        runner.run_frames(1);
        if (runner.game().host_get_player_move_state() == 0 &&
            runner.game().host_get_player_anim() == "stance_idle")
            break;
    }
    CHECK(runner.game().host_get_player_move_state() == 0,
          "P7: releasing S ends the duck (fighter stands up)");
}

int main() {
    std::printf("=== Soak Parser Defects Test (P1/P2/P3/P7) ===\n");
    std::fflush(stdout);

    test_p1_weapon_model();
    test_p2_battle_hit_target();
    test_p3_armor_helm();
    test_p7_duck_auto_repeat();

    std::printf("\n=== Soak Parser Defects: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
