// Damage formula (`wd.bCa` L513-514) + hit application (`ca.Cgb` L394-397).
// Ported term-for-term from sf2.502f0946.js — line refs in README.md.

#include "scene/damage.hpp"

#include <algorithm>
#include <cctype>
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

// JS `Ef.EIa` (L802) -> `Ef.Iia(a,b,"Attribute")`: the direct
// `<Attribute Name Shift>` children of a rating node.
void parse_rating_attrs(const pugi::xml_node n,
                        std::vector<RatingAttribute>& out) {
    for (const pugi::xml_node a : n.children("Attribute")) {
        RatingAttribute ra;
        const char* nm = a.attribute("Name").value();
        if (nm != nullptr) ra.name = nm;
        ra.shift = a.attribute("Shift").as_float(0.0f);
        out.push_back(std::move(ra));
    }
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
    // `v.lT` (L1156) = `<ResistanceDoublingRange Value>` (the `dl.A8a` L1422
    // resistance divisor). The static fallback is 500 (the pre-existing A2
    // local); `u.H` leaves the value unset when the node is absent.
    if (const pugi::xml_node lt = root.child("ResistanceDoublingRange")) {
        if (lt.attribute("Value")) v.resistance_doubling_range = lt.attribute("Value").as_float();
    }
    // `v.Mib` (L625685): `<Aspect Antilimit DoublingRange Limit>` -> `v.CY`
    // (the `Be.eea` perk-rating aspect curve). Shipped 0 / 108 / 1.2.
    if (const pugi::xml_node asp = root.child("Aspect")) {
        v.aspect_antilimit = asp.attribute("Antilimit").as_float(0.0f);
        v.aspect_doubling_range = asp.attribute("DoublingRange").as_float(0.0f);
        v.aspect_limit = asp.attribute("Limit").as_float(0.0f);
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
    // `xc.Akb` (L820-821): `<RatingEvaluation PerkAspectParameter>` ->
    // `xc.gX` + the `xc.t$` `<Damage>` rows. Each row: nodeName/attrName, its
    // `<Attribute>` shifts (`Ef.EIa`), the `Ef.Pib`/`Ef.Oib` averages, the
    // `CancellingItem`, and its `<Defense>` rows (`Wm.Hia`).
    if (const pugi::xml_node re = root.child("RatingEvaluation")) {
        const char* pa = re.attribute("PerkAspectParameter").value();
        v.rating_perk_aspect = pa != nullptr ? pa : "";
        v.rating_table.clear();
        for (const pugi::xml_node d : re.children()) {
            RatingDamageRow row;
            row.node_name = d.name();
            const char* nm = d.attribute("Name").value();
            if (nm != nullptr) row.attr_name = nm;
            parse_rating_attrs(d, row.attributes);
            row.average_quantity = d.attribute("AverageQuantity").as_float(0.0f);
            row.average_base_damage = d.attribute("AverageBaseDamage").as_float(0.0f);
            row.recharge_rate = d.attribute("RechargeRate").as_float(0.0f);
            row.magic_recharge_rate = d.attribute("MagicRechargeRate").as_float(0.0f);
            const char* ci = d.attribute("CancellingItem").value();
            if (ci != nullptr) row.cancelling_item = ci;
            for (const pugi::xml_node df : d.children("Defense")) {
                RatingDefense def;
                const char* dn = df.attribute("Name").value();
                if (dn != nullptr) def.attr_name = dn;
                def.weight = df.attribute("Weight").as_float(0.0f);
                const char* dc = df.attribute("CancellingItem").value();
                if (dc != nullptr) def.cancelling_item = dc;
                parse_rating_attrs(df, def.attributes);
                row.defenses.push_back(std::move(def));
            }
            v.rating_table.push_back(std::move(row));
        }
        // Diagnostic (boot, once): the parsed table census (phase-1 evidence).
        std::fprintf(stdout, "[fx] RatingEvaluation PerkAspectParameter=%s rows=%d\n",
                     v.rating_perk_aspect.c_str(),
                     static_cast<int>(v.rating_table.size()));
        for (const RatingDamageRow& r : v.rating_table) {
            std::fprintf(stdout,
                         "[fx]   Rating row %s Name=%s avgBase=%.4f avgQty=%.4f "
                         "recharge=%.4f magicRecharge=%.4f cancel=%s attrs=%d defs=%d\n",
                         r.node_name.c_str(), r.attr_name.c_str(),
                         r.average_base_damage, r.average_quantity,
                         r.recharge_rate, r.magic_recharge_rate,
                         r.cancelling_item.c_str(),
                         static_cast<int>(r.attributes.size()),
                         static_cast<int>(r.defenses.size()));
            for (const RatingDefense& df : r.defenses) {
                std::fprintf(stdout,
                             "[fx]     Defense %s weight=%.4f cancel=%s attrs=%d\n",
                             df.attr_name.c_str(), df.weight,
                             df.cancelling_item.c_str(),
                             static_cast<int>(df.attributes.size()));
            }
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

// ---------------------------------------------------------------------------
// RatingEvaluation arithmetic (JS `xc.JBa` L812-815, `dl.A8a` L1421-1422,
// `dl.Gz` L1423, `dl.k5a`/`j5a` L1428).
// ---------------------------------------------------------------------------

namespace {

// `xc.msb` (L815): for each (name, val) in `b`, add val to the FIRST matching
// entry of `a`.
void rating_merge(std::vector<RatingAttrPair>& a,
                  const std::vector<RatingAttrPair>& b) {
    for (const RatingAttrPair& p : b) {
        for (RatingAttrPair& e : a) {
            if (e.first == p.first) {
                e.second += p.second;
                break;
            }
        }
    }
}

// `u.H(a,b=0)` (L1263017): parse a float; absent/empty/NaN -> `def`. (The
// JS `gy` (L2894) throws 18 on a non-numeric value instead; the shipped
// first-`<Set>` `Aspect` values are all numeric, so that guard is unreachable
// and the `u.H` default is the faithful in-range behaviour.)
float js_float(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    try {
        std::size_t used = 0;
        const float v = std::stof(s, &used);
        if (used != s.size()) return def;
        return v;
    } catch (const std::exception&) {
        return def;
    }
}

// `Jw` match test (L414690 / L415059): `Xb` empty (JS null/"") or == the row's
// Damage Name, AND `Xi` empty or == the Defense Name.
bool perk_rating_match(const PerkRating& z, const std::string& row_name,
                       const std::string& def_name) {
    if (!z.damage.empty() && z.damage != row_name) return false;
    return z.defense.empty() || z.defense == def_name;
}

// `r.oma(xc.gX)` + `gy` (L414976 / L415314): the perk `<Set>` value named by
// the settings `PerkAspectParameter` ("Aspect"), parsed as a float; 0 when the
// parameter name is unset or absent from the perk's `<Set>`.
float perk_aspect(const PerkModel& perk, const FightParams& fp) {
    if (fp.rating_perk_aspect.empty()) return 0.0f;
    const auto it = perk.set.find(fp.rating_perk_aspect);
    if (it == perk.set.end()) return 0.0f;
    // The `<Set>` value may be a `?Method[...]` expression
    // (`?RandomAspect[min,max]` / `?Aspect[expr]`), not a plain number: the JS
    // reads it through the `Qa` engine (`Wgb` off 688816 / `Ffb` off 689007),
    // so a bare `js_float` would collapse it to 0.
    const SetValueRuntime& rt = set_value_runtime();
    SetValueCtx ctx;
    ctx.fp = &fp;
    ctx.level = rt.level;                 // `p.o.bb()`
    ctx.is_raid = rt.is_raid;             // `?CurrentFight[].isRaid`
    ctx.is_player = rt.is_player;         // `?PlayerParameter[Me].isPlayer`
    ctx.default_perks_aspect = rt.default_perks_aspect;  // `wd.yV`
    ctx.me_attrs = rt.me_attrs;
    ctx.enemy_attrs = rt.enemy_attrs;
    ctx.rand01 = rt.rand01;               // `Da.pg.jf()`
    ctx.aspect_scale = [](int level) { return aspect_scale_for_level(level); };
    for (const auto& kv : perk.set) ctx.set_vals[kv.first] = js_float(kv.second);
    return static_cast<float>(eval_set_value(it->second, ctx));
}

// `xc.nsb` (L815): for each (name f, val e) in `attrs`, for every `<Defense>`
// row `<Attribute>` named f, `other.attr[f] = (other.attr[f] + e) | 0`.
void rating_nsb(FighterParams& other,
                const std::vector<RatingDefense>& defenses,
                const std::vector<RatingAttrPair>& attrs) {
    for (const RatingAttrPair& p : attrs) {
        for (const RatingDefense& def : defenses) {
            for (const RatingAttribute& da : def.attributes) {
                if (da.name != p.first) continue;
                const float cur = other.attr(p.first);
                other.attributes[p.first] =
                    static_cast<float>(static_cast<int>(cur + p.second));
            }
        }
    }
}

}  // namespace

namespace {

// Recursive-descent evaluator for the `Qa` value-expression subset the shipped
// perk `<Set>` values and trigger attributes use (JS `Qa.oh`). Mirrors the JS
// operand order; unknown methods fail (the caller falls back to `js_float`).
struct SetExpr {
    const std::string& s;
    const SetValueCtx& ctx;
    std::size_t pos = 0;

    void skip() {
        while (pos < s.size() &&
               std::isspace(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
    }
    bool eat(char c) {
        skip();
        if (pos < s.size() && s[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    std::optional<double> expr() {
        auto v = term();
        if (!v) return std::nullopt;
        for (;;) {
            if (eat('+')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v += *r;
            } else if (eat('-')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v -= *r;
            } else {
                return v;
            }
        }
    }
    std::optional<double> term() {
        auto v = factor();
        if (!v) return std::nullopt;
        for (;;) {
            if (eat('*')) {
                auto r = factor();
                if (!r) return std::nullopt;
                *v *= *r;
            } else if (eat('/')) {
                auto r = factor();
                if (!r || *r == 0.0) return std::nullopt;
                *v /= *r;
            } else {
                return v;
            }
        }
    }
    std::optional<double> factor() {
        skip();
        if (eat('-')) {
            auto v = factor();
            if (!v) return std::nullopt;
            return -*v;
        }
        if (eat('(')) {
            auto v = expr();
            if (!v || !eat(')')) return std::nullopt;
            return v;
        }
        if (pos < s.size() &&
            (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
            std::size_t len = 0;
            try {
                const double d = std::stod(s.substr(pos), &len);
                if (len == 0) return std::nullopt;
                pos += len;
                return d;
            } catch (...) {
                return std::nullopt;
            }
        }
        if (pos < s.size() && s[pos] == '?') return qref();
        if (pos < s.size() && s[pos] == '_') {
            // Bare `_X` -> the perk `<Set>` entry, parsed as a number.
            const std::size_t st = pos;
            while (pos < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[pos])) ||
                    s[pos] == '_')) {
                ++pos;
            }
            const auto it = ctx.set_vals.find(s.substr(st + 1, pos - st - 1));
            return it != ctx.set_vals.end() ? std::optional<double>(it->second)
                                            : std::nullopt;
        }
        return std::nullopt;
    }
    std::optional<double> qref() {
        ++pos;  // '?'
        std::string name;
        while (pos < s.size() &&
               (std::isalnum(static_cast<unsigned char>(s[pos])) ||
                s[pos] == '_')) {
            name += s[pos++];
        }
        skip();
        std::string arg;
        if (eat('[')) {
            std::size_t depth = 1;
            const std::size_t start = pos;
            while (pos < s.size() && depth > 0) {
                if (s[pos] == '[') ++depth;
                if (s[pos] == ']') --depth;
                ++pos;
            }
            if (depth != 0) return std::nullopt;
            arg = s.substr(start, pos - start - 1);
        }
        std::string field;
        if (eat('.')) {
            while (pos < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[pos])) ||
                    s[pos] == '_')) {
                field += s[pos++];
            }
        }
        if (name == "RandomAspect") {
            // `Wgb`: exactly two comma args `[min,max]`; else 0.
            const std::size_t comma = arg.find(',');
            if (comma == std::string::npos) return std::optional<double>(0.0);
            const int lo = static_cast<int>(js_float(arg.substr(0, comma)));
            const int hi =
                static_cast<int>(js_float(arg.substr(comma + 1))) + 1;
            double c = static_cast<double>(lo);
            const double r = ctx.rand01 ? ctx.rand01() : 0.0;
            c += static_cast<double>(static_cast<int>((hi - c) * r));
            c += ctx.aspect_scale ? ctx.aspect_scale(ctx.level) : 0.0;
            return c;
        }
        if (name == "Aspect") {
            // `Ffb`: exactly one arg; `eea(kc(expr))`.
            if (arg.find(',') != std::string::npos) return std::optional<double>(0.0);
            SetExpr inner{arg, ctx};
            double v = 0.0;
            auto r = inner.expr();
            inner.skip();
            if (r && inner.pos == arg.size()) v = *r;
            const FightParams& fp =
                ctx.fp != nullptr ? *ctx.fp : FightParams::defaults();
            return static_cast<double>(aspect_curve(static_cast<float>(v), fp));
        }
        if (name == "CurrentFight" && field == "isRaid") {
            return std::optional<double>(ctx.is_raid ? 1.0 : 0.0);
        }
        if (name == "PlayerParameter") {
            if (field == "isPlayer")
                return std::optional<double>(ctx.is_player ? 1.0 : 0.0);
            if (field == "DefaultPerksAspect")
                return std::optional<double>(ctx.default_perks_aspect);
            if (field == "DamageConverter")
                return std::optional<double>(ctx.damage_converter);
            if (field == "Level") return std::optional<double>(ctx.level);
            return std::nullopt;
        }
        if (name == "PlayerAttribute") {
            const auto& m = (arg == "Enemy") ? ctx.enemy_attrs : ctx.me_attrs;
            const auto it = m.find(field);
            return std::optional<double>(it != m.end() ? it->second : 0.0);
        }
        if (name == "Variable") {
            const auto it = ctx.vars.find(arg);
            return std::optional<double>(it != ctx.vars.end() ? it->second : 0.0);
        }
        if (name == "Abs") {
            SetExpr inner{arg, ctx};
            auto r = inner.expr();
            inner.skip();
            if (!r || inner.pos != arg.size()) return std::nullopt;
            return std::fabs(*r);
        }
        if (name == "Hit") {
            if (field == "Damage") return std::optional<double>(ctx.hit_damage);
            if (field == "BaseDamage")
                return std::optional<double>(ctx.hit_base_damage);
            return std::nullopt;
        }
        return std::nullopt;
    }
};

}  // namespace

double eval_set_value(const std::string& raw, const SetValueCtx& ctx) {
    if (raw.empty()) return 0.0;
    // A plain `_`-substituted value or a numeric prefix need not be an
    // expression; `js_float` is the faithful fallback (the JS `gy`/`ky` path).
    const unsigned char c0 = static_cast<unsigned char>(raw[0]);
    if (raw[0] != '?' && raw[0] != '(' && raw[0] != '-' && raw[0] != '_' &&
        raw[0] != '.' && !std::isdigit(c0)) {
        return js_float(raw);
    }
    SetExpr p{raw, ctx};
    auto v = p.expr();
    p.skip();
    if (v && p.pos == raw.size()) return *v;
    return js_float(raw);
}

float rating_attribute(const FighterParams& w, const std::string& name) {
    const auto it = w.attributes.find(name);
    return it != w.attributes.end() ? it->second : -3.4028234663852886e38f;
}

bool rating_cancelled(const FighterParams& w, const std::string& item) {
    if (item.empty()) return false;  // a null `hI` never cancels
    for (const std::string& n : w.equipment_names) {
        if (n == item) return true;
    }
    return false;
}

float warrior_rating(const FighterParams& self, const FighterParams& other,
                     const std::vector<RatingAttrPair>& attrs,
                     const FightParams& fp) {
    FighterParams other_mut = other;  // `nsb` mutates the OTHER warrior
    float c = 0.0f;
    for (const RatingDamageRow& row : fp.rating_table) {
        if (row.node_name != "Damage") continue;
        if (rating_cancelled(self, row.cancelling_item)) continue;
        const float h = row.average_base_damage;  // `Kva`
        // `d` = the row's own attributes (`iWa`) merged with the side list
        // (`msb`).
        std::vector<RatingAttrPair> d;
        d.reserve(row.attributes.size() + attrs.size());
        for (const RatingAttribute& ra : row.attributes) {
            d.push_back(RatingAttrPair{ra.name, ra.shift});
        }
        rating_merge(d, attrs);
        rating_nsb(other_mut, row.defenses, attrs);
        float B = 0.0f;
        for (const RatingDefense& def : row.defenses) {
            if (rating_cancelled(self, def.cancelling_item)) continue;
            if (def.attributes.empty()) continue;  // `D.attributes[0]`
            // `F = min(1, h * iea(self.qb, self, other, d, D.attr[0]))`.
            IntervalDamage iv;
            iv.attack_attrs.reserve(d.size());
            for (const RatingAttrPair& p : d) {
                iv.attack_attrs.emplace_back(p.first, p.second);
            }
            const float g = balance_multiplier(self, other_mut, iv,
                                               def.attributes[0].name, fp);
            float F = std::min(1.0f, h * g);
            // `xc.JBa` perk loops (L414665 Me / L415034 Enemy): the SELF's
            // `<Rating Player="Me">` entries scale F by
            // `1 + (ff-1)*eea(A - other.zBa(dQ))`; the OTHER's
            // `<Rating Player="Enemy">` entries divide F by the same factor.
            // `A` = the perk `<Set>` value named by the settings
            // `PerkAspectParameter` (0 when unset/absent).
            for (const PerkModel& perk : self.perks) {
                for (const PerkRating& z : perk.ratings) {
                    if (z.player != "Me") continue;
                    if (!perk_rating_match(z, row.attr_name,
                                           def.attributes[0].name))
                        continue;
                    F *= 1.0f + (z.multiplier - 1.0f) *
                                    aspect_curve(perk_aspect(perk, fp) -
                                                     rating_attribute(other_mut,
                                                                      z.enemy_attr),
                                                 fp);
                }
            }
            for (const PerkModel& perk : other_mut.perks) {
                for (const PerkRating& z : perk.ratings) {
                    if (z.player != "Enemy") continue;
                    if (!perk_rating_match(z, row.attr_name,
                                           def.attributes[0].name))
                        continue;
                    F /= 1.0f + (z.multiplier - 1.0f) *
                                    aspect_curve(perk_aspect(perk, fp) -
                                                     rating_attribute(other_mut,
                                                                      z.enemy_attr),
                                                 fp);
                }
            }
            B += def.weight * F;
        }
        // `g.xha > 0`: `B *= xha * (X7a()*yBa(self) + k6a()*JAa(self))` where
        // `X7a`/`k6a` are the Magic Pain/DamageRecharge bases and `yBa`/`JAa`
        // the base × the warrior's attr (`v.jA`, L1186).
        if (row.magic_recharge_rate > 0.0f) {
            const float pain = magic_aq(fp.magic_pain_base, fp.magic_pain_attr, self);
            const float dmg = magic_aq(fp.magic_damage_base, fp.magic_damage_attr, self);
            B *= row.magic_recharge_rate *
                 (fp.magic_pain_base * pain + fp.magic_damage_base * dmg);
        }
        c += B;
    }
    return c;
}

// `xc.Bua(list)` (JS L417872): add each `Ba` delta onto the fighter's attribute
// map. `get(e,f) && set(e, f.G + (d|0))` — `f` is a fresh `ja(0)`, so an absent
// attribute starts at 0; the `&&` short-circuit never fires because a `ja` is
// always truthy. ACCUMULATES, so a name listed by two rules gets both deltas.
static void apply_side_attrs(FighterParams& w,
                             const std::vector<RatingAttrPair>& list) {
    for (const RatingAttrPair& p : list) {
        const auto it = w.attributes.find(p.first);
        const float base = (it != w.attributes.end()) ? it->second : 0.0f;
        w.attributes[p.first] = base + static_cast<float>(static_cast<int>(p.second));
    }
}

float rating_ratio(const FighterParams& a, const FighterParams& b,
                   const RatingRule& rule,
                   const std::vector<RatingAttrPair>& side1_attrs,
                   const std::vector<RatingAttrPair>& side2_attrs,
                   const std::vector<std::pair<float, float>>& resistances,
                   const FightParams& fp) {
    float c = rule.present ? rule.player_rating : 0.0f;            // `eVa`
    float d = rule.present ? rule.enemy_rating : 0.0f;              // `yUa`
    const float e = rule.present ? rule.rating_correction : 0.0f;   // `jVa`
    if (c == 0.0f) {
        c = b.player_rating;  // `c==0 -> b.W3`
    } else if (c < 0.0f) {
        c = warrior_rating(a, b, side1_attrs, fp);  // `c<0 -> a.JBa(b,h)`
    }
    if (d == 0.0f) {
        d = b.enemy_rating;  // `d==0 -> b.C_`
    } else if (d < 0.0f) {
        d = warrior_rating(b, a, side2_attrs, fp);  // `d<0 -> b.JBa(a,k)`
    }
    // `t=b.clone(); x=a.clone(); t.Bua(k); x.Bua(h); t.attributes.get(n,q);
    // x.attributes.get(n,r)` (JS L728750-728752): the side lists are applied to
    // the CLONES first, so `q` (the enemy) and `r` (the player) already carry
    // their side's `<Attributes DamageFactor>` delta. Reading the bare units
    // (as this port did) left both at 0, making `2^((q-r)*l)` identically 1.
    FighterParams tb = b;  // `t` = the enemy clone
    FighterParams xa = a;  // `x` = the player clone
    apply_side_attrs(tb, side2_attrs);
    apply_side_attrs(xa, side1_attrs);
    // `l = v.ACa()` (DamageFactor Base), `n = v.zCa()` (DamageFactor attr).
    const float l = fp.damage_factor_base;
    const float q = tb.attr(fp.damage_factor_attr);
    const float r = xa.attr(fp.damage_factor_attr);
    // Resistance: `x = z.vX` (rule), `z = p.o.Pw.c0(z.eta)` (save). The
    // caller resolves the save half; a missing entry is 0.
    float k = 1.0f, h = 1.0f;
    for (const auto& rz : resistances) {
        const float x = rz.first;
        const float z = rz.second;
        if (z < x) {
            k *= std::pow(2.0f, (x - z) / fp.resistance_doubling_range);
            h *= std::pow(2.0f, (z - x) / fp.resistance_doubling_range);
        }
    }
    c = d / c * std::pow(2.0f, (q - r) * l) * k / h;
    c *= std::pow(2.0f,
                  2.0f * (b.rating_correction + e) / fp.damage_doubling_range);
    return c;
}

std::vector<RatingAttrPair> rating_side_attrs(
    const std::vector<RatingSideRule>& rules, int side, int level) {
    std::vector<RatingAttrPair> out;
    for (const RatingSideRule& r : rules) {
        // `Lb.Ti()` -> `d_a()` -> `c_a(p.o.bb())`: the `<Level Min Max>` gate.
        if (level < r.min_level || level > r.max_level) continue;
        const bool non_defense = (r.apply_to == side || r.apply_to == 3);
        for (const auto& kv : r.attrs) {
            const bool is_defense = kv.first.find("Defense") != std::string::npos;
            if (non_defense ? !is_defense : is_defense) {
                out.push_back(RatingAttrPair{kv.first, static_cast<float>(kv.second)});
            }
        }
    }
    return out;
}

// `bb.OE`/`bb.M3`/`bb.xe` + the `Zi` ctor/`parse` (L453078/455854/454581/433102).
// One `<Attributes>` node -> one `RatingSideRule` (the `wB` map + `Li` + range).
static RatingSideRule rating_side_rule_from_node(
    const pugi::xml_node& node, const std::map<std::string, float>& wv) {
    RatingSideRule r;
    // `bb.xe`: `c = c!=null?c:"All"; Player->1 / Bot->2 / All->3 / else 0`.
    const char* at = node.attribute("ApplyTo").value();
    const std::string ats = at != nullptr ? at : "All";
    r.apply_to = ats == "Player" ? 1 : (ats == "Bot" ? 2 : (ats == "All" ? 3 : 0));
    // `Zi` ctor: `for(c of v.wv) wB.set(c.name, 0)` — pre-seeded at 0.
    for (const auto& kv : wv) r.attrs[kv.first] = 0;
    // `Zi.parse`: skip the four non-attribute names, ADD the rest.
    for (const pugi::xml_attribute a : node.attributes()) {
        const std::string k = a.name();
        const float v = a.as_float();
        if (k == "Round" || k == "ApplyTo" || k == "Eclipse" ||
            k == "WarriorPower") {
            if (k == "WarriorPower") {
                // `Zi.parse`: `WarriorPower` adds its value to EVERY `v.wv` name.
                for (const auto& kv : wv) r.attrs[kv.first] += static_cast<int>(v);
            }
            continue;
        }
        r.attrs[k] += static_cast<int>(v);
    }
    return r;
}

std::vector<RatingSideRule> parse_rating_side_rules(
    const pugi::xml_node& rules, const std::map<std::string, float>& wv) {
    std::vector<RatingSideRule> out;
    if (!rules) return out;
    // `bb.OE`: `<Level>` is a conditional container (its children get the
    // range); every other child is a rule pushed as-is.
    for (const pugi::xml_node child : rules.children()) {
        const std::string name = child.name();
        if (name == "Level") {
            // `bb.Ajb`: `Zf(a,0,2147483647)` = [Min, Max] with the defaults.
            int lo = 0, hi = 2147483647;
            if (const pugi::xml_attribute mn = child.attribute("Min"))
                lo = mn.as_int(0);
            if (const pugi::xml_attribute mx = child.attribute("Max"))
                hi = mx.as_int(2147483647);
            for (const pugi::xml_node g : child.children()) {
                if (std::string(g.name()) != "Attributes") continue;
                RatingSideRule r = rating_side_rule_from_node(g, wv);
                r.min_level = lo;
                r.max_level = hi;
                out.push_back(std::move(r));
            }
            continue;
        }
        if (name != "Attributes") continue;
        out.push_back(rating_side_rule_from_node(child, wv));
    }
    return out;
}

// `Be.parse`/`Jw.parse` body on ONE already-located `<Perk>` node, with the
// item-enchant `<Set>` override (`Be.clone` L1329-1330 `c.set(f[0],f[1])`,
// applied OVER the def's own `<Set>` before the `_`-substitution).
static PerkModel parse_perk_node(
    const pugi::xml_node& perk,
    const std::map<std::string, std::string>& ov) {
    PerkModel m;
    if (!perk) return m;
    // `Be.Zjb` (L681500): every `<Set>` attribute -> `iC` (the `_` lookup map).
    if (const pugi::xml_node set = perk.child("Set")) {
        for (const pugi::xml_attribute a : set.attributes()) {
            m.set[a.name()] = a.value();
        }
    }
    // `Be.clone` (L1329-1330): the enchant's `<Set>` attrs overwrite the def's.
    for (const auto& kv : ov) m.set[kv.first] = kv.second;
    // `Be.Ujb` (L681500) -> `Jw.parse` (L703284): the `<Rating>` children.
    const pugi::xml_node re = perk.child("RatingEvaluation");
    if (!re) return m;
    // `Jw.parse`'s `_`-substitution: an attribute value starting with `_` is
    // replaced by the perk `<Set>` entry named after the `_` (or by the bare
    // name when the set lacks it).
    const auto sub = [&m](const char* v) -> std::string {
        if (v == nullptr) return "";
        std::string s = v;
        if (!s.empty() && s[0] == '_') {
            const std::string key = s.substr(1);
            const auto it = m.set.find(key);
            s = it != m.set.end() ? it->second : key;
        }
        return s;
    };
    for (const pugi::xml_node r : re.children("Rating")) {
        PerkRating pr;
        const char* pl = r.attribute("Player").value();
        pr.player = (pl != nullptr && *pl != '\0') ? sub(pl) : "Me";
        pr.damage = sub(r.attribute("Damage").value());
        pr.defense = sub(r.attribute("Defense").value());
        pr.enemy_attr = sub(r.attribute("EnemyAttribute").value());
        pr.multiplier = js_float(sub(r.attribute("Multiplier").value()));
        m.ratings.push_back(std::move(pr));
    }
    return m;
}

PerkModel parse_perk_xml(
    const std::string& perk_xml,
    const std::map<std::string, std::string>& set_override) {
    pugi::xml_document doc;
    if (!doc.load_buffer(perk_xml.data(), perk_xml.size())) return {};
    return parse_perk_node(doc.document_element(), set_override);
}

PerkModel parse_perk_def(
    const std::string& perks_xml, const std::string& name,
    const std::map<std::string, std::string>& set_override) {
    pugi::xml_document doc;
    if (!doc.load_buffer(perks_xml.data(), perks_xml.size())) return {};
    const pugi::xml_node root = doc.child("Perks");
    if (!root) return {};
    for (const pugi::xml_node p : root.children("Perk")) {
        if (name == p.attribute("Name").value()) {
            return parse_perk_node(p, set_override);
        }
    }
    return {};
}

bool rating_perk_probe() {
    // 1) `<Aspect>` config + the `<RatingEvaluation>` row via the real loader.
    static const char* kSettings =
        "<Settings>"
        "<Aspect DoublingRange=\"108\" Limit=\"1.2\" Antilimit=\"0\" />"
        "<RatingEvaluation PerkAspectParameter=\"Aspect\">"
        "<Damage Name=\"Weapon\" AverageBaseDamage=\"0.1\">"
        "<Attribute Name=\"WeaponDamage\" />"
        "<Defense Name=\"BodyDefense\" Weight=\"0.24\">"
        "<Attribute Name=\"BodyDefense\" />"
        "</Defense>"
        "</Damage>"
        "</RatingEvaluation>"
        "</Settings>";
    load_fight_params_from_settings(kSettings);
    FightParams& fp = fight_params();
    fp.align_target_attributes.clear();  // deterministic `balance_multiplier`
    fp.eclipse = false;
    const bool cfg_ok = fp.rating_perk_aspect == "Aspect" &&
                        fp.aspect_antilimit == 0.0f &&
                        fp.aspect_doubling_range == 108.0f &&
                        fp.aspect_limit == 1.2f && fp.rating_table.size() == 1;
    // 2) `Be.eea` pinned directly (shipped config 0/108/1.2).
    const float c0 = aspect_curve(0.0f, fp);       // 1.2 - 0.2*1   = 1.0
    const float cpos = aspect_curve(108.0f, fp);   // 1.2 - 0.2*0.5 = 1.1
    const float cneg = aspect_curve(-108.0f, fp);  // 0 + 2^-1      = 0.5
    const bool curve_ok = std::fabs(c0 - 1.0f) < 1e-4f &&
                          std::fabs(cpos - 1.1f) < 1e-4f &&
                          std::fabs(cneg - 0.5f) < 1e-4f;
    // 3) the perk `<Rating>`/`<Set>` model from a real perks.xml snippet
    //    (`SkillsEnch02.EnchantmentLifeDrain`).
    static const char* kPerk =
        "<Perk Name=\"SkillsEnch02.EnchantmentLifeDrain\">"
        "<Set Aspect=\"0\" Base=\"25000\" Animation=\"Weapon\" Chance=\"0.4\" "
        "Frames=\"90\" DamageRating=\"Weapon\" MultiplierRating=\"2\" />"
        "<RatingEvaluation>"
        "<Rating Player=\"Me\" Damage=\"_DamageRating\" "
        "Defense=\"BodyDefense\" Multiplier=\"_MultiplierRating\" "
        "EnemyAttribute=\"EnchantmentResistance\" />"
        "<Rating Player=\"Me\" Damage=\"_DamageRating\" "
        "Defense=\"HeadDefense\" Multiplier=\"_MultiplierRating\" "
        "EnemyAttribute=\"EnchantmentResistance\" />"
        "</RatingEvaluation>"
        "</Perk>";
    const PerkModel perk = parse_perk_xml(kPerk);
    // A real `<Rating Player="Enemy">` perk (`SkillsEnch02.EnchantmentRegeneration`)
    // for the Enemy branch: `Defense="_DefenseRating"` -> `DefenseRating`.
    static const char* kPerkEnemy =
        "<Perk Name=\"SkillsEnch02.EnchantmentRegeneration\">"
        "<Set Aspect=\"0\" Base=\"750\" ChanceFactor=\"2.2\" Frames=\"300\" "
        "Defense=\"BodyDefense\" DefenseRating=\"BodyDefense\" />"
        "<RatingEvaluation>"
        "<Rating Player=\"Enemy\" Defense=\"_DefenseRating\" Multiplier=\"2\" "
        "EnemyAttribute=\"EnchantmentResistance\" />"
        "</RatingEvaluation>"
        "</Perk>";
    const PerkModel perk_enemy = parse_perk_xml(kPerkEnemy);
    const bool model_ok =
        perk.set.count("Aspect") == 1 && perk.set.at("Aspect") == "0" &&
        perk.ratings.size() == 2 && perk.ratings[0].player == "Me" &&
        perk.ratings[0].damage == "Weapon" &&
        perk.ratings[0].defense == "BodyDefense" &&
        std::fabs(perk.ratings[0].multiplier - 2.0f) < 1e-6f &&
        perk.ratings[0].enemy_attr == "EnchantmentResistance" &&
        perk_enemy.ratings.size() == 1 &&
        perk_enemy.ratings[0].player == "Enemy" &&
        perk_enemy.ratings[0].damage.empty() &&
        perk_enemy.ratings[0].defense == "BodyDefense" &&
        std::fabs(perk_enemy.ratings[0].multiplier - 2.0f) < 1e-6f;
    // 4) the branch: before/after on a controlled single-row evaluation.
    FighterParams self;
    self.is_player = true;
    self.attributes["WeaponDamage"] = 50.0f;
    self.attributes["BodyDefense"] = 12.0f;
    self.equipment_names = {"Fists", "NoRanged", "NoMagic"};
    FighterParams other;
    other.attributes["BodyDefense"] = 5.0f;
    other.iy.push_back(AlignDelta{1.0f, 0.0f, 0, 2});  // finite `balance`
    const std::vector<RatingAttrPair> none;
    const float r0 = warrior_rating(self, other, none, fp);
    self.perks.push_back(perk);  // Me perk on SELF -> F *= 2.2
    const float r1 = warrior_rating(self, other, none, fp);
    const float ratio_me = r0 > 0.0f ? r1 / r0 : 0.0f;
    self.perks.clear();
    other.perks.push_back(perk_enemy);  // Enemy perk on OTHER -> F /= 2.2
    const float r2 = warrior_rating(self, other, none, fp);
    const float ratio_enemy = r0 > 0.0f ? r2 / r0 : 0.0f;
    // Enemy WITH EnchantmentResistance=54 -> eea(-54) = 2^-0.5 ->
    // M = 1 + 2^-0.5 = 1.70710678.
    other.attributes["EnchantmentResistance"] = 54.0f;
    const float r3 = warrior_rating(self, other, none, fp);
    const float ratio_resist = r0 > 0.0f ? r3 / r0 : 0.0f;
    const float exp_resist = 1.0f / (1.0f + std::pow(2.0f, -0.5f));
    const bool branch_ok = r0 > 0.0f && std::fabs(ratio_me - 2.2f) < 1e-3f &&
                           std::fabs(ratio_enemy - (1.0f / 2.2f)) < 1e-3f &&
                           std::fabs(ratio_resist - exp_resist) < 1e-3f;
    // 5) the `<Set>` value-expression evaluators (`Wgb`/`Ffb`, offsets 688816 /
    //    689007): a `?RandomAspect[-30,30]` (the forge.xml item-enchant shape)
    //    + a `?Aspect[expr]`, vs the OLD `js_float` collapse to 0.
    const PerkModel perk_rand = parse_perk_xml(
        "<Perk Name=\"P\"><Set Aspect=\"?RandomAspect[-30,30]\" Base=\"25000\"/>"
        "</Perk>");
    SetValueCtx vc;
    vc.fp = &fp;
    vc.set_vals["Base"] = 25000.0;
    vc.rand01 = []() { return 0.9; };   // floor((31-(-30))*0.9)=floor(54.9)=54
    vc.aspect_scale = [](int) { return 100.0; };  // `gea(level)`
    const double rnd = eval_set_value(perk_rand.set.at("Aspect"), vc);
    const double old_rnd = js_float(perk_rand.set.at("Aspect"));  // pre-fix path
    SetValueCtx va;
    va.fp = &fp;
    const double asp0 = eval_set_value("?Aspect[0]", va);      // eea(0)   = 1.0
    const double aspn = eval_set_value("?Aspect[-108]", va);   // eea(-108) = 0.5
    // The trigger shape (perks.xml L725/L1148, non-raid): `_Chance *
    // ?Aspect[_Aspect*(1-isRaid*isPlayer) + DPA*isRaid*isPlayer - EnemyRes]`;
    // `_Aspect=0`, EnemyRes=54 -> `0.4 * eea(-54) = 0.4*2^-0.5`.
    SetValueCtx vt;
    vt.fp = &fp;
    vt.set_vals["Aspect"] = 0.0;
    vt.set_vals["Chance"] = 0.4;
    vt.is_player = true;
    vt.is_raid = false;
    vt.enemy_attrs["EnchantmentResistance"] = 54.0;
    const double tc = eval_set_value(
        "_Chance * ?Aspect[_Aspect * ( 1 - ?CurrentFight[].isRaid * "
        "?PlayerParameter[Me].isPlayer ) + ?PlayerParameter[Me].DefaultPerksAspect"
        " * ?CurrentFight[].isRaid * ?PlayerParameter[Me].isPlayer - "
        "?PlayerAttribute[Enemy].EnchantmentResistance]",
        vt);
    const double tc_exp = 0.4 * std::pow(2.0, -0.5);
    const bool setexpr_ok = std::fabs(rnd - 124.0) < 1e-6 &&
                            old_rnd == 0.0f &&
                            std::fabs(asp0 - 1.0) < 1e-4 &&
                            std::fabs(aspn - 0.5) < 1e-4 &&
                            std::fabs(tc - tc_exp) < 1e-3;
    const bool pass =
        cfg_ok && curve_ok && model_ok && branch_ok && setexpr_ok;
    std::fprintf(stdout,
                 "[rating-perk] cfg=%d curve=%d model=%d branch=%d setexpr=%d\n"
                 "[rating-perk] aspect antilimit=%.4f doublingRange=%.4f "
                 "limit=%.4f perkAspect=%s\n"
                 "[rating-perk] eea(0)=%.6f eea(+108)=%.6f eea(-108)=%.6f\n"
                 "[rating-perk] setexpr RandomAspect=%.1f (old js_float=%.1f) "
                 "Aspect[0]=%.6f Aspect[-108]=%.6f triggerChance=%.6f "
                 "(exp %.6f)\n"
                 "[rating-perk] rating before=%.9f Me=%.9f (x%.6f) "
                 "Enemy=%.9f (x%.6f) EnemyResist54=%.9f (x%.6f exp x%.6f)\n"
                 "[rating-perk] RESULT %s\n",
                 cfg_ok ? 1 : 0, curve_ok ? 1 : 0, model_ok ? 1 : 0,
                 branch_ok ? 1 : 0, setexpr_ok ? 1 : 0, fp.aspect_antilimit,
                 fp.aspect_doubling_range, fp.aspect_limit,
                 fp.rating_perk_aspect.c_str(), c0, cpos, cneg, rnd, old_rnd,
                 asp0, aspn, tc, tc_exp, r0, r1, ratio_me, r2, ratio_enemy, r3,
                 ratio_resist, exp_resist, pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    return pass;
}

// --- forge.xml `<AspectScale>` + the live `<Set>` runtime (Wgb/Ffb sources) --
// `ye` (L467424): the `v7` rows are `<Aspect>` children of `<AspectScale>`.
// `gv` (L467213): `FZa(a) = a>=fH ? a<=dH : false`, with `fH=dH=Level` when
// `Level` is present, else `fH=MinLevel(0)`, `dH=MaxLevel(2^31-1)`.
namespace {
struct AspectRow {
    int lo = 0;
    int hi = 2147483647;
    double value = 0.0;
};
std::vector<AspectRow>& aspect_rows() {
    static std::vector<AspectRow> rows;
    return rows;
}
}  // namespace

void load_aspect_scale_from_forge(const std::string& xml_text) {
    std::vector<AspectRow>& rows = aspect_rows();
    rows.clear();
    pugi::xml_document doc;
    if (!doc.load_buffer(xml_text.data(), xml_text.size())) return;
    const pugi::xml_node root = doc.child("Forge");
    if (!root) return;
    const pugi::xml_node scale = root.child("AspectScale");
    if (!scale) return;
    for (const pugi::xml_node a : scale.children("Aspect")) {
        AspectRow r;
        const pugi::xml_attribute lv = a.attribute("Level");
        if (lv) {
            r.lo = r.hi = lv.as_int();
        } else {
            const pugi::xml_attribute mn = a.attribute("MinLevel");
            const pugi::xml_attribute mx = a.attribute("MaxLevel");
            r.lo = mn ? mn.as_int() : 0;
            r.hi = mx ? mx.as_int() : 2147483647;
        }
        const pugi::xml_attribute v = a.attribute("Value");
        r.value = v ? v.as_double() : 0.0;
        rows.push_back(r);
    }
}

double aspect_scale_for_level(int level) {
    // `m5a` (L467806): the FIRST row whose `FZa` window contains `level`.
    for (const AspectRow& r : aspect_rows()) {
        if (level >= r.lo && level <= r.hi) return r.value;
    }
    return 0.0;
}

SetValueRuntime& set_value_runtime() {
    static SetValueRuntime rt;
    return rt;
}

}  // namespace sf2::scene
