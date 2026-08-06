// tests/e2e/test_e2e_facing.cpp
//
// Wave 11A M2 — FACING: "поворот к противнику должен быть всегда при
// нажатии любого удара; поворачивается от врага когда не должен" — an
// attack press must ALWAYS turn the fighter to face the opponent; walking
// (back-walk in particular) and hit reactions must NEVER change the mirror.
//
// Verified law (VERIFY_W11.md Q2 GREEN; SPEC_COMBAT_CORE.md Q2): the mirror
// changes ONLY via SetDirection at move START — the Controlled template
// (all 1key/2key/3key attacks, jumps, rolls, duck, magic cast) turns the
// fighter to face the enemy; StepForward/StepBack carry NO SetDirection
// (back-walk keeps facing); Hit reactions (Impulse Reverse=1) only reverse
// the knockback impulse, never the mirror; GetUp keeps own facing.
// The facing target is the LIVING enemy (battle), not the static bag spawn.
//
// E2E on the REAL binary (battle, --round-time 99):
//   run A — walk D past the disciple (enemy BEHIND the player), then ONE
//     kick press: the fighter must turn to face the enemy at the move start
//     (fr == (ex >= px) at the attack's first frame; the enemy is behind,
//     so fr must flip 1 -> 0).
//   run B — the same walk, then press A (back-walk): the fighter must keep
//     facing (fr stays 1 during step_back; no turn on movement input).
//   run C — walk close, idle until the enemy lands a hit: the hit reaction
//     must NOT turn the fighter (fr stays 1 through the reaction).
// RED on HEAD: run A keeps fr=1 (attacks never turn — only fresh direction
// input did), run B flips fr on the A press (the "turns away from the
// enemy" bug — the facing target was the static bag spawn).

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

std::string app_path;
std::string root_path;

// Boot the battle with the given events; returns the parsed frames.
e2e::RunResult boot(const std::vector<e2e::InputEvent>& events,
                    const char* tag) {
    e2e::RunSpec spec;
    spec.app = app_path;
    spec.root = root_path;
    spec.script = root_path + "/build/e2e_facing_" + tag + "_input.txt";
    spec.out_name = std::string("e2e_facing_") + tag;
    spec.max_frames = 1600;
    spec.no_log = true;       // stdout [STATE]/[COMBAT] are the probes
    spec.extra_args = {"--scene", "battle", "--round-time", "99"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        std::exit(1);
    }
    return e2e::run_app(spec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_e2e_facing <resf2_app> <repo_root>\n");
        return 1;
    }
    app_path = argv[1];
    root_path = argv[2];

    // ------------------------------------------------------ run A: attack
    // Walk D past the disciple (he stays near spawn: ex ~ -7..80 while the
    // player crosses to px ~ 380), release, then ONE kick press at 760.
    std::vector<e2e::InputEvent> evA;
    evA.push_back({200, true, "D"});
    evA.push_back({700, false, "D"});
    evA.push_back({760, true, "P"});
    evA.push_back({763, false, "P"});
    const e2e::RunResult runA = boot(evA, "attack");
    check(runA.exit_code == 0, "run A: resf2_app exited cleanly");
    const auto fa = e2e::parse_state_frames(runA);
    check(!fa.empty(), "run A produced [STATE] rows");
    if (fa.empty()) return resf2::test::summary();

    // The attack must start with the enemy BEHIND the player.
    const int atk_frame = e2e::first_frame_with(fa, "high_kick", 700);
    std::printf("facing: run A attack at frame %d\n", atk_frame);
    check(atk_frame > 0, "run A: the kick actually played after the P press");
    bool enemy_behind = false, turned = false;
    if (atk_frame > 0) {
        for (const auto& fr : fa) {
            if (fr.frame < atk_frame || fr.frame > atk_frame + 3) continue;
            if (fr.ex < fr.px) enemy_behind = true;   // enemy left of player
            if (fr.fr == 0) turned = true;            // facing left (enemy)
        }
        std::printf("facing: run A enemy behind=%d turned(left)=%d\n",
                    (int)enemy_behind, (int)turned);
    }
    check(enemy_behind,
          "run A: the walk carried the player PAST the disciple "
          "(the enemy is behind at the attack)");
    check(turned,
          "run A: the attack press TURNED the fighter to face the enemy "
          "(SetDirection at Controlled move start)");

    // ------------------------------------------------------ run B: back-walk
    // Same crossing walk, then press A (back-walk, no attack in between):
    // step_back must keep the facing (fr stays 1 the whole time).
    std::vector<e2e::InputEvent> evB;
    evB.push_back({200, true, "D"});
    evB.push_back({700, false, "D"});
    evB.push_back({760, true, "A"});
    evB.push_back({1000, false, "A"});
    const e2e::RunResult runB = boot(evB, "back");
    check(runB.exit_code == 0, "run B: resf2_app exited cleanly");
    const auto fb = e2e::parse_state_frames(runB);
    check(!fb.empty(), "run B produced [STATE] rows");
    if (fb.empty()) return resf2::test::summary();

    bool saw_step_back = false;
    bool back_turned = false;
    for (const auto& fr : fb) {
        if (fr.frame < 760 || fr.frame > 1000) continue;
        if (fr.anim == "step_back") saw_step_back = true;
        if (fr.fr == 0) back_turned = true;   // facing left = turned away
    }
    std::printf("facing: run B step_back seen=%d turned-away=%d\n",
                (int)saw_step_back, (int)back_turned);
    check(saw_step_back,
          "run B: pressing A while facing right plays the back-walk "
          "(step_back)");
    check(!back_turned,
          "run B: the back-walk NEVER turns the fighter (StepForward/"
          "StepBack carry no SetDirection — walking keeps facing)");

    // ------------------------------------------------------ run C: hit reaction
    // Walk close to the disciple (not past), idle; the enemy attacks; the
    // player's hit reaction must not flip the mirror (fr stays 1).
    std::vector<e2e::InputEvent> evC;
    evC.push_back({200, true, "D"});
    evC.push_back({400, false, "D"});
    const e2e::RunResult runC = boot(evC, "hit");
    check(runC.exit_code == 0, "run C: resf2_app exited cleanly");
    const auto fc = e2e::parse_state_frames(runC);
    check(!fc.empty(), "run C produced [STATE] rows");
    if (fc.empty()) return resf2::test::summary();

    // Find the frames of landed enemy hits on the player.
    long long first_hit_frame = -1;
    for (const auto& l : runC.stdout_lines) {
        if (l.find("[COMBAT] Enemy hit player") == std::string::npos) continue;
        // [STATE] rows carry f=; the [COMBAT] row has no frame — take the
        // closest [STATE] frame instead: parse from STATE rows below.
        (void)l;
        break;
    }
    // The engine's hit reaction is stun + flash (no reaction ANIM swaps the
    // player's pose — the original mirrors the mirror only at move starts),
    // so the guard is: from the first enemy hit on, the player's facing
    // never flips. Assert fr==1 across the WHOLE idle+hit window (>= 400).
    bool player_facing_stayed = true;
    int hit_rows = 0;
    for (const auto& l : runC.stdout_lines)
        if (l.find("[COMBAT] Enemy hit player") != std::string::npos) ++hit_rows;
    for (const auto& fr : fc) {
        if (fr.frame < 400) continue;
        if (fr.fr != 1) player_facing_stayed = false;
    }
    std::printf("facing: run C enemy-hit rows=%d (first at %lld) "
                "facing-stayed=%d\n",
                hit_rows, first_hit_frame, (int)player_facing_stayed);
    check(hit_rows > 0,
          "run C: the disciple attacked the idle player ([COMBAT] Enemy hit "
          "player rows)");
    check(player_facing_stayed,
          "run C: a landed hit reaction does NOT turn the fighter "
          "(Hit template flips only the knockback impulse, never the mirror)");

    return resf2::test::summary();
}
