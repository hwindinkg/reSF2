// tests/test_tactic_tables.cpp
//
// Pins TacticTableSet (ADR-005 D3) against the real .atf dump in
// assets/tactics/: the .atf family loads, absent/unreversed families
// report not-loaded WITHOUT error, and attack tables resolve by their
// parsed Header weapon names (NOT by filename).
//
// [ORIGINAL] families/types: PORT_GAPS.md:159-167.

#include "../engine/game/tactic_tables.hpp"

#include <cstdio>

using resf2::game::TacticFamily;
using resf2::game::TacticTable;
using resf2::game::TacticTableSet;
using resf2::game::TacticTableType;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

int main() {
    std::printf("\n=== TacticTableSet::load on the real dump ===\n");
    TacticTableSet set;
    CHECK(set.load("assets"), "load(\"assets\") completes without error");

    std::printf("\n=== family presence ===\n");
    CHECK(set.has_family(TacticFamily::kAtf),       ".atf family loaded");
    CHECK(!set.has_family(TacticFamily::kTbs),      "tbs family absent (attack/ missing, stub)");
    CHECK(!set.has_family(TacticFamily::kStb),      "stb family absent (shift/ missing, stub)");
    CHECK(!set.has_family(TacticFamily::kSts),      "sts family absent (shiftTables/ missing, stub)");
    CHECK(!set.has_family(TacticFamily::kDodge),    "dodge/ family absent from this dump");
    CHECK(!set.has_family(TacticFamily::kMovements),"movements/ family absent from this dump");
    CHECK(!set.has_family(TacticFamily::kOutcome),  "outcometablesforattack/ family absent");

    std::printf("\n=== attack_table resolves by parsed Header names ===\n");
    // assets/tactics/axes_fists.atf parses to Header{A='Axes', B='Fists', v=1}
    // -> index key "Axes_Fists" (weapon_a + "_" + weapon_b), NOT the filename.
    const TacticTable* pair = set.attack_table("Axes", "Fists");
    CHECK(pair != nullptr, "attack_table(Axes, Fists) resolves");
    if (pair) {
        CHECK(pair->type == TacticTableType::kAttackTable, "pair table is kAttackTable");
        CHECK(pair->name == "Axes_Fists", "pair table name is the header key Axes_Fists");
        CHECK(!pair->candidates.empty(), "pair table has candidate animations");
        CHECK(pair->record.size() == 858, "pair table keeps the stride-858 record");
    }

    // assets/tactics/fists.atf parses to Header{A='Fists', v=2}
    // -> index key "Fists" (weapon_a alone).
    const TacticTable* single = set.attack_table("Fists", "");
    CHECK(single != nullptr, "attack_table(Fists, \"\") resolves the v=2 single");
    if (single) {
        CHECK(single->name == "Fists", "v=2 table name is weapon_a alone");
    }

    std::printf("\n=== find / stubs / probe ===\n");
    const TacticTable* rt = set.find(TacticTableType::kAttackTable, "Axes_Fists");
    CHECK(rt != nullptr && rt == pair, "find(kAttackTable, name) round-trips");

    CHECK(set.find(TacticTableType::kShiftTable, "Axes_Fists") == nullptr,
          "stub family find -> nullptr");
    CHECK(set.find(TacticTableType::kDodgeTable, "anything") == nullptr,
          "absent directory family find -> nullptr");

    CHECK(set.animation_factor("anything", "anyone") == 0.0f,
          "animation_factor is 0.0f neutral until R2");

    std::printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
