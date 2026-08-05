// tests/e2e/test_e2e_smoke.cpp
//
// Step 1 harness canary: boot the REAL resf2_app with a scripted input and
// prove the whole harness path works end to end — spawn, script drive, state
// dump capture, [STATE] parsing, debug-log capture. Uses the existing dojo
// script (walk D at frame 200, punch Space at frame 300) and asserts the
// dump actually shows the walk and the punch on the real binary.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_e2e_smoke <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/tests/data/input_idle_walk_punch.txt";
    spec.out_name = "e2e_smoke";
    spec.max_frames = 600;   // ~440 gameplay frames after Boot/Loading
    spec.no_log = false;     // exercise the debug-log capture path

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.launched, "app was launched");
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    check(frames.size() >= 380,
          "the run reached the end of the script (>=380 gameplay frames)");
    if (frames.empty()) return resf2::test::summary();

    const int walk = e2e::first_frame_with(frames, "step_forward", 0);
    const int punch = e2e::first_frame_with(frames, "high_punch", 0);
    std::printf("smoke: walk@%d punch@%d frames=%zu\n", walk, punch,
                frames.size());
    check(walk >= 200 && walk <= 215,
          "the scripted D press starts a walk within ~10 frames");
    check(punch >= 300 && punch <= 315,
          "the scripted Space press starts a punch within ~10 frames");

    // The debug log must have been written and captured (no --no-log).
    check(!run.log_lines.empty(), "resf2_debug.log was produced and captured");

    return resf2::test::summary();
}
