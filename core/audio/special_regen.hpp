#pragma once

// Special-move regen (Phase 7.4) — spec + pure implementation of the JS
// `wd.MOa()` regen clock (sf2.502f0946.js L532-533, gated at L499).
//
// JS cites (exact lines, verified 2026-09-04):
//   - Gate — `wd.ia()` (L499):
//     `ca.Ka() != null && ca.Ka().eu == 2 && this.MOa()` — regen ticks once
//     per 60 Hz fighter tick and ONLY in fight phase 2 (`eu == 2`).
//   - `MOa()` (L532-533):
//     `JA += aT / (rlb * v.on())` (clamped to `aT`, fires `new yd(9, JA, 1)`);
//     `eA += DR / (hFa * v.on())` (clamped to `DR`, fires `yd(10, ...)`);
//     `mA += TR / (wGa * v.on())` (clamped to `TR`, fires `yd(11, ...)`);
//     `iu += pU / (UNa * v.on())` (clamped to `pU`, fires `yd(14, ...)` and
//     disarms `oU` at full). Each branch runs only when its unlock flag is
//     set (`m4`/`i2`/`SR` for 9/10/11; `oU && iu < pU` for 14) and only while
//     below max. `v.on()` is the global timescale (slow-mo divides the rate).
//   - `ju` ctor (L546, defaults): `UNa = 500, pU = 1, hFa = DR = 0/1...`:
//     `UNa=500, pU=1, hFa=0, DR=1, rlb=0, aT=1, wGa=0, TR=1, teb=0,
//      oU=true, SR=m4=i2=false` — durations are 0 until the equipped magic
//     item configures them, so a zero/negative duration must SNAP to full
//     (JS would divide by zero; the native guards).
//   - Fire gate — `wd.yJa(a)` (L501) blocks/allows codes 9/10/11/12/14 from
//     these meters before `Kl.Sgb(a)`. Its exact accept/block polarity is
//     NOT re-implemented here — the fight owner (forbidden files this
//     stream) must port it with the move code list.
//
// Placement note: the canonical home is `sf2::scene::Fighter` (as `Ja` beside
// the `Kl` key buffer) stepped from `FightController::update_fighter` — both
// forbidden to this stream. This header is dependency-free (no fight.hpp)
// so it can move there unchanged; the screens.cpp tick in this stream drives
// display-layer copies only and has NO gameplay impact.

namespace sf2::audio {

// The `Ja`/`ju` meters for one fighter (codes 9/10/11/14; code 12 is the
// `bh` magic-bullet count, NOT a regen meter).
struct SpecialMeters {
    bool punch_open_ = false;    // `Ja.m4` — code 9 (punch special) unlocked
    float punch_ = 0.0f;         // `Ja.JA` — code 9 charge
    float punch_max_ = 1.0f;     // `Ja.aT` — code 9 capacity
    float punch_time_ = 0.0f;    // `Ja.rlb` — code 9 full-charge ticks
    bool kick_open_ = false;     // `Ja.i2` — code 10 (kick special) unlocked
    float kick_ = 0.0f;          // `Ja.eA` — code 10 charge
    float kick_max_ = 1.0f;      // `Ja.DR` — code 10 capacity
    float kick_time_ = 0.0f;     // `Ja.hFa` — code 10 full-charge ticks
    bool ranged_open_ = false;   // `Ja.SR` — code 11 (ranged special) unlocked
    float ranged_ = 0.0f;        // `Ja.mA` — code 11 charge
    float ranged_max_ = 1.0f;    // `Ja.TR` — code 11 capacity
    float ranged_time_ = 0.0f;   // `Ja.wGa` — code 11 full-charge ticks
    bool raid_armed_ = true;     // `Ja.oU` — code 14 one-shot arm (`ju` = true)
    float raid_ = 0.0f;          // `Ja.iu` — code 14 charge
    float raid_max_ = 1.0f;      // `Ja.pU` — code 14 capacity (`ju` = 1)
    float raid_time_ = 500.0f;   // `Ja.UNa` — code 14 full-charge ticks (500)
};

// Which meters produced a new value this tick (the native stand-in for the
// `yp.Z(new yd(code, value, 1))` events JS MOa fires into the HUD charges,
// `fu.Hrb` L452).
struct RegenEvents {
    bool punch = false;    // code 9  (`yd(9, JA, 1)`)
    bool kick = false;     // code 10 (`yd(10, eA, 1)`)
    bool ranged = false;   // code 11 (`yd(11, mA, 1)`)
    bool raid = false;     // code 14 (`yd(14, iu, 1)`)
};

// One 60 Hz regen tick. `timescale` is JS `v.on()` (1.0 = real time).
inline RegenEvents regen_tick(SpecialMeters& m, float timescale) {
    RegenEvents ev;
    const float ts = timescale > 0.0f ? timescale : 1.0f;
    if (m.punch_open_ && m.punch_ < m.punch_max_) {
        if (m.punch_time_ <= 0.0f) {
            m.punch_ = m.punch_max_;
        } else {
            m.punch_ += m.punch_max_ / (m.punch_time_ * ts);
            if (m.punch_ > m.punch_max_) m.punch_ = m.punch_max_;
        }
        ev.punch = true;
    }
    if (m.kick_open_ && m.kick_ < m.kick_max_) {
        if (m.kick_time_ <= 0.0f) {
            m.kick_ = m.kick_max_;
        } else {
            m.kick_ += m.kick_max_ / (m.kick_time_ * ts);
            if (m.kick_ > m.kick_max_) m.kick_ = m.kick_max_;
        }
        ev.kick = true;
    }
    if (m.ranged_open_ && m.ranged_ < m.ranged_max_) {
        if (m.ranged_time_ <= 0.0f) {
            m.ranged_ = m.ranged_max_;
        } else {
            m.ranged_ += m.ranged_max_ / (m.ranged_time_ * ts);
            if (m.ranged_ > m.ranged_max_) m.ranged_ = m.ranged_max_;
        }
        ev.ranged = true;
    }
    if (m.raid_armed_ && m.raid_ < m.raid_max_) {
        if (m.raid_time_ <= 0.0f) {
            m.raid_ = m.raid_max_;
        } else {
            m.raid_ += m.raid_max_ / (m.raid_time_ * ts);
            if (m.raid_ > m.raid_max_) m.raid_ = m.raid_max_;
        }
        ev.raid = true;
        if (m.raid_ >= m.raid_max_) m.raid_armed_ = false;  // JS `oU = !1`
    }
    return ev;
}

// The L499 phase gate: regen runs only while the fight is live (`eu == 2`).
// Takes FightController::phase() unchanged (fight_phase::fight == 2).
inline bool regen_should_tick(int phase) { return phase == 2; }

// Spending a full charge when the special fires. Codes 9/10/11 spend to 0
// and re-regen (their unlock flags stay); code 14 re-arms via fire_raid.
inline bool consume_charge(float& value, float max) {
    if (value < max) return false;
    value = 0.0f;
    return true;
}

// Fires code 14: spends the full charge and re-arms the one-shot
// (JS `FKa()`: `iu = 0; oU = !0`, then regen resumes).
inline bool fire_raid(SpecialMeters& m) {
    if (m.raid_ < m.raid_max_) return false;
    m.raid_ = 0.0f;
    m.raid_armed_ = true;
    return true;
}

}  // namespace sf2::audio
