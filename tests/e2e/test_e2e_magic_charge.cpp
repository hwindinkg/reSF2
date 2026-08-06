// tests/e2e/test_e2e_magic_charge.cpp
//
// Wave 11A M1 — MAGIC CHARGE: "не могу зарядить магию ударами" — punches
// must fill the magic bar; at full the magic cast must fire the equipped
// magic.
//
// Verified formula (VERIFY_W11.md Q1 GREEN; SPEC_COMBAT_CORE.md Q1):
// per-fighter charge float 0..1 at Fighter+0x6EC / count at +0x6F0; every
// landed attack-interval hit charges BOTH fighters
//
//   charge += pow2Factor(attacker, blocked) * powf(2, rechargeAttr(recipient))
//             * pow2Factor(victim, critical) * damage,   clamp [0,1],
//   skip when the recipient's count is already 1 (full bar never overcharges);
//   suppressed when the ATTACKING move carries NoMagicRecharge="1"
//   (moves.xml RangedMissile/MagicMissile/RaidMissile/MagicAcidCloud).
//   Crossing 1.0 -> count=1 (MagicCharged). Round start: count=0,
//   charge=clamp(InitialCharge,0,1). The magic button (Key Type="Magic"
//   PressType="Tap") selects the MagicXXXPlayer move
//   (Template="1key|MagicPlayer", Type=ATTACK, Priority=110) by the equipped
//   magic item's SubType (MAGIC_FIRE_BALL -> FireBall -> "FireballPlayer"
//   TacticWeapon="FireBall"); the cast consumes the count.
//
// [HEURISTIC-TODO] Recharge attributes (PainRecharge/DamageRecharge) are
// zero-fallback in the MVP port (the spec's [UNCERTAIN] attr source: the
// stages.xml Magic*Recharge names do not match the 12-char lookup names),
// so powf(2, 0) = 1.0 and a landed punch (Damage Value=0.11) adds ~0.11 —
// the bar fills after ~10 landed hits, which is the E2E's observable.
//
// E2E on the REAL binary: boot --scene battle with --equip-magic
// MAGIC_FIRE_BALL, walk to the disciple, spam O punches, watch the
// [MAGIC-CHG] probe climb to 1.0 (count=1), then press X (the magic cast
// key) and assert the cast move FireballPlayer plays (anim
// 'fireball_player'), the [PROJECTILE] Fired FireBall probe fires the
// equipped magic, and the count is consumed back to 0.
// RED on HEAD: no [MAGIC-CHG] rows at all (charge system not ported), no
// [MAGIC-CAST], the X key does nothing.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

bool has_needle(const std::string& line, const char* needle) {
    return line.find(needle) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_magic_charge <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // Battle intro stance ~156 frames (same as the D1 battle recipe): walk
    // D toward the disciple, punch bursts while he is in reach, then
    // walk+punch cycles — every hit in reach charges BOTH fighters
    // (HighPunch <Damage Value=0.11> -> ~10 landed hits fill the bar; a
    // blocked hit charges the same because the block/crit pow2 factors are
    // disabled-neutral 1.0 with zero-fallback recharge attrs). The chase
    // ratchets the enemy toward the wall so he stops escaping; the cast
    // (X) goes at frame 2500, before the enemy's ~42-hit health pool runs
    // out. --round-time 99 keeps the fight alive for the whole script.
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "D"});
    events.push_back({356, false, "D"});
    for (int f : {360, 384, 408}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    for (int cyc = 0; cyc < 25; ++cyc) {
        const int w0 = 430 + cyc * 80;          // walk window (D hold)
        events.push_back({w0, true, "D"});
        events.push_back({w0 + 64, false, "D"});
        const int p0 = w0 + 68;                 // punch tap at reach
        events.push_back({p0, true, "O"});
        events.push_back({p0 + 2, false, "O"});
    }
    events.push_back({2500, true, "X"});        // magic cast (Type=Magic Tap)
    events.push_back({2503, false, "X"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_magic_charge_input.txt";
    spec.out_name = "e2e_magic_charge";
    spec.max_frames = 3000;
    spec.no_log = true;       // stdout [STATE]/[MAGIC-CHG]/[PROJECTILE] probes
    spec.extra_args = {"--scene", "battle", "--round-time", "99",
                       "--equip-magic", "MAGIC_FIRE_BALL"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    // ------------------------------------------------ magic actually on
    const auto equips = e2e::filter_lines(run.stdout_lines, "[equip]");
    bool magic_equipped = false;
    for (const auto& l : equips) {
        if (has_needle(l, "magic 'MAGIC_FIRE_BALL' equipped"))
            magic_equipped = true;
    }
    check(magic_equipped,
          "the E2E hook equipped the magic item (MAGIC_FIRE_BALL)");

    // ------------------------------------------------ landed hits charge
    const auto chg = e2e::filter_lines(run.stdout_lines, "[MAGIC-CHG]");
    std::printf("magic-charge: %zu [MAGIC-CHG] row(s)\n", chg.size());
    check(!chg.empty(),
          "landed punches produce magic-charge updates ([MAGIC-CHG] probe)");

    // ------------------------------------------------ the bar fills
    // Probe rows: [MAGIC-CHG] f=.. player role=0 chg=0.550 cnt=0 (one row
    // per fighter per landed hit; the player's own charge is the role=0
    // row tagged "player").
    float max_charge = 0.0f;
    bool saw_count1 = false;
    for (const auto& l : chg) {
        if (!has_needle(l, " player role=0 ")) continue;
        const std::size_t a = l.find("chg=");
        if (a != std::string::npos) {
            const float v = std::strtof(l.c_str() + a + 4, nullptr);
            if (v > max_charge) max_charge = v;
        }
        if (has_needle(l, "cnt=1")) saw_count1 = true;
    }
    std::printf("magic-charge: max player charge %.3f, count=1 seen: %d\n",
                max_charge, (int)saw_count1);
    check(max_charge >= 0.99f,
          "repeated landed punches fill the player's magic bar to 1.0");
    check(saw_count1,
          "crossing 1.0 makes magic AVAILABLE (count=1, MagicCharged)");

    // ------------------------------------------------ the cast fires
    const auto casts = e2e::filter_lines(run.stdout_lines, "[MAGIC-CAST]");
    std::printf("magic-charge: %zu [MAGIC-CAST] row(s)\n", casts.size());
    check(!casts.empty(),
          "the magic key press at full bar selects the cast ([MAGIC-CAST])");

    bool cast_fireball = false, cast_consumed = false;
    for (const auto& l : casts) {
        if (has_needle(l, "move='FireballPlayer'")) cast_fireball = true;
        if (has_needle(l, "cnt=0")) cast_consumed = true;
    }
    check(cast_fireball,
          "the cast selects the equipped magic's player move "
          "(FireballPlayer = MAGIC_FIRE_BALL SubType FireBall)");
    check(cast_consumed,
          "the cast consumes the magic charge (count back to 0)");

    const auto shots = e2e::filter_lines(run.stdout_lines, "[PROJECTILE] Fired");
    bool fired_fireball = false;
    for (const auto& l : shots) {
        if (has_needle(l, "Fired FireBall")) fired_fireball = true;
    }
    check(fired_fireball,
          "the cast fires the equipped magic projectile "
          "([PROJECTILE] Fired FireBall)");

    const int cast_frame = e2e::first_frame_with(frames, "fireball_player", 2000);
    std::printf("magic-charge: fireball_player anim at frame %d\n", cast_frame);
    check(cast_frame > 0,
          "the FireballPlayer animation actually played after the cast");

    return resf2::test::summary();
}
