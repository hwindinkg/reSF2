// tests/integration/test_soak_ai_pacing.cpp
//
// Soak-fix Wave 1 — A3/A4/A5 behavioral probes (SOAK_TRIAGE.md §1), ported
// from the human-approved escalation package
// (test_soak_ai_escalation.cpp + A3A4A5_RED_OUTPUT.txt, 2026-08-01).
//
// Root cause (established by debug-specialist, approved by human): decisions
// re-roll every frame because the engine's only re-entry gate is
// ResponseDelay (shipped 0/0) and the computed wait_frames is never APPLIED
// in the executor. The original re-decides only when the per-decision Wait
// countdown (decision+0x12) expires — the duration arithmetic of
// DECISION_SEMANTICS.md R4 §3.1 (VERIFY_R34.md GREEN):
//
//   attack path:      max(animFrames, min(animRange, (speedVal-damage)+1)) - 1
//   ready path:       animFrames
//   use-defense path: (c660(enemy,1) - damage) + 1
//
// All three probes FAIL on the current engine (RED evidence committed with
// this file):
//   [A3] closed=8.6 < 50 — one-frame step decisions (1.44 px) cover no ground
//   [A4] transitions=83 > 60 — the displayed decision flips every frame
//   [A5] damage=0.660 > 0.35 — attacks fire every cooldown expiry
//
// The fix (wait computation with REAL data + executor hold) is implemented
// test-first: no fix before these RED tests.

#include "../headless_test_runner.hpp"

#include <cmath>
#include <cstdio>
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

// [Soak A6] Let the 52-frame intro animation complete, then break the held
// stance with the first input so the fight (and the enemy AI) begins.
static void start_fight(resf2::test::HeadlessTestRunner& runner) {
    runner.run_frames(170);
    runner.tap_key(resf2::platform::Key::D, 2);
    runner.run_frames(10);
}

int main() {
    std::printf("=== Soak AI Pacing Probes (A3/A4/A5) ===\n");
    std::fflush(stdout);
    suppress_stdout();

    // ---- A3: enemy closes distance within N seconds of round start ----
    // The soak showed zero sustained enemy step animations — with tactic
    // Standard the enemy never approaches. A step decision must SUSTAIN (the
    // original holds each decision for its wait countdown), so the enemy
    // covers ground; one-frame steps at 90px/s * 16ms = 1.4px per decision
    // cannot.
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "battle";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) return 1;
        configure_battle(runner);
        start_fight(runner);

        const float ex0 = runner.game().host_get_enemy_pos_x();
        const float px = runner.game().host_get_player_pos_x();
        const float start_dist = std::fabs(ex0 - px);
        runner.run_frames(600);  // 10 s of fight
        const float end_dist = std::fabs(runner.game().host_get_enemy_pos_x() -
                                         runner.game().host_get_player_pos_x());
        const float closed = start_dist - end_dist;
        std::fprintf(stderr, "[A3] start_dist=%.1f end_dist=%.1f closed=%.1f\n",
                     start_dist, end_dist, closed);
        CHECK(closed >= 50.0f,
              "A3: enemy closes >= 50 world units within 10 s of round start");
    }

    // ---- A4: AI holds a decision for >= response-delay frames ----
    // The F1 overlay showed the decision flipping every frame. A decision
    // must persist (the original's wait countdown): over 120 sampled frames
    // the displayed pick must NOT change on the majority of frames.
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "battle";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) return 1;
        configure_battle(runner);
        start_fight(runner);

        std::string prev;
        int transitions = 0;
        int samples = 0;
        for (int i = 0; i < 120; ++i) {
            runner.run_frames(1);
            const std::string& pick = runner.game().host_get_ai_last_pick();
            if (pick.empty()) continue;
            if (!prev.empty() && pick != prev) ++transitions;
            prev = pick;
            ++samples;
        }
        std::fprintf(stderr, "[A4] samples=%d transitions=%d\n", samples, transitions);
        CHECK(samples >= 100 && transitions <= samples / 2,
              "A4: the displayed AI decision does not flip on the majority of frames");
    }

    // ---- A5: damage intake on approach is bounded ----
    // The soak showed constant 0.110 hits while the player closed in. With
    // the enemy's attack cadence faithful to the original (attack decisions
    // paced by animation durations + the ExpectedWait gate), a 10 s approach
    // must not cost more than ~3 hits (~35% of max health).
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "battle";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) return 1;
        configure_battle(runner);
        start_fight(runner);

        const float hp0 = runner.player_health_frac();
        runner.inject_key_down(resf2::platform::Key::D);  // approach the enemy
        runner.run_frames(600);  // 10 s
        runner.inject_key_up(resf2::platform::Key::D);
        const float damage = hp0 - runner.player_health_frac();
        std::fprintf(stderr, "[A5] hp0=%.3f hp_end=%.3f damage=%.3f\n",
                     hp0, runner.player_health_frac(), damage);
        CHECK(damage <= 0.35f,
              "A5: approach damage bounded at ~35% of max health over 10 s");
    }

    std::fprintf(stderr, "\n=== SOAK AI PACING PROBES: %d passed, %d failed ===\n",
                 tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
