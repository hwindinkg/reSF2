// tests/e2e/test_e2e_battle_location_bounds.cpp
//
// Wave 10B defect 2 (soak) — LOCATION BOUNDS IN BATTLE: "у локаций нет
// границ, за которые нельзя выйти". The dojo walk recipe
// (test_e2e_location_bounds) pins the explore-mode path; this test pins
// the BATTLE path on the REAL binary: boot straight into Battle and hold
// A (back) long enough to overshoot the LEFT wall, then D (forward) long
// enough to overshoot the RIGHT wall. The fighter's world x is driven by
// root motion and must clamp at the location's world box (params.xml
// Width -> world x in [-width/2, +width/2]) at EVERY frame — the fight
// arena has walls you cannot walk out of.
//
// Assert: both walls were actually reached (px_min <= -900, px_max >=
// +900 — a walk that never meets a wall cannot prove a clamp), and the
// player never leaves the box.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

// dojo params.xml: <Root Width="1960">; world x = params x - width/2.
constexpr float kHalfWorldW = 980.0f;
constexpr float kBoundSlack = 1.0f;   // float noise allowance

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_battle_location_bounds <resf2_app> "
                     "<repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Battle intro stance ~156 frames. A held 200..1350 walks LEFT from
    // spawn (-290) to ~-1820 (past the -980 wall at ~f720), then D held
    // 1450..4100 walks RIGHT to ~+1705 (past the +980 wall at ~f3400) —
    // the same overshoot budgets as the dojo bounds test. The sparring
    // partner is the battle enemy (on from frame 1) and may approach, but
    // the walk inputs keep driving the player.
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "A"});
    events.push_back({1350, false, "A"});
    events.push_back({1450, true, "D"});
    events.push_back({4100, false, "D"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_battle_location_input.txt";
    spec.out_name = "e2e_battle_location";
    spec.max_frames = 4200;
    spec.no_log = true;       // stdout [STATE] is the probe
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

    // ------------------------------------------------------------ px range
    float px_min = 1e9f, px_max = -1e9f;
    long long f_min = 0, f_max = 0;
    for (const auto& fr : frames) {
        if (fr.px < px_min) { px_min = fr.px; f_min = fr.frame; }
        if (fr.px > px_max) { px_max = fr.px; f_max = fr.frame; }
    }
    std::printf("battle-bounds: %zu frames, player x range [%.1f @ f%lld .. "
                "%.1f @ f%lld] (bounds +-%.0f)\n",
                frames.size(), px_min, f_min, px_max, f_max, kHalfWorldW);

    // The walk must have been long enough to actually meet both walls — a
    // player that never reached a boundary cannot prove a clamp.
    check(px_min <= -900.0f,
          "the back walk reached the left wall (px <= -900)");
    check(px_max >= 900.0f,
          "the forward walk reached the right wall (px >= +900)");
    check(px_min >= -(kHalfWorldW + kBoundSlack) &&
              px_max <= (kHalfWorldW + kBoundSlack),
          "the player never leaves the location world box (fight arena "
          "walk clamped)");

    return resf2::test::summary();
}
