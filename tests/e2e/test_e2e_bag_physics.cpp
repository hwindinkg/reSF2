// tests/e2e/test_e2e_bag_physics.cpp
//
// Wave 11B W1 - BAG PHYSICS: "Груша слишком сильно отталкивается (даже
// когда я её почти и не касаюсь)". VERIFY_W11 2 / SPEC_WORLD_FEEL 2:
// the original registers a bag hit ONLY on a real capsule collision
// (attack <AttackingParts> edges vs the bag's collisible body edges via
// the generic hit pipeline; moves.xml <Impulse X/Y/Z> split by the hit
// ratio t). There is NO distance fallback in the binary - the engine's
// "bag hits (distance fallback)" block (game.cpp:5177-5228, mislabeled
// [ORIGINAL]) is an invention: it counts a hit and swings the bag
// whenever |player_x - bag_x| < 200 after the capsule test missed.
//
// E2E on the REAL binary (probe-verified: the bag hangs at enemy spawn
// x=-7; HighPunch capsule hits land solidly from px ~ +45..+115; the
// capsule test grazes (50/50 hit/miss) through px ~ +140..+193 - i.e.
// the whole fallback window is inside the fist's reach, which is why the
// fallback could never be observed "firing alone" - it is an invention):
//  (a) Run A (tutorial, --tutorial-start): the fight_timer prologue
//      (intro dialog -> 4 hint steps -> punchbag dialog), then a right
//      walk with 17 punch taps sweeping px from ~+40 into +200 (the
//      fallback window), then a walk back to the solid hit zone with 3
//      real punches. Assert: ZERO "bag hits (distance fallback)" lines
//      (the deletion proof - on HEAD the sweep fires it), >= 3 real
//      [COMBAT] HIT! bag_edge lines, the tutorial counter ("[tutorial]
//      bag hits:") advances only AFTER a real capsule hit, and the
//      tutorial still reaches FIRST_FIGHT on the 3rd real hit.
//  (b) Run B (dojo, no tutorial): walk to px ~ +85..+115 (solid hit
//      zone) and punch 3x. Assert: >= 1 real [COMBAT] HIT! bag_edge
//      line and the bag swings (bag_move > 8: HighPunch Impulse X=245
//      distributed over the hit edge) - the positive control proving a
//      real capsule hit moves the bag.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;
using resf2::test::check_ge;

namespace {

// bag_move from a raw [STATE] line ("bag_move=12.34").
float bag_move_of(const std::string& raw) {
    static const std::regex re(R"(bag_move=([-\d.]+))");
    std::smatch m;
    if (!std::regex_search(raw, m, re)) return 0.0f;
    return std::stof(m[1]);
}

// Line index of the first line containing `needle` (or -1).
long long first_line_with(const std::vector<std::string>& lines,
                          const std::string& needle) {
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines[i].find(needle) != std::string::npos) return (long long)i;
    return -1;
}

}  // namespace

// Run A: tutorial - no distance fallback, real hits drive the counter.
static int run_tutorial(const std::string& app, const std::string& root) {
    // Prologue (fight_timer recipe): intro dialog 2..4, hint steps at
    // 170/290/410/530, punchbag dialog 560. Then the far sweep: D held
    // 600..1030 with O taps every 15 frames from 690 to 975 (20 punches
    // while px sweeps ~+30..+210 - through the fallback window). Then
    // walk back left and punch 3x in the solid hit zone.
    std::vector<e2e::InputEvent> events;
    for (int f = 2; f <= 4; ++f) {
        events.push_back({f, true, "Space"});
        events.push_back({f + 1, false, "Space"});
    }
    for (int f : {170, 290, 410, 530}) {
        events.push_back({f, true, "D"});
        events.push_back({f + 8, false, "D"});
    }
    events.push_back({560, true, "Space"});
    events.push_back({561, false, "Space"});
    events.push_back({600, true, "D"});
    events.push_back({1030, false, "D"});
    for (int f = 690; f <= 975; f += 15) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    // Walk back into the solid zone (px ~ +60..+110) and punch 3x.
    events.push_back({1080, true, "A"});
    events.push_back({1140, false, "A"});
    for (int f : {1170, 1200, 1230}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_bag_tutorial_input.txt";
    spec.out_name = "e2e_bag_tutorial";
    spec.max_frames = 1600;
    spec.no_log = true;
    spec.extra_args = {"--scene", "battle", "--tutorial-start"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly (tutorial run)");

    // (a) The deletion proof: the fallback's own log line must never
    // appear, no matter how close the player stands to the bag.
    const auto fallback_lines =
        e2e::filter_lines(run.stdout_lines, "bag hits (distance fallback)");
    std::printf("bag-tutorial: %zu fallback line(s)\n",
                fallback_lines.size());
    check(fallback_lines.empty(),
          "the distance fallback is gone (no 'bag hits (distance "
          "fallback)' rows - bag hits require a real capsule collision)");

    // (c) Real capsule hits must drive the tutorial counter: >= 3 bag
    // HIT! rows, the first counter row strictly AFTER the first real
    // hit, and FIRST_FIGHT still reached on the 3rd real hit.
    const auto hits = e2e::filter_lines(run.stdout_lines, "[COMBAT] HIT!");
    int bag_hits = 0;
    for (const auto& l : hits)
        if (l.find("bag_edge=") != std::string::npos) ++bag_hits;
    const auto counter_rows =
        e2e::filter_lines(run.stdout_lines, "[tutorial] bag hits:");
    const long long first_hit_idx = first_line_with(
        run.stdout_lines, "[COMBAT] HIT! move=");
    const long long first_counter_idx = first_line_with(
        run.stdout_lines, "[tutorial] bag hits:");
    const auto first_fight =
        e2e::filter_lines(run.stdout_lines, "state -> FIRST_FIGHT");
    std::printf("bag-tutorial: %d real bag hit(s), first hit @line %lld, "
                "first counter row @line %lld, %zu counter row(s), "
                "FIRST_FIGHT rows=%zu\n",
                bag_hits, first_hit_idx, first_counter_idx,
                counter_rows.size(), first_fight.size());

    check_ge(static_cast<double>(bag_hits), 3,
             "the tutorial advanced on REAL hits (>= 3 capsule hits)");
    check(first_hit_idx >= 0 && first_counter_idx > first_hit_idx,
          "the tutorial bag counter advances only AFTER a real capsule hit");
    check(!first_fight.empty(),
          "the tutorial still reaches FIRST_FIGHT after 3 real hits");
    return resf2::test::summary();
}

// Run B: a real capsule hit swings the bag (positive control).
static int run_real(const std::string& app, const std::string& root) {
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "D"});
    events.push_back({460, false, "D"});   // px ~ -290 + 260*1.38 = +69
    events.push_back({490, true, "O"});
    events.push_back({492, false, "O"});
    events.push_back({530, true, "D"});
    events.push_back({570, false, "D"});   // px ~ +124
    events.push_back({590, true, "O"});
    events.push_back({592, false, "O"});
    events.push_back({620, true, "O"});
    events.push_back({622, false, "O"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_bag_real_input.txt";
    spec.out_name = "e2e_bag_real";
    spec.max_frames = 900;
    spec.no_log = true;
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly (real run)");
    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the real run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    const auto hits = e2e::filter_lines(run.stdout_lines, "[COMBAT] HIT!");
    int bag_hits = 0;
    for (const auto& l : hits)
        if (l.find("bag_edge=") != std::string::npos) ++bag_hits;

    float bm = 0.0f;
    long long f_bm = 0;
    for (const auto& fr : frames) {
        if (fr.px == 0.0f) continue;
        const float mv = bag_move_of(fr.raw);
        if (mv > bm) { bm = mv; f_bm = fr.frame; }
    }
    std::printf("bag-real: %zu frames, %d bag capsule hit(s), "
                "max bag_move=%.2f @f%lld\n",
                frames.size(), bag_hits, bm, (long long)f_bm);

    check_ge(static_cast<double>(bag_hits), 1,
             "a real capsule hit registered ([COMBAT] HIT! bag_edge=...)");
    check_ge(static_cast<double>(bm), 8.0,
             "the bag swings on a real hit (bag_move > 8: HighPunch "
             "Impulse X=245 over the hit edge)");
    return resf2::test::summary();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_bag_physics <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    int rc = run_tutorial(app, root);
    if (rc != 0) return rc;
    return run_real(app, root);
}
