// tests/e2e/test_e2e_duck_repeat.cpp
//
// Wave 11A M3 — DUCK REPEAT: "при спаме s анимация приседания начинается
// сначала" — spamming S restarted the duck animation on every press.
//
// Verified guards (VERIFY_W11.md Q3 GREEN; SPEC_COMBAT_CORE.md Q3):
//   1. Duck requires Key Type="Down" PressType="Tap" — a FRESH EDGE only
//      (moves.xml Duck <Key Type="Down" PressType="Tap"/>).
//   2. The SemiUninterrupt window (1key template: a 1key move cannot start
//      while the current move is in its SemiUninterrupt interval).
//   3. The Controlled anti-restart: NOT(CurrentAnimation == $Move AND
//      SemiUninterrupt) — while the duck animation is inside its
//      SemiUninterrupt window (<Interval Name="SemiUninterrupt" End="4"/>,
//      frames 0..4), re-selection is REJECTED. A re-tap after the window
//      legitimately restarts (original behavior).
//   4. The selected move is queued at Fighter+0x218 and applied at the
//      start of the next frame (0x8F4AC4B4), so rapid same-frame
//      re-selections collapse into one switch.
//
// E2E on the REAL binary: after the intro stance, SPAM S (six taps, one
// per frame — all inside the duck's ~5-frame SemiUninterrupt window). The
// duck must start ONCE ([DUCK-PLAY] count == 1), the animation must run to
// completion without restarting (its frame clock keeps climbing), and the
// special must end into stance_idle.
// RED on HEAD: every tap re-selects Duck and play_animation() restarts the
// animation from its first frame — six [DUCK-PLAY] rows and the anim
// frame clock resets at each tap.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_duck_repeat <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Battle intro stance ~156 frames. Then SIX S taps, one per frame
    // (200..205; the duck's SemiUninterrupt window is frames 0..4 of the
    // animation — the taps all land inside it, so every re-tap must be
    // REJECTED; on HEAD every tap restarts the animation).
    std::vector<e2e::InputEvent> events;
    for (int f = 200; f <= 205; ++f) {
        events.push_back({f, true, "S"});
        events.push_back({f + 1, false, "S"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_duck_repeat_input.txt";
    spec.out_name = "e2e_duck_repeat";
    spec.max_frames = 400;
    spec.no_log = true;       // stdout [STATE]/[DUCK-PLAY] are the probes
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

    // ------------------------------------------------ the duck started once
    const auto plays = e2e::filter_lines(run.stdout_lines, "[DUCK-PLAY]");
    std::printf("duck-repeat: %zu [DUCK-PLAY] row(s)\n", plays.size());
    check(!plays.empty(),
          "the first S tap started the duck ([DUCK-PLAY] probe)");
    check(plays.size() == 1,
          "spamming S inside the SemiUninterrupt window does NOT restart "
          "the duck (exactly ONE Duck move start)");

    // ------------------------------------------------ ran to completion
    // The duck's animation frame clock (af=anim_time*fps) must climb
    // monotonically after the last tap — a restart would reset it. af is
    // parsed from the raw [STATE] rows (the runner exposes it via raw).
    float last_af = -1.0f;
    bool monotonic = true;
    int duck_rows = 0;
    for (const auto& fr : frames) {
        if (fr.frame < 206 || fr.frame > 260) continue;
        if (fr.anim != "duck") continue;
        ++duck_rows;
        std::size_t a = fr.raw.find("af=");
        if (a == std::string::npos) continue;
        const float af = std::strtof(fr.raw.c_str() + a + 3, nullptr);
        if (last_af >= 0.0f && af < last_af - 0.01f) monotonic = false;
        last_af = af;
    }
    std::printf("duck-repeat: duck anim rows=%d monotonic-clock=%d\n",
                duck_rows, (int)monotonic);
    check(duck_rows >= 30,
          "the duck animation kept playing for 30+ frames after the spam");
    check(monotonic,
          "the duck animation frame clock never resets during the spam "
          "(the animation runs to completion, no restart)");

    // ------------------------------------------------ the duck ends into idle
    const int idle_frame = e2e::first_frame_with(frames, "stance_idle", 200);
    const int duck_end = [&]() {
        int last = -1;
        for (const auto& fr : frames)
            if (fr.anim == "duck") last = (int)fr.frame;
        return last;
    }();
    std::printf("duck-repeat: duck last frame=%d, stance_idle at %d\n",
                duck_end, idle_frame);
    check(duck_end > 240,
          "the duck animation ran to completion (~1s) before standing up");
    check(idle_frame > duck_end,
          "after the duck the fighter returns to stance_idle");

    return resf2::test::summary();
}
