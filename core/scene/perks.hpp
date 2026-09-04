#pragma once

// Perk hit-actions (PERKS_STATIC section 5.2, JS L1290-1300): the 31-name
// `Ma` action factory. Combat-affecting actions execute for real;
// UI/presentation/magic ones are logged no-ops (exact list below).
// Design mirrors combat_decide.hpp: `decide_hit_perks` is PURE (inputs ->
// outcome, no fighters) so the combat golden pins it 1:1 (S16); fight.cpp
// applies the outcome. Trigger routing (bc bus/Kw.c8a/Fw queue) is OPEN —
// the fight calls the decider with the attacker's equipped list (empty
// until the perk-equip mapping lands) on every landed hit + ticks dots.
//
// Action semantics (spec table):
//   REAL: SetHit(9) override, ModAttributes(3) instant add,
//     ChangeAdditionalDamageValue(22) +Ly, DisableInterval(6) hT/F4,
//     Lifesteal(14) heal, ChangeImpulse(20) knockback scale,
//     ModHealthChange(12) DoT/HoT install, TurnOffCollision(30) vZ toggle.
//   NO-OP+log: ModIcon(1), ClearMods(4), ApplyModEffect(11), Provoke(13),
//     ModInvisibility(15), SetTactic(16), SetModVariable(17),
//     SetRangeVariable(18), SetCooldown(19), ChangeHitEffectScale(21),
//     SetDarkness(25), Switch(26, inert per spec), StealMagicMod(27),
//     SlowModel(28), ChangeModelColor(29), MoveModel(31), ModFlag(5),
//     ShowDebugLine(23), MarkPerkAsUsed(24), AddBullets(7), AddMagicCharge(8)
//     (last two need the magic/bullet systems).

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "scene/damage.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

// One equipped perk action (JS `Ma` entry: Name + attrs).
struct PerkAction {
    std::string type;  // e.g. "SetHit", "Lifesteal" (31 names, PERKS_STATIC)
    int ob = 1;  // `Jf.Ob` target scope (`Player`: ""/Me→1, Enemy→2;
                 // `e6a`: 1→owner model, 2→foe model; default 1 = Me)
    std::map<std::string, double> num;    // Value/Multiplier/Frames/...
    std::map<std::string, std::string> str;  // names
};

// A ticking damage/heal mod (JS `znb`/`Inb`, L1290/L1298).
struct ActiveMod {
    std::string name;
    int frames_left = 0;
    double per_frame = 0.0;  // signed: +heal / -damage (aM sign)
};

// The decided outcome of the hit-scope actions (applied by fight.cpp).
struct PerkHitOutcome {
    // SetHit overrides (JS `ppb` L1294-1295); `has_*` = param present.
    bool f_critical = false, has_critical = false;
    bool f_block = false, has_block = false;
    bool f_shock = false, has_shock = false;
    bool f_disarm = false, has_disarm = false;
    float f_damage = 0.0f;  // SetHit Zi override (JS `ppb` `bR`/`Zi`)
    bool has_damage = false;
    // ChangeAdditionalDamageValue `+Ly`. NOTE (REVIEW A LOW): the bus
    // routes this to future-hit `Ly` state (`WKa` sets live Ly —
    // `exec_action`, not the current hit); the field below exists for
    // decider parity (S16) and direct (non-bus) callers.
    float dmg_add = 0.0f;
    float heal = 0.0f;  // Lifesteal amount (added to attacker HP, clamped)
    double imp_x = 1.0, imp_y = 1.0, imp_z = 1.0;  // ChangeImpulse scales
    std::vector<std::pair<std::string, double>> attr_adds;  // ModAttributes
    // DisableInterval requests: (type or -1, name or "").
    std::vector<std::pair<int, std::string>> clears;
    bool collision_off = false;  // TurnOffCollision request
    std::vector<ActiveMod> install_dots;  // ModHealthChange installs
    std::vector<std::string> log;  // no-op lines (caller prints)
};

inline double perk_num(const PerkAction& a, const std::string& key, double def = 0.0) {
    const auto it = a.num.find(key);
    return it != a.num.end() ? it->second : def;
}

// Pure decider over the attacker's equipped actions (JS `lF` dispatch
// restricted to hit scope; trigger routing OPEN).
// `atk_so`/`foe_so` feed Lifesteal's exact ratio (`apb` L1294:
// `aM(model, VZ·Zi·(model.jb.so/model.so))` — heal = DamagePart × Zi ×
// foe_so/atk_so; both default 1.0).
inline PerkHitOutcome decide_hit_perks(const std::vector<PerkAction>& perks,
                                       const HitRecord& rec, float atk_so = 1.0f,
                                       float foe_so = 1.0f) {
    PerkHitOutcome o;
    for (const PerkAction& a : perks) {
        const std::string& t = a.type;
        if (t == "SetHit") {
            if (a.num.count("Critical")) {
                o.f_critical = a.num.at("Critical") != 0.0;
                o.has_critical = true;
            }
            if (a.num.count("Block")) {
                o.f_block = a.num.at("Block") != 0.0;
                o.has_block = true;
            }
            if (a.num.count("Shock")) {
                o.f_shock = a.num.at("Shock") != 0.0;
                o.has_shock = true;
            }
            if (a.num.count("Disarm")) {
                o.f_disarm = a.num.at("Disarm") != 0.0;
                o.has_disarm = true;
            }
            if (a.num.count("Damage")) {
                o.f_damage = static_cast<float>(a.num.at("Damage"));
                o.has_damage = true;
            }
        } else if (t == "Lifesteal") {
            o.heal += static_cast<float>(perk_num(a, "DamagePart", 0.0) *
                                         rec.final_damage * foe_so / atk_so);
        } else if (t == "ChangeAdditionalDamageValue") {
            o.dmg_add += static_cast<float>(perk_num(a, "Value", 0.0));
        } else if (t == "ChangeImpulse") {
            // `Lp.parse`: `R2/S2/T2 = u.H(...)` — MISSING multiplier is
            // 0.0, NOT 1.0 (`u.H` defaults 0; the ctor 1s are overwritten).
            // `YLa` SETS (last action wins), it does not multiply.
            o.imp_x = perk_num(a, "MultiplierX", 0.0);
            o.imp_y = perk_num(a, "MultiplierY", 0.0);
            o.imp_z = perk_num(a, "MultiplierZ", 0.0);
        } else if (t == "ModAttributes") {
            // `VKa`: every numeric param is an attribute add on the
            // target (`aP` expr map); DamageFactor also records Bb.Tua
            // (skipped — OPEN).
            for (const auto& kv : a.num) {
                o.attr_adds.emplace_back(kv.first, kv.second);
            }
        } else if (t == "DisableInterval") {
            int type = -1;
            const auto it = a.str.find("IntervalType");
            if (it != a.str.end()) {
                if (it->second == "Attack") type = 4;
                else if (it->second == "Block") type = 5;
                else if (it->second == "Invulnerable") type = 6;
                else if (it->second == "Invisible") type = 7;
            }
            std::string name;
            const auto nt = a.str.find("IntervalName");
            if (nt != a.str.end()) name = nt->second;
            o.clears.emplace_back(type, name);
        } else if (t == "TurnOffCollision") {
            o.collision_off = perk_num(a, "Off", 1.0) != 0.0;
        } else if (t == "ModHealthChange") {
            ActiveMod m;
            const auto nt = a.str.find("Name");
            m.name = nt != a.str.end() ? nt->second : "dot";
            m.frames_left = static_cast<int>(perk_num(a, "Frames", 60.0));
            m.per_frame = perk_num(a, "PerFrameValue", 0.0);
            o.install_dots.push_back(m);
        } else {
            o.log.push_back("perknoop " + t);
        }
    }
    return o;
}

// Tick installed DoTs/HoTs (JS `Inb` via `znb`: `aM(model,O3)`/frame).
// Mutates hp (clamped to [0, max_hp]) and drops expired mods. Entries
// with `frames_left<=0` are persistent (verbatim `jp` with null `frames`
// never counts down in `ia`) — they tick until cleared.
inline void tick_active_mods(std::vector<ActiveMod>& mods, float& hp, float max_hp) {
    for (std::size_t i = 0; i < mods.size();) {
        ActiveMod& m = mods[i];
        hp += static_cast<float>(m.per_frame);
        if (hp < 0.0f) hp = 0.0f;
        if (hp > max_hp) hp = max_hp;
        if (m.frames_left > 0 && --m.frames_left <= 0) {
            mods[i] = mods.back();
            mods.pop_back();
        } else {
            ++i;
        }
    }
}

}  // namespace sf2::scene
