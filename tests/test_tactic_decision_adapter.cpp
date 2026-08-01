// tests/test_tactic_decision_adapter.cpp
//
// Pins TacticDecisionAdapter (engine/game/tactic_decision_adapter,
// ADR-005 D7) — the strangler adapter mapping a TacticDecision onto the
// legacy enemy_ai_state_ int (game.cpp:1730: 0=idle, 1=approach, 2=attack,
// 3=retreat, 4=block), so the existing movement/attack execution code runs
// unchanged.
//
// Mapping rows pinned (ADR-005 D7 table, first match wins):
//   stage fired, animation is an attack        -> 2 (attack) + animation name
//   stage fired, animation is a step/movement  -> 1/3 (approach/retreat by sign)
//   UseDefense fired                           -> 4 (block)
//   wait_frames > 0 or no stage fired          -> 0 (idle)
//
// Attack-vs-movement classification: candidate-name lookup against the
// stage's table (attack_table candidates = attacks; movements family =
// steps), exercised against the REAL .atf dump, with the [HEURISTIC-TODO]
// name-list fallback (ForwardStep/BackStep/ShortAttack/Duck) carrying the
// category names tacticSettings.xml actually ships until family data lands.

#include "../engine/game/tactic_decision_adapter.hpp"

#include <cstdio>
#include <string>

using resf2::game::DecisionStage;
using resf2::game::TacticDecision;
using resf2::game::TacticDecisionAdapter;
using resf2::game::TacticTable;
using resf2::game::TacticTableSet;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// The [HEURISTIC-TODO] name-list fallback entries. A candidate chosen from
// the real dump must NOT be classifiable via these names, so the
// table-candidate path is what the test then exercises.
static const char* const kFallbackNames[] = {
    "ShortAttack", "ForwardStep", "BackStep", "Retreat", "Duck", "Block",
};

// First candidate of `t` that no fallback name classifies ("" when none).
static std::string first_table_candidate(const TacticTable* t) {
    for (const std::string& c : t->candidates) {
        bool fallback = false;
        for (const char* f : kFallbackNames) {
            if (c == f) {
                fallback = true;
                break;
            }
        }
        if (!fallback && !c.empty()) return c;
    }
    return {};
}

int main() {
    TacticTableSet empty;  // no family data -> name-list fallback path

    std::printf("\n=== mapping rows (ADR-005 D7) ===\n");
    // Row 1: attack animation -> 2, with the animation name preserved.
    {
        const TacticDecision attack{DecisionStage::kTableAttack, "ShortAttack"};
        CHECK(TacticDecisionAdapter::to_legacy_state(attack, empty) ==
                  TacticDecisionAdapter::kStateAttack,
              "attack animation -> state 2");
        CHECK(TacticDecisionAdapter::animation_for(attack) == "ShortAttack",
              "animation_for(attack) returns the attack animation name");
    }

    // Row 2: step/movement -> 1/3 by sign (approach vs retreat).
    {
        const TacticDecision fwd{DecisionStage::kUseCautiousMovements,
                                 "ForwardStep"};
        CHECK(TacticDecisionAdapter::to_legacy_state(fwd, empty) ==
                  TacticDecisionAdapter::kStateApproach,
              "ForwardStep -> state 1 (approach)");

        const TacticDecision back{DecisionStage::kUseCautiousMovements,
                                  "BackStep"};
        CHECK(TacticDecisionAdapter::to_legacy_state(back, empty) ==
                  TacticDecisionAdapter::kStateRetreat,
              "BackStep -> state 3 (retreat)");

        const TacticDecision retreat{DecisionStage::kUseCautiousMovements,
                                     "Retreat"};
        CHECK(TacticDecisionAdapter::to_legacy_state(retreat, empty) ==
                  TacticDecisionAdapter::kStateRetreat,
              "Retreat -> state 3 (retreat)");
    }

    // Row 3: UseDefense fired -> 4 (block), whatever sub-action fired.
    {
        CHECK(TacticDecisionAdapter::to_legacy_state(
                  {DecisionStage::kUseDefense, "Block"}, empty) ==
                  TacticDecisionAdapter::kStateBlock,
              "UseDefense fired (Block) -> state 4");
        CHECK(TacticDecisionAdapter::to_legacy_state(
                  {DecisionStage::kUseDefense, "CounterAttack"}, empty) ==
                  TacticDecisionAdapter::kStateBlock,
              "UseDefense fired (CounterAttack) -> state 4");
        CHECK(TacticDecisionAdapter::to_legacy_state(
                  {DecisionStage::kUseDefense, "Dodge"}, empty) ==
                  TacticDecisionAdapter::kStateBlock,
              "UseDefense fired (Dodge) -> state 4");
    }

    // Legacy placeholder semantics: Duck is the roulette's block name
    // (game.cpp:1790: Duck -> 4).
    {
        const TacticDecision duck{DecisionStage::kUseCautiousMovements, "Duck"};
        CHECK(TacticDecisionAdapter::to_legacy_state(duck, empty) ==
                  TacticDecisionAdapter::kStateBlock,
              "Duck (legacy block placeholder) -> state 4");
    }

    // Row 4: wait_frames > 0 or no stage fired -> 0 (idle).
    {
        const TacticDecision wait{DecisionStage::kQuickAttack, "Dodge", 5};
        CHECK(TacticDecisionAdapter::to_legacy_state(wait, empty) ==
                  TacticDecisionAdapter::kStateIdle,
              "wait_frames > 0 (unclassified animation) -> state 0");

        const TacticDecision none;  // kIdle stage, nothing fired
        CHECK(TacticDecisionAdapter::to_legacy_state(none, empty) ==
                  TacticDecisionAdapter::kStateIdle,
              "no stage fired -> state 0");
        CHECK(TacticDecisionAdapter::animation_for(none).empty(),
              "animation_for(idle decision) is \"\" (caller idle fallback)");

        CHECK(TacticDecisionAdapter::to_legacy_state(
                  {DecisionStage::kIdle, "", 7}, empty) ==
                  TacticDecisionAdapter::kStateIdle,
              "kIdle stage with wait_frames > 0 -> state 0");
    }

    // Row precedence: the ADR table reads top-down — an attack animation wins
    // over the wait row (D3 depends on this: the ExpectedWait attack path
    // carries a positive wait_frames and must still map to state 2).
    {
        const TacticDecision attack{DecisionStage::kTableAttack, "ShortAttack",
                                    4};
        CHECK(TacticDecisionAdapter::to_legacy_state(attack, empty) ==
                  TacticDecisionAdapter::kStateAttack,
              "attack animation + wait_frames > 0 -> state 2 (row order)");
    }

    std::printf("\n=== classification (name-list fallback) ===\n");
    CHECK(TacticDecisionAdapter::is_attack("ShortAttack", empty),
          "is_attack(ShortAttack)");
    CHECK(!TacticDecisionAdapter::is_attack("ForwardStep", empty),
          "!is_attack(ForwardStep)");
    CHECK(!TacticDecisionAdapter::is_attack("Duck", empty),
          "!is_attack(Duck)");
    CHECK(!TacticDecisionAdapter::is_attack("", empty),
          "!is_attack(\"\")");

    CHECK(TacticDecisionAdapter::is_step("ForwardStep", empty),
          "is_step(ForwardStep)");
    CHECK(TacticDecisionAdapter::is_step("BackStep", empty),
          "is_step(BackStep)");
    CHECK(TacticDecisionAdapter::is_step("Retreat", empty),
          "is_step(Retreat)");
    CHECK(!TacticDecisionAdapter::is_step("ShortAttack", empty),
          "!is_step(ShortAttack)");

    CHECK(TacticDecisionAdapter::is_block("Duck", empty),
          "is_block(Duck)");
    CHECK(TacticDecisionAdapter::is_block("Block", empty),
          "is_block(Block)");
    CHECK(!TacticDecisionAdapter::is_block("ShortAttack", empty),
          "!is_block(ShortAttack)");

    CHECK(TacticDecisionAdapter::step_is_retreat("BackStep"),
          "step_is_retreat(BackStep)");
    CHECK(TacticDecisionAdapter::step_is_retreat("Retreat"),
          "step_is_retreat(Retreat)");
    CHECK(!TacticDecisionAdapter::step_is_retreat("ForwardStep"),
          "!step_is_retreat(ForwardStep)");

    std::printf("\n=== classification (attack_table candidates, real dump) ===\n");
    TacticTableSet set;
    CHECK(set.load("assets"), "TacticTableSet::load(\"assets\")");
    const TacticTable* fists = set.attack_table("Fists", "");
    CHECK(fists != nullptr, "attack_table(Fists, \"\") resolves the v=2 single");
    if (fists) {
        const std::string real = first_table_candidate(fists);
        CHECK(!real.empty(),
              "the Fists attack table has a candidate no fallback name matches");
        if (!real.empty()) {
            std::printf("    (real candidate: %s)\n", real.c_str());
            CHECK(TacticDecisionAdapter::is_attack(real, set),
                  "real attack-table candidate classifies as attack");
            CHECK(TacticDecisionAdapter::to_legacy_state(
                      {DecisionStage::kTableAttack, real}, set) ==
                      TacticDecisionAdapter::kStateAttack,
                  "real attack-table candidate -> state 2");
            CHECK(TacticDecisionAdapter::animation_for(
                      {DecisionStage::kTableAttack, real}) == real,
                  "animation_for(real candidate) returns its name");
        }
    }

    std::printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
