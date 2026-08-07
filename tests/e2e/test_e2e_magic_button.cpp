// tests/e2e/test_e2e_magic_button.cpp
//
// Wave 10A defect 6 — MAGIC BUTTON: with a magic item equipped, the fight
// HUD showed no magic button. The button frames exist in the fight
// buttons atlas (btn_magic_normal / btn_magic_action), and render_touch_
// controls already drew them — but the Game's equipped_magic_ field was
// only READ there and never synced from the inventory, so it was always
// empty and the button never appeared.
//
// E2E on the REAL binary: boot --scene battle with --equip-magic
// MAGIC_FIRE_BALL (forces the item into the hermetic inventory), let the
// fight HUD render, and assert the [MAGIC-BTN] probe fires with the
// equipped item and the magic button frame. [Wave 11C P1] The PreFight
// (VS) screen opens the battle - the HUD (and the probe) renders once the
// ~96-frame VS phase ends, so the run needs ~140 frames, not 60.
// RED on HEAD: no [MAGIC-BTN] rows at all (equipped_magic_ never populated).

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
                     "usage: test_e2e_magic_button <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // No input needed — the fight HUD renders once the PreFight (VS) phase
    // (~96 frames) ends.
    std::vector<e2e::InputEvent> events;

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_magic_input.txt";
    spec.out_name = "e2e_magic";
    spec.max_frames = 140;
    spec.extra_args = {"--scene", "battle", "--equip-magic",
                       "MAGIC_FIRE_BALL"};
    spec.no_log = true;       // stdout [MAGIC-BTN] is the probe
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    const auto btns = e2e::filter_lines(run.stdout_lines, "[MAGIC-BTN]");
    std::printf("magic-button: %zu [MAGIC-BTN] row(s)\n", btns.size());
    check(!btns.empty(),
          "the fight HUD rendered the magic button ([MAGIC-BTN] probe)");
    bool equipped_ok = false, frame_ok = false;
    for (const auto& l : btns) {
        if (l.find("equipped='MAGIC_FIRE_BALL'") != std::string::npos)
            equipped_ok = true;
        if (l.find("frame='btn_magic_normal'") != std::string::npos)
            frame_ok = true;
    }
    check(equipped_ok,
          "the magic button carries the equipped magic item "
          "(MAGIC_FIRE_BALL)");
    check(frame_ok,
          "the magic button draws the batchButtonsFight magic frame "
          "(btn_magic_normal)");

    return resf2::test::summary();
}
