// engine/game/tactic_settings.hpp
//
// [ORIGINAL] tacticSettings.xml — the enemy AI decision model.
//
// The original AI does NOT pick moves from distance thresholds. Every
// candidate animation carries a *weight*, and the choice is a roulette-wheel
// draw over those weights. A weight is not a constant: it is a curve
// evaluated per decision against the current fight state (distance, health,
// damage taken, frame counts...).
//
// PC source, class `cc` (sf2_beautified.js:20044-20122):
//
//   parse()  — reads Base, CounterFactor, DamageFactor, HealthFactor,
//              EnemyHealthFactor, AnimationFramesFactor, ChildFramesFactor,
//              MagicBulletFactor, MissileBulletFactor, HitFactor,
//              DistanceFactor, Shift, ConditionalDesigionFactor, Limit,
//              AntiLimit, FactorType.
//
//   Gb(ctx)  — the score, a plain dot product plus Shift (line 20096):
//                c = counter*CounterFactor
//                  + damage*DamageFactor
//                  + (1-health)*HealthFactor
//                  + (1-enemy_health)*EnemyHealthFactor
//                  + anim_frames*AnimationFramesFactor
//                  + magic_bullets*MagicBulletFactor
//                  + missile_bullets*MissileBulletFactor
//                  + hits*HitFactor
//                  + child_frames*ChildFramesFactor
//                  + distance*DistanceFactor
//                  + Shift
//              then mapped to a weight through one of two curves.
//
//   QYa(a)   — FactorType="Linear" (the default, line 20117):
//                a >= 0 : Base + (Limit     - Base) * min(1,  a)
//                a <  0 : Base + (AntiLimit - Base) * min(1, -a)
//
//   NYa(a)   — FactorType="Exponential" (line 20113):
//                a >= 0 : Limit     + (Base - Limit)     * 2^-a
//                a <  0 : AntiLimit + (Base - AntiLimit) * 2^a
//
// Note the asymmetry, which is easy to get wrong: Linear interpolates *from*
// Base *towards* Limit, while Exponential decays *from* Base *towards*
// Limit. Both agree at a == 0 (weight == Base) and both saturate at Limit /
// AntiLimit, so only the shape between differs.
//
// The roulette draw is `jL` (line 19910) over `iCa` (line 19930): sum the
// weights of all candidates, draw a uniform value in [0, sum), then walk the
// list subtracting until it goes negative. `iCa` resolves which weight entry
// applies to an animation by matching `<Animation Name="...">`, with an
// unnamed `<Animation Base="..."/>` acting as the catch-all default — which
// is why every tactic in tacticSettings.xml ends with a bare
// `<Animation Base="100" />`.

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace resf2::game {

// ADR-005 D4 — injectable RNG for the roulette draw. Contract: returns a
// value in [0, RAND_MAX], the same range as std::rand, so the draw formula
// (rng()/RAND_MAX * sum) is identical to the existing jL behavior.
// Production binds std::rand (RNG-sequence determinism parity with the
// baseline); tests bind a seeded LCG.
using RngSource = std::function<unsigned()>;

// GAP-4 step B4 (R2 GREEN) — per-animation memory sums backing the
// <AnimationFactors> probe. [ORIGINAL] FUN_8f44ac78 @ 0x8F44AC78 scores each
// child inline: a += child.DamageFactor*D(A) + child.CounterFactor*C(A) +
// child.HitFactor*H(A), where D(A)/C(A)/H(A) are the exponentially-decayed
// damage/counter/hit sums over animation A's record-id set
// (reverse/analysis/VERIFY_FUN_8f44ac78.md, ATF_RECORD_858.md §3). These
// maps are the engine-side feed for those sums; an absent animation reads as
// 0.0f, so all-empty is exactly the no-probe baseline (neutral-by-zero,
// ADR-005 R3) until Phase C feeds them from TacticMemory.
struct AnimationMemorySums {
    std::unordered_map<std::string, float> damage;   // D(A)
    std::unordered_map<std::string, float> counter;  // C(A)
    std::unordered_map<std::string, float> hits;     // H(A)
};

// The fight state a weight curve is evaluated against.
// Field order mirrors the terms of Gb() so the two can be read side by side.
struct TacticContext {
    float counter = 0;          // a.counter — successful counters so far
    float damage = 0;           // a.Xb      — damage dealt by the animation
    float health = 1;           // a.o1      — self health, normalised 0..1
    float enemy_health = 1;     // a.q1      — enemy health, normalised 0..1
    float anim_frames = 0;      // a.xY      — frames of the animation
    float magic_bullets = 0;    // a.cl
    float missile_bullets = 0;  // a.K2
    float hits = 0;             // a.tf      — hits landed
    float child_frames = 0;     // a.pZ
    float distance = 0;         // a.Lya     — distance to the enemy, points

    // --- ADR-005 D2/D5 extension (decision pipeline context) ---------------
    // [ORIGINAL] key schema 0x8F797574..0x8F797C58 includes AnimationFactors
    // and CurrentAnimation. Populated by the caller from TacticMemory + fight
    // state; all default-neutral until the pipeline wires them (Phase C/D).
    float animation_factor = 0;   // per-target probe result (a.a6.S5a) — D5
    float strikes = 0;            // from TacticMemory — D8
    float round_factor = 0;       // tactic's RoundFactor, mirrored for scoring
    float self_interval = 0;      // frames since own last action (Intervals)
    float enemy_interval = 0;     // frames since enemy's last action (EnemyIntervals)
    std::string current_animation; // CurrentAnimation key — the probe's target

    // GAP-4 B4 (R2 GREEN): per-animation (D,C,H) memory sums consumed by the
    // per-child <AnimationFactors> probe in TacticWeight::score(). Default
    // empty -> every lookup 0.0f (neutral) until Phase C wires TacticMemory.
    AnimationMemorySums anim_memory;
};

// One weight curve: a `<Animation>` / `<...Chance>` element.
class TacticWeight {
public:
    enum class Curve { kLinear, kExponential };

    // Raw attributes, kept public so the debug overlay can print them.
    float base = 0;
    float limit = 0;
    float anti_limit = 0;
    float counter_factor = 0;
    float damage_factor = 0;
    float health_factor = 0;
    float enemy_health_factor = 0;
    float animation_frames_factor = 0;
    float child_frames_factor = 0;
    float magic_bullet_factor = 0;
    float missile_bullet_factor = 0;
    float hit_factor = 0;
    float distance_factor = 0;
    float shift = 0;
    // R2 (GAP-4 B4): there is NO scalar `AnimationFactors` attribute in the
    // native parse — parse_weight (FUN_8f44c474) reads 15 attributes and the
    // name appears only as a child *element* (ATF_RECORD_858.md §2). The A1
    // coefficient was removed; the probe is the per-child sum below.
    Curve curve = Curve::kLinear;

    // Per-target probe entries: `<AnimationFactors Animation="..." .../>`
    // children (real XML shape — confirmed in this dump inside <Animation>,
    // <QuickAttackChance>, <EvadeChance>). Defined out-of-line below because
    // the entry embeds a TacticWeight by value. Absent probe data is
    // neutral-by-zero, never an error (ADR-005 R3).
    struct AnimationFactorEntry;
    std::vector<AnimationFactorEntry> animation_factor_entries;

    // Gb() — score the context, then map through the curve.
    [[nodiscard]] float evaluate(const TacticContext& ctx) const;

    // The raw score before the curve. Exposed for the F1 overlay, which shows
    // why a weight came out the way it did.
    [[nodiscard]] float score(const TacticContext& ctx) const;

private:
    [[nodiscard]] float apply_curve(float a) const;
};

// One `<AnimationFactors Animation="..." .../>` child: the weight curve that
// scores the probe against a specific target animation.
struct TacticWeight::AnimationFactorEntry {
    std::string animation;
    TacticWeight factors;
};

// One `<Tactic>`: a named set of animation weights, resolved through the
// Template chain so inherited entries are already merged in.
struct TacticDef {
    std::string name;
    std::string template_name;
    std::string type;  // "Tabular" / "ExpectedWait" / "" (default = Tabular)

    // Ordered, because resolution is first-match and the unnamed catch-all
    // must stay last. `first` is the animation name; empty means default.
    std::vector<std::pair<std::string, TacticWeight>> animation_weights;

    // --- ADR-005 D2: the 20 decision-level keys ----------------------------
    // [ORIGINAL] key list 0x8F797574..0x8F797C58 / PORT_GAPS.md:152-157,
    // shaped exactly like the real assets/tacticSettings.xml schema.
    // All inert until the Phase C/D pipeline wires them.

    // <UseDefense> is a container of 3 chance curves; its mere presence is
    // the stage-1 gate.
    bool use_defense = false;
    TacticWeight counter_attack_chance;   //   <CounterAttackChance>
    TacticWeight dodge_chance;            //   <DodgeChance>
    TacticWeight block_chance;            //   <BlockChance>

    // Standalone chance curves.
    TacticWeight use_safe_attack_chance;  // <UseSafeAttackChance>
    TacticWeight table_attack_chance;     // <TableAttackChance>
    TacticWeight cautious_movements_chance;  // <CautiousMovementsChance>
    TacticWeight dodge_missiles_chance;   // <DodgeMissilesChance>
    TacticWeight dodge_magic_chance;      // <DodgeMagicChance>

    // <QuickAttacks>/<Evades> are containers of per-animation
    // <QuickAttackChance Animation="...">/<EvadeChance Animation="...">
    // entries; order preserved, entry count = the stage repeat count (R6).
    std::vector<std::pair<std::string, TacticWeight>> quick_attack_chances;
    std::vector<std::pair<std::string, TacticWeight>> evade_chances;

    // <DistanceError>/<FrameError>/<ResponseDelay>/<EnemyResponseDelay> are
    // <Min Base/><Max Base/> ranges, not scalars.
    struct MinMax {
        float min = 0;
        float max = 0;
    };
    MinMax distance_error;
    MinMax frame_error;
    MinMax response_delay;
    MinMax enemy_response_delay;

    // <ExpectedWait> is an animation-weight list (same shape as
    // animation_weights), not a single curve.
    std::vector<std::pair<std::string, TacticWeight>> expected_wait;

    // <Memory Strikes=".." RoundFactor=".."/> carries Strikes/RoundFactor as
    // attributes; a `Memory` depth attribute is absent in this dump -> 0
    // (ring-depth source flagged R5).
    int strikes = 0;
    float round_factor = 0;
    int memory = 0;

    // Presence flags for the decision-level keys, recorded at parse time so
    // template inheritance can tell "declared with all-zero attributes" from
    // "not declared" (the same local-wins rule as animation_weights).
    enum DecisionKey : unsigned {
        kUseDefenseKey         = 1u << 0,
        kUseSafeAttackKey      = 1u << 1,
        kTableAttackKey        = 1u << 2,
        kCautiousMovementsKey  = 1u << 3,
        kDodgeMissilesKey      = 1u << 4,
        kDodgeMagicKey         = 1u << 5,
        kQuickAttacksKey       = 1u << 6,
        kEvadesKey             = 1u << 7,
        kDistanceErrorKey      = 1u << 8,
        kFrameErrorKey         = 1u << 9,
        kResponseDelayKey      = 1u << 10,
        kEnemyResponseDelayKey = 1u << 11,
        kExpectedWaitKey       = 1u << 12,
        kMemoryKey             = 1u << 13,
    };
    unsigned declared_keys = 0;

    // iCa() — the weight that applies to `animation`, or nullptr if the
    // tactic declares no weights at all.
    [[nodiscard]] const TacticWeight* weight_for(const std::string& animation) const;
};

class TacticSettings {
public:
    // Loads tacticSettings.xml from the usual asset search paths.
    bool load(const std::string& asset_root);

    [[nodiscard]] const TacticDef* tactic(const std::string& name) const;
    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] size_t count() const { return tactics_.size(); }

    // jL() — roulette-wheel pick over `candidates`. Returns the index of the
    // chosen candidate, or -1 when every weight is zero (the original
    // returns -1 too, and the caller then does nothing this decision).
    // The 3/4-arg forms bind std::rand (production parity); the RngSource
    // overloads take an injected draw source (ADR-005 D4).
    [[nodiscard]] int choose(const TacticDef& tactic,
                             const std::vector<std::string>& candidates,
                             const TacticContext& ctx) const;
    [[nodiscard]] int choose(const TacticDef& tactic,
                             const std::vector<std::string>& candidates,
                             const TacticContext& ctx,
                             RngSource rng) const;

    // Same draw, but reports the weights it used. For the F1 overlay.
    [[nodiscard]] int choose_debug(const TacticDef& tactic,
                                   const std::vector<std::string>& candidates,
                                   const TacticContext& ctx,
                                   std::vector<float>& out_weights) const;
    [[nodiscard]] int choose_debug(const TacticDef& tactic,
                                   const std::vector<std::string>& candidates,
                                   const TacticContext& ctx,
                                   std::vector<float>& out_weights,
                                   RngSource rng) const;

private:
    std::unordered_map<std::string, TacticDef> tactics_;
    bool loaded_ = false;

    // Template="X" inherits X's weights; entries declared locally win.
    void resolve_templates();
};

}  // namespace resf2::game
