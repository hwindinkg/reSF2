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
//
// `a.Cea(side)` (JS `Ul.Cea` L395) returns the per-side `Vm` (L396: `k$` for
// side 1, `FV` for side 2, default `FV`). `Vm.bp` defaults to 1 and is set by
// the `ERuleDamageFactor` rule (`bn.Zk` L436: `c.bp = this.zUa`, where
// `this.zUa = u.H(attributes.get("Factor"), 1)` from `bn.parse` L436508). The
// native carries the resolved value on `IntervalDamage::side_bp` (set by
// `FightController::apply_hit` from the per-round rule pass).
// Application (Cgb): lethal check (hp < bR -> Zi = hp + 0.01, lethal),
// invulnerable -> 0, HP -= Zi.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "scene/physics.hpp"

namespace sf2::scene {

// The `ja` value carrier (JS class `ja` L565: a single int field `G`).
// Attribute lookups read into it: `attributes.get(name, out)` sets out.G.
struct AttrValue {
    float value = 0.0f;
};

// One `<AttributesAlign><Delta Factor Shift Priority Eclipse>` row (JS class
// `Ci`, L800 ctor). `bp`/`shift`/`priority` are the `Factor`/`Shift`/
// `Priority` attrs; `eclipse_op` is `OP` (L191):
//   absent Eclipse -> 2, Eclipse="0" -> 1, Eclipse="1" -> 0.
// `eNa(M)` (L1204) keeps a row when `OP==2`, or `OP==1` outside eclipse, or
// `OP==0` inside eclipse.
struct AlignDelta {
    float bp = 0.0f;      // `Factor`
    float shift = 0.0f;   // `Shift`
    int priority = 0;     // `Priority`
    int eclipse_op = 2;   // `OP`
};

// One `<Rating>` child of a perk's `<RatingEvaluation>` (JS `Jw` g="2E0",
// `Jw.parse` L703284). The perk-rating modifier the `xc.JBa` Me/Enemy loops
// read (`Be.x4`). `_`-prefixed attribute values are already substituted from
// the perk's `<Set>` map at parse time.
struct PerkRating {
    std::string player = "Me";  // `Ob` (Player; default "Me")
    std::string damage;         // `Xb` (Damage; "" = the JS null)
    std::string defense;        // `Xi` (Defense; "" = the JS null)
    float multiplier = 0.0f;    // `ff` (Multiplier; `u.H(...,0)`)
    std::string enemy_attr;     // `dQ` (EnemyAttribute; "" = the JS null)
};

// One equipped perk, reduced to what the rating sum reads (JS `Be` subset):
// the `<Set>` attribute map (`iC`, `Be.Zjb` L681500) + the
// `<RatingEvaluation><Rating>` list (`x4`, `Be.Ujb` L681500).
struct PerkModel {
    std::map<std::string, std::string> set;  // `iC` (the `<Set>` attrs)
    std::vector<PerkRating> ratings;         // `x4`
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
    // `IY` (JS `xc.IY`, parsed L191 from `<AttributesAlign><Delta>`): the
    // fighter's align-armor rows. `pAa` reads `(attacker.qb ? defender :
    // attacker).IY`, filtered to the max `Priority` by `Ci.a5a` (L800).
    std::vector<AlignDelta> iy;
    // --- RatingEvaluation warrior fields (JS `xc`, `ur` parse L188-190) -----
    // `W3` (PlayerRating), `C_` (EnemyRating), `w4` (RatingCorrection): the
    // Warrior-XML overrides `dl.A8a` L1421 reads. The `xc` ctor defaults are
    // -1/-1/0 (L807), so an absent attribute leaves the override unset.
    float player_rating = -1.0f;      // `W3`
    float enemy_rating = -1.0f;       // `C_`
    float rating_correction = 0.0f;   // `w4`
    // `jt()` (L808): the warrior's equipment MODEL names — the `xc.mDa`
    // cancelling-item test (`g.hI`/`D.hI`). Shipped save: Body/Head/Fists/
    // NoRanged/NoMagic; the enemy's from its Warrior template items.
    std::vector<std::string> equipment_names;
    // `Wk()` (L413727) restricted to the perks the rating sum reads: the
    // equipped `<Perks>` (`AK`/`TE`) + the equipped items' perks (`Oa`).
    // EMPTY for the fresh save (no `<Rating>` perk equipped), so the `xc.JBa`
    // perk loops are no-ops there. Resolving `AK` from the save's `<Perks>`
    // against perks.xml is the OPEN perk-equip mapping (`PERKS_STATIC` 5.1).
    std::vector<PerkModel> perks;

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
//
// One `<HitEffect>` (JS `em` g="2AB" L660438, parsed from
// internal_settings.xml `<HitEffects>` by `v.wDa.parse` L1158). The camera
// hit-effect config the `ZAa`/`DL` latch picks by hit type. Field names map
// 1:1 to the JS fields (`type`/`YIa`/`jz`/`mva`/`nva`/`$za`/`aAa`).
struct HitEffect {
    std::string type;         // `type` — "CriticalHit" / "HeadHit" / "Shock"
    int pause_time = 0;       // `YIa` — PauseTime (frames)
    int effect_time = 0;      // `jz`  — EffectTime (frames)
    float amplitude_x = 0.0f; // `mva` — AmplitudeX
    float amplitude_y = 0.0f; // `nva` — AmplitudeY
    float frequency_x = 0.0f; // `$za` — FrequencyX
    float frequency_y = 0.0f; // `aAa` — FrequencyY
};

// `<RatingEvaluation>` settings table (JS `xc.Akb` L820-821 -> `xc.t$` rows,
// row class `$u` g="164"). PHASE 1 stores the table only; the `xc.JBa`
// (L812-815) weighted summation is phase 2.
//
// One `<Attribute>` child (JS `Ef` g="163", `Ef.Iia` L802): Name + Shift.
struct RatingAttribute {
    std::string name;    // `Ef.name` (Name)
    float shift = 0.0f;  // `Ef.shift` (Shift; `u.H(...,0)`)
};

// One `<Defense>` child (JS `Wm` g="161", `Wm.Iia` L801): Name + Weight +
// CancellingItem + its own `<Attribute>` list (`Ef.EIa`).
struct RatingDefense {
    std::string attr_name;       // `Wm.attrName` (Name)
    float weight = 0.0f;         // `Wm.weight` (Weight; `u.H(...,0)`)
    std::string cancelling_item; // `Wm.hI` (CancellingItem)
    std::vector<RatingAttribute> attributes;  // `Ef.EIa` (`<Attribute>`)
};

// One `<RatingEvaluation><Damage>` row (JS `$u` g="164").
struct RatingDamageRow {
    std::string node_name;  // `nodeName` (child element name; shipped "Damage")
    std::string attr_name;  // `attrName` (Name: Weapon/Unarmed/Ranged/Magic)
    float average_quantity = 0.0f;     // `hYa` (AverageQuantity; `Ef.Pib`)
    float average_base_damage = 0.0f;  // `Kva` (AverageBaseDamage; `Ef.Oib`)
    float recharge_rate = 0.0f;        // `gmb` (RechargeRate; `Ef.Oib`)
    float magic_recharge_rate = 0.0f;  // `xha` (MagicRechargeRate; `Ef.Oib`)
    std::string cancelling_item;       // `hI` (CancellingItem)
    std::vector<RatingAttribute> attributes;  // `Ef.EIa` (`<Attribute>`)
    std::vector<RatingDefense> defenses;      // `KP` (`Wm.Hia` L801)
};

struct FightParams {
    std::string block_damage_attr = "BlockDamageFactor";
    float block_damage_base = 0.0001f;
    std::string crit_damage_attr = "CriticalDamage";
    float crit_damage_base = 0.0001f;
    // `v.gya` = `<CriticalHit><Probability Base Attribute/>` (L1157) — the
    // crit-chance row read by `A9a`/`p8a` (L529): Base * attr when present.
    std::string crit_chance_attr = "CriticalChance";
    float crit_chance_base = 0.0001f;
    // `v.kha` = `<Lifesteal Attribute Base/>` (L1158, `Eh` L1180) — the
    // gear-lifesteal row `udb` (L403) reads.
    std::string lifesteal_attr = "Lifesteal";
    float lifesteal_base = 0.0001f;
    std::string block_defense_attr = "BodyDefense";
    float damage_doubling_range = 10.0f;  // BP
    // `v.lT` (L1156) = `<ResistanceDoublingRange Value>` — the `dl.A8a`
    // L1422 resistance doubling divisor (the fight.cpp A2 local used 500).
    float resistance_doubling_range = 500.0f;  // lT
    float damage_factor_base = 0.0001f;   // Ypa
    float damage_factor_max = 20000.0f;   // Zpa
    std::string damage_factor_attr = "DamageFactor";
    // `v.lNa` (L1154): `<SlowMotion Defense="BodyDefense"/>` — the shipped
    // value is "BodyDefense" (the static default is ""). The unblocked,
    // no-`<Defense>`, no-hit-capsule-Xi fallback in `LAa`.
    std::string slowmotion_defense = "BodyDefense";
    // Shock config (JS `hw` = `v.Ub`, parsed L1194-1196 from
    // internal_settings.xml `<Shock>` — values verified 2026-09-04).
    float shock_threshold = 999.0f;     // `Treshold.Value`
    float shock_frame_reduction = 0.001f;  // `FrameReduction.Value` (Xza)
    int shock_loosening_delay = 12;     // `LooseningDelay.Frames` (MFa)
    float shock_crit_base = 0.0001f;    // `CriticalHitChance.Base`
    float shock_head_base = 0.0001f;    // `HeadHitChance.Base`
    // `v.Qxa` (L1157): `<CounterPunches Value="50"/>` — the Punchbag hit
    // cadence `ca.Cgb` L396 tests against the defender's landed-hit counter
    // (`a.model.sI != v.Qxa`). Data-driven: `u.I(...,2)` is the JS fallback
    // (radix-10 parse, `2` when the attribute is absent/unparseable), so the
    // static default here is 2 and the shipped file resolves it to 50.
    int counter_punches = 2;
    // `v.nV` (L1157) = `u.I(a.A("Combo").attributes.get("MinHits"),3)` — the
    // `aw()` announce threshold the `iu` (`Vx`) combo tracker arms `Ui` at.
    int combo_min_hits = 3;
    // `v.Lpa` (L1157) = `u.I(a.A("Combo").attributes.get("Time"),90)` — the
    // `pCa()` frame budget `iu.wyb` counts `OV` against (the combo window).
    int combo_time = 90;
    // AlignTargetAttributes (JS `v.wv`): attribute name -> Align value.
    std::map<std::string, float> align_target_attributes;
    // `p.o.Yh` — the eclipse flag `v.eNa` (L1204) tests. The dojo has no
    // eclipse, so `OP==1` rows apply and `OP==0` rows are filtered out.
    bool eclipse = false;
    // NOTE `<ModifiedAlignFormula>`: `pAa` L1205 gates on `v.Seb.g6a(name)`,
    // but `$v` (L608763) is `constructor(){this.k8=[]} g6a(name){...}` with
    // NO `parse` and no caller — `v.Seb.k8` is always empty, so `g6a` always
    // returns null and the log-remap branch is DEAD in the shipped build
    // (`<ModifiedAlignFormula>` is also absent from the JS string table).
    // Not ported: implementing it would invent behaviour the game never runs.
    // Magic charge tables (JS `v.jA` = settings `<Magic>`, Yv rows:
    // InitialCharge/PainRecharge/DamageRecharge, Base=0.0001 + Mk attr).
    std::string magic_initial_attr = "MagicInitialCharge";
    float magic_initial_base = 0.0001f;
    std::string magic_pain_attr = "MagicPainRecharge";
    float magic_pain_base = 0.0001f;
    std::string magic_damage_attr = "MagicDamageRecharge";
    float magic_damage_base = 0.0001f;

    // `v.wDa` = internal_settings.xml `<HitEffects>` (JS `Vv` g="2AC",
    // parsed L1158 by `v.wDa.parse(a.A("HitEffects"))`). The shipped file
    // carries exactly three rows in document order: CriticalHit, HeadHit,
    // Shock (internal_settings.xml L556-558).
    std::vector<HitEffect> hit_effects;

    // `xc.gX` (L820) = `<RatingEvaluation PerkAspectParameter>` (shipped
    // "Aspect"). `ImpossibleRatio`/`EasyRatio` are read by `xc.Akb` and
    // DISCARDED (no assignment), so they are not stored.
    std::string rating_perk_aspect;
    // `xc.t$` (L820-821): the `<RatingEvaluation>` `<Damage>` rows in document
    // order (shipped: Weapon, Unarmed, Ranged, Magic).
    std::vector<RatingDamageRow> rating_table;
    // `v.CY` (JS `Gv` g="248", set by `v.Mib` L625685 from the settings
    // `<Aspect Antilimit DoublingRange Limit>`; shipped 0 / 108 / 1.2). The
    // `Be.eea` (L691464) perk-rating aspect curve reads these.
    float aspect_antilimit = 0.0f;       // `tva` (Antilimit)
    float aspect_doubling_range = 0.0f;  // `cda` (DoublingRange)
    float aspect_limit = 0.0f;           // `lha` (Limit)

    // The process-wide instance (JS `v` statics), populated at boot from
    // internal_settings.xml by `load_fight_params_from_settings`.
    static const FightParams& defaults();
};

// JS `ca.ZAa(a,b,c)` (L422): scans `v.wDa.sda` IN DOCUMENT ORDER and
// returns the first effect whose type matches an active flag —
// `a` = critical (`se`), `b` = unblocked head hit (`Uq && !block`),
// `c` = shock (`Ub`). Returns nullptr when none match.
const HitEffect* select_hit_effect(bool critical, bool head, bool shock);

// The mutable process-wide `FightParams` (JS `v`). `load_fight_params_from_settings`
// writes it; `FightParams::defaults()` reads it.
FightParams& fight_params();

// Populate `fight_params()` from the internal_settings.xml text (JS `v` parse
// L1154-1158): `<AlignTargetAttributes>` -> `v.wv`, `<BlockDefense Attribute>`
// -> `v.pYa`, `<SlowMotion Defense>` -> `v.lNa`, `<DamageFactor>` ->
// `ACa/zCa`, `<BlockDamageFactor>` -> `VY`, `<CriticalHit><Damage>` -> `HZ`,
// `<DamageDoublingRange Value>` -> `BP`, `<ModifiedAlignFormula>` -> `v.Seb`,
// `<Lifesteal Attribute/Base>` -> `v.kha`, `<Regeneration>` -> `v.Bja`.
// Malformed/absent nodes leave the shipped default in place.
void load_fight_params_from_settings(const std::string& xml_text);

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
    // QX: the ATTACK move's `TacticWeapon` split on '|' (JS `jc.Gsb` L800 ->
    // `e.da.Ua.QX`, passed to `bCa` L510 and tested by `c2a` L820 for
    // "Fists"). Empty for non-attack intervals.
    std::vector<std::string> qx;
    // `a.Cea(attackerIsPlayer?1:2).bp` (JS `Ul.Cea` L395 + `Vm.bp` L396).
    // 1.0 unless an active `ERuleDamageFactor` (`bn.Zk` L436) set the interval's
    // `Vm.bp` to its `Factor` for the attacker's side. Shipped `stages.xml`
    // carries 54 such rules (9 `THROWS_ONLY` blocks: `Throw` Factor=1,
    // `Punch`/`Kick`/`Weapon`/`Missile`/`Magic` Factor=0, ApplyTo=Player).
    float side_bp = 1.0f;
};

// The damage computation result (JS `wd.Bb` = `pu` L558).
struct HitRecord {
    float raw_damage = 0.0f;   // `bR` - from bCa
    float final_damage = 0.0f; // `Zi` - after the lethal check
    bool lethal = false;       // `Iza`
    bool blocked = false;      // `block`
    bool critical = false;     // `se`
    bool shock = false;        // `Ub`
    bool disarm = false;       // `Yi` (false for unarmed - Au==owned, L394)
    bool head_hit = false;     // `Uq`
    bool first_hit = false;    // `ep` (first landed hit of the round, !Dga)
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

// `kea`/`qea` (L536): 2^(attr*Bc) when the flag holds, else 1.
// Exported for the Jma magic recharge (`Hwa(2^e*c*b*a)` needs the same
// block/crit multis the damage path used).
float block_mult(const FighterParams& defender, bool blocked,
                 const FightParams& fp = FightParams::defaults());
float crit_mult(const FighterParams& attacker, bool critical,
                const FightParams& fp = FightParams::defaults());

// `jA.AQ(name, params)`: row Bc × attr(Mk) when the fighter carries the
// attribute, else Bc (magic Initial/Pain/Damage recharge table lookup).
inline float magic_aq(float base, const std::string& attr, const FighterParams& params) {
    if (!attr.empty() && params.has_attr(attr)) return base * params.attr(attr);
    return base;
}

// Ranged/magic state ops (JS `hZ`/`Hwa`/`LA`, lb==null branch):
// `hZ(n)` = zL(bh+n); `Hwa(v)` = bh==0 && yL(my+v) (my clamped [0,1]);
// `LA` = my>=1 converts to a bullet + reset (skipped under
// `ERuleNoBulletsReplenishment`), bullets cap at 1.
inline int bullets_add(int bh, int n) { return bh + n; }
inline double charge_add(int bh, double my, double v) {
    if (bh != 0) return my;
    double r = my + v;
    if (r > 1.0) r = 1.0;
    if (r < 0.0) r = 0.0;
    return r;
}
struct LaNorm {
    int bh = 0;
    double my = 0.0;
};
inline LaNorm la_normalize(int bh, double my, bool no_replenish) {
    if (my >= 1.0 && !no_replenish) {
        bh += 1;
        my = 0.0;
    }
    if (bh > 1) bh = 1;
    return LaNorm{bh, my};
}

// Jma recharge amount: `Hwa(2^e*c*b*Zi)`.
inline double magic_recharge(double e, double b, double c, double zi) {
    return std::pow(2.0, e) * b * c * zi;
}

// The critical chance (JS `v.gya.p8a` L605 + `wd.A9a` L529):
//   CriticalHitChance Base (0.0001) * the ATTACKER's CriticalChance attr.
// `force` overrides the RNG (demo: force a crit).
float crit_chance(const FighterParams& attacker,
                  const FightParams& fp = FightParams::defaults());

// ---------------------------------------------------------------------------
// Shock / pain / disarm (JS `wd` L490/L517-528 + `R8a` L531-532)
// ---------------------------------------------------------------------------

// Per-fighter shock state (JS `wd` fields, init L490:
// `sr=0, vc=sn=false, Wx=-1, ws=false`).
struct ShockState {
    float pain_sr = 0.0f;    // `sr` - accumulates Zi, decays per frame
    bool shocked_vc = false;  // `vc` - shock/disarm latch (vetoes re-shock)
    bool disarm_sn = false;   // `sn` - disarm latch (no re-arm while set)
    int weapon_wx = -1;       // `Wx` - pickup timer, frames (-1 = idle)
    bool weapon_ws = false;   // `ws` - weapon strike (adds 0 pain; ola OPEN)
};

// JS `Orb(a)` (L517): `sr+=a; return !vc && sr>threshold`.
inline bool orb_hit(ShockState& st, float add, float threshold) {
    st.pain_sr += add;
    return !st.shocked_vc && st.pain_sr > threshold;
}

// JS `Pnb` (L528): `sr=max(sr-Xza,0)` decay every tick; the pickup timer
// `!vc&&Wx>=0&&(Wx==0&&Wqb(),Wx--)` — returns true exactly when `Wqb`
// (weapon pickup) must fire.
inline bool shock_tick(ShockState& st, float frame_reduction) {
    st.pain_sr = std::max(0.0f, st.pain_sr - frame_reduction);
    if (!st.shocked_vc && st.weapon_wx >= 0) {
        if (st.weapon_wx == 0) {
            st.weapon_wx = -1;
            return true;
        }
        --st.weapon_wx;
    }
    return false;
}

// JS `wd.R8a(attacker)` decider on the TARGET (L531-532), verbatim shape:
//   `ecb->true` (`ecb=false`, L2475); `vc->false`;
//   `b=Zi/atk.so`; `c=Orb(ws?0:b)`; `e=f=false`;
//   `se&&(e=a*b>RJa)`; `Uq&&!block&&(f=d*b>RJa)`; return `(c||f)?true:e`.
// `crit_term` = `iya*hya`-attribute, `head_term` = `pDa*oDa`-attribute
// (both `Base + attr` per the `p8a` pattern; OPEN exact formula).
// `crit_roll`/`head_roll` are `uf.RJa()` draws (port: the fight stream).
// The decomposition mirrors combat_golden.js `r8a()` (S8 vectors).
struct R8aOut {
    bool raw = false;    // return value: feeds BOTH `Bb.Ub` and `Bb.Yi`
    bool pain_c = false;  // `c` — Orb pain-shock
    bool crit_e = false;  // `e` — crit-shock term
    bool head_f = false;  // `f` — head-shock term
};
inline R8aOut r8a_decide(bool ecb, bool target_vc, float zi_over_so,
                         bool pain_shock_c, float crit_term, bool se, float crit_roll,
                         float head_term, bool head_zone_uq, bool blocked, float head_roll) {
    R8aOut o;
    if (ecb) {
        o.raw = true;
        return o;
    }
    if (target_vc) return o;
    o.pain_c = pain_shock_c;
    if (se) o.crit_e = crit_term * zi_over_so > crit_roll;
    if (head_zone_uq && !blocked) o.head_f = head_term * zi_over_so > head_roll;
    o.raw = (o.pain_c || o.head_f) ? true : o.crit_e;
    return o;
}

// ---------------------------------------------------------------------------
// RatingEvaluation arithmetic (JS `xc.JBa` L812-815, `dl.A8a` L1421-1422,
// `dl.Gz` L1423, `dl.k5a`/`j5a` L1428, `v.OAa` L1219).
//
// The map difficulty (`Wc` `DifficultyEvaluation`, screens.cpp) is the rating
// RATIO `d/c * 2^((q-r)*ACa) * k/h * 2^(2*(w4+jVa)/BP)` (`dl.A8a` L1422),
// where `c`/`d` are the two sides' ratings. Each side's rating is the
// `xc.JBa` weighted sum over the `<RatingEvaluation>` `<Damage>` rows:
//   per row:  h = Kva (AverageBaseDamage)
//             per `<Defense>` D: F = min(1, h * iea(self.qb, self, other,
//                                              rowAttrs+sideAttrs, D.attr[0]))
//                                B += D.weight * F
//   `c += B`; a row with `xha` (MagicRechargeRate) scales B by
//   `xha * (X7a()*yBa(self) + k6a()*JAa(self))` (`v.jA`, L815).
//
// PORTED: `zBa`, `mDa` (cancelling item), `iWa`/`msb`/`nsb`, the defense
// weighted sum + `iea` (`balance_multiplier`), the `xha` magic branch, the
// `A8a`/`Gz` formula, the `qAa` side split, the perk `<Rating>` Me/Enemy loops
// (`xc.Wk` items' `x4`, perks.xml `<Rating Player=..>`) and the `xc.gX`
// PerkAspect branch (`Be.eea` + `v.CY` `<Aspect>` config + `oma`/`gy`).
// NOT PORTED (unported subsystems — see damage.cpp OPEN): `v.cw()`/`v.EQ()`/
// `v.Wka`/`Fm`/`Bua` (the warrior-from-save model) and the perk-EQUIP mapping
// (`AK` from the save's `<Perks>` against perks.xml) — so `FighterParams::perks`
// is empty unless a caller fills it. The shipped fresh save has NO `<Rating>`
// perk equipped, so the loops are EMPTY there and the sum is exact.

// One `Ba` (name, value) pair (JS `Ba` L112): the merged attribute list
// `JBa` builds (`iWa`/`msb`) and the `k5a`/`j5a` side list.
struct RatingAttrPair {
    std::string first;    // attribute name
    float second = 0.0f;  // value
};

// `xc.zBa(name)` (L812): the attribute value, or -FLT_MAX when absent.
float rating_attribute(const FighterParams& w, const std::string& name);

// `xc.mDa(name)` (L812): true when `name` is one of the warrior's equipment
// model names (`jt()`); a cancelling item skips the row/defense. An empty
// `item` (a null `hI`) never cancels.
bool rating_cancelled(const FighterParams& w, const std::string& item);

// `xc.JBa(other, attrs)` (L812-815): the per-warrior weighted rating sum over
// the `<RatingEvaluation>` rows. `attrs` is the `k5a`/`j5a` side list. The
// `nsb` step mutates the OTHER warrior's attributes, so `other` is copied.
// The perk `<Rating>` loops (L414665 Me / L415034 Enemy) are ported: the
// SELF's `<Rating Player="Me">` entries scale each Defense's `F`, the OTHER's
// `<Rating Player="Enemy">` entries divide it.
float warrior_rating(const FighterParams& self, const FighterParams& other,
                     const std::vector<RatingAttrPair>& attrs,
                     const FightParams& fp = FightParams::defaults());

// `Be.eea(x)` (L691464): the perk-rating aspect curve, `v.CY` config
// (shipped Antilimit=0, DoublingRange=108, Limit=1.2).
//   `x>=0` -> Limit - (Limit-1)*2^(-x/DoublingRange)
//   `x<0`  -> Antilimit + 2^(x/DoublingRange)
inline float aspect_curve(float x,
                          const FightParams& fp = FightParams::defaults()) {
    if (x >= 0.0f) {
        return fp.aspect_limit -
               (fp.aspect_limit - 1.0f) *
                   std::pow(2.0f, -x / fp.aspect_doubling_range);
    }
    return fp.aspect_antilimit + std::pow(2.0f, x / fp.aspect_doubling_range);
}

// `Be.parse` + `Jw.parse` (L681500 / L703284) on one `<Perk>` document:
// `<Set>` -> `set`, each `<RatingEvaluation><Rating>` -> `ratings`, with the
// `_`-prefix value substitution against the perk's own `<Set>` map. Returns
// an empty model on a malformed document.
//
// `set_override` is the item-enchant `Be.clone(set, rating)` (L1329-1330:
// `c=d.A("Set"); ... c.set(f[0],f[1])`): the save's `<Enchantments><Perk
// Name><Set ...>` attributes are written OVER the def's own `<Set>` (the
// merged map then feeds the `_`-substitution AND the `xc.gX` PerkAspect
// lookup `perk_aspect`). Empty = the plain def.
PerkModel parse_perk_xml(
    const std::string& perk_xml,
    const std::map<std::string, std::string>& set_override = {});

// `v.Rg.jn(name)` + `Be.clone` (L1328-1330) over a whole perks.xml document:
// find the `<Perks><Perk Name=name>` def, merge `set_override` over its
// `<Set>`, and parse it. Empty model when the def is absent/malformed.
PerkModel parse_perk_def(
    const std::string& perks_xml, const std::string& name,
    const std::map<std::string, std::string>& set_override = {});

// `--rating-perk-probe`: JS-exact before/after of the `xc.JBa` perk
// `<Rating>`/`<Aspect>` branch. True on pass.
bool rating_perk_probe();

// The `ERuleRatingEvaluation` inputs (`eVa`/`yUa`/`jVa`, JS `qn` L881).
struct RatingRule {
    float player_rating = 0.0f;      // eVa (PlayerRating)
    float enemy_rating = 0.0f;       // yUa (EnemyRating)
    float rating_correction = 0.0f;  // jVa (RatingCorrection)
    bool present = false;            // `z8a() != null`
};

// `dl.A8a(a,b)` + `dl.Gz` (L1421-1423): the fight rating ratio. `a` = the
// player, `b` = the enemy (the LAST warrior of `v.EQ(fight.Xs)`).
// `side1_attrs` = `k5a()`, `side2_attrs` = `j5a()`.
// `resistances` = the active `ERuleResistance` rows as (vX, save-value) pairs;
// `A8a` reads `x = z.vX` (the rule) and `z = p.o.Pw.c0(z.eta)` (the save).
float rating_ratio(const FighterParams& a, const FighterParams& b,
                   const RatingRule& rule,
                   const std::vector<RatingAttrPair>& side1_attrs,
                   const std::vector<RatingAttrPair>& side2_attrs,
                   const std::vector<std::pair<float, float>>& resistances,
                   const FightParams& fp = FightParams::defaults());

// One `ERuleAttributes` rule for the `dl.qAa` (L1428) split.
struct RatingSideRule {
    int apply_to = 3;                  // `mc()` (1 Player / 2 Bot / 3 All)
    std::map<std::string, int> attrs;  // the `hea()` attribute map
};

// `dl.qAa(side)` (L1428): `side==1` (`k5a`) takes the NON-Defense attrs of
// Player/All rules (and the Defense attrs of Bot rules); `side==2` (`j5a`)
// the mirror. `Cb(name,"Defense")` = `name` contains "Defense".
std::vector<RatingAttrPair> rating_side_attrs(
    const std::vector<RatingSideRule>& rules, int side);

}  // namespace sf2::scene
