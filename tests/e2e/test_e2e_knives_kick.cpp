// tests/e2e/test_e2e_knives_kick.cpp
//
// Wave 10B soak D7 — KNIVES + P WRONG MOVE: "ножи всё ещё атакуют
// атакой, как на два нажатия кнопки атаки (o) после одного нажатия на p"
// — with knives equipped, ONE kick press (P) played knives_double_slash,
// a 2key|Central|Weapon PUNCH-template move, instead of a kick.
//
// moves.xml ships NO knives kick templates at all (the knives family is
// KnivesSlash/DoubleSlash/HeavySlash/SuperSlash/SpinningSlash/UpperSlash/
// LowSlash — all "Weapon" templates). So the ORIGINAL falls back to the
// unarmed kicks for the kick branch: HighKick (1key|Central|Unarmed|Kick),
// FrontKick (2key|Forward|Unarmed|Kick), BackKick (2key|Back|Unarmed|Kick)
// per direction. The weapon-move allowance in the player's move selector
// must never leak PUNCH-template weapon moves into a KICK press.
//
// E2E on the REAL binary: boot straight into Battle with
// --equip-weapon WEAPON_KNIVES (the hermetic hook, same pattern as
// --equip-magic MAGIC_FIRE_BALL), walk toward the disciple, then press P
// ONCE (single tap, no direction held). Assert the [COMBAT] decision log
// picks HighKick (anim 'high_kick'), and knives_double_slash NEVER plays.
// RED on HEAD: [COMBAT] Kick -> KnivesDoubleSlash (prio 115 beats
// HighKick's 110 — the 2key weapon move is selectable on a 1-key press).

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
                     "usage: test_e2e_knives_kick <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Battle intro stance ~156 frames (same as the D1 battle recipe): walk
    // D 200..356, then ONE P tap at 360 (no direction held -> Central).
    // The player's selector skips the distance check, so the kick fires
    // even out of reach; the [COMBAT]/[STATE] probes show WHICH move.
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "D"});
    events.push_back({356, false, "D"});
    events.push_back({360, true, "P"});
    events.push_back({362, false, "P"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_knives_kick_input.txt";
    spec.out_name = "e2e_knives_kick";
    spec.max_frames = 600;
    spec.no_log = true;       // stdout [STATE]/[COMBAT] are the probes
    spec.extra_args = {"--scene", "battle", "--round-time", "99",
                       "--equip-weapon", "WEAPON_KNIVES"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    // ------------------------------------------------ weapon actually on
    const auto equips = e2e::filter_lines(run.stdout_lines, "[equip]");
    bool knives_equipped = false;
    for (const auto& l : equips) {
        if (l.find("combat weapon synced to: Knives") != std::string::npos)
            knives_equipped = true;
    }
    check(knives_equipped,
          "the E2E hook equipped the knives (combat weapon synced to Knives)");

    // --------------------------------------------------- the kick decision
    const auto kicks = e2e::filter_lines(run.stdout_lines, "[COMBAT] Kick");
    std::printf("knives-kick: %zu [COMBAT] Kick decision(s)\n", kicks.size());
    check(!kicks.empty(),
          "a single P press produced a Kick decision ([COMBAT] Kick)");

    bool picked_high_kick = false;
    bool picked_double_slash = false;
    for (const auto& l : kicks) {
        if (l.find("HighKick") != std::string::npos) picked_high_kick = true;
        if (l.find("KnivesDoubleSlash") != std::string::npos)
            picked_double_slash = true;
    }
    check(picked_high_kick,
          "one P with knives plays the unarmed kick HighKick "
          "(no knives kick exists in moves.xml; the original falls back "
          "to the unarmed kick)");
    check(!picked_double_slash,
          "the kick branch never selects knives_double_slash "
          "(a 2key PUNCH-template weapon move)");

    // ------------------------------------------------ what actually played
    const int kick_frame = e2e::first_frame_with(frames, "high_kick", 360);
    const int ds_frame = e2e::first_frame_with(frames, "knives_double_slash");
    std::printf("knives-kick: high_kick at frame %d, double_slash at %d\n",
                kick_frame, ds_frame);
    check(kick_frame > 0, "the high_kick animation actually played");
    check(ds_frame < 0,
          "knives_double_slash animation never played this run");

    return resf2::test::summary();
}
