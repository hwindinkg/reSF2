// tests/e2e/test_e2e_location_bounds.cpp
//
// Wave 10A defect 2 — LOCATION BOUNDS: fighters walk out of the arena. The
// player's world x is driven by root motion (step_forward NPivot deltas)
// and the step/align paths with NO clamp against the location's world box
// (params.xml Width -> world x in [-width/2, +width/2]; the dojo Width is
// 1960, so the box is +-980). The enemy side was clamped in defect 1; the
// player still walks past +-980 forever.
//
// E2E on the REAL binary: hold D (forward) from f=200 to f=2600 — a 2400
// frame walk at ~2.7 u/frame would carry the player ~+6200 without a wall —
// then hold A (back) to f=4000 so the left wall is also exercised. Assert:
// player world x stays inside +-981 at every frame, AND the walk was long
// enough that both walls were actually reached (px_max >= +900, px_min
// <= -900) — a test that never reaches the wall proves nothing.

#include <cmath>
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
                     "usage: test_e2e_location_bounds <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Intro stance ~160 frames. The fighter walks at ~1.3 u/frame (root
    // motion), so order matters for the frame budget: A held 200..1350
    // turns the fighter and walks LEFT from spawn (-290) to ~-1820 (past
    // the -980 wall at ~f720), then D held 1450..4100 turns and walks RIGHT
    // to ~+1705 (past the +980 wall at ~f3400). Both walls are hit with
    // overshoot, so an unclamped walk is caught miles outside. The
    // sparring partner is left OFF (no B) so nothing distracts the walk.
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "A"});
    events.push_back({1350, false, "A"});
    events.push_back({1450, true, "D"});
    events.push_back({4100, false, "D"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_location_bounds_input.txt";
    spec.out_name = "e2e_location_bounds";
    spec.max_frames = 4200;   // ~1240 gameplay frames after Boot/Loading
    spec.no_log = true;       // stdout [STATE] is the probe
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
    std::printf("bounds: %zu frames, player x range [%.1f @ f%lld .. "
                "%.1f @ f%lld] (bounds +-%.0f)\n",
                frames.size(), px_min, f_min, px_max, f_max, kHalfWorldW);

    // The walk must have been long enough to actually meet both walls — a
    // player that never reached the boundary cannot prove a clamp.
    check_ge(static_cast<double>(px_max), 900.0,
             "the forward walk reached the right wall (px >= +900)");
    check(px_min <= -900.0,
          "the back walk reached the left wall (px <= -900)");
    check(px_min >= -(kHalfWorldW + kBoundSlack) &&
              px_max <= (kHalfWorldW + kBoundSlack),
          "the player never leaves the location world box (walk clamped)");

    return resf2::test::summary();
}
