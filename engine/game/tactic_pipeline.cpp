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

// [Soak-fix A4] The CautiousMovements candidate pool: MOVEMENT animations
// only. SOAK_TRIAGE.md A3's suspected subsystem is the pipeline approach
// logic — the stage previously rouletted over the FULL animation_weights
// pool (attacks/specials like BossAbility=3000 dominate), so the enemy
// almost never picked ForwardStep and stood still. A cautious-movement
// decision must pick a movement; the pool is the tactic's named weights the
// executor can move with (step/retreat moves; BackHandflip is the back-
// handflip retreat per SOAK_TRIAGE M2). [HEURISTIC-TODO] the original's
// movement candidate list is unpinned (no movements/ table assets exist);
// empty filter falls back to the full weights so tactics without movement
// anims keep rouletting over their declared set.
std::vector<std::string> movement_candidates(const TacticDef& def) {
    std::vector<std::string> out;
    for (const auto& [name, w] : def.animation_weights) {
        (void)w;
        if (name.empty()) continue;
        if (name == "ForwardStep" || name == "BackStep" ||
            name == "BackHandflip" || name == "Retreat") {
            out.push_back(name);
        }
    }
    if (out.empty()) return weight_candidates(def);
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

// [ORIGINAL] R4 (GREEN, VERIFY_R34.md): the ExpectedWait gate weight w comes
// from the first <ExpectedWait> record with an empty name (default record) or
// whose name matches the current animation — FUN_8f446b98 @ 0x8F446B98;
// no record -> log + return 1.0f (0x3F800000, verified raw). The weight is a
// PROBABILITY (the attack gate), never frames.
float expected_wait_weight(const TacticDef& def, const TacticContext& ctx) {
    for (const auto& [name, w] : def.expected_wait) {
        if (name.empty() || name == ctx.current_animation) {
            return w.evaluate(ctx);
        }
    }
    return 1.0f;
}

// [Soak-fix A4] [ORIGINAL] R4 wait mapping — the {Wait=%d} frames are
// duration arithmetic on the picked animation / enemy, per decision path
// (DECISION_SEMANTICS.md R4 §3.1; VERIFY_R34.md GREEN:
//   param_1[0x12] = max(animFrames, min(animRange, (speedVal-damage)+1)) - 1
//   (paths 1/2), cbe0-1 (path 3); the use-defense push loop FUN_8f457fb8
//   stores (c660(enemy,1) - damage) + 1):
//   attack path:      max(animFrames, min(animRange, (speedVal-damage)+1)) - 1
//   ready path:       animFrames
//   use-defense path: (c660(enemy,1) - damage) + 1
//   speedVal = (speed+1) * (maxAttr - X + 2)   (FUN_8f47d294 / FUN_8f47c660)
//   animFrames = (frames+1) * (maxAttr - X + 2) + 1   (FUN_8f47cbe0) — the
//     live path feeds ctx.anim_frames ALREADY in this R4-modeled shape
//     (the verified formula applied to the current animation's real frame
//     count with zero-fallback for the [UNCERTAIN] maxAttr/X terms, leaving
//     the +2/+1 constants: 2*frames+3); scripted tests feed raw values.
// Inputs are engine-fed via TacticContext (see tactic_settings.hpp):
// ctx.anim_frames (R4-modeled duration), ctx.damage (picked move's damage),
// and the fighter-attribute mapping ctx.speed_attr/max_attr/anim_x
// ([HEURISTIC-TODO R4]: the binary's attribute-name lists behind
// FUN_8f43f0b8/FUN_8f43f0cc are runtime-populated, names [UNCERTAIN]).
// Missing/unknown stays zero-fallback per plan; no fabricated constants.
// The signed result is stored as the binary stores it (decision+0x12); the
// trace prints |wait|.
int compute_wait_frames(DecisionStage stage, const TacticContext& ctx) {
    const float speed_val = (ctx.speed_attr + 1.0f) *
                            (ctx.max_attr - ctx.anim_x + 2.0f);
    switch (stage) {
        case DecisionStage::kUseDefense:
            return static_cast<int>(speed_val - ctx.damage + 1.0f);
        case DecisionStage::kUseSafeAttack:
        case DecisionStage::kTableAttack:
        case DecisionStage::kQuickAttack:
            return static_cast<int>(std::max(
                ctx.anim_frames,
                std::min(ctx.anim_range, speed_val - ctx.damage + 1.0f)) -
                1.0f);
        default:  // ready path — idle / dodge / evade / movement
            return static_cast<int>(ctx.anim_frames);
    }
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
    // [ORIGINAL] the tracer prints |wait| (bic r2, r3, r3, asr #0x1f @
    // FUN_8f4556fc, call site 0x8F455AC8); decision+0x12 stores the signed
    // duration-arithmetic result (R4).
    lines_.push_back("Decision {Wait=" + std::to_string(std::abs(wait_frames)) + "}");
}

bool chance_fires(const TacticWeight& curve, const TacticContext& ctx, RngSource rng) {
    // [ORIGINAL] R3 (GREEN, VERIFY_R34.md): fire iff score > threshold —
    // strict GT at every threshold stage (ASM vcmpe.f32 + movgt, 7 comparator
    // sites in the FUN_8f45ab38 @ 0x8F45AB38 evaluate block 0x8F45ACB4,
    // quick-attack FUN_8f45456c, Evade loop). Threshold = the rng roll,
    // rolled at the stage's evaluation point. Per-slot cadence (R6): the
    // binary rolls each QuickAttack/Evade slot's threshold ONCE per slot
    // (FUN_8f45a920/aa2c) and persists it across decisions; the engine
    // evaluates each stage once per decide(), so a call-time roll matches —
    // the persistence nuance only matters once slot results are cached
    // between decisions.
    const float score = curve.evaluate(ctx);
    const float threshold = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
    return threshold < score;
}

const std::string& quick_attack_animation(const TacticDef& def, std::size_t index) {
    // [ORIGINAL] R6 (GREEN, VERIFY_R56.md §4): entry -> animation by XML
    // document order — the i-th <QuickAttackChance> entry (FUN_8f45456c @
    // 0x8F45456C scores entry[i] at table+0x1f8, stride 0x6c; the tracer
    // numbers the entries 1-based, "QuickAttack[%d]" @ 0x8F798161).
    return def.quick_attack_chances[index].first;
}

const std::string& evade_animation(const TacticDef& def, std::size_t index) {
    // [ORIGINAL] R6 (GREEN, VERIFY_R56.md §4): entry -> animation by XML
    // document order — the i-th <EvadeChance> entry (Evade loop in
    // FUN_8f45ab38 @ 0x8F45AD88 scores entry[i] at table+0x204, stride 0x6c;
    // the tracer numbers the entries 1-based, "Evade[%d]" @ 0x8F798184).
    return def.evade_chances[index].first;
}

// ---- the seven stages ----

// [ORIGINAL] "UseDefense: %s / %.4f / %.4f / %.4f" @ 0x8F7980B5 — gate:
// <UseDefense> presence (PORT_GAPS.md:152). R3 (GREEN, VERIFY_R34.md):
// UseDefense is the ONE divergence from the shared threshold comparator — a
// one-roll 4-way cumulative-interval draw (FUN_8f453a94 @ 0x8F453A94) over
// A/B/C = the CounterAttack/Dodge/Block chance evaluates (table+0x18/+0x78/
// +0xd8):
//     A > r            -> 2 (CounterAttack)
//     A + B > r        -> 3 (Dodge)
//     C + (A + B) > r  -> 4 (Block)
//     else             -> 1 (no defense)
// Strict inequalities throughout (r == A falls to bin 3, etc.).
// [HEURISTIC-TODO] bin 1's action is consumed by the switch in FUN_8f459b44
// and unpinned — the engine maps it to no-fire (trace with empty name).
std::optional<TacticDecision> stage_use_defense(const TacticDef& def,
                                                const TacticContext& ctx,
                                                TacticMemory& mem,
                                                const TacticTableSet&,
                                                RngSource rng, DecisionTrace& trace) {
    const float a = def.counter_attack_chance.evaluate(ctx);
    const float b = def.dodge_chance.evaluate(ctx);
    const float c = def.block_chance.evaluate(ctx);
    const std::vector<float> scores = {a, b, c};
    int choice = 1;  // no defense
    // [D4] EnemyResponseDelay gate (ADR-005 D8): while the reaction
    // countdown window is open (enemy_reaction_frames > 0, ticked per AI
    // frame), the stage-1 reaction draw is blocked — no reaction to fresh
    // player actions mid-window. A reaction that fires opens the window.
    //   [HEURISTIC-TODO] granularity pending @re-verifier R5: which stages
    //   the native binary gates and where the window starts are unpinned;
    //   what is wired is exactly the API the memory exposes.
    if (def.use_defense && mem.enemy_reaction_frames == 0) {
        const float r = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
        if (a > r) {
            choice = 2;
        } else if (a + b > r) {
            choice = 3;
        } else if (c + (a + b) > r) {
            choice = 4;
        }
    }
    if (choice >= 2) {
        // A reaction fired: open the EnemyResponseDelay window.
        mem.start_enemy_reaction(def.enemy_response_delay.min,
                                 def.enemy_response_delay.max, rng);
        const std::string name = kDefenseAnimations[choice - 2];
        trace.stage(DecisionStage::kUseDefense, name, scores);
        return TacticDecision{DecisionStage::kUseDefense, name};
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
                                                   TacticMemory& mem,
                                                   const TacticTableSet&,
                                                   RngSource rng,
                                                   DecisionTrace& trace) {
    const float score = def.dodge_missiles_chance.evaluate(ctx);
    // [D4] EnemyResponseDelay gate (ADR-005 D8): the stage-4 reaction roll
    // is blocked mid-window, same contract as stage 1; a dodge that fires
    // opens the window.
    //   [HEURISTIC-TODO] granularity pending @re-verifier R5 (see
    //   stage_use_defense).
    if (mem.enemy_reaction_frames == 0 &&
        chance_fires(def.dodge_missiles_chance, ctx, rng)) {
        mem.start_enemy_reaction(def.enemy_response_delay.min,
                                 def.enemy_response_delay.max, rng);
        trace.stage(DecisionStage::kDodgeMissiles, kDefenseAnimations[1], {score});
        return TacticDecision{DecisionStage::kDodgeMissiles, kDefenseAnimations[1]};
    }
    trace.stage(DecisionStage::kDodgeMissiles, "", {score});
    return std::nullopt;
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
// [Soak-fix A4] the pick draws from the MOVEMENT candidate pool
// (movement_candidates — steps/retreats only, see its [HEURISTIC-TODO]).
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
    const auto picked = pick(def, movement_candidates(def), ctx, mem, tables, rng);
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

    // [ORIGINAL] R4 (GREEN, VERIFY_R34.md): the ExpectedWait weight is a
    // probability, not frames. Gate: attack fires iff gate < roll, where
    // gate = w >= 1 ? 1 - 1/w : 0 (FUN_8f459b44; DAT_8f459f60 = 0.0f
    // verified) -> P(attack) ~ 1/w; w < 1 -> gate 0 -> practically always
    // attacks. The Wait frames come from animation duration arithmetic, NOT
    // the weight (see compute_wait_frames below).
    bool gate_attack = false;
    if (result.type == "ExpectedWait") {
        const float w = expected_wait_weight(def, wired);
        const float gate = w >= 1.0f ? 1.0f - 1.0f / w : 0.0f;
        const float roll = static_cast<float>(rng()) / static_cast<float>(RAND_MAX);
        gate_attack = gate < roll;  // attack fires
        // else: keep waiting (FUN_8f459b44 early return) — the ready-path
        // wait below holds the current state for the current animation's
        // duration, standing in for the binary's running countdown.
    }

    // [Soak-fix A4] The wait mapping applies to EVERY decision (not just the
    // ExpectedWait attack branch): the executor holds the decision for
    // wait_frames (the binary's decision+0x12 per-tick countdown), so the
    // enemy does not re-roll every frame. Path = the fired stage; an
    // ExpectedWait gate attack on an otherwise-idle decision takes the
    // attack path (the gate IS that path's draw).
    DecisionStage wait_stage = result.stage;
    if (gate_attack && wait_stage == DecisionStage::kIdle) {
        wait_stage = DecisionStage::kUseSafeAttack;
    }
    result.wait_frames = compute_wait_frames(wait_stage, wired);
    trace.decision(result.wait_frames);
    return result;
}

}  // namespace resf2::game
