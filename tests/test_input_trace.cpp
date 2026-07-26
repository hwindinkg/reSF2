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

namespace {

struct Frame {
    int frame = 0;
    int move_state = 0;
    std::string anim;
    std::string move;
    float px = 0.0f;
    int aligned = 0;        // the move on screen carries an applied <Align>
    float anchor_x = 0.0f;  // world X of that <Align>'s anchor node
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
    const std::regex re_align(R"(al=(\d+) anchor_x=([-\d.]+))");
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
        std::smatch ma;
        if (std::regex_search(line, ma, re_align)) {
            fr.aligned = std::stoi(ma[1]);
            fr.anchor_x = std::stof(ma[2]);
        }
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

    // ------------------------------------------------------------- <Align>
    //
    // [ORIGINAL] Model::alignAnimation @ 0x101661d0, driven from
    // ModelAnimation::playInfo @ 0x10164fa0, places a new animation so the node
    // named by <Align><Pivot Part> keeps the world position it has in the pose
    // being left (658 of the 800 <Align> blocks in moves.xml target
    // Object="Pivot", and 613 anchor a heel). That is the invariant asserted
    // here: across an animation change into an aligned move, the anchor node
    // must not jump.
    //
    // px cannot be used for this. px is NPivot plus the align offset, and
    // NPivot sways inside an animation by design — during the intro stance it
    // travels 167 units and comes back. Only the anchor is held.
    //
    // The sample is taken one frame after the switch: one of the two places
    // that start an animation runs after update_animation(), so on the switch
    // frame itself the reported anim is already the new one while the node
    // positions are still the old one's. One frame later both agree.
    auto frame_at = [&](int f) -> const Frame* {
        for (const auto& fr : frames)
            if (fr.frame == f) return &fr;
        return nullptr;
    };

    int aligned_transitions = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].anim == frames[i - 1].anim) continue;
        const Frame* before = frame_at(frames[i].frame - 1);
        const Frame* after = frame_at(frames[i].frame + 1);
        if (!before || !after) continue;
        if (!before->aligned || !after->aligned) continue;   // not comparable
        ++aligned_transitions;
        const float jump = std::fabs(after->anchor_x - before->anchor_x);
        std::printf("  align %s -> %-14s anchor %.2f -> %.2f (jump %.2f)\n",
                    before->anim.c_str(), after->anim.c_str(),
                    before->anchor_x, after->anchor_x, jump);
        check(jump < 2.0,
              "the <Align> anchor holds its world position entering '" +
                  after->anim + "'");
    }
    check_ge(static_cast<double>(aligned_transitions), 2.0,
             "at least two aligned animation changes were actually observed");

    // A looping aligned stance must stay put. Before the anchor was held, an
    // idle inherited whatever offset the previous animation left it with and
    // the fighter crept along the floor.
    float idle_lo = 0.0f, idle_hi = 0.0f;
    int idle_n = 0;
    for (const auto& fr : frames) {
        if (fr.frame <= idle3 + 20 || fr.anim != "stance_idle" || !fr.aligned) continue;
        if (idle_n == 0) { idle_lo = idle_hi = fr.anchor_x; }
        idle_lo = std::min(idle_lo, fr.anchor_x);
        idle_hi = std::max(idle_hi, fr.anchor_x);
        ++idle_n;
    }
    check_ge(static_cast<double>(idle_n), 60.0,
             "the trace contains a long stretch of looping idle to measure");
    std::printf("  idle stretch: %d frames, anchor span %.2f\n",
                idle_n, idle_hi - idle_lo);
    check(idle_hi - idle_lo < 5.0,
          "a looping idle does not walk the fighter along the floor");

    // Walking has to actually move the fighter.
    float px_at_walk = 0.0f, px_at_idle2 = 0.0f;
    for (const auto& fr : frames) {
        if (fr.frame == walk) px_at_walk = fr.px;
        if (fr.frame == idle2) px_at_idle2 = fr.px;
    }
    check_ge(px_at_idle2 - px_at_walk, 30.0, "walking forward moves the fighter to the right");

    // ------------------------------------------------------------- combat run
    // Walk up to the punching bag and hit it. With a scripted run the timestep
    // is fixed, so this is reproducible: the same frame count, the same hit and
    // the same bag displacement to three decimals on every run. Before that was
    // true, a marginal collision landed in some runs and not others, and the
    // bag looked as if it never reacted.
    const std::string script2 = root + "/tests/data/input_walk_and_punch_bag.txt";
    const std::string out2 = root + "/build/input_trace_combat.out";
    const std::string cmd2 = "\"\"" + app + "\" --assets \"" + root +
                             "\" --input-script \"" + script2 +
                             "\" --max-frames 900 --dump-state --no-log > \"" +
                             out2 + "\" 2>&1\"";
    check(std::system(cmd2.c_str()) == 0, "combat run exited cleanly");

    std::ifstream f2(out2);
    check(f2.good(), "combat trace was produced");
    if (!f2.good()) return resf2::test::summary();

    const std::regex re_bag(R"(\[STATE\] f=(\d+) .* bag_move=([-\d.]+))");
    int combat_frames = 0, combat_hits = 0;
    float bag_before = -1.0f, bag_max = 0.0f;
    int first_hit_frame = -1, last_frame = 0;
    while (std::getline(f2, line)) {
        if (line.find("[COMBAT] HIT") != std::string::npos) ++combat_hits;
        std::smatch m;
        if (!std::regex_search(line, m, re_bag)) continue;
        ++combat_frames;
        const int fr = std::stoi(m[1]);
        const float mv = std::stof(m[2]);
        last_frame = fr;
        // Settled displacement before any punch can land.
        if (fr == 300) bag_before = mv;
        if (mv > bag_max) bag_max = mv;
        if (combat_hits > 0 && first_hit_frame < 0) first_hit_frame = fr;
    }

    check_ge(static_cast<double>(combat_frames), 550.0,
             "the combat run reached the end of its script");
    check(combat_hits >= 1, "the punch registers a collision with the bag");
    check(bag_before >= 0.0f && bag_before < 5.0f,
          "the bag hangs still before the punch (settled displacement < 5)");
    check_ge(bag_max, 50.0,
             "the punch visibly swings the bag (peak displacement from rest)");
    std::printf("combat: %d frames, %d hit(s), bag rest=%.2f peak=%.2f, last frame %d\n",
                combat_frames, combat_hits, bag_before, bag_max, last_frame);

    return resf2::test::summary();
}
