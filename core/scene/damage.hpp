#pragma once

// Damage: the `wd.bCa` formula (L513-514) + the `ca.Cgb` hit application
// (L394-397), ported term-for-term from sf2.502f0946.js. See
// core/scene/README.md for the full JS study with line refs.
//
// Formula (bCa), with the JS operand order:
//   d  = wd.LAa(a, block, KD)      // defense attribute name (L536)
//   h  = 2^(DamageFactor * 0.0001) // capped at 20000 (v.ACa/E9a/zCa)
//   b  = block ? 2^(defender.BlockDamageFactor * 0.0001) : 1   (kea, L512)
//   c  = crit  ? 2^(attacker.CriticalDamage   * 0.0001) : 1   (qea, L512)
//   g  = balance(attacker, defender, attackAttrs, defenseAttr) (iea L1205)
//   g  = (a.Xb + attacker.Ly) * g * b * c * h * attacker.UZ
//   g  = max(g, 0)
//   g  = attacker.c2a(defenseAttr, g)   // Fists armor: * M_  (L820)
//   g *= a.Cea(attackerIsPlayer?1:2).bp // interval Vm multiplier (L395)
//   g *= attacker.dta
//   g *= attacker.so
// Application (Cgb): lethal check (hp < bR -> Zi = hp + 0.01, lethal),
// invulnerable -> 0, HP -= Zi.

#include <map>
#include <string>

#include "scene/physics.hpp"

namespace sf2::scene {

// The `ja` value carrier (JS class `ja` L565: a single int field `G`).
// Attribute lookups read into it: `attributes.get(name, out)` sets out.G.
struct AttrValue {
    float value = 0.0f;
};

// Fighter parameters the damage formula reads (JS `xc`/`El` fields).
struct FighterParams {
    bool is_player = false;   // `qb`
    float level = 1.0f;       // `level`
    float xb = 0.0f;          // `Xb` — base Damage (from Warrior XML)
    float uz = 1.0f;          // `UZ` — TrustFailed modifier (clamped 0..1)
    float m_ = 1.0f;          // `M_` — FistsDamageMod (armor for Fists)
    float pp = 0.0f;          // `PP` — Difficulty
    float dta = 1.0f;         // `wd.dta` — the fighter's damage scaling (1)
    float so = 1.0f;          // `wd.so` — the fighter's damage scaling (1)
    float ly = 0.0f;          // `wd.Ly` — extra base damage (0 by default)
    // Attribute map (JS `attributes` = the `ud` map).
    std::map<std::string, float> attributes;

    // JS `ud.get(name, out)` — returns the attribute value (0 if absent).
    float attr(const std::string& name) const {
        const auto it = attributes.find(name);
        return it != attributes.end() ? it->second : 0.0f;
    }
    bool has_attr(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }
};

// Global fight params (JS `v` statics, read from internal_settings.xml):
//   VY = BlockDamageFactor (Mk=Attribute, Bc=Base=0.0001)
//   HZ = CriticalHit/Damage (Mk=Attribute="CriticalDamage", Bc=0.0001)
//   pYa = BlockDefense Attribute = "BodyDefense"
//   BP = DamageDoublingRange Value = 10
//   Ypa = DamageFactor Base = 0.0001; Zpa = MaxValue = 20000
//   Xpa = DamageFactor Attribute = "DamageFactor"
//   lNa = SlowMotion Defense = ""
//   wv = AlignTargetAttributes (empty for the default)
struct FightParams {
    std::string block_damage_attr = "BlockDamageFactor";
    float block_damage_base = 0.0001f;
    std::string crit_damage_attr = "CriticalDamage";
    float crit_damage_base = 0.0001f;
    std::string block_defense_attr = "BodyDefense";
    float damage_doubling_range = 10.0f;  // BP
    float damage_factor_base = 0.0001f;   // Ypa
    float damage_factor_max = 20000.0f;   // Zpa
    std::string damage_factor_attr = "DamageFactor";
    std::string slowmotion_defense = "";  // lNa
    // AlignTargetAttributes (JS `v.wv`): attribute name -> Align value.
    std::map<std::string, float> align_target_attributes;

    static const FightParams& defaults() {
        static const FightParams k;
        return k;
    }
};

// One Attack interval's damage block (JS `Ul` L774): the base Damage value
// + the sub-<Damage> attribute shifts (SZ) + the Defense names (KP).
struct IntervalDamage {
    float base_damage = 0.0f;      // `Ul.Xb` (the <Damage Value=..>)
    bool no_critical = false;      // `Ul.a3` (NoCritical attr)
    std::string hit_body_part;     // `Ul.HC` (Damage BodyPart attr)
    // SZ: (attribute name, shift) pairs from the sub-<Damage> elements.
    std::vector<std::pair<std::string, float>> attack_attrs;
    // KP: Defense attribute names from the sub-<Damage><Defense> elements.
    std::vector<std::string> defense_names;
};

// The damage computation result (JS `wd.Bb` = `pu` L558).
struct HitRecord {
    float raw_damage = 0.0f;   // `bR` — from bCa
    float final_damage = 0.0f; // `Zi` — after the lethal check
    bool lethal = false;       // `Iza`
    bool blocked = false;      // `block`
    bool critical = false;     // `se`
    bool shock = false;        // `Ub`
    bool head_hit = false;     // `Uq`
    std::string defense;       // `JP` — the defense attribute name used
    std::string target_part;   // the hit capsule's BodyPart
    std::string hit_edge;      // the ATTACKER's edge that landed
    int frame = 0;             // fight frame (native log)
    float hp_before = 0.0f;    // target HP before application
    float hp_after = 0.0f;     // target HP after application
};

// The damage formula inputs (JS `wd.bCa(a, block, crit, KD, QX)`):
//   interval    = the Attack interval (a)
//   attacker    = the attacker's params (f.parameters)
//   defender    = the defender's params (this.parameters)
//   defense_attr = the defense attribute name (d — from LAa)
//   blocked     = whether the target is blocking (b)
//   critical    = whether the hit crits (c)
//   fighter_params = the global fight params (v statics)
// Returns the raw damage (bR).
float compute_damage(const IntervalDamage& interval, const FighterParams& attacker,
                     const FighterParams& defender, const std::string& defense_attr,
                     bool blocked, bool critical, const HitCapsule* hit_cap,
                     const FightParams& fp = FightParams::defaults());

// The LAa lookup (JS `wd.LAa` L536): the defense attribute name for the hit.
//   a.KP (the interval's Defense list) non-empty -> KP[0]
//   blocked -> v.pYa (BlockDefense Attribute = "BodyDefense")
//   hit_cap has a Defense (Xi) -> that
//   else -> v.lNa (SlowMotion Defense, empty)
std::string select_defense(const IntervalDamage& interval, bool blocked,
                           const HitCapsule* hit_cap,
                           const FightParams& fp = FightParams::defaults());

// Applies the damage to the defender (JS `ca.Cgb` L394 core):
//   lethal check: hp < raw -> final = hp + 0.01, lethal = true
//   invulnerable -> final = 0
//   hp -= final
// Returns the updated HitRecord (hp_before/hp_after set).
void apply_damage(HitRecord& rec, float hp, bool invulnerable);

// The critical chance (JS `v.gya.p8a` L605 + `wd.A9a` L529):
//   CriticalHitChance Base (0.0001) * the ATTACKER's CriticalChance attr.
// `force` overrides the RNG (demo: force a crit).
float crit_chance(const FighterParams& attacker,
                  const FightParams& fp = FightParams::defaults());

// Whether a crit roll succeeds (JS `v.Lcb` L604 + `Da.cT` L1200):
//   chance > 100 || random01 * 100 < chance
bool roll_crit(float chance);

}  // namespace sf2::scene
