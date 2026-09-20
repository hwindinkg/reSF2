// Damage formula (`wd.bCa` L513-514) + hit application (`ca.Cgb` L394-397).
// Ported term-for-term from sf2.502f0946.js — line refs in README.md.

#include "scene/damage.hpp"

#include <algorithm>
#include <cmath>

#include "xml_doc.hpp"

namespace sf2::scene {

namespace {

// JS `IAa(a, Mk, Bc)` (L512): if `a` (flag) -> 2^(attr(Mk) * Bc), else 1.
float attr_exp(const FighterParams& who, const std::string& attr, float base,
               bool flag) {
    if (!flag) return 1.0f;
    return std::pow(2.0f, who.attr(attr) * base);
}

// JS `Bh.Gb(v.wv, name)` (L1165): the AlignTargetAttributes lookup (0 if
// the attribute is not in the align list).
float align_value(const FightParams& fp, const std::string& name) {
    const auto it = fp.align_target_attributes.find(name);
    return it != fp.align_target_attributes.end() ? it->second : 0.0f;
}

// JS `Ci.x7a` + `Ci.a5a` (L800): `x7a` = the max `Priority` in the list;
// `a5a(a,b)` copies only the rows carrying that priority into `b`.
std::vector<AlignDelta> max_priority_deltas(const std::vector<AlignDelta>& in) {
    std::vector<AlignDelta> out;
    if (in.empty()) return out;
    int best = in.front().priority;
    for (const AlignDelta& d : in) best = std::max(best, d.priority);
    for (const AlignDelta& d : in) {
        if (d.priority == best) out.push_back(d);
    }
    return out;
}

// JS `v.eNa(a)` (L1204): true = the row is FILTERED OUT. `b = p.o.Yh` is the
// eclipse flag: `!(b&&OP==0 || !b&&OP==1 || OP==2)`. So outside an eclipse
// the participating rows are OP==2 and OP==1; inside, OP==2 and OP==0.
bool eclipse_filtered(const AlignDelta& d, bool eclipse) {
    const bool in_eclipse_armor = eclipse && d.eclipse_op == 0;
    const bool out_eclipse_armor = !eclipse && d.eclipse_op == 1;
    const bool always = d.eclipse_op == 2;
    return !(in_eclipse_armor || out_eclipse_armor || always);
}

// JS `v.pAa(a,b,c,d,e,f,g,h)` (L1204-1205) + `v.l5a(t)` (L1206): the balance
// multiplier, verbatim.
//   a = attacker.qb, b = attacker params, c = defender params,
//   d = the interval's SZ (attack attrs), e = the defense attr name.
//   k = align(e); l = -k; e := defender.attr(e)  (`<Damage>` `Value` term)
//   x = Ci.a5a((a ? c : b).IY)                   (max-Priority subset)
//   per attack attr (name C, shift S):
//     B = attacker.attr(C) + S ; A = align(C)
//     W = min (a) / max (!a) over the kept rows of
//         (B-e)*(1-Q) + (A-k)*Q  -/+ M   (Q = row.bp, M = row.shift)
//     t = max(t, W) ; l = A - k
//   return t ; `iea` retries once when t > 10 then `l5a` = 2^(t/BP).
// The `v.Seb.g6a(C)` log-remap between the two is DEAD in the shipped build
// (`$v`/`v.Seb` has no `parse`, so `k8` is always empty) — see damage.hpp.
// The caller (`iea`) discards the `g`/`h` out-params (`n`/`q`), so they are
// not carried here.
float balance_multiplier(const FighterParams& attacker,
                         const FighterParams& defender,
                         const IntervalDamage& interval,
                         const std::string& defense_attr,
                         const FightParams& fp) {
    const float k = align_value(fp, defense_attr);
    const float e = defender.attr(defense_attr);
    const std::vector<AlignDelta> x = max_priority_deltas(
        attacker.is_player ? defender.iy : attacker.iy);

    float t = -3.4028234663852886e38f;  // -FLT_MAX
    for (const auto& ad : interval.attack_attrs) {
        const float B = attacker.attr(ad.first) + ad.second;
        const float A = align_value(fp, ad.first);
        float W;
        if (attacker.is_player) {
            W = 3.4028234663852886e38f;  // +FLT_MAX (min over the rows)
            for (const AlignDelta& m : x) {
                if (eclipse_filtered(m, fp.eclipse)) continue;
                const float U = (B - e) * (1.0f - m.bp) + (A - k) * m.bp - m.shift;
                if (U < W) W = U;
            }
        } else {
            W = -3.4028234663852886e38f;  // -FLT_MAX (max over the rows)
            for (const AlignDelta& m : x) {
                if (eclipse_filtered(m, fp.eclipse)) continue;
                const float U = (B - e) * (1.0f - m.bp) + (A - k) * m.bp + m.shift;
                if (W < U) W = U;
            }
        }
        if (t < W) t = W;
    }
    // No `SZ` entries (3 shipped blocks): the JS loop never runs, `t` stays
    // -FLT_MAX and `l5a` = 2^(-FLT_MAX/BP) = 0 — i.e. the hit deals no
    // damage. Deliberately NOT special-cased to 1.

    // `iea` (L1205-1206): retry once when t > 10, then `l5a` = 2^(t/BP).
    return std::pow(2.0f, t / fp.damage_doubling_range);
}

}  // namespace

FightParams& fight_params() {
    static FightParams params;
    return params;
}

const FightParams& FightParams::defaults() { return fight_params(); }

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

    // g = attacker.c2a(interval.qx, g) (L514 -> L820): `c2a(a,b)` returns
    // `b * M_` when the ATTACK MOVE's `QX` list (`TacticWeapon` split on
    // '|', `jc.Gsb` L800) contains "Fists"; otherwise `b`. The old port
    // tested the DEFENSE attribute name — the wrong operand.
    if (std::find(interval.qx.begin(), interval.qx.end(), "Fists") !=
        interval.qx.end()) {
        g *= attacker.m_;
    }

    // g *= a.Cea(attackerIsPlayer ? 1 : 2).bp — the interval's per-side
    // multiplier (`Vm.bp`, default 1). `a.Cea` (L395) selects `k$` for side 1
    // and `FV` for side 2; `bCa` asks for the ATTACKER's side. The value is
    // resolved per round by the `ERuleDamageFactor` rule (`bn.Zk` L436, via
    // `FightController::rules_apply_round_effects`) and carried on
    // `IntervalDamage::side_bp` (the port's `Ul.Cea(side).bp` analog).
    g *= interval.side_bp;

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
    // `v.gya` = the CriticalHit/Probability row (Base=0.0001, Attribute=
    // "CriticalChance", internal_settings L487-488) — `p8a` (L529): if the
    // attr exists -> base * value, else base.
    if (!fp.crit_chance_attr.empty() && attacker.has_attr(fp.crit_chance_attr)) {
        return fp.crit_chance_base * attacker.attr(fp.crit_chance_attr);
    }
    return fp.crit_chance_base;
}

// ---------------------------------------------------------------------------
// internal_settings.xml -> fight_params() (JS `v` statics, parse L1154-1158)
// ---------------------------------------------------------------------------
namespace {

// JS `Eh.parse` (L1180): `Mk = Attribute != null ? Attribute : "COM"`,
// `Bc = Base`. `v.VY`/`v.HZ`/`v.kha`/`v.Bja`/`v.gya` all use this shape.
void parse_eh(const pugi::xml_node n, std::string& attr, float& base) {
    if (!n) return;
    const char* a = n.attribute("Attribute").value();
    attr = (a != nullptr && *a != '\0') ? a : "COM";
    if (n.attribute("Base")) base = n.attribute("Base").as_float();
}

// JS `hw.parse` (L1194-1196) — the `<Shock>` block.
void parse_shock(const pugi::xml_node n, FightParams& v) {
    if (!n) return;
    if (n.child("Treshold")) v.shock_threshold = n.child("Treshold").attribute("Value").as_float();
    if (n.child("FrameReduction"))
        v.shock_frame_reduction = n.child("FrameReduction").attribute("Value").as_float();
    if (n.child("LooseningDelay"))
        v.shock_loosening_delay = n.child("LooseningDelay").attribute("Frames").as_int();
    const pugi::xml_node chc = n.child("CriticalHitChance");
    if (chc.attribute("Base")) v.shock_crit_base = chc.attribute("Base").as_float();
    const pugi::xml_node hhc = n.child("HeadHitChance");
    if (hhc.attribute("Base")) v.shock_head_base = hhc.attribute("Base").as_float();
}

// JS `Yv` (`v.jA`, `<Magic>`, L1158) — the three recharge rows.
void parse_magic(const pugi::xml_node n, FightParams& v) {
    if (!n) return;
    parse_eh(n.child("InitialCharge"), v.magic_initial_attr, v.magic_initial_base);
    parse_eh(n.child("PainRecharge"), v.magic_pain_attr, v.magic_pain_base);
    parse_eh(n.child("DamageRecharge"), v.magic_damage_attr, v.magic_damage_base);
}

}  // namespace

void load_fight_params_from_settings(const std::string& xml_text) {
    if (xml_text.empty()) return;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml_text.data(), xml_text.size())) return;
    const pugi::xml_node root = doc.document_element();
    if (!root) return;
    FightParams& v = fight_params();

    // `v.wv` (L1157): `<AlignTargetAttributes><Attribute Name Value/>`.
    if (const pugi::xml_node al = root.child("AlignTargetAttributes")) {
        v.align_target_attributes.clear();
        for (const pugi::xml_node a : al.children("Attribute")) {
            const char* nm = a.attribute("Name").value();
            if (nm != nullptr && *nm != '\0') {
                v.align_target_attributes[nm] = a.attribute("Value").as_float();
            }
        }
    }
    // `v.pYa` (L1156) = `<BlockDefense Attribute>`.
    if (const pugi::xml_node bd = root.child("BlockDefense")) {
        const char* a = bd.attribute("Attribute").value();
        if (a != nullptr) v.block_defense_attr = a;
    }
    // `v.lNa` (L1154) = `<SlowMotion Defense>`.
    if (const pugi::xml_node sm = root.child("SlowMotion")) {
        const char* d = sm.attribute("Defense").value();
        if (d != nullptr) v.slowmotion_defense = d;
    }
    // `v.BP` (L1156) = `<DamageDoublingRange Value>`.
    if (const pugi::xml_node bp = root.child("DamageDoublingRange")) {
        if (bp.attribute("Value")) v.damage_doubling_range = bp.attribute("Value").as_float();
    }
    // `v.ACa/zCa/E9a` (L1155) = `<DamageFactor Base Attribute MaxValue>`.
    if (const pugi::xml_node df = root.child("DamageFactor")) {
        if (df.attribute("Base")) v.damage_factor_base = df.attribute("Base").as_float();
        const char* a = df.attribute("Attribute").value();
        if (a != nullptr && *a != '\0') v.damage_factor_attr = a;
        if (df.attribute("MaxValue")) v.damage_factor_max = df.attribute("MaxValue").as_float();
    }
    // `v.VY` (L1156) = `<BlockDamageFactor Base Attribute>`.
    parse_eh(root.child("BlockDamageFactor"), v.block_damage_attr, v.block_damage_base);
    // `v.HZ` (L1156) = `<CriticalHit><Damage Base Attribute>`.
    parse_eh(root.child("CriticalHit").child("Damage"), v.crit_damage_attr,
             v.crit_damage_base);
    // `v.gya` (L1157) = `<CriticalHit><Probability Base Attribute>`.
    parse_eh(root.child("CriticalHit").child("Probability"), v.crit_chance_attr,
             v.crit_chance_base);
    // `v.kha` (L1158) = `<Lifesteal Base Attribute>`.
    parse_eh(root.child("Lifesteal"), v.lifesteal_attr, v.lifesteal_base);
    // `v.Qxa` (L1157) = `u.I(a.A("CounterPunches").attributes.get("Value"),2)`
    // — the Punchbag reaction cadence. `as_int(2)` is the `u.I(...,2)` fallback.
    if (const pugi::xml_node cp = root.child("CounterPunches")) {
        v.counter_punches = cp.attribute("Value").as_int(2);
    }
    // `v.nV`/`v.Lpa` (L1157) = `<Combo MinHits="3" Time="90"/>` — the `iu`
    // (`Vx`) combo tracker: `wyb` resets the run once `OV > pCa()` (= Time)
    // frames pass with no landed hit; `wgb` arms the announce at `aw()`
    // (= MinHits). Values verified 2026-09-20 against
    // reference/extracted/xml/res/internal_settings.xml `<Combo>`.
    if (const pugi::xml_node cb = root.child("Combo")) {
        v.combo_min_hits = cb.attribute("MinHits").as_int(3);
        v.combo_time = cb.attribute("Time").as_int(90);
    }
    std::fprintf(stdout, "[fx] Combo MinHits=%d Time=%d (shipped XML)\n",
                 v.combo_min_hits, v.combo_time);
    std::fflush(stdout);
    // Diagnostic (boot, once): the shipped value vs the JS `u.I(...,2)` fallback.
    std::fprintf(stdout, "[fx] CounterPunches=%d (shipped XML; fallback=2)\n",
                 v.counter_punches);
    std::fflush(stdout);
    // `v.Ub` (L1158) = `<Shock>`.
    parse_shock(root.child("Shock"), v);
    // `v.jA` (L1158) = `<Magic>`.
    parse_magic(root.child("Magic"), v);
    // `v.wDa` (L1158) = `<HitEffects>`: every `<HitEffect Type PauseTime
    // EffectTime AmplitudeX FrequencyX AmplitudeY FrequencyY/>` (JS `em`
    // L660438 `parse`). Document order is load-bearing: `ZAa` returns the
    // first match, so CriticalHit shadows HeadHit/Shock when both fire.
    if (const pugi::xml_node he = root.child("HitEffects")) {
        v.hit_effects.clear();
        for (const pugi::xml_node e : he.children("HitEffect")) {
            HitEffect hef;
            const char* t = e.attribute("Type").value();
            hef.type = t != nullptr ? t : "";
            hef.pause_time = e.attribute("PauseTime").as_int(0);
            hef.effect_time = e.attribute("EffectTime").as_int(0);
            hef.amplitude_x = e.attribute("AmplitudeX").as_float(0.0f);
            hef.amplitude_y = e.attribute("AmplitudeY").as_float(0.0f);
            hef.frequency_x = e.attribute("FrequencyX").as_float(0.0f);
            hef.frequency_y = e.attribute("FrequencyY").as_float(0.0f);
            v.hit_effects.push_back(hef);
        }
        // Diagnostic (boot, once): the parsed per-type hit-effect table —
        // the values `ZAa`/`DL` drive (pause = hit-stop frames, effect =
        // judder frames, amp/freq = the `d3a` sinusoid).
        for (const HitEffect& e : v.hit_effects) {
            std::fprintf(stdout,
                         "[fx] HitEffects %s pause=%d effect=%d "
                         "ampX=%.2f freqX=%.2f ampY=%.2f freqY=%.2f\n",
                         e.type.c_str(), e.pause_time, e.effect_time,
                         e.amplitude_x, e.frequency_x, e.amplitude_y,
                         e.frequency_y);
        }
        std::fflush(stdout);
    }
}

// JS `ca.ZAa(a,b,c)` (L422): `for(f of v.wDa.sda) if(c&&f.type=="Shock"||
// a&&f.type=="CriticalHit"||b&&f.type=="HeadHit") return f; return null`.
const HitEffect* select_hit_effect(bool critical, bool head, bool shock) {
    const FightParams& v = fight_params();
    for (const HitEffect& f : v.hit_effects) {
        if ((shock && f.type == "Shock") ||
            (critical && f.type == "CriticalHit") ||
            (head && f.type == "HeadHit")) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace sf2::scene
