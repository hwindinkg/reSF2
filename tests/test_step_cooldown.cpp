// tests/test_step_cooldown.cpp
//
// Integration test: verify that the 200 ms step cooldown prevents rapid
// A/D tap spam from walking the fighter backward.
//
// [ORIGINAL] Each step_forward animation leaves the heel 49.8 units behind
// NPivot while stance_idle wants it 30.3 behind. Net ~8 units backward per
// tap when taps are unconstrained. Without the cooldown, spamming D at
// 50 ms intervals walked the fighter BACK to the start.
//
// [FIX] step_cooldown_ms_ = 200 in game.cpp gates step starts. Only taps
// spaced >= 200 ms apart produce a new step. The fighter therefore moves
// forward on each gated step and ends up well past the starting position.
//
// This test runs the real resf2_app with a fast-tap input script and
// asserts on the position delta across the spam window.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "check.hpp"

using resf2::test::check;
using resf2::test::check_ge;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_step_cooldown <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];
    const std::string script = root + "/tests/data/input_step_cooldown.txt";
    const std::string out = root + "/build/step_cooldown.out";

    // 600 poll frames gives ~440 gameplay frames after Boot/Loading, enough
    // to cover the entire script (last event at frame 457).
    const std::string cmd = "\"\"" + app + "\" --assets \"" + root +
                            "\" --input-script \"" + script +
                            "\" --max-frames 600 --dump-state --no-log > \"" +
                            out + "\" 2>&1\"";
    const int rc = std::system(cmd.c_str());
    check(rc == 0, "resf2_app exited cleanly");

    std::ifstream f(out);
    check(f.good(), "trace file was produced");
    if (!f.good()) return resf2::test::summary();

    // Parse [STATE] lines for position.
    const std::regex re_px(R"(\[STATE\] f=(\d+) .* px=([-\d.]+))");
    float px_before_spam = 0.0f;
    float px_after_spam = 0.0f;
    bool saw_before = false;
    int frames_seen = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, re_px)) continue;
        const int fr = std::stoi(m[1]);
        const float px = std::stof(m[2]);
        ++frames_seen;
        // Just before the spam starts (frame 220).
        if (fr == 215) { px_before_spam = px; saw_before = true; }
        // After the spam ends (last event at 457).
        if (fr >= 220) px_after_spam = px;
    }

    std::printf("step_cooldown: frames=%d px %.1f -> %.1f (advance %.1f)\n",
                frames_seen, px_before_spam, px_after_spam,
                px_after_spam - px_before_spam);

    check(saw_before, "trace reached frame 215 (start of spam window)");
    check_ge(static_cast<double>(frames_seen), 400.0,
             "the run produced enough frames to cover the spam script");

    // The critical assertion: the fighter MUST have moved forward during
    // the spam. With the cooldown, each step is gated to 200 ms so the
    // fighter takes ~5-6 steps over the 237-frame spam (220..457) and
    // moves forward by at least 50 units. Without the cooldown, every
    // tap restarts the animation and the <Align> snap-back walks the
    // fighter BACK to where he started (advance ~= 0 or negative).
    check_ge(static_cast<double>(px_after_spam - px_before_spam), 50.0,
             "rapid D-tap moves the fighter FORWARD (step cooldown works)");

    return resf2::test::summary();
}
