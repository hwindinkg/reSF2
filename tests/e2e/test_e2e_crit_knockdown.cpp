// tests/e2e/test_e2e_crit_knockdown.cpp
//
// Wave 11A M4 — CRITS + KNOCKDOWN: "нет крит ударов, нельзя сбить с ног" —
// crits never fired and nothing ever knocked the enemy down.
//
// Verified mechanics (VERIFY_W11.md §1 GREEN; SPEC_WORLD_FEEL.md §1):
//   - crit chance = attr(CriticalChance) * 0.0001 (internalSettings.xml
//     <CriticalHit><Probability Base="0.0001" Attribute="CriticalChance"/>;
//     the stages.xml hero template Default seeds CriticalChance=1000 ->
//     10%). Rolled per landed hit when NOT blocked (hit+0x1C2 == 0) and
//     the attacking move has no NoCritical (move+0x4C), at damage time
//     (FUN_8f4aa998 @ game+0x3F3998).
//   - crit damage multiplier = 2^(0.0001 * attr(CriticalDamage))
//     (FUN_8f4a95a8 @ game+0x3F25A8; CriticalDamage=0 -> 1.0).
//   - knockdown: a received hit of Type=Critical (or Type=Shock — shock
//     not ported) selects the FALL family: HighHitFall / MiddleHitFall /
//     SweepHitFall / SpinningHitFall / OverheadHitFall (moves.xml
//     6375..6549, Template Fall = Hit|NotTitan) + the fall's bodyfall
//     sound (High/Middle/Sweep -> bodyfall3, Spinning/Overhead ->
//     bodyfall1), recovery via StandupAfterThrowFall.
//
// E2E on the REAL binary (battle, --round-time 99):
//   run 1 — --crit-attr 10000 1000 (E2E hook mirroring --equip-magic: 100%
//     crit chance so the mechanism is deterministic; the 0.0001 scaling is
//     still exercised: 10000*0.0001 = 1.0). Chase+punch the disciple:
//     every UNBLOCKED hit is a crit with mult = 2^0.1 = 1.0718, every crit
//     knocks the enemy down (high_hit_fall anim + bodyfall3), and blocked
//     hits never crit and never knock down.
//   run 2 — NO hook: the [CRIT] probe must show chance=0.1000, i.e. the
//     stages.xml Default template seeded CriticalChance=1000 (10%).
// RED on HEAD: no [CRIT] rows, no crit=1 [COMBAT] rows, no [KNOCKDOWN],
// no fall anim (the crit system was not ported; the damage multiplier was
// hardcoded 1.0).

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

e2e::RunResult boot(const std::vector<e2e::InputEvent>& events,
                    const char* tag,
                    const std::vector<std::string>& extra_args) {
    e2e::RunSpec spec;
    spec.app = app_path;
    spec.root = root_path;
    spec.script = root_path + "/build/e2e_crit_" + tag + "_input.txt";
    spec.out_name = std::string("e2e_crit_") + tag;
    spec.max_frames = 3800;
    spec.no_log = true;       // stdout [STATE]/[COMBAT]/[CRIT]/[KNOCKDOWN]
    spec.extra_args = {"--scene", "battle", "--round-time", "99"};
    for (const auto& a : extra_args) spec.extra_args.push_back(a);
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        std::exit(1);
    }
    return e2e::run_app(spec);
}

// The battle knockback herd recipe (D1): walk in, punch bursts, then
// walk+punch cycles that chase/ratchet the disciple.
std::vector<e2e::InputEvent> chase_recipe() {
    std::vector<e2e::InputEvent> ev;
    ev.push_back({200, true, "D"});
    ev.push_back({356, false, "D"});
    for (int f : {360, 384, 408}) {
        ev.push_back({f, true, "O"});
        ev.push_back({f + 2, false, "O"});
    }
    for (int cyc = 0; cyc < 38; ++cyc) {
        const int w0 = 430 + cyc * 80;
        ev.push_back({w0, true, "D"});
        ev.push_back({w0 + 64, false, "D"});
        const int p0 = w0 + 68;
        ev.push_back({p0, true, "O"});
        ev.push_back({p0 + 2, false, "O"});
    }
    return ev;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_crit_knockdown <resf2_app> <repo_root>\n");
        return 1;
    }
    app_path = argv[1];
    root_path = argv[2];

    // -------------------------------------------------- run 1: 100% crits
    const e2e::RunResult run1 = boot(chase_recipe(), "hit",
                                     {"--crit-attr", "10000", "1000"});
    check(run1.exit_code == 0, "run 1: resf2_app exited cleanly");
    const auto f1 = e2e::parse_state_frames(run1);
    check(!f1.empty(), "run 1 produced [STATE] rows");
    if (f1.empty()) return resf2::test::summary();

    // The player's crit chance: the hook set attr 10000 -> chance 1.0.
    const auto crits = e2e::filter_lines(run1.stdout_lines, "[CRIT]");
    std::printf("crit: %zu [CRIT] row(s)\n", crits.size());
    check(!crits.empty(),
          "landed hits run the crit roll ([CRIT] probe)");
    bool full_chance = false, crit_fired = false;
    for (const auto& l : crits) {
        if (l.find("attacker=player chance=1.0000") != std::string::npos)
            full_chance = true;
        if (l.find("crit=1") != std::string::npos) crit_fired = true;
    }
    check(full_chance,
          "the --crit-attr hook set the player's crit chance to 1.0 "
          "(10000 * 0.0001 — the binary's probability base)");
    check(crit_fired,
          "the crit roll FIRES on landed unblocked hits");

    // Every unblocked hit is a crit with the exact multiplier 2^0.1.
    const auto combats = e2e::filter_lines(run1.stdout_lines,
                                           "[COMBAT] Player hit enemy:");
    int crit_hits = 0, blocked_hits = 0;
    bool mult_ok = true, blocked_never_crit = true;
    for (const auto& l : combats) {
        if (l.find("crit=1") != std::string::npos) {
            ++crit_hits;
            if (l.find("mult=1.0718") == std::string::npos) mult_ok = false;
        }
        if (l.find("blk=0.50") != std::string::npos) {
            ++blocked_hits;
            // blocked hits never roll the crit (hit+0x1C2 gate) and never
            // carry the multiplier
            if (l.find("crit=1") != std::string::npos ||
                l.find("mult=1.0000") == std::string::npos)
                blocked_never_crit = false;
        }
    }
    std::printf("crit: %d crit hit(s), %d blocked hit(s)\n",
                crit_hits, blocked_hits);
    check(crit_hits >= 3,
          "the first unblocked hits are critical (100% crit chance)");
    check(mult_ok,
          "every crit hit carries the exact multiplier "
          "2^(0.0001*1000) = 2^0.1 = 1.0718");
    check(blocked_never_crit,
          "blocked hits never crit and never carry the multiplier "
          "(the roll is gated on the hit+0x1C2 blocked flag)");

    // ------------------------------------------------------ knockdown
    const auto kds = e2e::filter_lines(run1.stdout_lines, "[KNOCKDOWN]");
    std::printf("crit: %zu [KNOCKDOWN] row(s)\n", kds.size());
    check(!kds.empty(),
          "a critical hit knocks the enemy down ([KNOCKDOWN] probe)");
    bool fall_sound_ok = !kds.empty();
    for (const auto& l : kds) {
        if (l.find("sound='bodyfall3'") == std::string::npos)
            fall_sound_ok = false;
    }
    // Every unblocked hit is a crit and every crit hits the floor — the
    // counts must match (a blocked hit is never a knockdown).
    check(static_cast<int>(kds.size()) == crit_hits,
          "exactly the critical hits knock the enemy down "
          "(blocked hits never do)");
    check(fall_sound_ok,
          "the knockdown plays the fall family's bodyfall3 sound "
          "(High/Middle/Sweep falls)");
    // The enemy's own animation must show the fall (eanim= in [STATE]).
    int fall_frame = -1;
    for (const auto& fr : f1) {
        if (fr.frame < 350) continue;
        std::size_t a = fr.raw.find("eanim='");
        if (a == std::string::npos) continue;
        const std::string eanim =
            fr.raw.substr(a + 7, fr.raw.find('\'', a + 7) - (a + 7));
        if (eanim == "high_hit_fall") { fall_frame = (int)fr.frame; break; }
    }
    std::printf("crit: high_hit_fall enemy anim first at frame %d\n",
                fall_frame);
    check(fall_frame > 0,
          "the enemy actually plays the HighHitFall animation "
          "(moves.xml 6375, the punch's High zone fall)");

    // -------------------------------------------------- run 2: attr 1000
    // Without the hook the stages.xml Default template seeds the player's
    // CriticalChance=1000 -> chance = 0.1 (10%).
    const e2e::RunResult run2 = boot(chase_recipe(), "seed", {});
    check(run2.exit_code == 0, "run 2: resf2_app exited cleanly");
    const auto crits2 = e2e::filter_lines(run2.stdout_lines, "[CRIT]");
    bool seeded_1000 = false;
    for (const auto& l : crits2) {
        if (l.find("chance=0.1000") != std::string::npos) seeded_1000 = true;
    }
    std::printf("crit: run 2 seeded chance=0.1000 seen: %d\n",
                (int)seeded_1000);
    check(seeded_1000,
          "without the hook the player's crit chance is 0.1 — the "
          "stages.xml Default template seeds CriticalChance=1000 (10%)");

    return resf2::test::summary();
}
