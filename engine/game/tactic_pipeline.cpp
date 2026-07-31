// engine/game/tactic_pipeline.cpp
//
// TacticDecisionPipeline implementation (ADR-005 D1).
// See tactic_pipeline.hpp for the decision-order contract;
// MEMORY_INDEXING_R56.md §3.1 for the per-stage tracer strings,
// PORT_GAPS.md:171-178 for the stage order.

#include "tactic_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace resf2::game {

namespace {

// uniform in [min, max] INCLUSIVE — same contract as
// TacticMemory::start_response_delay (rng()==0 -> min, rng()==RAND_MAX -> max).
float draw_uniform(float min, float max, RngSource rng) {
    if (max < min) std::swap(min, max);
    const float t = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
    return min + t * (max - min);
}

// D5 probe wiring (plan L334-336): the weight-side <AnimationFactors> memory
// sums (D/C/H) are fed once per decision from TacticMemory, so the R2-GREEN
// inline probe terms in TacticWeight::score() are live
// (VERIFY_FUN_8f44ac78.md §3). The feed is candidate-independent; the
// per-candidate part (ctx.animation_factor = tables.animation_factor(candidate,
// ctx.current_animation)) is wired at each candidate site below and reads
// 0.0f until the v=7 record-id sets land (TacticTableSet::animation_factor).
void feed_anim_memory(TacticContext& ctx, TacticMemory& mem) {
    ctx.anim_memory.damage.clear();
    ctx.anim_memory.counter.clear();
    ctx.anim_memory.hits.clear();
    for (const MemoryRecord& r : mem.records) {
        // decayed_* applies the lazy decay (VERIFY_FUN_8f44ac78.md §3)
        ctx.anim_memory.damage[r.name] = mem.decayed_damage(r.name);
        ctx.anim_memory.counter[r.name] = mem.decayed_counter(r.name);
        ctx.anim_memory.hits[r.name] = mem.decayed_hits(r.name);
    }
}

// [ORIGINAL] a.a6.S5a — the per-target table probe, set before a candidate
// is scored. 0.0f stub: a miss is neutral, never an error (ADR-005 D5/R3).
void wire_candidate(TacticContext& ctx, const TacticTableSet& tables,
                    const std::string& candidate) {
    ctx.animation_factor = tables.animation_factor(candidate, ctx.current_animation);
}

// jL roulette over an explicit candidate list (TacticSettings::choose),
// with the D5 probe wired for the candidate set.
std::optional<std::string> pick(const TacticDef& def,
                                const std::vector<std::string>& candidates,
                                const TacticContext& ctx, TacticMemory& mem,
                                const TacticTableSet& tables, RngSource rng) {
    if (candidates.empty()) return std::nullopt;
    TacticSettings settings;  // choose() is stateless on the instance
    TacticContext wired = ctx;
    wire_candidate(wired, tables, candidates.front());  // stub -> one wire is exact
    const int idx = settings.choose(def, candidates, wired, rng);
    if (idx < 0) return std::nullopt;  // every weight zero — jL returns -1
    return candidates[static_cast<std::size_t>(idx)];
}

// The tactic's own animation_weights as a candidate list (named entries only;
// the unnamed catch-all is a weight source, not a candidate).
std::vector<std::string> weight_candidates(const TacticDef& def) {
    std::vector<std::string> out;
    for (const auto& [name, w] : def.animation_weights) {
        (void)w;
        if (!name.empty()) out.push_back(name);
    }
    return out;
}

// Stage-3 target pick: the .atf attack table keyed by the current animation
// when present, else the tactic's animation_weights.
std::optional<std::string> pick_attack_target(const TacticDef& def,
                                              const TacticContext& ctx,
                                              TacticMemory& mem,
                                              const TacticTableSet& tables,
                                              RngSource rng) {
    // [HEURISTIC-TODO] .atf key source: default = the single-weapon key from
    // ctx.current_animation (v=2 shape); the pair-key source (weapon_a +
    // weapon_b) is unpinned — TacticContext carries no weapon pair yet.
    std::vector<std::string> candidates;
    if (const TacticTable* t = tables.attack_table(ctx.current_animation, "")) {
        candidates = t->candidates;
    }
    if (candidates.empty()) candidates = weight_candidates(def);
    return pick(def, candidates, ctx, mem, tables, rng);
}

// [HEURISTIC-TODO] UseDefense sub-action -> animation: default = action-name
// placeholders; "Block" is a real moves.xml animation name, "CounterAttack"
// and "Dodge" are unpinned labels until the P3 golden pins them.
const char* kDefenseAnimations[3] = {"CounterAttack", "Dodge", "Block"};

// [HEURISTIC-TODO R4] ExpectedWait pick: the same jL roulette as
// TacticSettings::choose, but over the <ExpectedWait> list — weight_for()
// reads animation_weights, not expected_wait, so choose() cannot be reused.
// Returns the entry index or -1 when every weight is zero.
int pick_expected_wait(const TacticDef& def, const TacticContext& ctx,
                       RngSource rng) {
    float sum = 0;
    std::vector<float> weights;
    weights.reserve(def.expected_wait.size());
    for (const auto& [name, w] : def.expected_wait) {
        (void)name;
        float v = w.evaluate(ctx);
        if (v < 0) v = 0;  // negative weights can't win the draw
        weights.push_back(v);
        sum += v;
    }
    if (sum <= 0) return -1;
    float g = static_cast<float>(rng()) / static_cast<float>(RAND_MAX) * sum;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        g -= weights[i];
        if (g < 0) return static_cast<int>(i);
    }
    return static_cast<int>(weights.size()) - 1;  // guard against fp rounding
}

}  // namespace

const char* stage_label(DecisionStage s) {
    switch (s) {
        case DecisionStage::kUseDefense: return "UseDefense";
        case DecisionStage::kUseSafeAttack: return "UseSafeAttack";
        case DecisionStage::kTableAttack: return "TableAttack";
        case DecisionStage::kDodgeMissiles: return "DodgeMissiles";
        case DecisionStage::kQuickAttack: return "QuickAttack";
        case DecisionStage::kEvade: return "Evade";
        case DecisionStage::kUseCautiousMovements: return "UseCautiousMovements";
        case DecisionStage::kIdle: return "Idle";
    }
    return "?";
}

void DecisionTrace::begin() { lines_.clear(); }

void DecisionTrace::stage(DecisionStage s, const std::string& name,
                          const std::vector<float>& scores, int index) {
    // [ORIGINAL] tracer format strings (MEMORY_INDEXING_R56.md §3.1):
    // "UseDefense: %s / %.4f / %.4f / %.4f", "QuickAttack[%d]: %s / %.4f", ...
    std::string line = stage_label(s);
    if (index > 0) line += "[" + std::to_string(index) + "]";
    line += ": " + name;
    for (float v : scores) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), " / %.4f", v);
        line += buf;
    }
    lines_.push_back(std::move(line));
}

void DecisionTrace::distance_error(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "DistanceError: %.3f", v);
    lines_.emplace_back(buf);
}

void DecisionTrace::frame_error(int v) {
    lines_.push_back("FrameError: " + std::to_string(v));
}

void DecisionTrace::intervals(float self, float enemy) {
    // [HEURISTIC-TODO] the original prints %s — IntervalRec entries rendered
    // record-by-record (MEMORY_INDEXING_R56.md §3.1); the engine pipeline has
    // no IntervalRec list yet, so the frame counts stand in.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Intervals: %.0f", self);
    lines_.emplace_back(buf);
    std::snprintf(buf, sizeof(buf), "EnemyIntervals: %.0f", enemy);
    lines_.emplace_back(buf);
}

void DecisionTrace::decision_type(const std::string& t) {
    lines_.push_back("DecisionType: " + t);
}

void DecisionTrace::decision(int wait_frames) {
    lines_.push_back("Decision {Wait=" + std::to_string(wait_frames) + "}");
}

bool chance_fires(const TacticWeight& curve, const TacticContext& ctx, RngSource rng) {
    // [HEURISTIC-TODO R3] comparator — roll vs curve value.
    const float value = curve.evaluate(ctx);
    const float roll = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
    return roll < value;
}

const std::string& quick_attack_animation(const TacticDef& def, std::size_t index) {
    // [HEURISTIC-TODO R6] default: document order.
    return def.quick_attack_chances[index].first;
}

const std::string& evade_animation(const TacticDef& def, std::size_t index) {
    // [HEURISTIC-TODO R6] default: document order.
    return def.evade_chances[index].first;
}

// ---- the seven stages ----

// [ORIGINAL] "UseDefense: %s / %.4f / %.4f / %.4f" @ 0x8F7980B5 — gate:
// <UseDefense> presence (PORT_GAPS.md:152); the three sub-chances
// (CounterAttackChance, DodgeChance, BlockChance) roll in that order and the
// first that fires wins the stage.
std::optional<TacticDecision> stage_use_defense(const TacticDef& def,
                                                const TacticContext& ctx,
                                                TacticMemory&, const TacticTableSet&,
                                                RngSource rng, DecisionTrace& trace) {
    const std::vector<float> scores = {
        def.counter_attack_chance.evaluate(ctx),
        def.dodge_chance.evaluate(ctx),
        def.block_chance.evaluate(ctx),
    };
    if (def.use_defense) {
        const TacticWeight* subs[3] = {&def.counter_attack_chance,
                                       &def.dodge_chance,
                                       &def.block_chance};
        for (int i = 0; i < 3; ++i) {
            if (chance_fires(*subs[i], ctx, rng)) {
                const std::string name = kDefenseAnimations[i];
                trace.stage(DecisionStage::kUseDefense, name, scores);
                return TacticDecision{DecisionStage::kUseDefense, name};
            }
        }
    }
    trace.stage(DecisionStage::kUseDefense, "", scores);
    return std::nullopt;
}

// [ORIGINAL] "UseSafeAttack: %s / %.4f" @ 0x8F7980FE — the standalone
// UseSafeAttackChance rolls; on fire the safe-attack animation is picked.
// [HEURISTIC-TODO] the safe-attack animation selection is unpinned; default =
// the tactic's own roulette over animation_weights.
std::optional<TacticDecision> stage_use_safe_attack(const TacticDef& def,
                                                    const TacticContext& ctx,
                                                    TacticMemory& mem,
                                                    const TacticTableSet& tables,
                                                    RngSource rng,
                                                    DecisionTrace& trace) {
    const float score = def.use_safe_attack_chance.evaluate(ctx);
    if (!chance_fires(def.use_safe_attack_chance, ctx, rng)) {
        trace.stage(DecisionStage::kUseSafeAttack, "", {score});
        return std::nullopt;
    }
    const auto picked = pick(def, weight_candidates(def), ctx, mem, tables, rng);
    trace.stage(DecisionStage::kUseSafeAttack, picked.value_or(""), {score});
    if (!picked) return std::nullopt;  // fired but every weight zero -> no decision
    return TacticDecision{DecisionStage::kUseSafeAttack, *picked};
}

// [ORIGINAL] "TableAttack: %s / %.4f" @ 0x8F79812E — TableAttackChance rolls;
// on fire the target is picked via attack_table(...) when present, else the
// tactic's animation_weights (candidate picks reuse TacticSettings::choose).
std::optional<TacticDecision> stage_table_attack(const TacticDef& def,
                                                 const TacticContext& ctx,
                                                 TacticMemory& mem,
                                                 const TacticTableSet& tables,
                                                 RngSource rng,
                                                 DecisionTrace& trace) {
    const float score = def.table_attack_chance.evaluate(ctx);
    if (!chance_fires(def.table_attack_chance, ctx, rng)) {
        trace.stage(DecisionStage::kTableAttack, "", {score});
        return std::nullopt;
    }
    const auto picked = pick_attack_target(def, ctx, mem, tables, rng);
    trace.stage(DecisionStage::kTableAttack, picked.value_or(""), {score});
    if (!picked) return std::nullopt;
    return TacticDecision{DecisionStage::kTableAttack, *picked};
}

// [ORIGINAL] "DodgeMissiles: %s / %.4f" @ 0x8F798144 — the standalone
// DodgeMissilesChance rolls; on fire the fighter dodges.
// [HEURISTIC-TODO] the dodge animation is unpinned; default = the "Dodge"
// action label (no such moves.xml animation name) until the P3 golden pins it.
std::optional<TacticDecision> stage_dodge_missiles(const TacticDef& def,
                                                   const TacticContext& ctx,
                                                   TacticMemory&,
                                                   const TacticTableSet&,
                                                   RngSource rng,
                                                   DecisionTrace& trace) {
    const float score = def.dodge_missiles_chance.evaluate(ctx);
    if (!chance_fires(def.dodge_missiles_chance, ctx, rng)) {
        trace.stage(DecisionStage::kDodgeMissiles, "", {score});
        return std::nullopt;
    }
    trace.stage(DecisionStage::kDodgeMissiles, kDefenseAnimations[1], {score});
    return TacticDecision{DecisionStage::kDodgeMissiles, kDefenseAnimations[1]};
}

// [ORIGINAL] "QuickAttack[%d]: %s / %.4f" @ 0x8F798161 — the loop over the
// +0x8c score vector (MEMORY_INDEXING_R56.md §3.1) dumps EVERY entry (1-based)
// before any roll; the first entry that fires wins the stage.
std::optional<TacticDecision> stage_quick_attack(const TacticDef& def,
                                                 const TacticContext& ctx,
                                                 TacticMemory& mem,
                                                 const TacticTableSet& tables,
                                                 RngSource rng,
                                                 DecisionTrace& trace) {
    for (std::size_t i = 0; i < def.quick_attack_chances.size(); ++i) {
        const std::string& anim = quick_attack_animation(def, i);
        // D5: wire the per-candidate probe before scoring the entry.
        TacticContext wired = ctx;
        wire_candidate(wired, tables, anim);
        const float score = def.quick_attack_chances[i].second.evaluate(wired);
        trace.stage(DecisionStage::kQuickAttack, anim, {score},
                    static_cast<int>(i) + 1);
    }
    for (std::size_t i = 0; i < def.quick_attack_chances.size(); ++i) {
        TacticContext wired = ctx;
        wire_candidate(wired, tables, quick_attack_animation(def, i));
        if (chance_fires(def.quick_attack_chances[i].second, wired, rng)) {
            return TacticDecision{DecisionStage::kQuickAttack,
                                  quick_attack_animation(def, i)};
        }
    }
    return std::nullopt;
}

// [ORIGINAL] "Evade[%d]: %s / %.4f" @ 0x8F798184 — one line per <EvadeChance>
// entry (1-based), the loop over the +0x98 score vector dumps every entry
// before any roll; the first entry to fire wins the stage.
std::optional<TacticDecision> stage_evade(const TacticDef& def,
                                          const TacticContext& ctx,
                                          TacticMemory& mem,
                                          const TacticTableSet& tables,
                                          RngSource rng,
                                          DecisionTrace& trace) {
    for (std::size_t i = 0; i < def.evade_chances.size(); ++i) {
        const std::string& anim = evade_animation(def, i);
        TacticContext wired = ctx;
        wire_candidate(wired, tables, anim);
        const float score = def.evade_chances[i].second.evaluate(wired);
        trace.stage(DecisionStage::kEvade, anim, {score},
                    static_cast<int>(i) + 1);
    }
    for (std::size_t i = 0; i < def.evade_chances.size(); ++i) {
        TacticContext wired = ctx;
        wire_candidate(wired, tables, evade_animation(def, i));
        if (chance_fires(def.evade_chances[i].second, wired, rng)) {
            return TacticDecision{DecisionStage::kEvade,
                                  evade_animation(def, i)};
        }
    }
    return std::nullopt;
}

// [ORIGINAL] "UseCautiousMovements: %s / %.4f" @ 0x8F798199 — the standalone
// CautiousMovementsChance rolls; on fire the cautious movement is picked.
// [HEURISTIC-TODO] the cautious-movement animation selection is unpinned;
// default = the tactic's own roulette over animation_weights.
std::optional<TacticDecision> stage_use_cautious_movements(const TacticDef& def,
                                                           const TacticContext& ctx,
                                                           TacticMemory& mem,
                                                           const TacticTableSet& tables,
                                                           RngSource rng,
                                                           DecisionTrace& trace) {
    const float score = def.cautious_movements_chance.evaluate(ctx);
    if (!chance_fires(def.cautious_movements_chance, ctx, rng)) {
        trace.stage(DecisionStage::kUseCautiousMovements, "", {score});
        return std::nullopt;
    }
    const auto picked = pick(def, weight_candidates(def), ctx, mem, tables, rng);
    trace.stage(DecisionStage::kUseCautiousMovements, picked.value_or(""), {score});
    if (!picked) return std::nullopt;
    return TacticDecision{DecisionStage::kUseCautiousMovements, *picked};
}

TacticDecision decide(const TacticDef& def, const TacticContext& ctx,
                      TacticMemory& mem, const TacticTableSet& tables,
                      RngSource rng, DecisionTrace& trace) {
    trace.begin();

    // D5: feed the weight-side <AnimationFactors> sums once per decision.
    TacticContext wired = ctx;
    feed_anim_memory(wired, mem);

    TacticDecision result;  // idle
    result.type = def.type.empty() ? "Tabular" : def.type;

    // The seven stages run in tracer order; every stage traces its line-group
    // (the original dumps the whole decision), the first hit wins.
    bool decided = false;
    const auto try_stage = [&](auto fn) {
        if (auto d = fn(def, wired, mem, tables, rng, trace)) {
            if (!decided) {
                result.stage = d->stage;
                result.animation = d->animation;
                decided = true;
            }
        }
    };
    try_stage(stage_use_defense);
    try_stage(stage_use_safe_attack);
    try_stage(stage_table_attack);
    try_stage(stage_dodge_missiles);
    try_stage(stage_quick_attack);
    try_stage(stage_evade);
    try_stage(stage_use_cautious_movements);

    // Epilogue in tracer order.
    result.distance_error =
        draw_uniform(def.distance_error.min, def.distance_error.max, rng);
    trace.distance_error(result.distance_error);
    result.frame_error = static_cast<int>(std::llround(
        draw_uniform(def.frame_error.min, def.frame_error.max, rng)));
    trace.frame_error(result.frame_error);
    trace.intervals(mem.self_interval(), mem.enemy_interval());
    trace.decision_type(result.type);

    // [HEURISTIC-TODO R4] wait_frames mapping: default = the picked
    // <ExpectedWait> entry's weight value as frames, clamped >= 0. Other
    // decision types never reach here (the loader rejects them).
    if (result.type == "ExpectedWait") {
        const int idx = pick_expected_wait(def, wired, rng);
        if (idx >= 0) {
            const float v = def.expected_wait[static_cast<std::size_t>(idx)]
                                .second.evaluate(wired);
            result.wait_frames = std::max(0, static_cast<int>(std::llround(v)));
        }
    }
    trace.decision(result.wait_frames);
    return result;
}

}  // namespace resf2::game
