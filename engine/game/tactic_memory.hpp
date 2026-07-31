// engine/game/tactic_memory.hpp
//
// TacticMemory — the enemy-AI fight-memory state model (ADR-005 D8),
// adjusted to the R5 binary evidence in
// reverse/analysis/MEMORY_INDEXING_R56.md (commit 6cf0faa).
//
// [ORIGINAL] R5 — NO ring depth exists. `<Memory>` parses only Strikes and
// RoundFactor (both float, defaults 10.0f; FUN_8f4488ac @ 0x8F4488AC, sites
// 0x8f448b90-0x8f448c0c). Records live in an UNBOUNDED vector: find-or-create
// FUN_8f4b151c @ 0x8F4B151C (doubling growth `count ? 2*count : 1`, no
// eviction), 20-record initial capacity per slot from FUN_8f4b0dac @
// 0x8F4B0DAC. Effective depth = exponential decay only.
//
// [ORIGINAL] R5 — Strikes is a decay RATE, not a counter: on every access,
// k = powf(2, -frames/rate) (FUN_8f72ed40 = powf) scales all five accumulator
// floats in place, last_frame is stamped unconditionally, decay only applies
// when frames >= 1 (VERIFY_FUN_8f44ac78.md; write-back order from
// FUN_8f4b173c: +0x08 damage, +0x0c counter, +0x04 strike_damage, +0x14 hits,
// +0x10 strike_count). frames = owner event counter − rec->last_frame, where
// the owner counter is the hits-taken event counter fighter+0x71c. In this
// engine model the decay clock is driven by tick() — the hits-taken event
// counter has no P1 event source yet:
//   [HEURISTIC-TODO] decay frame source: engine tick() frames stand in for
//   the binary's fighter+0x71c hits-taken event counter (incremented per
//   damage event FUN_8f4aa998) — pending @re-verifier GREEN on the P2 damage
//   wiring.
//
// [ORIGINAL] R5 — feeds (record ids keyed by ANIMATION NAME here; the binary
// keys by interned record-id — [HEURISTIC-TODO] pending the v=7 name→ids
// wiring, MEMORY_INDEXING_R56.md §5):
//   * damage landed  -> record_strike: strike_damage += amount,
//     strike_count += 1 (FUN_8f4b173c @ 0x8F4B173C, called from FUN_8f4aa998
//     tail with the attacker's current animation, mirrored into both
//     fighters' memories: victim slot 1, attacker slot 0);
//   * "Uninterrupt"  -> record_counter: counter += 1
//     (FUN_8f4b1830 @ 0x8F4B1830).
// In this build only the counter channel is combat-fed; the probe's D and H
// channels are never fed (MEMORY_INDEXING_R56.md §2.4) — the fields exist and
// decay, and read as neutral 0.0f until a later phase feeds them.
//
// [ORIGINAL] R5 — reset points:
//   * lazy decay on every access (above);
//   * round end: all five accumulator floats of every record scaled by
//     RoundFactor (FUN_8f4a84e8 @ 0x8F4A84E8, per fighter from FUN_8f4275d4);
//   * fighter reset: full zero (FUN_8f4ac6bc @ 0x8F4AC6BC).
//
// [ORIGINAL] R5 — Intervals/EnemyIntervals (the tracer's prints) reset when a
// new action is recorded; the engine model exposes them as
// self_interval()/enemy_interval() = frames since the last self/enemy action,
// feeding TacticContext::self_interval / enemy_interval.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "tactic_settings.hpp"  // RngSource (ADR-005 D4 contract)

namespace resf2::game {

// One per-animation memory record (native AtfMemoryRecord, 0x1c bytes,
// MEMORY_INDEXING_R56.md §2.4). The binary's +0x00 anim_record_id key is the
// animation NAME here (engine-side keying per ADR-005 D8).
struct MemoryRecord {
    std::string name;         // key — the animation name
    float damage = 0;         // "D" probe channel (native +0x08)
    float counter = 0;        // "C" probe channel (native +0x0c) — the only
                              // channel combat-fed in this build ("Uninterrupt")
    float hits = 0;           // "H" probe channel (native +0x14)
    float strike_damage = 0;  // fed on damage landed (native +0x04)
    float strike_count = 0;   // fed on damage landed (native +0x10)
    // [HEURISTIC-TODO] `frames` (float) is the decay-delta cache: the elapsed
    // frames at the last access (the binary's local `frames = cur -
    // rec->last_frame`, memory_indexing_r56.candidate.cpp:122), written on
    // every access so k = 2^(-frames/rate) reads straight off the record —
    // pending @re-verifier GREEN on the exact D8 field list.
    float frames = 0;         // elapsed frames at last access (delta cache)
    int last_frame = 0;       // decay stamp (native +0x18, owner-counter units)
};

// The fight-memory state model (ADR-005 D8, R5-verified).
//
// Public state mirrors the native layout where the engine has a consumer;
// methods carry the [ORIGINAL] semantics anchored in MEMORY_INDEXING_R56.md.
struct TacticMemory {
    TacticMemory() { records.reserve(20); }  // [ORIGINAL] FUN_8f4b0dac: 20-record
                                             // initial capacity per slot

    // One AI frame. Advances the interval counters, decrements both
    // countdowns floor at 0 (never negative), and advances the decay clock
    // ([HEURISTIC-TODO] stand-in for fighter+0x71c, see file header).
    void tick();

    // The last self/enemy action (animation name), appended to the unbounded
    // action log and resetting the matching interval counter to 0
    // ([ORIGINAL] Intervals/EnemyIntervals reset on action, §3).
    void record_self(const std::string& name);
    void record_enemy(const std::string& name);

    // [ORIGINAL] R5 feeds — damage landed / "Uninterrupt" event
    // (FUN_8f4b173c / FUN_8f4b1830): lazy decay, then
    // strike_damage += amount, strike_count += 1 (resp. counter += 1).
    // The binary mirrors the feed into both fighters' memories; the caller
    // owns the mirroring by calling the other fighter's TacticMemory.
    void record_strike(const std::string& name, float amount);
    void record_counter(const std::string& name);

    // [ORIGINAL] <ResponseDelay>/<EnemyResponseDelay> <Min Base/><Max Base/>
    // ranges: roll frames_until_next_decision / enemy_reaction_frames
    // uniformly within [min, max] INCLUSIVE (RngSource contract: [0,
    // RAND_MAX], so rng()==0 hits min and rng()==RAND_MAX hits max).
    //   [HEURISTIC-TODO] roll rounding (llround vs floor) and the exact
    //   consumption site are not pinned by binary evidence — pending
    //   @re-verifier GREEN.
    void start_response_delay(float min, float max, RngSource rng);
    void start_enemy_reaction(float min, float max, RngSource rng);

    // The AI made a decision now: clears the next-decision countdown.
    //   [HEURISTIC-TODO] post-decision countdown semantics (0 = unblocked)
    //   pending @re-verifier GREEN on the decision-loop wiring.
    void record_decision();

    // [ORIGINAL] FUN_8f4a84e8: round-end scale of ALL FIVE accumulator
    // floats of every record by round_factor (the tactic's RoundFactor);
    // stamps (frames/last_frame) are NOT scaled.
    void round_end(float round_factor);

    // [ORIGINAL] FUN_8f4ac6bc: full zero — records, action logs, interval
    // counters, countdowns, and the decay clock all restart.
    void reset();

    // TacticContext::self_interval / enemy_interval feed — frames since the
    // last self/enemy action ([ORIGINAL] Intervals/EnemyIntervals, §3).
    [[nodiscard]] float self_interval() const { return static_cast<float>(frames_since_self); }
    [[nodiscard]] float enemy_interval() const { return static_cast<float>(frames_since_enemy); }

    // Decayed-sum accessors for the probe channels (VERIFY_FUN_8f44ac78.md):
    // lazy decay write-back on access, then the single-channel sum. An
    // absent animation reads 0.0f and creates NO record (neutral-by-zero,
    // ADR-005 R3 — the native probe sums zero records for an empty id set,
    // MEMORY_INDEXING_R56.md §5.3).
    [[nodiscard]] float decayed_damage(const std::string& name);
    [[nodiscard]] float decayed_counter(const std::string& name);
    [[nodiscard]] float decayed_hits(const std::string& name);

    // --- public state (ADR-005 D8) --------------------------------------
    std::vector<MemoryRecord> records;   // per-animation memory, UNBOUNDED
    std::vector<std::string> self_actions;   // unbounded, no eviction
    std::vector<std::string> enemy_actions;  // unbounded, no eviction
    int frames_since_self = 0;        // Intervals
    int frames_since_enemy = 0;       // EnemyIntervals
    int frames_until_next_decision = 0;  // ResponseDelay countdown
    int enemy_reaction_frames = 0;       // EnemyResponseDelay countdown
    float strikes = 10.0f;  // decay rate ([ORIGINAL] tactic+0x00, default
                            // 10.0f from the tactic ctor 0x41200000)

private:
    // Decay clock — [HEURISTIC-TODO] stand-in for the binary's fighter+0x71c
    // hits-taken event counter (see file header).
    int frame_ = 0;

    // [ORIGINAL] FUN_8f4b151c: linear find-or-create, unbounded (vector
    // doubling growth, no eviction).
    MemoryRecord* find_or_create(const std::string& name);

    // [ORIGINAL] lazy decay (VERIFY_FUN_8f44ac78.md / FUN_8f4b173c): when
    // frames >= 1, k = powf(2, -frames/strikes) scales all five floats in the
    // native write-back order; last_frame and the frames-delta cache are
    // updated unconditionally. rate 0.0f with frames >= 1 zeroes the record
    // (2^-inf == 0 — the binary's no-tactic default).
    void decay(MemoryRecord& rec);
};

inline void TacticMemory::tick() {
    ++frame_;
    ++frames_since_self;
    ++frames_since_enemy;
    if (frames_until_next_decision > 0) --frames_until_next_decision;
    if (enemy_reaction_frames > 0) --enemy_reaction_frames;
}

inline void TacticMemory::record_self(const std::string& name) {
    self_actions.push_back(name);  // unbounded — no eviction (R5: no ring)
    frames_since_self = 0;
}

inline void TacticMemory::record_enemy(const std::string& name) {
    enemy_actions.push_back(name);
    frames_since_enemy = 0;
}

inline MemoryRecord* TacticMemory::find_or_create(const std::string& name) {
    for (MemoryRecord& r : records) {
        if (r.name == name) return &r;
    }
    records.emplace_back();  // vector growth doubles capacity, no eviction
    records.back().name = name;
    return &records.back();
}

inline void TacticMemory::decay(MemoryRecord& rec) {
    const int frames = frame_ - rec.last_frame;
    if (frames >= 1) {
        const float k = std::pow(2.0f, -(float)frames / strikes);  // FUN_8f72ed40
        rec.damage *= k;          // +0x08 — native write-back order
        rec.counter *= k;         // +0x0c
        rec.strike_damage *= k;   // +0x04
        rec.hits *= k;            // +0x14
        rec.strike_count *= k;    // +0x10
    }
    rec.frames = static_cast<float>(frames);  // delta cache (always written)
    rec.last_frame = frame_;                  // unconditional stamp
}

inline void TacticMemory::record_strike(const std::string& name, float amount) {
    MemoryRecord* rec = find_or_create(name);
    decay(*rec);
    rec->strike_damage += amount;  // FUN_8f4b173c: += amount on decayed base
    rec->strike_count += 1.0f;     //                       += 1
}

inline void TacticMemory::record_counter(const std::string& name) {
    MemoryRecord* rec = find_or_create(name);
    decay(*rec);
    rec->counter += 1.0f;  // FUN_8f4b1830: += 1 on decayed base
}

inline void TacticMemory::start_response_delay(float min, float max, RngSource rng) {
    if (max < min) std::swap(min, max);
    const float t = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
    frames_until_next_decision = static_cast<int>(std::llround(min + t * (max - min)));
}

inline void TacticMemory::start_enemy_reaction(float min, float max, RngSource rng) {
    if (max < min) std::swap(min, max);
    const float t = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
    enemy_reaction_frames = static_cast<int>(std::llround(min + t * (max - min)));
}

inline void TacticMemory::record_decision() {
    frames_until_next_decision = 0;
}

inline void TacticMemory::round_end(float round_factor) {
    for (MemoryRecord& r : records) {
        r.damage *= round_factor;        // FUN_8f4a84e8: all five floats
        r.counter *= round_factor;
        r.hits *= round_factor;
        r.strike_damage *= round_factor;
        r.strike_count *= round_factor;
    }
}

inline void TacticMemory::reset() {
    records.clear();
    self_actions.clear();
    enemy_actions.clear();
    frames_since_self = 0;
    frames_since_enemy = 0;
    frames_until_next_decision = 0;
    enemy_reaction_frames = 0;
    frame_ = 0;
}

inline float TacticMemory::decayed_damage(const std::string& name) {
    for (MemoryRecord& r : records) {
        if (r.name == name) {
            decay(r);
            return r.damage;
        }
    }
    return 0.0f;  // neutral-by-zero, no record created
}

inline float TacticMemory::decayed_counter(const std::string& name) {
    for (MemoryRecord& r : records) {
        if (r.name == name) {
            decay(r);
            return r.counter;
        }
    }
    return 0.0f;
}

inline float TacticMemory::decayed_hits(const std::string& name) {
    for (MemoryRecord& r : records) {
        if (r.name == name) {
            decay(r);
            return r.hits;
        }
    }
    return 0.0f;
}

}  // namespace resf2::game
