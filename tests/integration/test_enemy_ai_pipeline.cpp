// tests/integration/test_enemy_ai_pipeline.cpp
//
// GAP-4 Phase D step D3 — the LIVE enemy-AI decision branch runs through the
// TacticDecisionPipeline + TacticDecisionAdapter (ADR-005 D1/D7).
//
// With the pipeline wired, the F1 overlay stash (ai_last_candidates_ /
// ai_last_weights_ / ai_last_distance_ / ai_last_pick_, ADR C5) is fed from
// the DecisionTrace + adapter result: the candidate rows are the SEVEN stage
// line-groups in the tracer's fixed order (UseDefense .. UseCautiousMovements)
// instead of the legacy roulette categories ("ForwardStep", "ShortAttack",
// ...), and enemy_ai_state_ is the adapter's legacy state int the executor
// reads (0..4).
//
// This test is the wiring canary: it FAILS on the D2-era code (roulette
// stash) and passes once the live block routes through decide().

#include "../headless_test_runner.hpp"

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
    std::printf("=== Enemy AI Pipeline Wiring Test ===\n");
    std::fflush(stdout);

    suppress_stdout();

    int failures = 0;

    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "battle";
        config.hermetic = true;  // deterministic empty inventory -> fists

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) {
            std::fprintf(stderr, "FAIL: init() returned false\n");
            return 1;
        }
        configure_battle(runner);

        // Run until the pipeline has fed the stash one full decision (the
        // seven stage rows), or give up. Decisions fire every
        // enemy_ai_decision_interval_ = 0.8 s (~48 frames at 16 ms).
        bool decided = false;
        const int kMaxFrames = 900;  // 15 s — plenty for several decisions
        for (int i = 0; i < kMaxFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_ai_last_candidates().size() >= 7) {
                decided = true;
                break;
            }
        }

        const auto& g = runner.game();
        const std::vector<std::string>& cand = g.host_get_ai_last_candidates();
        const std::vector<float>& weights = g.host_get_ai_last_weights();
        const int st = g.host_get_enemy_ai_state();
        const std::string& pick = g.host_get_ai_last_pick();

        if (!decided) {
            std::fprintf(stderr,
                "FAIL: no pipeline decision within %d frames (stash has %zu rows)\n",
                kMaxFrames, cand.size());
            ++failures;
        } else {
            // The candidate rows are the DecisionTrace stage line-groups in
            // the tracer's fixed order (PORT_GAPS.md:171-178): the roulette
            // would yield {"ForwardStep", "ShortAttack", ...} instead.
            // Rows 0..3 are the fixed stages; the looped stages repeat per
            // tactic entry (QuickAttack[i]*, then Evade[i]*), so only their
            // relative order and the trailing UseCautiousMovements are pinned.
            CHECK(cand.size() >= 7, "stash has at least the seven stage rows");
            CHECK(cand[0] == "UseDefense" && cand[1] == "UseSafeAttack" &&
                  cand[2] == "TableAttack" && cand[3] == "DodgeMissiles",
                  "fixed stages 1-4 in tracer order");

            std::size_t idx = 4;
            while (idx < cand.size() &&
                   cand[idx].rfind("QuickAttack", 0) == 0) ++idx;
            while (idx < cand.size() && cand[idx].rfind("Evade", 0) == 0) ++idx;
            CHECK(idx < cand.size() && cand[idx] == "UseCautiousMovements",
                  "QuickAttack/Evade rows precede UseCautiousMovements");

            // The F1 overlay indexes candidates and weights together — they
            // must stay parallel (ADR C5).
            CHECK(cand.size() == weights.size(), "candidates/weights parallel");

            // The adapter result is the legacy state int the executor reads.
            CHECK(st >= 0 && st <= 4, "enemy_ai_state_ in legacy range 0..4");

            // The pick is the winning stage (+ adapter animation).
            CHECK(!pick.empty(), "ai_last_pick_ non-empty");

            // The overlay shows the ctx.distance that drove the decision.
            CHECK(g.host_get_ai_last_distance() >= 0.0f, "ai_last_distance_ valid");
        }

        std::fprintf(stderr,
            "Decided=%d candidates=%zu state=%d pick='%s' dist=%.0f\n",
            decided ? 1 : 0, cand.size(), st, pick.c_str(),
            g.host_get_ai_last_distance());
    }

    // ---- Final verdict ----
    if (tests_failed > 0 || failures > 0) {
        std::fprintf(stderr, "\n=== ENEMY AI PIPELINE WIRING TEST FAILED (%d failures) ===\n",
                     tests_failed + failures);
        return 1;
    }
    std::fprintf(stderr, "\n=== ENEMY AI PIPELINE WIRING TEST PASSED ===\n");
    return 0;
}
