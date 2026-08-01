// engine/game/tactic_decision_adapter.hpp
//
// TacticDecisionAdapter — ADR-005 D7, Phase A of the FSM strangler: maps a
// completed TacticDecision onto the legacy enemy_ai_state_ int (game.cpp:
// 1730: 0=idle, 1=approach, 2=attack, 3=retreat, 4=block), so the existing
// movement/attack execution code in game.cpp runs unchanged until Phase B
// removes the FSM.
//
// Mapping rows (ADR-005 D7 table, first match wins):
//   stage fired, animation is an attack        -> 2 (attack) + animation name
//   stage fired, animation is a step/movement  -> 1/3 (approach/retreat by sign)
//   UseDefense fired                           -> 4 (block)
//   wait_frames > 0 or no stage fired          -> 0 (idle)
//
// Attack-vs-movement classification: candidate-name lookup against the
// stage's table family (attack_table candidates = attacks; movements family
// = steps), with a [HEURISTIC-TODO] name-list fallback
// (ForwardStep/BackStep/ShortAttack/Duck) carrying the category names
// tacticSettings.xml actually ships until the family data lands (the
// movements family is absent from this dump — test_tactic_tables).
//
// Header-only; D3 wires it into the live enemy-AI block.

#pragma once

#include <string>
#include <string_view>

#include "tactic_pipeline.hpp"
#include "tactic_tables.hpp"

namespace resf2::game {

class TacticDecisionAdapter {
public:
    // [ORIGINAL] legacy enemy-AI states (game.cpp:1730).
    static constexpr int kStateIdle = 0;
    static constexpr int kStateApproach = 1;
    static constexpr int kStateAttack = 2;
    static constexpr int kStateRetreat = 3;
    static constexpr int kStateBlock = 4;

    // ADR-005 D7 rows -> legacy state int. `tables` supplies the
    // table-candidate classification (empty set = name-list fallback only).
    [[nodiscard]] static int to_legacy_state(const TacticDecision& d,
                                             const TacticTableSet& tables);

    // The animation to execute for `d`; "" -> the caller falls back to the
    // idle animation (fists_idle in game.cpp).
    [[nodiscard]] static std::string animation_for(const TacticDecision& d);

    // ---- attack-vs-movement classification ----
    // Candidate-name lookup against the stage's table family first, then the
    // [HEURISTIC-TODO] name-list fallback until family data lands.
    [[nodiscard]] static bool is_attack(std::string_view animation,
                                        const TacticTableSet& tables);
    [[nodiscard]] static bool is_step(std::string_view animation,
                                      const TacticTableSet& tables);
    // "Duck" (legacy roulette's block placeholder, game.cpp:1790) and "Block"
    // (the UseDefense stage's real moves.xml name).
    [[nodiscard]] static bool is_block(std::string_view animation,
                                       const TacticTableSet& tables);
    // Sign of a step: true = retreat/backwards (BackStep, Retreat).
    [[nodiscard]] static bool step_is_retreat(std::string_view animation);

private:
    // Is `animation` a candidate of any loaded table of `type`?
    [[nodiscard]] static bool in_candidates(std::string_view animation,
                                            const TacticTableSet& tables,
                                            TacticTableType type);
};

inline int TacticDecisionAdapter::to_legacy_state(const TacticDecision& d,
                                                  const TacticTableSet& tables) {
    // Rows 1-3 apply only to a fired stage; row 4 is the catch-all.
    if (d.stage != DecisionStage::kIdle) {
        if (is_attack(d.animation, tables)) return kStateAttack;
        if (is_step(d.animation, tables)) {
            return step_is_retreat(d.animation) ? kStateRetreat
                                                : kStateApproach;
        }
        // UseDefense's sub-actions (CounterAttack/Dodge/Block) are defense
        // labels, never attack-table candidates, so the stage check is exact.
        if (d.stage == DecisionStage::kUseDefense) return kStateBlock;
        if (is_block(d.animation, tables)) return kStateBlock;
    }
    // Row 4: wait_frames > 0 or no stage fired -> idle. (An attack decision
    // with a positive wait_frames still maps to 2 by row order — the
    // ExpectedWait attack path in decide() carries a positive wait.)
    return kStateIdle;
}

inline std::string TacticDecisionAdapter::animation_for(
    const TacticDecision& d) {
    // "" -> the caller falls back to the idle animation.
    return d.animation;
}

inline bool TacticDecisionAdapter::is_attack(std::string_view animation,
                                             const TacticTableSet& tables) {
    // Table path: any attack table's candidates are attacks (ADR-005 D7:
    // "attack_table candidates = attacks").
    if (in_candidates(animation, tables, TacticTableType::kAttackTable)) {
        return true;
    }
    // [HEURISTIC-TODO] name-list fallback until family data lands — the
    // category names tacticSettings.xml ships (assets/tacticSettings.xml:
    // "ShortAttack", ...).
    return animation == "ShortAttack";
}

inline bool TacticDecisionAdapter::is_step(std::string_view animation,
                                          const TacticTableSet& tables) {
    // Table path: the movements family's candidates are steps (ADR-005 D7:
    // "movements family = steps").
    if (in_candidates(animation, tables, TacticTableType::kMovementsTable)) {
        return true;
    }
    // [HEURISTIC-TODO] name-list fallback — the movements family is absent
    // from this dump; "Retreat" is the legacy roulette's retreat alias
    // (game.cpp:1788-1789: BackStep/Retreat -> 3).
    return animation == "ForwardStep" || animation == "BackStep" ||
           animation == "Retreat";
}

inline bool TacticDecisionAdapter::is_block(std::string_view animation,
                                            const TacticTableSet& tables) {
    // [HEURISTIC-TODO] name-list fallback only: block is a defense action
    // (no table family); "Duck" is the legacy roulette's block placeholder
    // (game.cpp:1790: Duck -> 4), "Block" the real moves.xml name the
    // UseDefense stage produces (kDefenseAnimations).
    (void)tables;
    return animation == "Duck" || animation == "Block";
}

inline bool TacticDecisionAdapter::step_is_retreat(
    std::string_view animation) {
    // [HEURISTIC-TODO] the sign of a step comes from its name until the
    // movements-family records land; ForwardStep -> approach, BackStep /
    // Retreat -> retreat (game.cpp:1786-1789).
    return animation == "BackStep" || animation == "Retreat";
}

inline bool TacticDecisionAdapter::in_candidates(std::string_view animation,
                                                 const TacticTableSet& tables,
                                                 TacticTableType type) {
    if (animation.empty()) return false;
    for (const TacticTable& t : tables.tables()) {
        if (t.type != type) continue;
        for (const std::string& c : t.candidates) {
            if (c == animation) return true;
        }
    }
    return false;
}

}  // namespace resf2::game
