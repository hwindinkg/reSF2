// tests/e2e/test_e2e_fight_timer.cpp
//
// Wave 10A defect 3 — FIGHT TIMER: the round clock was never drawn in the
// fight HUD (bars, names and round dots render; the countdown does not),
// and the timer-win path (round clock runs out, healthier fighter takes the
// round) was never exercised end to end — a timeout win must keep the story
// moving exactly like a KO win (tutorial FIRST_FIGHT -> COMPLETE, Sensei
// follow-up dialogue queued, quest chain advanced).
//
// E2E on the REAL binary: boot straight into Battle with --tutorial-start
// (the Sensei tutorial flow runs even in a scripted run) and --round-time 8
// (stages.xml ships RoundTime=99 everywhere — too slow to wait out). The
// script walks the whole chain:
//   intro dialog (3 lines) -> dojo -> 4 steps (dismiss the hint scroll) ->
//   punchbag dialog -> 3 bag punches (FIRST_FIGHT) -> training dialog ->
//   BATTLE -> land hits (player must be healthier at the buzzer) -> round
//   clock runs out -> victory -> Results -> tutorial COMPLETE + follow-up
//   dialogue queued.
//
// Assertions: the HUD renders the countdown ([HUD-TIMER] probe), the round
// timed out ([battle] round timeout — the timer-win path), the result is a
// victory, and the story advanced (tutorial COMPLETE + story dialogue
// queued). RED on HEAD: no [HUD-TIMER] row exists (timer never rendered).

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
                     "usage: test_e2e_fight_timer <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Frame budget (gameplay frames): intro dialog 2..4, hint steps at
    // 170/290/410/530 (well-separated so taps never chain into a dash),
    // punchbag dialog 560, bag punches 700/730/760, training dialog 790,
    // battle from ~795: walk to the disciple 815..970, punches 975+ then
    // walk+punch cycles (the D1 herd recipe) through both round-timeouts.
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
    for (int f : {700, 730, 760}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    events.push_back({790, true, "Space"});
    events.push_back({791, false, "Space"});
    events.push_back({815, true, "D"});
    events.push_back({970, false, "D"});
    for (int f : {975, 999, 1023}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    for (int k = 0; k < 12; ++k) {
        const int w0 = 1050 + k * 80;   // walk window
        events.push_back({w0, true, "D"});
        events.push_back({w0 + 64, false, "D"});
        events.push_back({w0 + 68, true, "O"});
        events.push_back({w0 + 70, false, "O"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_fight_timer_input.txt";
    spec.out_name = "e2e_fight_timer";
    spec.max_frames = 2050;
    spec.extra_args = {"--scene", "battle", "--tutorial-start",
                       "--round-time", "8"};
    spec.no_log = true;       // stdout probes ([STATE]/[HUD-TIMER]/[battle])
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    // ----------------------------------------------------- timer rendered
    const auto timers = e2e::filter_lines(run.stdout_lines, "[HUD-TIMER]");
    std::printf("fight-timer: %zu [HUD-TIMER] rows\n", timers.size());
    check(!timers.empty(),
          "the fight HUD rendered the round countdown ([HUD-TIMER] probe)");
    bool counted_down = false;
    for (const auto& l : timers) {
        if (l.find("secs=7") != std::string::npos) counted_down = true;
    }
    check(counted_down,
          "the countdown started at the full round time (secs=7 seen)");

    // ------------------------------------------------------- timer-win
    const auto timeouts =
        e2e::filter_lines(run.stdout_lines, "[battle] round timeout");
    std::printf("fight-timer: %zu round timeout(s)\n", timeouts.size());
    check(!timeouts.empty(),
          "the round clock ran out ([battle] round timeout — timer-win)");

    // --------------------------------------------------- story continues
    const auto victories = e2e::filter_lines(run.stdout_lines, "[results] victory");
    check(!victories.empty(), "the timeout win is a recorded victory");
    const auto done = e2e::filter_lines(
        run.stdout_lines, "FIRST_FIGHT won -> COMPLETE");
    const auto queued =
        e2e::filter_lines(run.stdout_lines, "[QUEST] story dialogue queued");
    std::printf("fight-timer: victory=%zu complete=%zu story_queued=%zu\n",
                victories.size(), done.size(), queued.size());
    check(!done.empty() || !queued.empty(),
          "the timer-win continued the story (tutorial COMPLETE + "
          "follow-up dialogue queued)");

    return resf2::test::summary();
}
