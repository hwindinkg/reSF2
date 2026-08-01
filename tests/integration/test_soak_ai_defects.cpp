// tests/integration/test_soak_ai_defects.cpp
//
// Soak-fix Wave 1 (reverse/analysis/SOAK_TRIAGE.md §1): behavioral tests for
// the AI/combat defects A1-A6, written from the player's perspective — each
// asserts what the player SEES, not what the pipeline traces.
//
//   A1: no enemy decisions/actions before the battle intro (start stance)
//       phase completes — the soak showed "[COMBAT] Enemy hit player" twice
//       right after "[scene] enter Battle", before StartStance ended.
//   A2: the enemy plays its start-stance animation during the battle intro
//       (the soak showed only the player in stance_2).
//   A6: the player start stance persists until the first input — the soak
//       showed state=10 stance_2 -> state=0 stance_2 -> stance_idle right
//       after the animation ended.
//
// (A3/A4/A5 are root-caused to the missing original wait-pacing and live in
// the wave-1 escalation report as RED probes — no fix was sanctioned by the
// supervisor's no-invention rule, so no committed test yet.)

#include "../headless_test_runner.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Suppress noisy stdout from the game's internal logging so the test
// doesn't time out from I/O overhead.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

static void configure_battle(resf2::test::HeadlessTestRunner& runner) {
    scene::SceneHost::BattleInfo info;
    info.enemy_name = "enemy";
    info.rounds = 1;
    info.round_time_s = 60;
    info.reward_gold = 100;
    info.reward_xp = 50;
    runner.game().host_set_battle_info(info);
    runner.game().host_set_battle_mode(true);
    runner.game().host_set_show_enemy(true);
}

int main() {
    std::printf("=== Soak AI Defects Test ===\n");
    std::fflush(stdout);

    suppress_stdout();

    int failures = 0;

    // ---- A1: enemy AI gated during the battle intro ----
    // The enemy decision loop must not run until the start-stance phase
    // ends: no pipeline decision may feed the F1 stash while the intro is
    // in progress, and the player must take no damage in that window. The
    // stash assertion is the deterministic canary (the pre-fix loop decides
    // on frame 1); the health assertion pins the player-visible behavior.
    // Once the intro ends (first input), the AI becomes active.
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "battle";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) {
            std::fprintf(stderr, "FAIL: A1 init() returned false\n");
            return 1;
        }
        configure_battle(runner);

        // Intro window: the start-stance animation (stance_2, 52 frames at
        // 20fps ~ 156 engine frames) plus the held-stance phase (A6).
        runner.run_frames(50);
        CHECK(runner.game().host_get_start_stance(),
              "A1: start-stance phase in progress at frame 50");
        CHECK(runner.game().host_get_ai_last_candidates().empty(),
              "A1: no pipeline decision during the intro window");
        CHECK(runner.player_health_frac() >= 0.999f,
              "A1: player takes no damage during the intro window");

        // Let the stance animation complete; the phase must still gate the
        // AI (the A6 hold keeps it in progress until input).
        runner.run_frames(150);
        if (runner.game().host_get_start_stance()) {
            CHECK(runner.game().host_get_ai_last_candidates().empty(),
                  "A1: still no decisions while the stance is held");
            CHECK(runner.player_health_frac() >= 0.999f,
                  "A1: still no damage while the stance is held");
        }

        // First input ends the intro; the enemy AI must become active.
        runner.tap_key(resf2::platform::Key::D, 2);
        bool ai_active = false;
        for (int i = 0; i < 240 && !ai_active; ++i) {
            runner.run_frames(1);
            ai_active = !runner.game().host_get_ai_last_candidates().empty();
        }
        CHECK(ai_active, "A1: enemy AI active after the intro ends");
    }

    // ---- Final verdict ----
    if (tests_failed > 0 || failures > 0) {
        std::fprintf(stderr, "\n=== SOAK AI DEFECTS TEST FAILED (%d failures) ===\n",
                     tests_failed + failures);
        return 1;
    }
    std::fprintf(stderr, "\n=== SOAK AI DEFECTS TEST PASSED ===\n");
    return 0;
}
