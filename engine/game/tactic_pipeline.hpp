// engine/game/tactic_pipeline.hpp
//
// TacticDecisionPipeline — the seven-stage AI decision pipeline (ADR-005 D1).
//
// [ORIGINAL] decision order fixed by the debug tracer's format strings
// (reverse/analysis/PORT_GAPS.md:171-178, MEMORY_INDEXING_R56.md §3.1,
// GOLDEN_TESTS.md §2):
//
//   UseDefense -> UseSafeAttack -> TableAttack -> DodgeMissiles ->
//   QuickAttack[i] -> Evade[i] -> UseCautiousMovements
//   then DistanceError / FrameError / Intervals / EnemyIntervals /
//   DecisionType / Decision {Wait=%d}
//
// The tracer prints ONE line-group per decision: every stage is evaluated
// and printed (its score line, looped stages one line per entry), the first
// stage that FIRES wins the decision; when none fires the decision is an
// idle wait. The epilogue lines then follow in tracer order.
//
// Stage internals are JS-port defaults pending the P3 golden test; every
// unpinned choice carries a [HEURISTIC-TODO] with its R-number.

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "tactic_memory.hpp"
#include "tactic_settings.hpp"
#include "tactic_tables.hpp"

namespace resf2::game {

// [ORIGINAL] the seven decision stages in the tracer's fixed order
// (string island 0x8F798090..0x8F79834C; per-stage address in the comments,
// MEMORY_INDEXING_R56.md §3.1).
enum class DecisionStage {
    kIdle,                  // no stage fired — sentinel only, never traced
    kUseDefense,            // [ORIGINAL] "UseDefense: %s / %.4f / %.4f / %.4f" @ 0x8F7980B5
    kUseSafeAttack,         // [ORIGINAL] "UseSafeAttack: %s / %.4f" @ 0x8F7980FE
    kTableAttack,           // [ORIGINAL] "TableAttack: %s / %.4f" @ 0x8F79812E
    kDodgeMissiles,         // [ORIGINAL] "DodgeMissiles: %s / %.4f" @ 0x8F798144
    kQuickAttack,           // [ORIGINAL] "QuickAttack[%d]: %s / %.4f" @ 0x8F798161
    kEvade,                 // [ORIGINAL] "Evade[%d]: %s / %.4f" @ 0x8F798184
    kUseCautiousMovements,  // [ORIGINAL] "UseCautiousMovements: %s / %.4f" @ 0x8F798199
};

// The tracer's prefix for a stage ("UseDefense", "QuickAttack", ...).
[[nodiscard]] const char* stage_label(DecisionStage s);

// One completed decision.
struct TacticDecision {
    DecisionStage stage = DecisionStage::kIdle;
    std::string animation;      // chosen animation; empty = idle/wait
    int wait_frames = 0;        // the {Wait=%d} value (ExpectedWait epilogue);
                               // SIGNED duration-arithmetic result (R4 GREEN),
                               // printed via abs() by the DecisionTrace
    std::string type;           // "Tabular" / "ExpectedWait" (normalized)
    float distance_error = 0;   // DistanceError jitter
    int frame_error = 0;        // FrameError jitter
};

// DecisionTrace — records one line-group per stage + the epilogue lines in
// the tracer's print order, so tests can assert the order contract now and
// the P3 golden can diff against captured original output later
// (GOLDEN_TESTS.md §2).
class DecisionTrace {
public:
    void begin();  // clears

    // One stage line-group: "UseDefense: <name> / s0 / s1 / s2", or
    // "QuickAttack[<index>]: <name> / s0" for the looped stages (index is
    // the 1-based tracer index; pass 0 for the non-looped stages).
    void stage(DecisionStage s, const std::string& name,
               const std::vector<float>& scores, int index = 0);

    void distance_error(float v);          // "DistanceError: %.3f"
    void frame_error(int v);               // "FrameError: %d"
    void intervals(float self, float enemy);  // "Intervals: .."/"EnemyIntervals: .."
    void decision_type(const std::string& t);  // "DecisionType: %s"
    void decision(int wait_frames);        // "Decision {Wait=%d}"

    [[nodiscard]] const std::vector<std::string>& lines() const { return lines_; }

private:
    std::vector<std::string> lines_;
};

// ---- R3 — the ONE place a chance curve is compared to a roll. ----
// [ORIGINAL] R3 GREEN (reverse/analysis/VERIFY_R34.md): fire iff score >
// threshold — strict GT at every threshold stage (ASM vcmpe.f32 + movgt, 7
// comparator sites in FUN_8f45ab38 @ 0x8F45ACB4, FUN_8f45456c, the Evade
// loop). Threshold = the rng roll, rolled at the stage's evaluation point
// (per-slot cadence per R6). UseDefense is the one divergence (4-way
// cumulative draw — see stage_use_defense).
[[nodiscard]] bool chance_fires(const TacticWeight& curve,
                                const TacticContext& ctx,
                                RngSource rng);

// ---- R6 — QuickAttack/Evade index mapping, ONE function per family. ----
// [ORIGINAL] R6 GREEN (reverse/analysis/VERIFY_R56.md §4): entry -> animation
// by XML document order — the i-th <QuickAttackChance>/<EvadeChance> entry
// (FUN_8f45456c @ 0x8F45456C scores entry[i] at table+0x1f8/+0x204, stride
// 0x6c), which the tracer numbers 1-based ("QuickAttack[%d]" @ 0x8F798161 /
// "Evade[%d]" @ 0x8F798184).
[[nodiscard]] const std::string& quick_attack_animation(const TacticDef& def,
                                                        std::size_t index);
[[nodiscard]] const std::string& evade_animation(const TacticDef& def,
                                                 std::size_t index);

// The seven stages. Each takes the decision context plus the trace sink
// (the per-stage line-group) and returns the decision it produces, or
// nullopt when it does not fire. The first fired stage in tracer order wins.
std::optional<TacticDecision> stage_use_defense(const TacticDef& def, const TacticContext& ctx,
                                                TacticMemory& mem, const TacticTableSet& tables,
                                                RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_use_safe_attack(const TacticDef& def, const TacticContext& ctx,
                                                    TacticMemory& mem, const TacticTableSet& tables,
                                                    RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_table_attack(const TacticDef& def, const TacticContext& ctx,
                                                 TacticMemory& mem, const TacticTableSet& tables,
                                                 RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_dodge_missiles(const TacticDef& def, const TacticContext& ctx,
                                                   TacticMemory& mem, const TacticTableSet& tables,
                                                   RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_quick_attack(const TacticDef& def, const TacticContext& ctx,
                                                 TacticMemory& mem, const TacticTableSet& tables,
                                                 RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_evade(const TacticDef& def, const TacticContext& ctx,
                                          TacticMemory& mem, const TacticTableSet& tables,
                                          RngSource rng, DecisionTrace& trace);
std::optional<TacticDecision> stage_use_cautious_movements(const TacticDef& def,
                                                           const TacticContext& ctx,
                                                           TacticMemory& mem,
                                                           const TacticTableSet& tables,
                                                           RngSource rng,
                                                           DecisionTrace& trace);

// decide() — the full pipeline. Runs the seven stages in tracer order (all
// seven trace their line-groups; the first hit wins), then the epilogue
// (jitter ranges, intervals, decision type, ExpectedWait gate + wait
// mapping) in tracer order. Returns the winning decision, or the idle
// decision when nothing fires.
[[nodiscard]] TacticDecision decide(const TacticDef& def, const TacticContext& ctx,
                                    TacticMemory& mem, const TacticTableSet& tables,
                                    RngSource rng, DecisionTrace& trace);

}  // namespace resf2::game
