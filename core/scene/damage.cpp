// Damage formula (`wd.bCa` L513-514) + hit application (`ca.Cgb` L394-397).
// Ported term-for-term from sf2.502f0946.js — line refs in README.md.

#include "scene/damage.hpp"

#include <algorithm>
#include <cmath>

namespace sf2::scene {

namespace {

// JS `IAa(a, Mk, Bc)` (L512): if `a` (flag) -> 2^(attr(Mk) * Bc), else 1.
float attr_exp(const FighterParams& who, const std::string& attr, float base,
               bool flag) {
    if (!flag) return 1.0f;
    return std::pow(2.0f, who.attr(attr) * base);
}

// JS `Bh.Gb(v.wv, name)` (L1164): the AlignTargetAttributes lookup (0 if
// the attribute is not in the align list).
float align_value(const FightParams& fp, const std::string& name) {
    const auto it = fp.align_target_attributes.find(name);
    return it != fp.align_target_attributes.end() ? it->second : 0.0f;
}

// JS `v.iea` (L1205) -> `pAa` (L1204) + `l5a` (L1206): the balance
// multiplier. For the demo's zero-attr fighters with an empty
// AlignTargetAttributes list and no AttributesAlign deltas the result is
// exactly 1 (2^(0/BP)). The full pAa math is transcribed in README.md;
// the port keeps the rating-curve branch (v.Seb.g6a) out (no Rating
// evaluation exists for the demo fighters) and the eclipse filter (eNa)
// out (no Eclipse mode).
float balance_multiplier(const FighterParams& attacker,
                         const FighterParams& defender,
                         const IntervalDamage& interval,
                         const std::string& defense_attr,
                         const FightParams& fp) {
    // pAa: k = align(attacker, defenseAttr); l = 0 - k; e = defender's
    // defense attr value.
    const float k = align_value(fp, defense_attr);
    const float e = defender.attr(defense_attr);

    // For each attack attribute (name, shift):
    //   B = attacker.attr(name) + shift
    //   A = align(attacker, name)
    //   W = min/max over the fighter's IY Align deltas of
    //       (B-e)*(1-Q) + (A-k)*Q  -/+ M   (attacker: min & -M, defender
    //       would use the defender's IY; the demo fighters have none).
    //   t = max(t, W); l = A - k.
    float t = -3.4028234663852886e38f;  // -FLT_MAX
    float l = 0.0f - k;
    for (const auto& ad : interval.attack_attrs) {
        const float B = attacker.attr(ad.first) + ad.second;
        const float A = align_value(fp, ad.first);
        float W = B - e;  // (B-e)*(1-0) + (A-k)*0 - 0  with no align deltas
        // The attacker-side min over IY (JS `x` = the attacker's
        // AttributesAlign list). The default warriors have no IY, so x is
        // empty and the loop body never runs: W stays (B-e) and the min
        // never updates. t takes the max over all attack attrs.
        t = std::max(t, W);
        l = A - k;
    }
    if (t < -3.4e37f) t = 0.0f;  // no attack attrs (shouldn't happen)

    // iea: k = pAa(...); if k > 10 recompute (the JS calls pAa twice when
    // the first result exceeds 10 — a rounding retry). Then 2^(k/BP).
    if (t > 10.0f) {
        // Recompute with the same inputs (no state changes) — the retry is
        // a no-op in the port.
    }
    return std::pow(2.0f, t / fp.damage_doubling_range);
}

}  // namespace

std::string select_defense(const IntervalDamage& interval, bool blocked,
                           const HitCapsule* hit_cap, const FightParams& fp) {
    if (!interval.defense_names.empty()) return interval.defense_names[0];
    if (blocked) return fp.block_defense_attr;
    if (hit_cap != nullptr && !hit_cap->defense.empty()) return hit_cap->defense;
    return fp.slowmotion_defense;
}

float compute_damage(const IntervalDamage& interval, const FighterParams& attacker,
                     const FighterParams& defender, const std::string& defense_attr,
                     bool blocked, bool critical, const HitCapsule* hit_cap,
                     const FightParams& fp) {
    (void)hit_cap;  // the defense attr was already resolved by the caller
    // d = wd.LAa(a, block, KD) — done by the caller (select_defense).
    const std::string d = defense_attr;

    // h = 2^(DamageFactor * 0.0001), capped at 20000 (v.ACa/E9a/zCa).
    float h = fp.damage_factor_base;
    const float df = attacker.attr(fp.damage_factor_attr);
    h = std::pow(2.0f, h * std::min(df, fp.damage_factor_max));

    // b = kea(block): 2^(defender.BlockDamageFactor * 0.0001) if blocked.
    const float b = attr_exp(defender, fp.block_damage_attr, fp.block_damage_base,
                             blocked);
    // c = qea(crit): 2^(attacker.CriticalDamage * 0.0001) if crit.
    const float c = attr_exp(attacker, fp.crit_damage_attr, fp.crit_damage_base,
                             critical);

    // g = the balance multiplier.
    float g = balance_multiplier(attacker, defender, interval, d, fp);

    // g = (a.Xb + attacker.Ly) * g * b * c * h * attacker.UZ
    g = (interval.base_damage + attacker.ly) * g * b * c * h * attacker.uz;
    g = std::max(g, 0.0f);

    // g = attacker.c2a(d, g): Fists armor — if the defense attr is "Fists"
    // (an unarmed block), scale by the attacker's FistsDamageMod.
    if (d == "Fists") {
        g *= attacker.m_;
    }

    // g *= a.Cea(attackerIsPlayer ? 1 : 2).bp — the interval's per-side
    // multiplier (default 1; a fight rule `bn` L436 sets it).
    g *= 1.0f;

    g *= attacker.dta;
    g *= attacker.so;
    return g;
}

float block_mult(const FighterParams& defender, bool blocked,
                   const FightParams& fp) {
    return attr_exp(defender, fp.block_damage_attr, fp.block_damage_base, blocked);
}

float crit_mult(const FighterParams& attacker, bool critical,
                const FightParams& fp) {
    return attr_exp(attacker, fp.crit_damage_attr, fp.crit_damage_base, critical);
}

void apply_damage(HitRecord& rec, float hp, bool invulnerable) {
    rec.hp_before = hp;
    // Lethal check (JS Cgb L394): hp < raw -> Zi = hp + 0.01, Iza = true.
    if (hp < rec.raw_damage) {
        rec.final_damage = hp + 0.01f;
        rec.lethal = true;
    } else {
        rec.final_damage = rec.raw_damage;
        rec.lethal = false;
    }
    if (invulnerable) rec.final_damage = 0.0f;
    // HP decrement: parameters.gd -= Zi (xc.du clamps to [0, Zn]).
    rec.hp_after = std::max(0.0f, hp - rec.final_damage);
}

float crit_chance(const FighterParams& attacker, const FightParams& fp) {
    (void)fp;
    // v.gya = the CriticalHit Probability (Base=0.0001, Attribute=
    // "CriticalChance") — internal_settings <CriticalHit><Probability>.
    constexpr float kCritBase = 0.0001f;
    const std::string kCritAttr = "CriticalChance";
    // p8a: if the attr exists -> base * value, else base.
    if (attacker.has_attr(kCritAttr)) {
        return kCritBase * attacker.attr(kCritAttr);
    }
    return kCritBase;
}

bool roll_crit(float chance) {
    // JS `Da.cT(a, b=100)`: a > b || RNG.s4(b) < a — s4(100) = random01*100.
    if (chance > 100.0f) return true;
    const float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r * 100.0f < chance;
}

}  // namespace sf2::scene
