// tests/integration/test_soak_quest_defects.cpp
//
// Soak-fix Wave 3 (reverse/analysis/SOAK_TRIAGE.md §3 Dojo/quest + §7 log
// spam): behavioral tests for Q1, Q2, Q3 and L1, written from the player's
// perspective — each asserts what the player SEES, not what the pipeline
// traces.
//
//   Q1: the quest movement stage (the Sensei "move!" hint scroll) must not
//       complete on a SINGLE step. A d press walks ~50 units; the old check
//       (displacement > 25) dismissed the hint on the first press. The
//       original asks the player to take a few steps (forward/back).
//   Q2: the punching bag must VISIBLY react when hit. The soak showed hits
//       counting progress while the bag hung motionless (only the distance
//       fallback fired — it counts progress but applies no impulse).
//   Q3: after the bag phase, the Sensei dialog must hand over to the Kenji
//       fight (stages.xml Zone=Punchbag Battle=Training Fight=2, warrior
//       Dojo_Disciple) instead of returning to the dojo.
//   L1: the map scene must log "[MAP] round_progress" once per progress
//       change, not once per frame (~150 lines/frame in the soak log).
//
// All four probes FAIL on the current engine (RED evidence committed with
// this file):
//   [Q1] hint dismissed after 1 step (displacement 50 > 25)
//   [Q2] bag displacement stays 0 while the hit counter climbs (fallback)
//   [Q3] no battle queued after the bag phase; dialog returns to the dojo
//   [L1] ~300 round_progress lines over 300 frames
//
// The fixes are implemented test-first: no fix before these RED tests.

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

// Suppress noisy stdout from the game's internal logging so the test
// doesn't time out from I/O overhead. FAIL diagnostics go to stderr.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;
namespace scn = resf2::scene;

// ---------- deterministic key driving ----------
// run_frames() calls poll_events() FIRST, which clears keys_just_pressed, so
// an edge injected between run_frames() calls is wiped before on_update().
// Edges must therefore be delivered on a manually-driven frame (poll, inject,
// update, render, advance) — the same pattern tap_key() uses. The manual
// frame must NOT poll again, or it would wipe the edge it was built to carry.

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

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
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

// One step: press, release, and wait for the full step cycle to end.
static void step_tap(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    edge_down(r, k);
    r.run_frames(2);
    edge_up(r, k);
    for (int i = 0; i < 70; ++i) {
        r.run_frames(1);
        if (r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
}

// Step toward the bag until within ~120 world units of it. At this range the
// precise edge collision misses (the attacking limb cannot reach), so a hit
// registers through the DISTANCE FALLBACK — the exact situation the soak hit:
// progress counted while the bag hangs motionless.
static void walk_to_bag(resf2::test::HeadlessTestRunner& r) {
    for (int i = 0; i < 8; ++i) {
        const float bag_x = r.game().host_get_enemy_pos_x();
        const float px = r.game().host_get_player_pos_x();
        const float dist = std::fabs(bag_x - px);
        if (dist < 120.0f) {
            std::fprintf(stderr, "  [walk] reached dist=%.0f after %d step(s)\n",
                         dist, i);
            return;
        }
        const bool bag_right = bag_x > px;
        step_tap(r, bag_right ? plat::Key::D : plat::Key::A);
    }
}

// One kick (P): fires HighKick, plays through the attack interval, recovers.
static void kick(resf2::test::HeadlessTestRunner& r) {
    edge_down(r, plat::Key::P);
    r.run_frames(2);
    edge_up(r, plat::Key::P);
    r.run_frames(60);
}

int main() {
    std::printf("=== Soak Quest Defects Test (Q1/Q2/Q3 + L1) ===\n");
    std::fflush(stdout);
    suppress_stdout();

    // ---- Q1: the movement stage needs >= 4 step events, not 1 press ----
    // The Sensei "move!" hint scroll is the observable quest movement stage.
    // It must stay up after a single d press (one step) and dismiss only
    // after a few steps — the original asks the player to move forward/back
    // several times.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: Q1 init() returned false\n"); return 1; }
        warm_up(runner);
        // A save mid-tutorial (BAG) shows the hint scroll on dojo entry.
        runner.game().host_set_tutorial_state("BAG");
        runner.game().host_reset_menu_state();
        runner.run_frames(40);  // overlay grace period
        CHECK(runner.game().host_get_movement_hint_visible(),
              "Q1: movement hint scroll is up at the start");
        const float x0 = runner.game().host_get_player_pos_x();
        step_tap(runner, plat::Key::D);
        std::fprintf(stderr, "  [Q1] one d press walked %.0f units, hint visible=%d\n",
                     runner.game().host_get_player_pos_x() - x0,
                     (int)runner.game().host_get_movement_hint_visible());
        CHECK(runner.game().host_get_movement_hint_visible(),
              "Q1: a SINGLE step does not complete the movement stage");
        // Three more steps (4 total): the stage completes.
        for (int i = 0; i < 3; ++i) step_tap(runner, plat::Key::D);
        CHECK(!runner.game().host_get_movement_hint_visible(),
              "Q1: the movement stage completes after 4 step events");
    }

    // ---- Q2: the bag VISIBLY reacts when hit ----
    // The soak showed progress counting while the bag never moved: only the
    // distance fallback fired (it counts a hit but applies no impulse to the
    // Verlet nodes). A registered hit must displace the bag.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: Q2 init() returned false\n"); return 1; }
        warm_up(runner);
        runner.game().host_set_tutorial_state("BAG");  // bag phase active
        walk_to_bag(runner);

        const int hits0 = runner.game().host_get_tutorial_bag_hits();
        const char* cap = "test_soak_quest_q2_stdout.tmp";
        std::freopen(cap, "w", stdout);  // capture which hit path fires
        kick(runner);
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif
        const int hits1 = runner.game().host_get_tutorial_bag_hits();
        {
            std::ifstream f(cap);
            std::string line;
            while (std::getline(f, line))
                if (line.find("[COMBAT] HIT!") != std::string::npos ||
                    line.find("distance fallback") != std::string::npos)
                    std::fprintf(stderr, "  [Q2] hit-path: %s\n", line.c_str());
        }
        std::remove(cap);
        float peak = 0.0f;
        for (int i = 0; i < 120; ++i) {
            runner.run_frames(1);
            const float d = runner.game().host_get_bag_displacement();
            if (d > peak) peak = d;
        }
        std::fprintf(stderr,
                     "  [Q2] hits %d -> %d, peak bag displacement %.2f (y_adjust %.1f)\n",
                     hits0, hits1, peak, runner.game().host_get_y_adjust());
        CHECK(hits1 > hits0,
              "Q2: a close-range kick registers a hit on the bag");
        CHECK(peak > 5.0f,
              "Q2: the bag displaces visibly within ~2 s of the hit");
    }

    // ---- Q3: after the bag phase the Sensei dialog hands over to the
    //      Kenji (Dojo_Disciple) fight ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: Q3 init() returned false\n"); return 1; }
        warm_up(runner);
        runner.game().host_set_tutorial_state("BAG");
        walk_to_bag(runner);

        // Three spaced kicks = three registered bag hits -> FIRST_FIGHT.
        for (int i = 0; i < 3; ++i) {
            kick(runner);
            runner.run_frames(20);
        }
        std::fprintf(stderr, "  [Q3] bag hits=%d tutorial_state='%s'\n",
                     runner.game().host_get_tutorial_bag_hits(),
                     runner.game().host_get_tutorial_state().c_str());
        CHECK(runner.game().host_get_tutorial_bag_hits() >= 3,
              "Q3: three hits land on the bag");
        CHECK(runner.game().host_get_tutorial_state() == "COMPLETE",
              "Q3: the bag phase advances the tutorial state");

        // The FIRST_FIGHT dialog auto-triggers -> Dialogue scene.
        bool in_dialogue = false;
        for (int i = 0; i < 60; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_current_scene() == scn::SceneId::Dialogue) {
                in_dialogue = true;
                break;
            }
        }
        CHECK(in_dialogue, "Q3: the Kenji intro dialog opens after the bag phase");
        const std::string loc = runner.game().host_get_battle_location();
        std::fprintf(stderr, "  [Q3] battle_location='%s' after bag phase\n",
                     loc.c_str());
        CHECK(loc == "dojo",
              "Q3: the training fight is queued behind the dialog (dojo)");

        // Advancing the dialog must enter the Battle scene, not the dojo.
        runner.tap_key(plat::Key::Space, 1);
        bool in_battle = false;
        for (int i = 0; i < 90; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_current_scene() == scn::SceneId::Battle) {
                in_battle = true;
                break;
            }
        }
        CHECK(in_battle,
              "Q3: the dialog hands over to the Kenji fight (Battle scene)");
    }

    // ---- L1: round_progress is logged once per state change ----
    // The map scene printed "[MAP] round_progress" every frame (~150 lines
    // in the soak log). Over 300 frames a change-gated log emits at most one
    // line per distinct (zone, battle, done, total) tuple.
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 320;
        config.height = 180;
        config.fixed_dt_ms = 16;
        config.hermetic = true;
        config.start_scene = "map";
        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) { std::fprintf(stderr, "FAIL: L1 init() returned false\n"); return 1; }

        const char* cap = "test_soak_quest_map_stdout.tmp";
        std::freopen(cap, "w", stdout);
        runner.run_frames(300);
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif

        int lines = 0;
        {
            std::ifstream f(cap);
            std::string line;
            while (std::getline(f, line))
                if (line.find("[MAP] round_progress") != std::string::npos) ++lines;
        }
        std::remove(cap);
        std::fprintf(stderr, "  [L1] round_progress lines over 300 frames: %d\n",
                     lines);
        CHECK(lines <= 2,
              "L1: round_progress logged once per progress change, not per frame");
    }

    // ---- Final verdict ----
    if (tests_failed > 0) {
        std::fprintf(stderr, "\n=== SOAK QUEST DEFECTS TEST FAILED (%d failures) ===\n",
                     tests_failed);
        return 1;
    }
    std::fprintf(stderr, "\n=== SOAK QUEST DEFECTS TEST PASSED ===\n");
    return 0;
}
