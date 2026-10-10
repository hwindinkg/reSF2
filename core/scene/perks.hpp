#pragma once

// Perk hit-actions (PERKS_STATIC section 5.2, JS L1290-1300): the `Ma`
// action factory (`Ma.create` L703951). The hit-scope subset folds into
// `decide_hit_perks` (PURE, pinned by S16); `fight.cpp::exec_action` runs
// the rest via the JS `Fw.lF` switch (type numbers below).
// Design mirrors combat_decide.hpp: `decide_hit_perks` is PURE (inputs ->
// outcome, no fighters) so the combat golden pins it 1:1 (S16); fight.cpp
// applies the outcome. Trigger routing (bc bus/Kw.c8a/Fw queue) is WIRED in
// trigger.hpp's `TrigBus` + `FightController::run_bus_hit` (slots 1/2/5/6/7/
// 12/13/14/8); this pure decider is the hit-scope (SetHit/Lifesteal/
// DisableInterval) reducer `run_bus_hit` calls.
//
// Action semantics (type numbers = the JS `Fw.lF` switch, L661209):
//   REAL: ModIcon(1), ModAttributes(3), ClearMods(4), ModFlag(5),
//     DisableInterval(6), AddBullets(7) `Rob`, AddMagicCharge(8) `Sob`,
//     SetHit(9), SetModFrames(10), Provoke(13), Lifesteal(14),
//     ModInvisibility(15), SetTactic(16) `qpb`, SetModVariable(17) `dka`,
//     SetRangeVariable(18), SetCooldown(19) `npb` (fighter ability-button
//     cooldown via `wd.wKa`/`wd.b5`), ChangeImpulse(20),
//     ChangeHitEffectScale(21), ChangeAdditionalDamageValue(22) +Ly,
//     SlowModel(28) `Kvb`, ChangeModelColor(29), TurnOffCollision(30),
//     SetDarkness(25) `bu` screen overlay, MoveModel(31) `Ow` model tween,
//     ModHealthChange(12) DoT/HoT install, ApplyModEffect(11) `cpb`
//     (perk-icon pulse/stack via `Hr.Maa`).
//   NO-OP+log: Switch(26, applies its nested `<Case>` actions via `Z4a`;
//     no shipped case content), StealMagicMod(27, OPEN — see below),
//     ShowDebugLine(23), MarkPerkAsUsed(24). (`Effect`/
//     `StopEffect`/`StopFollowEffect` are handled by `exec_action` but are
//     not shipped `Ma` tags.)
//
// StealMagicMod(27, JS `Kf` L716844) stays OPEN: `Kf.Ywb` swaps the model's
// `parameters.Mg` (Magic item, `Fd`/`hk`) with a `clone`+`dE` of
// `a.model.jb.parameters.Mg`, then `Q3a` grafts the source model's magic NODES
// (`me` tagged `Kf.qTa="MagicPlayer"` / `Kf.ueb=["MagicPlayer","MagicMissile",
// "MagicMissileStart","MagicMissileFly"]`, plus `Mo` fragments with `zl.locks`)
// into the owner via the `Ae` linked-interval transfer (`nw`), and registers
// them with the `Su.FT`/`JB.u5` updaters. The port has NO model node graph
// (`me`/`Mo`/`xl`/`QX`/`priority`), NO per-fighter parameter store
// (`parameters.Fd/hk`), NO magic ITEM slot (`Mg.Yb`), and no
// `?PlayerParameter[...].Magic` resolver — only `bullets`/`charge`/`raid`
// (JS `bh`/`my`/`dO`). `Kf` is therefore not expressible JS-exact.

#include <functional>
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
    // The RAW attribute strings (pre-`_Var`-substitution), so the action can be
    // RE-substituted with the merged Set at equip time (`Be.clone` / the
    // template-merge Set, JS `Hf.H2` L697632). Only the item-ref-override path
    // reads these; a plain def action keeps its load-time `num`/`str`.
    std::map<std::string, std::string> raw;
    // The owning perk's merged `<Set>` numeric map (the `_X` operands of a
    // `Qa` expression attribute, e.g. `SetHit Damage="?Hit[].Damage*_Factor"`).
    // JS `Qa.oh` binds the perk Set via `Rsb` at parse (`Ma.parse` L703).
    std::map<std::string, double> set_num;
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

// Pure decider over the hit-scope actions the bus collected (JS `Fw.lF`
// dispatch, combat subset).
// `atk_so`/`foe_so` feed Lifesteal's exact ratio (`apb` L1294:
// `aM(model, VZ·Zi·(model.jb.so/model.so))` — heal = DamagePart × Zi ×
// foe_so/atk_so; both default 1.0).
// `eval_damage` evaluates a NON-numeric `SetHit Damage` attribute (a `Qa`
// expression, JS `$p.parse` L712: `this.Xb=Qa.oh(Damage)`, evaluated by
// `ppb` L663774: `b.Xb.Wb().ou()` -> BOTH `bR` and `Zi`). Empty = dropped
// (the old behaviour); `run_bus_hit` supplies the live evaluator.
inline PerkHitOutcome decide_hit_perks(
    const std::vector<PerkAction>& perks, const HitRecord& rec,
    float atk_so = 1.0f, float foe_so = 1.0f,
    const std::function<double(const PerkAction&, const std::string&)>&
        eval_damage = {}) {
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
            } else if (eval_damage) {
                // `$p.parse` put the non-numeric `Damage` in `a.str`; JS `ppb`
                // evaluates `Xb` at hit time and sets BOTH `bR` and `Zi`.
                const auto di = a.str.find("Damage");
                if (di != a.str.end() && !di->second.empty()) {
                    o.f_damage = static_cast<float>(eval_damage(a, di->second));
                    o.has_damage = true;
                }
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
