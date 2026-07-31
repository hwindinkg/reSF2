// tests/test_tactic_memory.cpp
//
// Pins the TacticMemory state model (engine/game/tactic_memory.hpp, ADR-005
// D8) to the R5 binary evidence in
// reverse/analysis/MEMORY_INDEXING_R56.md (commit 6cf0faa):
//
//   * NO ring depth exists — the per-animation memory is an UNBOUNDED record
//     vector (find-or-create FUN_8f4b151c, doubling growth, no eviction;
//     initial capacity 20 records/slot from FUN_8f4b0dac). Effective depth =
//     exponential decay only.
//   * Strikes = decay rate: k = powf(2, -frames/rate) (FUN_8f72ed40 = powf),
//     lazy on every access, write-back to all five accumulator floats,
//     last_frame stamped unconditionally (VERIFY_FUN_8f44ac78.md).
//   * Strike feed on damage landed: strike_damage += amount, strike_count +=
//     1 (FUN_8f4b173c); "Uninterrupt" feed: counter += 1 (FUN_8f4b1830).
//   * Round end: all five record floats scaled by RoundFactor
//     (FUN_8f4a84e8); fighter reset zeroes everything (FUN_8f4ac6bc).
//   * Intervals/EnemyIntervals = frames since the last self/enemy action.

#include "../engine/game/tactic_memory.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using resf2::game::MemoryRecord;
using resf2::game::TacticMemory;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

#define CHECK_NEAR(a, b, msg) do { \
    double _va = (double)(a), _vb = (double)(b); \
    if (std::fabs(_va - _vb) > 1e-3) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %f, expected %f\n", \
                     __LINE__, msg, _va, _vb); ++tests_failed; \
    } else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Seeded LCG honoring the RngSource contract: values in [0, RAND_MAX].
static resf2::game::RngSource make_lcg(unsigned seed) {
    return [seed]() mutable -> unsigned {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) % ((unsigned)RAND_MAX + 1u);
    };
}

// Stub RNG returning a fixed value (0 -> min endpoint, RAND_MAX -> max).
static resf2::game::RngSource fixed_rng(unsigned v) {
    return [v]() -> unsigned { return v; };
}

static const MemoryRecord* find_rec(const TacticMemory& m, const std::string& name) {
    for (const MemoryRecord& r : m.records) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

int main() {
    std::printf("\n=== Decay math exact: k = 2^(-frames/rate) (R5) ===\n");
    {
        TacticMemory m;
        m.strikes = 10.0f;
        // Seed via the R5 feeds (damage path FUN_8f4b173c, "Uninterrupt"
        // path FUN_8f4b1830), then seed the probe channels directly (they
        // are never fed in this build, MEMORY_INDEXING_R56.md §2.4).
        m.record_strike("Throw", 100.0f);   // strike_damage=100, strike_count=1
        m.record_counter("Throw");          // counter=1
        for (MemoryRecord& r : m.records) {
            if (r.name == "Throw") { r.damage = 100.0f; r.hits = 16.0f; }
        }
        m.records.push_back(MemoryRecord{"Kick", 64.0f, 8.0f, 16.0f, 0, 0, 0, 0});

        // 30 ticks with rate 10 -> k = 2^(-30/10) = 2^-3 = 0.125
        for (int i = 0; i < 30; ++i) m.tick();

        CHECK_NEAR(m.decayed_damage("Kick"), 64.0 * 0.125, "D channel decays by 2^-3");
        CHECK_NEAR(m.decayed_counter("Kick"), 8.0 * 0.125, "C channel decays by 2^-3");
        CHECK_NEAR(m.decayed_hits("Kick"), 16.0 * 0.125, "H channel decays by 2^-3");
        // Throw's FIRST access: exactly 30 elapsed frames -> the record's
        // `frames` field caches the delta, so k = 2^(-frames/rate) reads
        // straight off it. (A same-frame second access would re-stamp 0.)
        CHECK_NEAR(m.decayed_damage("Throw"), 100.0 * 0.125, "strike-fed record D decays");
        const MemoryRecord* tr = find_rec(m, "Throw");
        CHECK(tr != nullptr, "Throw record present");
        if (tr) {
            CHECK(tr->frames == 30.0f, "record.frames caches elapsed delta");
            CHECK_NEAR(tr->counter, 1.0 * 0.125, "counter feed decays (all five)");
            CHECK_NEAR(tr->hits, 16.0 * 0.125, "seeded H channel decays");
            CHECK_NEAR(tr->strike_damage, 100.0 * 0.125, "strike_damage decays (all five)");
            CHECK_NEAR(tr->strike_count, 1.0 * 0.125, "strike_count decays (all five)");
        }
    }

    std::printf("\n=== Decay formula against record.frames (R5) ===\n");
    {
        TacticMemory m;
        m.strikes = 4.0f;  // halve every 4 frames
        m.records.push_back(MemoryRecord{"A", 256.0f, 0, 0, 0, 0, 0, 0});
        for (int i = 0; i < 8; ++i) m.tick();  // 8 frames -> k = 2^-2 = 0.25
        float got = m.decayed_damage("A");
        const MemoryRecord* r = find_rec(m, "A");
        CHECK(r != nullptr && std::fabs((double)got -
               (double)(256.0 * std::pow(2.0, -(double)r->frames / 4.0))) <= 1e-3,
              "decayed value == seed * 2^(-frames/rate)");
    }

    std::printf("\n=== Rate 0.0f zeroes records on any decay (R5 default) ===\n");
    {
        TacticMemory m;
        m.strikes = 0.0f;  // DAT default when no tactic: 0.0f -> full wipe
        m.record_counter("Throw");
        m.tick();  // frames >= 1, k = 2^(-inf) = 0
        CHECK_NEAR(m.decayed_counter("Throw"), 0.0, "rate 0 zeroes the record");
    }

    std::printf("\n=== Unbounded growth: NO ring, NO eviction (R5 deviation) ===\n");
    {
        TacticMemory m;
        // 25 distinct animations -> past the 20-record initial capacity
        // (FUN_8f4b0dac) with no eviction (FUN_8f4b151c doubling growth).
        for (int i = 0; i < 25; ++i)
            m.record_strike("Anim" + std::to_string(i), 1.0f);
        CHECK(m.records.size() == 25, "25 records survive past capacity 20");
        bool all_present = true;
        for (int i = 0; i < 25; ++i) {
            const MemoryRecord* r = find_rec(m, "Anim" + std::to_string(i));
            if (!r || r->strike_count != 1.0f) { all_present = false; break; }
        }
        CHECK(all_present, "no record evicted; every strike_count intact");

        // Action logs are unbounded too.
        for (int i = 0; i < 25; ++i) m.record_self("Self" + std::to_string(i));
        CHECK(m.self_actions.size() == 25, "self action log unbounded");
        CHECK(m.self_actions[24] == "Self24", "last recorded action still present");
    }

    std::printf("\n=== Lazy write-back (decay at ACCESS, not at tick) ===\n");
    {
        TacticMemory m;
        m.strikes = 10.0f;
        m.record_counter("Throw");
        for (int i = 0; i < 10; ++i) m.tick();
        // tick alone must not touch the records (decay is lazy).
        const MemoryRecord* before = find_rec(m, "Throw");
        CHECK(before && before->counter == 1.0f && before->last_frame == 0,
              "ticks alone do not decay (lazy)");
        // First access decays by 2^-1 and stamps.
        CHECK_NEAR(m.decayed_counter("Throw"), 0.5, "access 1 decays 10 frames");
        const MemoryRecord* after = find_rec(m, "Throw");
        CHECK(after && after->last_frame == 10, "last_frame stamped on access");
        // Immediate second access: zero elapsed frames -> no further decay.
        CHECK_NEAR(m.decayed_counter("Throw"), 0.5, "same-frame re-access is stable");
        // 10 more ticks -> decays again by 2^-1 from the stamped base.
        for (int i = 0; i < 10; ++i) m.tick();
        CHECK_NEAR(m.decayed_counter("Throw"), 0.25, "decay applies per elapsed delta");
    }

    std::printf("\n=== Countdown flooring at 0, never negative ===\n");
    {
        TacticMemory m;
        m.start_response_delay(3.0f, 3.0f, fixed_rng(0));
        m.start_enemy_reaction(4.0f, 4.0f, fixed_rng(0));
        CHECK(m.frames_until_next_decision == 3, "response delay countdown set");
        CHECK(m.enemy_reaction_frames == 4, "enemy reaction countdown set");
        for (int i = 0; i < 7; ++i) m.tick();
        CHECK(m.frames_until_next_decision == 0, "countdown floors at 0");
        CHECK(m.enemy_reaction_frames == 0, "enemy countdown floors at 0");
        CHECK(m.frames_until_next_decision >= 0 && m.enemy_reaction_frames >= 0,
              "never negative");
    }

    std::printf("\n=== Response-delay roll within Min/Max inclusive ===\n");
    {
        TacticMemory m;
        // Endpoints reachable: rng 0 -> min, rng RAND_MAX -> max.
        m.start_response_delay(30.0f, 60.0f, fixed_rng(0));
        CHECK(m.frames_until_next_decision == 30, "rng=0 -> min endpoint");
        m.start_response_delay(30.0f, 60.0f, fixed_rng((unsigned)RAND_MAX));
        CHECK(m.frames_until_next_decision == 60, "rng=RAND_MAX -> max endpoint");
        // Distribution stays inside [min, max] over many rolls.
        auto lcg = make_lcg(7);
        resf2::game::RngSource rng = std::ref(lcg);
        bool in_range = true;
        for (int i = 0; i < 500; ++i) {
            m.start_enemy_reaction(30.0f, 60.0f, rng);
            if (m.enemy_reaction_frames < 30 || m.enemy_reaction_frames > 60) {
                in_range = false; break;
            }
        }
        CHECK(in_range, "500 rolls stay within [30, 60]");
        // Degenerate single-value range and inverted Min/Max.
        m.start_response_delay(5.0f, 5.0f, fixed_rng(12345));
        CHECK(m.frames_until_next_decision == 5, "min == max -> that value");
        m.start_response_delay(60.0f, 30.0f, fixed_rng(0));
        CHECK(m.frames_until_next_decision == 30, "inverted range normalized");
    }

    std::printf("\n=== record_decision clears the decision countdown ===\n");
    {
        TacticMemory m;
        m.start_response_delay(40.0f, 40.0f, fixed_rng(0));
        CHECK(m.frames_until_next_decision == 40, "countdown armed");
        m.record_decision();
        CHECK(m.frames_until_next_decision == 0, "decision made -> countdown cleared");
    }

    std::printf("\n=== Round-end scaling of all five floats (R5) ===\n");
    {
        TacticMemory m;
        m.records.push_back(MemoryRecord{"Throw", 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 7.0f, 9});
        m.record_self("Step");
        m.start_response_delay(3.0f, 3.0f, fixed_rng(0));
        m.round_end(0.5f);  // FUN_8f4a84e8: scale by RoundFactor
        const MemoryRecord* r = find_rec(m, "Throw");
        CHECK(r != nullptr, "record present after round end");
        if (r) {
            CHECK_NEAR(r->damage, 5.0, "damage scaled by RoundFactor");
            CHECK_NEAR(r->counter, 10.0, "counter scaled by RoundFactor");
            CHECK_NEAR(r->hits, 15.0, "hits scaled by RoundFactor");
            CHECK_NEAR(r->strike_damage, 20.0, "strike_damage scaled");
            CHECK_NEAR(r->strike_count, 25.0, "strike_count scaled");
            CHECK(r->frames == 7.0f && r->last_frame == 9,
                  "stamps are NOT scaled (five floats only)");
        }
        CHECK(m.self_actions.size() == 1 && m.frames_until_next_decision == 3,
              "action log and countdowns untouched by round end");
    }

    std::printf("\n=== Reset zeroing (R5 fighter reset) ===\n");
    {
        TacticMemory m;
        m.strikes = 10.0f;
        m.record_strike("Throw", 5.0f);
        m.record_self("Step");
        m.record_enemy("Throw");
        for (int i = 0; i < 7; ++i) m.tick();
        m.start_response_delay(9.0f, 9.0f, fixed_rng(0));
        m.start_enemy_reaction(9.0f, 9.0f, fixed_rng(0));
        m.reset();  // FUN_8f4ac6bc: full zero
        CHECK(m.records.empty(), "records cleared");
        CHECK(m.self_actions.empty() && m.enemy_actions.empty(), "action logs cleared");
        CHECK(m.frames_since_self == 0 && m.frames_since_enemy == 0,
              "interval counters zeroed");
        CHECK(m.frames_until_next_decision == 0 && m.enemy_reaction_frames == 0,
              "countdowns zeroed");
        // The decay clock is zeroed too: 10 ticks after reset decay by 2^-1,
        // not by 2^-((7+10)/10).
        m.record_counter("Throw");
        for (int i = 0; i < 10; ++i) m.tick();
        CHECK_NEAR(m.decayed_counter("Throw"), 0.5, "decay clock restarts from reset");
    }

    std::printf("\n=== Interval feed (Intervals/EnemyIntervals) ===\n");
    {
        TacticMemory m;
        for (int i = 0; i < 5; ++i) m.tick();
        CHECK_NEAR(m.self_interval(), 5.0, "self_interval counts frames");
        CHECK_NEAR(m.enemy_interval(), 5.0, "enemy_interval counts frames");
        m.record_self("Throw");
        CHECK_NEAR(m.self_interval(), 0.0, "self action resets self_interval");
        CHECK_NEAR(m.enemy_interval(), 5.0, "enemy_interval unaffected by self action");
        m.record_enemy("ShortAttack");
        CHECK_NEAR(m.enemy_interval(), 0.0, "enemy action resets enemy_interval");
        CHECK(m.self_actions.size() == 1 && m.self_actions[0] == "Throw",
              "self action recorded");
        CHECK(m.enemy_actions.size() == 1 && m.enemy_actions[0] == "ShortAttack",
              "enemy action recorded");
        m.tick();
        CHECK_NEAR(m.self_interval(), 1.0, "interval advances again after action");
    }

    std::printf("\n=== Absent animation reads 0 without creating (neutral-by-zero) ===\n");
    {
        TacticMemory m;
        m.record_counter("Throw");
        CHECK_NEAR(m.decayed_damage("Nonexistent"), 0.0, "absent D reads 0");
        CHECK_NEAR(m.decayed_counter("Nonexistent"), 0.0, "absent C reads 0");
        CHECK_NEAR(m.decayed_hits("Nonexistent"), 0.0, "absent H reads 0");
        CHECK(m.records.size() == 1, "read of an absent animation creates no record");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
