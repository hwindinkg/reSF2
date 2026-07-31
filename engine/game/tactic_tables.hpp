// engine/game/tactic_tables.hpp
//
// TacticTableSet — the typed table registry for the AI tactic model
// (ADR-005 D3). One parser per family behind a family descriptor table;
// a missing directory or an unimplemented family parser is NOT an error —
// the set simply stays partial (0 of 6 table dirs exist in this dump).
//
// [ORIGINAL] the 7 families and 10 table types come from the binary's
// path/type strings (PORT_GAPS.md:159-167).

#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace resf2::game {

// [ORIGINAL] the 10 table types from the binary's type strings
// (PORT_GAPS.md:166-167). "Strange tactic type: %s" rejects others.
enum class TacticTableType {
    kRandomAnimation,     // RandomAnimation
    kNoneTable,           // NoneTable
    kAttackTable,         // AttackTable
    kMovementsTable,      // MovementsTable
    kDodgeTable,          // DodgeTable
    kAttackTableOld,      // AttackTableOld
    kSummaryResultTable,  // SummaryResultTable
    kQuickAttack,         // QuickAttack
    kShiftTable,          // ShiftTable
    kThrowTactics,        // ThrowTactics
};

// [ORIGINAL] the 7 table families, all loaded from assets/tactics/*
// (PORT_GAPS.md:159-164).
enum class TacticFamily {
    kTbs = 0,   // attack/*.tbs
    kStb,       // shift/*.stb
    kSts,       // shiftTables/*.sts
    kAtf,       // *.atf
    kDodge,     // dodge/
    kMovements, // movements/
    kOutcome,   // outcometablesforattack/
};

inline constexpr std::size_t kTacticFamilyCount = 7;

struct TacticTable {
    TacticTableType type;
    std::string name;
    // Ordered candidate animations + per-candidate data rows.
    // Payload interpretation is family-specific (stride-858 record for .atf);
    // kept as bytes+names until the family's semantics are reversed.
    std::vector<std::string> candidates;
    std::vector<std::uint8_t> record;   // raw row data (may be empty)
};

class TacticTableSet {
public:
    // Loads every family that exists under <root>/tactics/ (falling back to
    // <root>/assets/tactics/ for the relocated dump). A missing dir or an
    // unimplemented family parser is NOT an error — the set stays partial.
    bool load(const std::string& asset_root);

    // .atf lookup: pair -> "Axes_Fists"; v=2 single -> "Fists".
    [[nodiscard]] const TacticTable* attack_table(
        std::string_view weapon_a, std::string_view weapon_b) const;
    [[nodiscard]] const TacticTable* find(TacticTableType type,
                                          std::string_view name) const;
    [[nodiscard]] bool has_family(TacticFamily f) const;

    // Per-target AnimationFactors probe (a.a6.S5a, ADR-005 D5).
    // A miss yields 0.0f and is neutral, never an error.
    [[nodiscard]] float animation_factor(std::string_view animation,
                                         std::string_view target) const;

    [[nodiscard]] std::size_t table_count() const { return tables_.size(); }

private:
    std::vector<TacticTable> tables_;   // family-tagged, name-indexed
    std::bitset<kTacticFamilyCount> families_loaded_;
};

}  // namespace resf2::game
