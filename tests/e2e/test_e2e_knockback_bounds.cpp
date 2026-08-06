// tests/e2e/test_e2e_knockback_bounds.cpp
//
// Wave 10A defect 1 — KNOCKBACK OVERFLOW: an enemy knocked back too far can
// fly out of the location ("отбрасывается очень далеко, может и улететь за
// локацию"). The knockback velocity (moves.xml <Impulse X>, reversed) is
// applied for the whole stun duration with NO arena clamp, so repeated hits
// push the enemy past the location's world box (params.xml Width -> world x
// in [-width/2, +width/2]).
//
// E2E on the REAL binary: dojo, B toggles the sparring partner (the enemy
// fighter at enemy spawn), the player walks in, punches (O) repeatedly. Each
// unblocked hit knocks the partner back by the authored impulse; a chasing
// player ratchets the hit positions toward the wall. Assert: with >=4
// unblocked hits landed, the partner's world x stays inside the location
// bounds at every frame. RED on HEAD (no clamp), GREEN after the fix.

#include <algorithm>
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
                     "usage: test_e2e_knockback_bounds <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Intro stance ~160 frames; B at 190 toggles the sparring partner. The
    // recipe herds the disciple RIGHT into the +980 wall (world box =
    // params.xml Width 1960 / 2):
    //
    //   1. Walk right (D held) from spawn (-290) to ~-100 so the disciple
    //      (at -7) is inside punch reach (moves.xml HighPunch <Distance
    //      Max=250>).
    //   2. Punch burst (O taps): every unblocked hit knocks the disciple
    //      RIGHT by the authored impulse (245 units, reversed — the player
    //      stays LEFT of him), which on HEAD ratchets him to +4419, past
    //      the +980 wall.
    //   3. Cycle: D-hold walks the player right after the knocked-away
    //      disciple (~23 u/frame effective recovery can't outrun a 150 u/s
    //      walk), one O tap reconnects at reach, knock, repeat.
    //
    // A must never be down at a punch tap — a Back-direction chain
    // (SpinningPunch) plays a looping animation that locks the fighter.
    std::vector<e2e::InputEvent> events;
    events.push_back({190, true, "B"});
    events.push_back({191, false, "B"});
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
    spec.script = root + "/build/e2e_knockback_input.txt";
    spec.out_name = "e2e_knockback";
    spec.max_frames = 4000;   // ~1240 gameplay frames after Boot/Loading
    spec.no_log = true;       // stdout [STATE]/[HIT-FEEDBACK] is the probe
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
    std::printf("knockback: %zu frames, %d unblocked hit(s) of %zu total\n",
                frames.size(), unblocked, hits.size());

    // ------------------------------------------------------------ ex range
    float ex_min = 1e9f, ex_max = -1e9f;
    long long f_min = 0, f_max = 0;
    for (const auto& fr : frames) {
        if (fr.ex < ex_min) { ex_min = fr.ex; f_min = fr.frame; }
        if (fr.ex > ex_max) { ex_max = fr.ex; f_max = fr.frame; }
    }
    std::printf("knockback: enemy x range [%.1f @ f%lld .. %.1f @ f%lld] "
                "(bounds +-%.0f)\n",
                ex_min, f_min, ex_max, f_max, kHalfWorldW);

    // The script must actually have driven the enemy — a test that lands no
    // hits proves nothing. A chase ratchet needs several hits to matter.
    check_ge(static_cast<double>(unblocked), 4.0,
             "at least 4 unblocked hits landed (the enemy was driven)");
    check(ex_min >= -(kHalfWorldW + kBoundSlack) &&
              ex_max <= (kHalfWorldW + kBoundSlack),
          "the enemy never leaves the location world box (knockback clamped)");

    return resf2::test::summary();
}
