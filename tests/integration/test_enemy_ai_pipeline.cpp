// tests/integration/test_enemy_ai_pipeline.cpp
//
// GAP-4 Phase D+E — the LIVE enemy-AI decision branch runs through the
// TacticDecisionPipeline (ADR-005 D1/D7): re-entry on the loaded path is
// gated by the ResponseDelay frame countdown (ADR-005 D8), and Phase E
// deleted the TacticDecisionAdapter, the legacy enemy_ai_state_ int and the
// invented enemy_ai_decision_interval_ (the countdown is the ONLY gate).
//
// With the pipeline wired, the F1 overlay stash (ai_last_candidates_ /
// ai_last_weights_ / ai_last_distance_ / ai_last_pick_, ADR C5) is fed from
// the DecisionTrace: the candidate rows are the SEVEN stage line-groups in
// the tracer's fixed order (UseDefense .. UseCautiousMovements) instead of
// the legacy roulette categories ("ForwardStep", "ShortAttack", ...), and
// the execute block consumes the stored TacticDecision directly (E2).
//
// This test is the wiring canary: it FAILS on the D2-era code (roulette
// stash) and passes once the live block routes through decide().
//
// The D4 section is the gate canary: the loaded path decides through the
// ResponseDelay countdown (starts at 0 = unblocked). The countdown window
// contract (roll within [Min,Max], tick to exactly 0, no re-entry before 0)
// is pinned both against the SHIPPED tactic data (ResponseDelay 0/0,
// EnemyResponseDelay 30/60) and at the TacticMemory mechanism level.
// The E3 section pins the no-settings path (ADR P4 / GATE GE): settings
// absent -> traced idle/wait decision, neutral enemy, no rand()%100
// roulette anywhere.

#include "../headless_test_runner.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/game/tactic_memory.hpp"

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

            // The pick is the winning stage (+ animation).
            CHECK(!pick.empty(), "ai_last_pick_ non-empty");

            // The overlay shows the ctx.distance that drove the decision.
            CHECK(g.host_get_ai_last_distance() >= 0.0f, "ai_last_distance_ valid");
        }

        std::fprintf(stderr,
            "Decided=%d candidates=%zu pick='%s' dist=%.0f\n",
            decided ? 1 : 0, cand.size(), pick.c_str(),
            g.host_get_ai_last_distance());
    }

    // ---- E2: the executor consumes the stored TacticDecision directly ----
    // (ADR-005 Phase B) The execute block switches on the stored decision
    // (E3 deleted the legacy enemy_ai_state_ int, the adapter and the
    // fallback branches). An attack decision drives the attack window
    // exactly as the legacy state-2 path did: high_punch + the attacking
    // flag (cooldown gate preserved). The pipeline re-decides every frame
    // (shipped ResponseDelay 0/0), so an attack lands within the horizon
    // with overwhelming probability.
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
            std::fprintf(stderr, "FAIL: E2 init() returned false\n");
            return 1;
        }
        configure_battle(runner);

        // Wait for the first pipeline decision (the stash fills on frame 1).
        const int kMaxFrames = 300;
        for (int i = 0; i < kMaxFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_ai_last_candidates().size() >= 7) break;
        }

        // An attack decision drives the attack window exactly as the legacy
        // state-2 path did: high_punch + the attacking flag (cooldown gate
        // preserved). The pipeline re-decides every frame (shipped
        // ResponseDelay 0/0), so an attack lands within the horizon with
        // overwhelming probability.
        // An attack decision drives the attack window exactly as the legacy
        // state-2 path did: high_punch + the attacking flag (cooldown gate
        // preserved). The pipeline re-decides every frame (shipped
        // ResponseDelay 0/0), so an attack lands within the horizon with
        // overwhelming probability.
        bool saw_attack_execution = false;
        const int kAttackScanFrames = 600;
        for (int i = 0; i < kAttackScanFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_enemy_attacking() &&
                runner.game().host_get_enemy_anim() == "high_punch") {
                saw_attack_execution = true;
                break;
            }
        }
        CHECK(saw_attack_execution,
              "attack decision -> high_punch + attacking flag (direct consumption)");
    }

    // ---- E3: settings absent -> traced idle/wait decision, neutral enemy ----
    // (ADR P4 / GATE GE, supervisor requiredChange) With no tacticSettings
    // (and no table families) the enemy must not move, attack or block: the
    // stored decision is the idle wait, the stash shows "(no tactics)", and
    // no rand()%100 roulette remains (GATE GE grep). RED on the pre-E3
    // fallback (the enemy approached or attacked), GREEN once the fallback
    // branch is deleted.
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
            std::fprintf(stderr, "FAIL: E3 init() returned false\n");
            return 1;
        }
        configure_battle(runner);
        runner.game().host_unload_tactics();

        const float x0 = runner.game().host_get_enemy_pos_x();
        bool saw_move = false;
        bool saw_attack = false;
        bool saw_block = false;
        bool saw_non_idle_anim = false;
        const int kObserveFrames = 300;  // 5 s — several 0.8 s fallback windows
        for (int i = 0; i < kObserveFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_enemy_pos_x() != x0) saw_move = true;
            if (runner.game().host_get_enemy_attacking()) saw_attack = true;
            if (runner.game().host_get_enemy_blocking()) saw_block = true;
            if (runner.game().host_get_enemy_anim() != "fists_idle") {
                saw_non_idle_anim = true;
            }
        }

        const resf2::game::TacticDecision& d =
            runner.game().host_get_enemy_last_decision();
        CHECK(d.stage == resf2::game::DecisionStage::kIdle &&
                  d.animation.empty(),
              "settings absent -> traced idle/wait decision");
        CHECK(runner.game().host_get_ai_last_pick() == "(no tactics)",
              "settings absent -> stash pick '(no tactics)'");
        CHECK(!saw_attack, "settings absent -> enemy never attacks");
        CHECK(!saw_block, "settings absent -> enemy never blocks");
        CHECK(!saw_move, "settings absent -> enemy never moves");
        CHECK(!saw_non_idle_anim,
              "settings absent -> enemy stays in the idle animation");
    }

    // ---- D4: ResponseDelay countdown gates loaded-path re-entry ----
    // (E3: the invented enemy_ai_decision_interval_ gate + its probes are
    // gone — the countdown is the ONLY re-entry gate; Phase E retired the
    // interval-sabotage canary with them.)
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
            std::fprintf(stderr, "FAIL: D4 init() returned false\n");
            return 1;
        }
        configure_battle(runner);

        bool decided = false;
        const int kMaxFrames = 400;  // ~6.4 s
        for (int i = 0; i < kMaxFrames; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_ai_last_candidates().size() >= 7) {
                decided = true;
                break;
            }
        }
        CHECK(decided,
              "loaded path decides via the ResponseDelay countdown gate");

        if (decided) {
            // Window contract against the SHIPPED Standard tactic, which
            // declares ResponseDelay 0/0: the countdown reads 0 (0 lies in
            // [Min,Max]) on every observed frame and the tick never drives
            // it negative. EnemyResponseDelay (shipped 30/60) bounds the
            // reaction countdown; it only moves when a stage-1/4 reaction
            // fires (random in the live battle), so the pin is the bound.
            bool window_ok = true;
            for (int i = 0; i < 120; ++i) {
                runner.run_frames(1);
                const int cd = runner.game().host_get_enemy_decision_countdown();
                const int rc = runner.game().host_get_enemy_reaction_countdown();
                if (cd != 0 || rc < 0 || rc > 60) {
                    std::fprintf(stderr,
                        "  window violation: cd=%d rc=%d at frame %d\n",
                        cd, rc, i);
                    window_ok = false;
                    break;
                }
            }
            CHECK(window_ok,
                  "decision countdown 0 (shipped [0,0]) + reaction countdown within [0,60]");
        }
    }

    // ---- D4 mechanism: the countdown window contract (TacticMemory) ----
    {
        resf2::game::TacticMemory mem;
        auto lcg = [seed = 7u]() mutable -> unsigned {
            seed = seed * 1103515245u + 12345u;
            return (seed >> 16) % ((unsigned)RAND_MAX + 1u);
        };

        // ResponseDelay: the roll lands in [Min,Max] inclusive and the
        // per-frame tick reaches exactly 0 at the rolled frame — the gate
        // (countdown == 0) stays closed until then ("no pipeline re-entry
        // before frames_until_next_decision == 0").
        mem.start_response_delay(10.0f, 20.0f, lcg);
        const int rolled = mem.frames_until_next_decision;
        CHECK(rolled >= 10 && rolled <= 20,
              "start_response_delay rolls within [Min,Max]");
        int ticks = 0;
        while (mem.frames_until_next_decision > 0 && ticks < 32) {
            mem.tick();
            ++ticks;
        }
        CHECK(ticks == rolled && mem.frames_until_next_decision == 0,
              "countdown ticks down to exactly 0 -> gate re-opens");

        // Shipped Standard data is 0/0: the roll is 0, so the loaded path
        // re-enters immediately (the live battle observes countdown == 0).
        mem.start_response_delay(0.0f, 0.0f, lcg);
        CHECK(mem.frames_until_next_decision == 0,
              "shipped ResponseDelay [0,0] -> countdown 0 (immediate re-entry)");

        // EnemyResponseDelay: same roll/decrement contract, floor at 0.
        mem.start_enemy_reaction(30.0f, 60.0f, lcg);
        const int eroll = mem.enemy_reaction_frames;
        CHECK(eroll >= 30 && eroll <= 60,
              "start_enemy_reaction rolls within [Min,Max]");
        for (int i = 0; i < 128; ++i) mem.tick();
        CHECK(mem.enemy_reaction_frames == 0,
              "reaction countdown ticks to 0 and floors (never negative)");
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
