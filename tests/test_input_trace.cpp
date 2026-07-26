// tests/test_input_trace.cpp
//
// Integration test: run the real resf2_app with a scripted input sequence and
// assert on the animation state machine it produces.
//
// This replaces a CTest PASS_REGULAR_EXPRESSION version of the same idea. That
// one was not trustworthy: CTest passes if the regex matches anywhere, so a run
// that died at frame 240 still matched "anim='stance_idle'" (which appears at
// frame ~161) and reported green. Parsing the trace here lets the test require
// that the run actually completed and that every expected transition happened.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "check.hpp"

using resf2::test::check;
using resf2::test::check_ge;

namespace {

struct Frame {
    int frame = 0;
    int move_state = 0;
    std::string anim;
    std::string move;
    float px = 0.0f;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_input_trace <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];
    const std::string script = root + "/tests/data/input_idle_walk_punch.txt";
    const std::string out = root + "/build/input_trace.out";

    // --max-frames counts POLL frames, but the input script and the [STATE]
    // dump are driven by GAMEPLAY frames, and the Boot/Loading scenes eat
    // roughly the first 160 poll frames. 600 poll frames leaves ~440 gameplay
    // frames, enough for the whole script (last event at gameplay frame 310).
    const std::string cmd = "\"\"" + app + "\" --assets \"" + root +
                            "\" --input-script \"" + script +
                            "\" --max-frames 600 --dump-state --no-log > \"" +
                            out + "\" 2>&1\"";
    const int rc = std::system(cmd.c_str());
    check(rc == 0, "resf2_app exited cleanly");

    std::ifstream f(out);
    check(f.good(), "trace file was produced");
    if (!f.good()) return resf2::test::summary();

    const std::regex re(
        R"(\[STATE\] f=(\d+) ms=(-?\d+) ha=(\d+) anim='([^']*)' move='([^']*)' px=([-\d.]+))");
    std::vector<Frame> frames;
    bool rejected = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("[ANIM] Rejected") != std::string::npos) {
            std::fprintf(stderr, "  %s\n", line.c_str());
            rejected = true;
        }
        std::smatch m;
        if (!std::regex_search(line, m, re)) continue;
        Frame fr;
        fr.frame = std::stoi(m[1]);
        fr.move_state = std::stoi(m[2]);
        fr.anim = m[4];
        fr.move = m[5];
        fr.px = std::stof(m[6]);
        frames.push_back(fr);
    }

    // No animation may ever be refused: a finished non-looping animation must
    // release its priority slot, otherwise the fighter freezes on its last
    // frame and later inputs are ignored.
    check(!rejected, "no animation was refused");

    // The run must actually reach the end of the script. A premature exit used
    // to hide behind a regex match on an early frame.
    check_ge(static_cast<double>(frames.size()), 380.0,
             "the run produced enough gameplay frames to finish the script");
    if (frames.empty()) return resf2::test::summary();

    // Collapse to state transitions.
    std::vector<Frame> transitions;
    for (const auto& fr : frames) {
        if (transitions.empty() || transitions.back().anim != fr.anim ||
            transitions.back().move_state != fr.move_state) {
            transitions.push_back(fr);
        }
    }
    std::printf("--- transitions ---\n");
    for (const auto& t : transitions)
        std::printf("  f=%4d ms=%2d anim=%-16s move=%-14s px=%.1f\n",
                    t.frame, t.move_state, t.anim.c_str(), t.move.c_str(), t.px);

    auto first_frame_with = [&](const std::string& anim, int after) -> int {
        for (const auto& fr : frames)
            if (fr.frame > after && fr.anim == anim) return fr.frame;
        return -1;
    };

    // The scripted sequence is: intro stance -> idle -> walk (D held 200..230)
    // -> idle -> punch (Space at 300) -> idle.
    const int intro = first_frame_with("stance_2", 0);
    check(intro == 1, "the intro stance starts on frame 1");

    const int idle1 = first_frame_with("stance_idle", intro);
    check(idle1 > 0 && idle1 < 200, "the fighter reaches the idle stance before the first input");

    const int walk = first_frame_with("step_forward", idle1);
    check(walk >= 200 && walk <= 210, "walking starts within ~10 frames of the key press");

    const int idle2 = first_frame_with("stance_idle", walk);
    check(idle2 > walk && idle2 < 300, "the fighter returns to idle after walking");

    const int punch = first_frame_with("high_punch", idle2);
    check(punch >= 300 && punch <= 310, "the punch starts within ~10 frames of the key press");

    const int idle3 = first_frame_with("stance_idle", punch);
    check(idle3 > punch, "the fighter returns to idle after punching");

    // Walking has to actually move the fighter.
    float px_at_walk = 0.0f, px_at_idle2 = 0.0f;
    for (const auto& fr : frames) {
        if (fr.frame == walk) px_at_walk = fr.px;
        if (fr.frame == idle2) px_at_idle2 = fr.px;
    }
    check_ge(px_at_idle2 - px_at_walk, 30.0, "walking forward moves the fighter to the right");

    return resf2::test::summary();
}
