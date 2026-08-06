// tests/e2e/test_e2e_battle_knockback_bounds.cpp
//
// Wave 10B defect 1 (soak) — KNOCKBACK OVERFLOW IN BATTLE: "враг
// отбрасывается очень далеко, может и улететь за локацию". The dojo
// sparring recipe (test_e2e_knockback_bounds) pins the explore-mode path;
// this test pins the BATTLE path on the REAL binary: boot straight into
// Battle, walk after the disciple and punch — every unblocked hit knocks
// him away by the authored <Impulse X> (moves.xml), and a chasing player
// ratchets the hit positions toward the wall. The enemy's world x must
// stay inside the location's world box (params.xml Width -> world x in
// [-width/2, +width/2]) at EVERY frame.
//
// Assert: >= 4 unblocked hits landed (the enemy was actually driven), the
// herd carried him INTO the right wall (ex_max >= +900 — a test that never
// reaches the wall proves nothing), and the enemy never leaves the box.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;
using resf2::test::check_ge;

namespace {

// dojo params.xml: <Root Width="1960">; world x = params x - width/2.
constexpr float kHalfWorldW = 980.0f;
constexpr float kBoundSlack = 1.0f;   // float noise allowance

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_battle_knockback_bounds <resf2_app> "
                     "<repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Battle intro stance ~156 frames (same StartStance as the dojo walk).
    // The recipe is the dojo knockback herd, minus the B toggle (the battle
    // scene shows the enemy from frame 1): walk right from spawn (-290)
    // toward the disciple (at -7), punch bursts while he is in reach, then
    // walk+punch cycles — every unblocked hit knocks him RIGHT (the player
    // stays LEFT of him), ratcheting him into the +980 wall. --round-time
    // 99 keeps the fight alive for the whole script. A must never be down
    // at a punch tap (a Back-direction chain locks the fighter).
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "D"});
    events.push_back({356, false, "D"});
    for (int f : {360, 384, 408}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    for (int cyc = 0; cyc < 40; ++cyc) {
        const int w0 = 430 + cyc * 80;          // walk window (D hold)
        events.push_back({w0, true, "D"});
        events.push_back({w0 + 64, false, "D"});
        const int p0 = w0 + 68;                 // punch tap at reach
        events.push_back({p0, true, "O"});
        events.push_back({p0 + 2, false, "O"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_battle_knockback_input.txt";
    spec.out_name = "e2e_battle_knockback";
    spec.max_frames = 3800;
    spec.no_log = true;       // stdout [STATE]/[HIT-FEEDBACK] is the probe
    spec.extra_args = {"--scene", "battle", "--round-time", "99"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    // ------------------------------------------------------------ hit count
    const auto hits = e2e::filter_lines(run.stdout_lines, "[HIT-FEEDBACK]");
    int unblocked = 0;
    for (const auto& l : hits)
        if (l.find("blocked=0") != std::string::npos) ++unblocked;
    std::printf("battle-knockback: %zu frames, %d unblocked hit(s) of %zu "
                "total\n", frames.size(), unblocked, hits.size());

    // ------------------------------------------------------------ ex range
    float ex_min = 1e9f, ex_max = -1e9f;
    long long f_min = 0, f_max = 0;
    for (const auto& fr : frames) {
        if (fr.ex < ex_min) { ex_min = fr.ex; f_min = fr.frame; }
        if (fr.ex > ex_max) { ex_max = fr.ex; f_max = fr.frame; }
    }
    std::printf("battle-knockback: enemy x range [%.1f @ f%lld .. %.1f @ "
                "f%lld] (bounds +-%.0f)\n",
                ex_min, f_min, ex_max, f_max, kHalfWorldW);

    // The script must actually have driven the enemy in the BATTLE — a test
    // that lands no hits proves nothing. A chase ratchet needs several hits.
    check_ge(static_cast<double>(unblocked), 4.0,
             "at least 4 unblocked hits landed in the battle (the enemy was "
             "driven)");
    check(ex_max >= 900.0f,
          "the herd carried the enemy INTO the right wall (ex >= +900)");
    check(ex_min >= -(kHalfWorldW + kBoundSlack) &&
              ex_max <= (kHalfWorldW + kBoundSlack),
          "the enemy never leaves the location world box (knockback "
          "clamped by the arena bounds)");

    return resf2::test::summary();
}
