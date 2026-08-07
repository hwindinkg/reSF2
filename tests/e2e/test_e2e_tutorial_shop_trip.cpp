// tests/e2e/test_e2e_tutorial_shop_trip.cpp
//
// Wave 11C P3 - TUTORIAL SHOP TRIP: after winning the Kenji (FIRST_FIGHT)
// training fight the tutorial advances to the SHOP TRIP (SPEC_PRESENTATION
// Q3.6: post-boss state 0xB) instead of jumping straight to COMPLETE - the
// "после победы над кенжи сенсей должен был отправить меня в магазин"
// report. The Sensei "find yourself a weapon" dialogue plays over the shop,
// the knives item card carries the tutorial_buy_knives HintBox, and buying
// the knives finishes the trip: state -> RETURN_MAP, the tutorial_return_map
// dialogue plays, and landing on the Map closes the tutorial (COMPLETE).
//
// E2E on the REAL binary: boot --scene shop with the tutorial parked at
// SHOP_TRIP (--tutorial-state hook) and a topped-up wallet (--add-gold 100,
// usersDefault.xml ships Money=0 and the knives cost 50). Assert:
//   1. the HintBox renders on the knives card ([SHOP-HINT] key=
//      'tutorial_buy_knives' box=1 arrow=1),
//   2. a Space press BUYS the knives with the keyboard affordance,
//   3. the buy finishes the trip (RETURN_MAP + tutorial_return_map queued),
//   4. the tutorial_return_map dialogue plays ([dialogue]/[DIALOG]),
//   5. the Map closes the loop ([tutorial] returned to map -> COMPLETE).
// RED on HEAD (before P3): no SHOP-TRIP state exists - the shop opens with
// no hint, Space does nothing and the tutorial never leaves SHOP_TRIP.

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
                     "usage: test_e2e_tutorial_shop_trip <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // --scene shop skips Boot/Loading, so the shop is live from frame 1.
    // Space at 40 buys the knives (keyboard BUY); the RETURN_MAP dialogue
    // opens on the same frame. Space at 60/80 walks out of the (two-play)
    // tutorial_return_map dialogue back to the Map, which COMPLETEs.
    std::vector<e2e::InputEvent> events;
    for (int f : {40, 60, 80}) {
        events.push_back({f, true, "Space"});
        events.push_back({f + 2, false, "Space"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_tutorial_shop_trip_input.txt";
    spec.out_name = "e2e_tutorial_shop_trip";
    spec.max_frames = 120;
    spec.extra_args = {"--scene", "shop", "--tutorial-state", "SHOP_TRIP",
                       "--add-gold", "100"};
    spec.no_log = true;       // stdout probes ([SHOP-HINT]/[tutorial]/[dialogue])
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    // ------------------------------------------------------- shop-trip hint
    const auto hints = e2e::filter_lines(run.stdout_lines, "[SHOP-HINT]");
    std::printf("shop-trip: %zu [SHOP-HINT] row(s)\n", hints.size());
    check(!hints.empty(),
          "the shop rendered at least one hint row ([SHOP-HINT] probe)");
    bool knives_hint = false;
    for (const auto& l : hints) {
        if (l.find("key='tutorial_buy_knives'") != std::string::npos &&
            l.find("box=1") != std::string::npos &&
            l.find("arrow=1") != std::string::npos)
            knives_hint = true;
    }
    check(knives_hint,
          "the HintBox rendered on the knives card (tutorial_buy_knives, "
          "box + arrow textures present)");

    // ------------------------------------------------------- keyboard BUY
    const auto buys = e2e::filter_lines(run.stdout_lines,
                                        "[SHOP] buy item='WEAPON_KNIVES' "
                                        "(keyboard) -> success");
    std::printf("shop-trip: %zu knife purchase(s)\n", buys.size());
    check(!buys.empty(),
          "Space bought the knives via the keyboard BUY affordance");

    // ------------------------------------------- trip finished -> RETURN_MAP
    const auto returned = e2e::filter_lines(
        run.stdout_lines,
        "[tutorial] knives bought during SHOP_TRIP -> RETURN_MAP");
    check(!returned.empty(),
          "buying the knives advanced the tutorial to RETURN_MAP "
          "(tutorial_return_map queued)");

    // ------------------------------------------------------- return dialogue
    const auto dialogues = e2e::filter_lines(run.stdout_lines, "[DIALOG]");
    std::printf("shop-trip: %zu [DIALOG] row(s)\n", dialogues.size());
    bool sensei_line = false;
    for (const auto& l : dialogues) {
        if (l.find("speaker='Sensei'") != std::string::npos) sensei_line = true;
    }
    check(sensei_line,
          "the tutorial_return_map dialogue played (Sensei line)");

    // ---------------------------------------------------------- loop closes
    const auto done = e2e::filter_lines(run.stdout_lines,
                                        "[tutorial] returned to map -> COMPLETE");
    std::printf("shop-trip: complete=%zu\n", done.size());
    check(!done.empty(),
          "returning to the Map closed the tutorial loop (COMPLETE)");

    // --------------------------------------------------------- chain order
    // The buy must happen after the hint rendered, the RETURN_MAP queued
    // line is printed INSIDE host_buy_item (before the [SHOP] success row),
    // and the COMPLETE after the trip was finished - the chain SHOP_TRIP ->
    // RETURN_MAP -> COMPLETE.
    size_t first_hint = run.stdout_lines.size(), first_buy = run.stdout_lines.size(),
           first_done = run.stdout_lines.size();
    for (size_t i = 0; i < run.stdout_lines.size(); ++i) {
        const auto& l = run.stdout_lines[i];
        if (first_hint == run.stdout_lines.size() &&
            l.find("key='tutorial_buy_knives'") != std::string::npos)
            first_hint = i;
        if (first_buy == run.stdout_lines.size() &&
            l.find("knives bought during SHOP_TRIP") != std::string::npos)
            first_buy = i;
        if (first_done == run.stdout_lines.size() &&
            l.find("returned to map -> COMPLETE") != std::string::npos)
            first_done = i;
    }
    check(first_hint < first_buy,
          "the hint rendered before the knives were bought");
    check(first_buy < first_done,
          "the RETURN_MAP transition happened before the tutorial completed");

    return resf2::test::summary();
}
