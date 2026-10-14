#pragma once

// Achievement-counter SESSION records (JS `fe` = the class `Vt`, readable.js
// L15084). The JS fight class `ca` owns one `fe` per fight; `fe.init(...)`
// (L15093) builds a per-fight session counter for every
// `internal_settings.xml <AchievementCounter><Counter>` row applicable to the
// fight (`v.iY.W5a(this.Da)`), and the fight feeds it:
//
//   * per PLAYER-landed hit — `this.fe.wqb(a.Pd.Sj(), b.Uq, b.ep, b.Yi,
//     a_data, b.block)` (L16187), 6 args: the attack move (`Sj()`), the
//     head-hit flag (`Uq`), the round's-first-unblocked-hit flag (`ep`), the
//     disarm flag (`Yi`), the lethal flag (`a_data` = `parameters.gd <= 0`),
//     and the block flag (`b.block`).
//   * per ENEMY-landed hit — `this.fe.Cqb(b.block)` (L16189).
//   * the round end — `JZa` (L17254): `setTime(R4 - Pz())`, `xrb(a.gd)`,
//     `Rrb` (PerfectRound), `vsb` (ShockWin), then `$K()` (the per-round
//     `cb` reset).
//   * the fight end — `Pf`/`KZa` (L17190-17252): `Ysb`/`Xsb`/`zrb`/`Eqb`/
//     `Dqb`/`Fqb`/`Msb`/`Iqb`/`Vqb`/`Fsb` + `complete(round.eL)`.
//
// The scene layer records the RAW, counter-def-independent facts here; the
// app layer (`screens.cpp`) maps them onto the counter DEFS. No counter name
// or Animation/Weapon string is hardcoded outside internal_settings.xml.

#include <string>
#include <vector>

namespace sf2::scene {

// One PLAYER-landed hit (the `wqb` payload).
struct AchievementHitRecord {
    // The attack move's animation-name list (JS `Sj().xl`, `d2` L29525 — an
    // exact string membership test against the counter's `Animation` attr).
    std::vector<std::string> anim_names;
    // The player's wielded item name (JS `fe.Hha.Hd.name`, `Hha` = `this.kc`
    // passed to `fe.init`; the shipped unarmed sentinel is "Fists").
    std::string weapon;
    bool head = false;     // `Uq`   (hit the Head zone)
    bool first = false;    // `ep`   (!Dga, the round's first unblocked hit)
    bool disarm = false;   // `Yi`
    bool blocked = false;  // `b.block`
    bool lethal = false;   // `a_data` (`parameters.gd <= 0`)
};

// One round's facts (`JZa` L420 + the `cb`/`O1` latches).
struct AchievementRoundRecord {
    int round_number = 0;
    bool player_won = false;            // `JZa` `a.qb` (the round winner)
    bool player_round_unhit = false;    // `a.cE` -> `Rrb` (PerfectRound)
    bool player_hit_at_all = false;     // `ca.O1` (`Cqb`, L15247)
    bool player_hit_unblocked = false;  // the BlockedRound disqualifier `cb`
    float player_hp_end = 0.0f;         // `a.gd` -> `xrb` (HealthRemained)
    int duration = 0;                   // `R4 - Pz()` -> `setTime`
    std::vector<AchievementHitRecord> player_hits;
};

}  // namespace sf2::scene
