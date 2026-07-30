#pragma once

// engine/game/damage_formula.hpp
//
// [ORIGINAL] 1:1 port of Model::getTotalDamage @ game+0x4527B4.
//
// Recovered with Ghidra from the relocated runtime dump (base 0x8F057000).
// The function was located via the sole xref to its assert string
// "Model::getTotalDamage - wtf so strong" (0x8F79A2A0) -- a developer sanity
// warning that fires when the computed damage leaves the range [0, 100000].
// See reverse/analysis/PORT_GAPS.md, section "GAP-3".
//
// ---------------------------------------------------------------------------
// The model, and why it is exponential
// ---------------------------------------------------------------------------
//
// Damage scales as a power of two in the *difference* of attributes, with a
// configurable range that doubles it:
//
//     factor = 2 ^ (attribute_delta / DamageDoublingRange)
//
// `<DamageDoublingRange Value="10"/>` in internalSettings.xml, verified as the
// literal 10.0f at runtime (global settings struct +0x18). So every 10 points
// of attribute advantage doubles the damage. That is the whole design: the
// "wtf so strong" assert exists precisely because this curve grows without
// bound if attributes are misconfigured.
//
// This is the single most important correction to reSF2's model, which used
//     attribute_multiplier = 1.0f + damage_factor_base * attr     // LINEAR
// The two agree only at attr == 0 -- which is exactly the value reSF2
// hardcoded, so the divergence was invisible until attributes existed.
//
// Also note `2.0f` is the *base* of the power, not a trailing "x2" multiplier.
// Keeping both (as the old code did) double-counts it.
//
// ---------------------------------------------------------------------------
// Verified disassembly structure
// ---------------------------------------------------------------------------
//
//   game+0x4527B4  getTotalDamage(self, hit, is_ranged, weapon, ctx)
//     enemy = self[0x1E4]                       ; the defender
//     base  = powf(2.0, baseAttr * baseWeight)
//     f1    = game+0x4A94F0(self,  is_ranged)   ; powf(2.0, w*attr) or 1.0
//     f2    = game+0x4A95A8(enemy, weapon)      ; powf(2.0, w*attr) or 1.0
//     f3    = game+0x60E794(...)                ; powf(2.0, delta / 10.0)
//     add   = hit[0x48] + enemy[0x774]
//     dmg   = base * f2 * f1 * f3 * add
//     dmg   = max(dmg, 0.0)                     ; DAT_8f4a9a5c = 0.0
//     crit  = game+0x42A8A8(hit)[1]
//     dmg   = dmg * crit * enemy[0x678] * enemy[0x6AC]
//     if (!(0.0 <= dmg <= 100000.0)) warn("wtf so strong")   ; DAT = 100000.0
//     return dmg
//
// Each of f1/f2/f3 returns exactly 1.0f (0x3F800000) when its selector
// argument is null, i.e. a disabled term is multiplicatively neutral -- it is
// never skipped or treated as 0.
//
// f1 takes the *attacker* (self, is_ranged) while f2 takes the *defender*
// (enemy, weapon). That asymmetry is the DamageAttribute / DefenseAttribute
// split that the game's built-in tracer prints.

#include <cmath>
#include <cstdio>

namespace resf2::game {

// [ORIGINAL] Constants verified against the live binary / internalSettings.xml.
struct DamageFormulaConstants {
    // The power base. `mov r0, #0x40000000` == 2.0f, passed to powf as the base
    // at every one of the four call sites.
    static constexpr float kPowBase = 2.0f;

    // <DamageDoublingRange Value="10"/>; read live as 10.0f from the global
    // settings struct at +0x18. Attribute delta is divided by this before the
    // power, so `range` points of advantage double the damage.
    static constexpr float kDamageDoublingRange = 10.0f;

    // <ResistanceDoublingRange Value="500"/> -- the same curve shape applied to
    // enchantment resistance elsewhere. Kept here for completeness.
    static constexpr float kResistanceDoublingRange = 500.0f;

    // Clamp bounds from DAT_8f4a9a5c / DAT_8f4a9a60. Exceeding the upper bound
    // does NOT clamp the result -- it only logs "wtf so strong" and returns the
    // value unchanged. Reproducing that exactly matters: a clamping
    // implementation would silently diverge on overpowered builds.
    static constexpr float kSanityMin = 0.0f;
    static constexpr float kSanityMax = 100000.0f;
};

// One attribute term: `powf(2.0, weight * attribute)`, or 1.0 when disabled.
// [ORIGINAL] game+0x4A94F0 and game+0x4A95A8 are both exactly this shape.
inline float attribute_factor(float weight, float attribute, bool enabled) {
    if (!enabled) {
        return 1.0f;  // 0x3F800000 -- the neutral value, not 0
    }
    return std::pow(DamageFormulaConstants::kPowBase, weight * attribute);
}

// The attribute-difference term.
// [ORIGINAL] game+0x60E794: powf(2.0, difference / DamageDoublingRange).
// The inner helper (game+0x60DF98) resolves the attacker's DamageAttribute
// against the defender's DefenseAttribute list and returns their difference.
inline float attribute_difference_factor(
    float difference,
    float doubling_range = DamageFormulaConstants::kDamageDoublingRange) {
    if (doubling_range == 0.0f) {
        return 1.0f;  // guard: the original would divide by zero here
    }
    return std::pow(DamageFormulaConstants::kPowBase, difference / doubling_range);
}

// Inputs to the formula, named after what the disassembly reads.
struct DamageInputs {
    // base = powf(2.0, base_attribute * base_weight)
    float base_attribute = 0.0f;
    float base_weight = 0.0f;

    // f1 -- attacker side (self, is_ranged). game+0x4A94F0
    float attacker_weight = 0.0f;
    float attacker_attribute = 0.0f;
    bool attacker_enabled = false;

    // f2 -- defender side (enemy, weapon). game+0x4A95A8
    float defender_weight = 0.0f;
    float defender_attribute = 0.0f;
    bool defender_enabled = false;

    // f3 -- DamageAttribute vs DefenseAttribute difference. game+0x60E794
    float attribute_difference = 0.0f;

    // add = hit[0x48] + enemy[0x774]
    float hit_damage = 0.0f;        // hit[0x48]
    float enemy_damage_bonus = 0.0f;  // enemy[0x774]

    // Applied after the max(0) clamp.
    float crit_factor = 1.0f;         // game+0x42A8A8(hit)[1]
    float enemy_multiplier_a = 1.0f;  // enemy[0x678]
    float enemy_multiplier_b = 1.0f;  // enemy[0x6AC]
};

// Set true to reproduce the original's debug warning on stderr.
inline bool& damage_warn_enabled() {
    static bool enabled = false;
    return enabled;
}

// [ORIGINAL] Model::getTotalDamage @ game+0x4527B4.
// Multiplication order is preserved from the disassembly (base * f2 * f1 * f3
// * add): float multiplication is not associative, so reordering can change the
// last bits of the result.
inline float get_total_damage(const DamageInputs& in) {
    using C = DamageFormulaConstants;

    const float base = std::pow(C::kPowBase, in.base_attribute * in.base_weight);
    const float f1 = attribute_factor(in.attacker_weight, in.attacker_attribute,
                                      in.attacker_enabled);
    const float f2 = attribute_factor(in.defender_weight, in.defender_attribute,
                                      in.defender_enabled);
    const float f3 = attribute_difference_factor(in.attribute_difference);

    const float add = in.hit_damage + in.enemy_damage_bonus;

    // Order as emitted: base * f2 * f1 * f3 * add
    float dmg = base * f2 * f1 * f3 * add;

    if (dmg < C::kSanityMin) {
        dmg = C::kSanityMin;
    }

    dmg = dmg * in.crit_factor * in.enemy_multiplier_a * in.enemy_multiplier_b;

    // [ORIGINAL] The check only warns; it does not clamp. Matching that is
    // required for 1:1 behaviour on very strong builds.
    if (!(dmg >= C::kSanityMin && dmg <= C::kSanityMax)) {
        if (damage_warn_enabled()) {
            std::fprintf(stderr, "Model::getTotalDamage - wtf so strong (%f)\n",
                         static_cast<double>(dmg));
        }
    }

    return dmg;
}

}  // namespace resf2::game
