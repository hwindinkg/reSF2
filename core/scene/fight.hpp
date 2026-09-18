#pragma once

// Fight controller: rounds, phases, timer, round-end and the fight HUD.
// Ported from the game's `ca` class (sf2.502f0946.js L379-433) — see
// core/scene/README.md "Fight controller (Phase 3.5)" for the full JS
// study with line refs. The controller turns the pieces (fighter/moves/
// physics/AI) into a complete fight:
//
//   - round flow:  round init (tx L407: timer = round length, Vt=false)
//                  -> phase 1 StartStance (FNa L409 -> xF(1))
//                  -> phase 2 Fight  (Rkb L410 -> xF(2) + HUD play())
//                  -> phase 3 EndStance (E3a L412 -> i4a L409 -> xF(3))
//   - phase machine: `eu` 0=idle 1=StartStance 2=Fight 3=EndStance; each
//     fighter's `Je` stance is synced by xF (L388) so the move conditions'
//     RoundStage gate matches the fight phase.
//   - round end (Onb L411): KO / timeout (TimeoutWin rule only).
//     KO -> the HIGHER-HP fighter wins the round (vfa L413);
//     `Ar.PEa` (L2020): `NF<=0` — the ENEMY wins (E3a c==3, L412-413).
//     The winner's `ng` (rounds won) increments.
//   - the timer: integer `xU` ticks (JS `Sf.iPa` L2036: `--xU`,
//     `NF = xU/60|0`, init `xU = gma*60+1` on reset, text `max(0,NF)`).
//     The fight decrements xU once per phase-2 tick while `Vt` (running);
//     the HUD and the timeout gate read NF.
//   - battle end: when the winner's `ng >= round.eL` (Rounds), or after a
//     KO in the final round, the battle ends (`bea` L413); `winner()` is
//     exposed for the results screen.
//   - HP recovery: `NA` (L414) heals BOTH fighters by `Da.qDa`
//     (HealthRecovery, stages.xml default 1) between rounds — NOT a full
//     reset (the round-2 fighters keep their damaged HP + the recovery).
//   - the timer: integer `xU` ticks (JS `Sf.iPa` L2036: `--xU`,
//     `NF = xU/60|0`, init `xU = gma*60+1` on reset, text `max(0,NF)`).
//     The fight decrements xU once per phase-2 tick while `Vt` (running);
//     the HUD and the timeout gate (`Ar.PEa`: `NF<=0`) read NF.
//   - the HUD: `Ar`/`Sf` (L2016-2040) — HealthBar_Full/Empty/Hit bars,
//     the digits.fnt timer, Round_Done/Undone indicators, the FIGHT!/
//     Round labels (fight/ui atlas).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "atlas.hpp"
#include "render/gl_types.hpp"
#include "scene/ai.hpp"
#include "scene/damage.hpp"
#include "scene/effects.hpp"
#include "scene/magic_effects.hpp"
#include "scene/fighter.hpp"
#include "scene/move_def.hpp"
#include "scene/perks.hpp"
#include "scene/physics.hpp"
#include "scene/trigger.hpp"
#include "scene/modes.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"

namespace sf2::scene {

// ---------------------------------------------------------------------------
// Stage <Rules> engine (JS `bb.OE`/`M3`/`xe` L887-894 + manager `du` L894-910).
//
// The JS parses every child of a Fight's <Rules> into a typed rule object
// (`bb.M3` dispatch L888-891 for the item/UI tags, `bb.xe` L891-894 for the
// `ERule*` combat tags) and keeps them in `du.Ae`; the active subset is the
// one that passes the per-round `kI` (Round attr, `Lb.kI` L846) and `Ti`
// (power range, `Lb.Ti`/`c_a` L846) gates (`du.osb` L898). `du.f_a`
// (L896-897) runs the per-round rule effects; `du.F1` (L897) calls each
// rule's `Zk`; `du.Ih(1,3,ze)` (L896, dispatched from `ca.ia` L389
// `PC(1,3)`) + `nj.hh` (L885-886) is the per-frame Ringout detection.
// `du.Oob` (L901) applies a fired rule and `ca.BT` (L392-393) records the
// round `ey`.
//
// The native keeps the FULL dispatch (every tag maps to a kind so a new rule
// plugs in without touching the parser) and now evaluates the rules the JS
// fight sim actually runs: the per-frame pass (`du.Ih(1,3,ze)` L896) for the
// field-exit detector (Ringout, with the node-zone exit) and the HotGround
// timer (with the `en.ZZa` `<Node>` zone test and `en` `Voa` animation gate);
// the per-round Zk apply pass (`du.F1(a)` L897) for Attributes /
// RemoveInterval / RechargeMagicEachRound / Tactic / Resistance /
// Invulnerability and the InvertJoystick `Iga` flag (`ca.LBa` L399 consumes
// it in the input path); and the landed-hit pass (`ca.Cgb` -> `PC(5/6/11)`
// L396/L423) for Regeneration / LifeSteal / Points / WinCombo / WinShock.
// The ApplyTo/Round/Eclipse/Death gating and the `<Level>` power range are
// live. The animation-scoped LoseFall (`jn`, arm + `Rba` zone, cp==4 anim
// match and the cp==7 reaction-start pulse), the Combo/Crazy `ws` mutuality
// (`$m`/`an` via `du.kZ`) and Invulnerability's `Zk` (`gn.ws=true`) are now
// implemented. Still parsed + gated but inert (OPEN, cited below):
// RatingEvaluation (UI), DamageFactor (no per-interval `Cea` setter; A5),
// Darkness / RandomArea / LightInTheDarkness (render-side), WinStyle (the
// model style score `dz` has no native source — COMBAT_STATIC App. C), and
// the perk/UI/item rules. `<ComplexRule>`/`<RandomRule>` child rules are
// now expanded by the shared parser (modes.hpp `append_rule_element`,
// `hp` direct-children semantics): `<ComplexRule>` flattens its children
// (JS `nh.parse` L853) and `<RandomRule>` flattens its direct children as
// pick-one choices (JS `pn.parse` L879); the choice is drawn at round start
// via `Da.pg.jf()` (`rules_begin_round`, the `Da.pg` analog `roll01_`).
// Shipped pattern is `RandomRule > ComplexRule` (407 wrappers across
// Duel / Duel_INTERMISSION / FINAL_BATTLE / C3_Challenge / C3_Duel).
// JS-EXACT STREAM: `cl.pmb` (L1413) ALWAYS draws `2147483647*Da.pg.jf()|0`
// and reseeds the shared stream (`Da.IT`, L2353) once per fight, then each
// `pn.M4` (L879) draws `eligible.length*Da.pg.jf()|0`. The port does the
// same on the OWNED `DaPrng` (`Da.pg` analog): one draw + reseed at the
// first `rules_begin_round`, then one draw per group - matching the JS draw
// ORDER/VALUES (previously the private mt19937 stream, and the reseed draw
// was skipped when a battle carried no `<RandomRule>`). Still OPEN: `qmb`
// (the second `$Ja` reseed, L1413) is not modeled, and `NoDoubles` across
// battles is not modeled (one pick per group per fight).
// ---------------------------------------------------------------------------
enum class FightRuleKind : int {
    none = 0,
    // --- `bb.M3` (L888-891): item/UI/wrapper tags (JS `Lb` base) ---------
    require_item, equip_item, random_acquired_item, no_button, no_animation,
    random_rule, complex_rule, rules_with_conditions, description,
    change_fight, currency_cost, raid_currency_cost, avatar, name,
    // --- `bb.xe` (L891-894): `ERule*` combat tags (JS `Ga` base) ---------
    attributes, combo, crazy, damage_factor, darkness, hot_ground,
    invert_joystick, invulnerability, life_steal, light_in_the_darkness,
    lose_fall, no_bullets_replenishment, no_health_bar, no_perks, perk,
    points, pvp, random_area, recharge_magic_each_round, regeneration,
    remove_interval, resistance, ringout, tactic, timeout_win, win_combo,
    win_shock, win_style, rating_evaluation,
};

// `bb.M3` (L888-891) + `bb.xe` (L891-894) tag dispatch. The `ERule*` names
// are the canonical `bb.xe` arguments (e.g. XML `TimeOutWin` ->
// `ERuleTimeoutWin`); the item/UI names are the raw `bb.M3` cases.
inline FightRuleKind fight_rule_kind(const std::string& tag) {
    if (tag == "RequireItem") return FightRuleKind::require_item;
    if (tag == "EquipItem") return FightRuleKind::equip_item;
    if (tag == "RandomAquiredItem") return FightRuleKind::random_acquired_item;
    if (tag == "NoButton") return FightRuleKind::no_button;
    if (tag == "NoAnimation") return FightRuleKind::no_animation;
    if (tag == "RandomRule") return FightRuleKind::random_rule;
    if (tag == "ComplexRule") return FightRuleKind::complex_rule;
    if (tag == "RulesWithConditions") return FightRuleKind::rules_with_conditions;
    if (tag == "Description") return FightRuleKind::description;
    if (tag == "ChangeFight") return FightRuleKind::change_fight;
    if (tag == "CurrencyCost") return FightRuleKind::currency_cost;
    if (tag == "RaidCurrencyCost") return FightRuleKind::raid_currency_cost;
    if (tag == "Avatar" || tag == "ERuleAvatar") return FightRuleKind::avatar;
    if (tag == "Name") return FightRuleKind::name;
    if (tag == "Attributes") return FightRuleKind::attributes;
    if (tag == "Combo") return FightRuleKind::combo;
    if (tag == "Crazy") return FightRuleKind::crazy;
    if (tag == "DamageFactor") return FightRuleKind::damage_factor;
    if (tag == "Darkness") return FightRuleKind::darkness;
    if (tag == "HotGround") return FightRuleKind::hot_ground;
    if (tag == "InvertJoystick") return FightRuleKind::invert_joystick;
    if (tag == "Invulnerability") return FightRuleKind::invulnerability;
    if (tag == "Lifesteal" || tag == "ERuleLifeSteal")
        return FightRuleKind::life_steal;
    if (tag == "LightInTheDarkness")
        return FightRuleKind::light_in_the_darkness;
    if (tag == "LoseFall") return FightRuleKind::lose_fall;
    if (tag == "NoBulletsReplenishment")
        return FightRuleKind::no_bullets_replenishment;
    if (tag == "NoHealthBar") return FightRuleKind::no_health_bar;
    if (tag == "NoPerks") return FightRuleKind::no_perks;
    if (tag == "Perk") return FightRuleKind::perk;
    if (tag == "Points") return FightRuleKind::points;
    if (tag == "Pvp") return FightRuleKind::pvp;
    if (tag == "RandomArea") return FightRuleKind::random_area;
    if (tag == "RechargeMagicEachRound")
        return FightRuleKind::recharge_magic_each_round;
    if (tag == "Regeneration") return FightRuleKind::regeneration;
    if (tag == "RemoveInterval") return FightRuleKind::remove_interval;
    if (tag == "Resistance") return FightRuleKind::resistance;
    if (tag == "Ringout") return FightRuleKind::ringout;
    if (tag == "SetTactic" || tag == "ERuleTactic")
        return FightRuleKind::tactic;
    if (tag == "TimeOutWin") return FightRuleKind::timeout_win;
    if (tag == "WinCombo") return FightRuleKind::win_combo;
    if (tag == "WinShock") return FightRuleKind::win_shock;
    if (tag == "WinStyle") return FightRuleKind::win_style;
    if (tag == "RatingEvaluation") return FightRuleKind::rating_evaluation;
    return FightRuleKind::none;
}

// One parsed rule (JS `Ga`/`Lb`, L846-848). Fields follow the JS names.
struct FightRule {
    FightRuleKind kind = FightRuleKind::none;
    std::string tag;                 // raw XML tag (debug/log)
    int apply_to = 3;                // `Li` = mc(): 1 Player / 2 Bot / 3 All / 0
    bool active = true;              // `Ga.active`
    bool death = false;              // `gra` (Death attr; Ringout kills on fire)
    // `Round="1|2"` (Lb.TIa L847): `Noa=false`, `lta` holds the numbers.
    bool has_rounds = false;
    std::vector<int> rounds;
    // `Eclipse` (Lb.MIa L847): mode 2 = unset, 1 = false, 0 = true.
    bool eclipse_set = false;
    int eclipse_mode = 2;
    // `Zf(a,0,MAX)` / `<Level Min Max>` power range (`xFa`/`wFa`, L846-847).
    // Filled by modes.hpp flattening the `<Level>` wrapper (`bb.Ajb` L894);
    // absent -> [0, INT_MAX] -> `Ti()` always true. `c_a` compares against
    // `p.o.bb()` = the save's current warrior level (`xf.bb` L129409 =
    // `Ca.level`); the native source is `FighterParams.level` (make_fighter).
    long power_min = 0;
    long power_max = 2147483647L;
    // JS `pn` (`ERuleRandom`, L879): group id + the wrapper's direct-child
    // index this rule came from when nested under a `<RandomRule>` (`pn.Ae`
    // order; `-1` = not random-wrapped). `pn.M4` picks ONE choice per group
    // via `Da.pg.jf()`; only that choice's rules activate. `random_each_round`
    // = `Refresh=="EachRound"` (`pn.Zsa==2`), else the pick is per fight.
    int random_group = -1;
    int random_choice = -1;
    bool random_no_doubles = false;  // `pn.aVa` (`u.ka` NoDoubles)
    bool random_each_round = false;
    // --- Ringout / field-exit detector (JS `nj` L885-886) -----------------
    std::string node;                // `ON` (Node attr; "NPivot")
    std::string axis;                // Axis attr ("X"/"Y"/"")
    float min_x = -1.0e5f;           // `ZG` (of(a,-1E5,1E5) first)
    float max_x = 1.0e5f;            // `BH` (second)
    float min_y = -1.0e5f;           // `dN`
    float max_y = 1.0e5f;            // `HO`
    int sequention_speed = 3;        // `tta` (SequentionSpeed default 3)
    // --- other parsed attrs (registered; effect OPEN) ---------------------
    int frames = 0;                  // HotGround Frames
    float value = 0.0f;              // WinCombo/Points Value
    // `en.Va` (L859) + `fv` (L861-862): HotGround's `<Node>` zones. `Axis=X`
    // fills min_x/max_x (`J`/`N`); `Axis=Y` fills min_y/max_y (`P`/`W`); the
    // other pair keeps the +/-3.4e38 defaults (never outside).
    struct HotZone {
        std::string name;  // `fv.name` (Node Name)
        std::string axis;  // `fv` Axis attr
        float min_x = -3.4028234663852886e38f;  // `J` (Axis X first)
        float max_x = 3.4028234663852886e38f;   // `N` (Axis X second)
        float min_y = -3.4028234663852886e38f;  // `P` (Axis Y first)
        float max_y = 3.4028234663852886e38f;   // `W` (Axis Y second)
    };
    std::vector<HotZone> hot_zones;  // `en.Va` (parsed by `Mia`, L860)
    // --- per-rule effect data (JS per-class parse bodies, L846-913) -------
    // `Zi` (`ERuleAttributes`, L849-850): attr name -> int delta (`wB`).
    // Every attr except Round/ApplyTo/Eclipse/WarriorPower lands here; the
    // `WarriorPower` attr fans out over the `v.wv` align names (L850).
    // Applied per round by `Zi.Zk`.
    std::map<std::string, int> attr_adds;
    // `lj` (`ERuleRemoveInterval`, L883): the interval TYPE code (`a9`):
    // Attack 4 / Block 5 / Invulnerable 6 / None 0 / SelfUninterrupt 3 /
    // Uninterrupt 2 / Unstable 1 (`fe.G0` + `fe` ctor remap, L773-774).
    int remove_interval_type = 0;
    // `pj` (`ERuleTactic`, L911): the tactic name (`CVa`).
    std::string tactic_name;
    // `mj` (`ERuleResistance`, L884): `eta` (Name) + `vX` (Value).
    std::string resist_name;
    float resist_value = 0.0f;
    bool resist_set = false;
    // `kj` (`ERuleRegeneration`, L882): `DUa` (FramesAfterHit), `kVa`
    // (Rate), `MUa` (WeaponStrike).
    int regen_frames_after_hit = 0;
    float regen_rate = 0.0f;
    bool regen_weapon_strike = false;
    // `bj` (`ERuleLifeSteal`, L865): `nUa` (DamagePart).
    float lifesteal_part = 0.0f;
    // `gj` (`ERulePoints`, L871-873): `ZN` (Type 0 Contest / 1 Score),
    // `IW` (Max), the Block/Critical/Shock filters (`eUa/ZTa/fUa/lUa/
    // hUa/vVa`) and `JH` (Defense: 0 HeadDefense / 1 BodyDefense / 2 any).
    int points_type = 0;
    float points_max = 0.0f;
    bool points_has_block = false;
    bool points_block = false;
    bool points_has_crit = false;
    bool points_crit = false;
    bool points_has_shock = false;
    bool points_shock = false;
    int points_defense = 2;
    // `rj` (`ERuleWinCombo`, L912): `pV` (Value).
    float win_combo_value = 0.0f;
    // `tj` (`ERuleWinStyle`, L913): `BVa` (`VIa(Type)`). Effect OPEN: `hh`
    // (L913) reads `a.xP` = the model style score `dz` snapshotted per
    // context (`ca.Ema` L420 `ze.rl/kl.xP=yb/pb.dz`), which is set only by
    // the achievement-counter event (`ca.z3` L423) — no native source
    // (COMBAT_STATIC App. C "OPEN-KEPT: live dz tick source"). See OPEN.
    int win_style_type = 0;
    // `Ce.EM` (L848): the rule's `<Animation Name="..."/>` child names
    // (JS `bn.parse` also folds a top-level `Animation` attr — DamageFactor
    // only, and its effect is OPEN). The animation-scoped rules match the
    // current animation (`Lba`, L848); `Mwa("Physical")` (L866) tests this
    // list for the "Physical" group.
    std::vector<std::string> animations;
    bool physical = false;   // `Mwa("Physical")` (L866) — gates `Zf(7)`.
    // `jn.tN` (L866): LoseFall armed flag. Armed by the current animation
    // (cp==4 `tN=this.Lba(a.AI)`, L867) or, for a Physical rule, by the
    // fall reaction (cp==7 `tN=!0`, L867).
    bool armed = false;
    // `$m.pV` (L852): Combo `Value`; `an.Upa` (L854): Crazy `VIa(Type)`.
    float combo_value = 0.0f;
    int crazy_style = 0;
    // `De.ws` (L852): the rule's own `ws` flag; `kZ` (L902) ANDs `!e.ws`
    // over the active `wV` (bit 10 = Invulnerability `gn`/Combo `$m`/
    // Crazy `an`) then drives the opposite fighter's `ola`. Set by each
    // rule's `Zk` (Invulnerability true, L863; Combo/Crazy `hh`, L852/L854).
    bool ws = false;
    // --- runtime per-round state (reset by the rule's `Zk`/`reset`) -------
    int hot_time = 0;          // `en.Qe` HotGround countdown (seconds)
    float hot_frac = 0.0f;     // `en.jc` sub-second accumulator
    bool hot_changed = false;  // `en.cK` (timer changed -> spawn effect)
    bool hot_haa = false;      // `en.haa` (L859): reset-pulse latch
    std::string hot_anim;      // last animation (native cp==4 edge: haa=false)
    int regen_counter = 0;     // `kj.jc` frames since the last landed hit
    int points_self = 0;       // `gj.qH` (Li=1) / `gj.gN` (Li=2)
};

// True for the rules that extend JS `Ga` (the `bb.xe` combat rules that
// carry `ApplyTo`/`mc()` and are split by `du.o4` L909 when ApplyTo=All).
// The `bb.M3` item/UI wrappers (`Lb`/`nh`/`pn`/`qn`) return false.
inline bool fight_rule_is_combat(FightRuleKind k) {
    switch (k) {
        case FightRuleKind::attributes:
        case FightRuleKind::combo:
        case FightRuleKind::crazy:
        case FightRuleKind::damage_factor:
        case FightRuleKind::darkness:
        case FightRuleKind::hot_ground:
        case FightRuleKind::invert_joystick:
        case FightRuleKind::invulnerability:
        case FightRuleKind::life_steal:
        case FightRuleKind::light_in_the_darkness:
        case FightRuleKind::lose_fall:
        case FightRuleKind::no_bullets_replenishment:
        case FightRuleKind::no_health_bar:
        case FightRuleKind::no_perks:
        case FightRuleKind::perk:
        case FightRuleKind::points:
        case FightRuleKind::pvp:
        case FightRuleKind::random_area:
        case FightRuleKind::recharge_magic_each_round:
        case FightRuleKind::regeneration:
        case FightRuleKind::remove_interval:
        case FightRuleKind::resistance:
        case FightRuleKind::ringout:
        case FightRuleKind::tactic:
        case FightRuleKind::timeout_win:
        case FightRuleKind::win_combo:
        case FightRuleKind::win_shock:
        case FightRuleKind::win_style:
            return true;
        default:
            return false;
    }
}

// `bb.xe` ApplyTo code (L891): Player=1, Bot=2, All=3, anything else=0.
inline int fight_rule_apply_to(const std::map<std::string, std::string>& a) {
    const auto it = a.find("ApplyTo");
    const std::string v = it != a.end() ? it->second : std::string("All");
    if (v == "Player") return 1;
    if (v == "Bot") return 2;
    if (v == "All") return 3;
    return 0;
}

inline bool fight_rule_bool(const std::map<std::string, std::string>& a,
                            const char* key, bool def) {
    const auto it = a.find(key);
    if (it == a.end()) return def;
    const std::string& v = it->second;
    return v == "1" || v == "true" || v == "True";
}

inline float fight_rule_float(const std::map<std::string, std::string>& a,
                              const char* key, float def) {
    const auto it = a.find(key);
    if (it == a.end()) return def;
    try {
        return std::stof(it->second);
    } catch (...) {
        return def;
    }
}

inline int fight_rule_int(const std::map<std::string, std::string>& a,
                          const char* key, int def) {
    const auto it = a.find(key);
    if (it == a.end()) return def;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;
    }
}

// `bb.VIa` (L891): the style `Type` attr -> code (Turtle 0, Hard 1, Brutal 2,
// Aggressive 3, Crazy 4, Fantastic 5; unknown/default -> 0). Shared by the
// WinStyle `BVa` (L913) and the Crazy `Upa` (L854).
inline int fight_rule_style_type(const std::map<std::string, std::string>& a) {
    const auto it = a.find("Type");
    const std::string t = it != a.end() ? it->second : std::string();
    return t == "Turtle" ? 0 : t == "Hard" ? 1 : t == "Brutal" ? 2
         : t == "Aggressive" ? 3 : t == "Crazy" ? 4
         : t == "Fantastic" ? 5 : 0;
}

// `bb.M3`/`bb.xe` -> `Ga`/`Lb` constructor (L885-894): parse one StageRule.
inline FightRule parse_fight_rule(const StageRule& sr) {
    FightRule r;
    r.kind = fight_rule_kind(sr.tag);
    r.tag = sr.tag;
    if (r.kind == FightRuleKind::none) return r;
    // `Ce.c4a`/`Mwa` (L848/L866): the rule's animation list + the
    // `Physical` group test (gates LoseFall's `Zf(7)` registration).
    r.animations = sr.animations;
    for (const std::string& n : r.animations) {
        if (n == "Physical") { r.physical = true; break; }
    }
    r.apply_to = fight_rule_apply_to(sr.attrs);
    r.death = fight_rule_bool(sr.attrs, "Death", false);
    r.eclipse_set = sr.attrs.find("Eclipse") != sr.attrs.end();
    if (r.eclipse_set) {
        r.eclipse_mode = fight_rule_bool(sr.attrs, "Eclipse", false) ? 0 : 1;
    }
    // `Round="1|2"` (Lb.TIa L847): non-empty -> lta list.
    const auto rit = sr.attrs.find("Round");
    if (rit != sr.attrs.end() && !rit->second.empty()) {
        r.has_rounds = true;
        std::string s = rit->second;
        std::size_t p = 0;
        while (p <= s.size()) {
            const std::size_t q = s.find('|', p);
            const std::string tok = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
            try {
                r.rounds.push_back(std::stoi(tok));
            } catch (...) {
            }
            if (q == std::string::npos) break;
            p = q + 1;
        }
    }
    // `nj.parse` (L886)/`jn.parse` (L868): Node + Axis + Min/Max.
    const auto nit = sr.attrs.find("Node");
    if (nit != sr.attrs.end()) r.node = nit->second;
    const auto ait = sr.attrs.find("Axis");
    if (ait != sr.attrs.end()) r.axis = ait->second;
    // `jn` ctor (L866) seeds the four bounds `BH=-1E5; ZG=1E5; HO=-1E5;
    // dN=1E5` (the reversed "always outside once armed" zone); `nj` ctor
    // (L885) keeps the normal `-1E5..1E5`. `of(a,1E5,-1E5)` (L867) -> Min
    // default +1E5, Max default -1E5 for LoseFall; `of(a,-1E5,1E5)` (L886)
    // for Ringout.
    const bool lose_fall = (r.kind == FightRuleKind::lose_fall);
    if (lose_fall) {
        r.min_x = 1.0e5f;
        r.max_x = -1.0e5f;
        r.min_y = 1.0e5f;
        r.max_y = -1.0e5f;
    }
    const float mn = fight_rule_float(sr.attrs, "Min", lose_fall ? 1.0e5f : -1.0e5f);
    const float mx = fight_rule_float(sr.attrs, "Max", lose_fall ? -1.0e5f : 1.0e5f);
    if (r.axis == "X") {  // JS: only Axis=X fills ZG/BH (L886/L867)
        r.min_x = mn;
        r.max_x = mx;
    } else if (r.axis == "Y") {  // JS: Axis=Y fills dN/HO
        r.min_y = mn;
        r.max_y = mx;
    }
    r.sequention_speed = fight_rule_int(sr.attrs, "SequentionSpeed", 3);
    r.frames = fight_rule_int(sr.attrs, "Frames", 0);
    r.value = fight_rule_float(sr.attrs, "Value", 0.0f);
    // `en.Mia` (L860) + `fv` (L861-862): the HotGround `<Node>` zones. Only
    // the `Axis`-named pair is filled; the other keeps +/-3.4e38 (JS leaves
    // J/N or P/W untouched). `of` (L16) gave Min -> first, Max -> second.
    if (r.kind == FightRuleKind::hot_ground) {
        for (const StageRule::Zone& z : sr.zones) {
            FightRule::HotZone hz;
            hz.name = z.name;
            hz.axis = z.axis;
            if (z.axis == "X" || z.axis == "x") {
                hz.min_x = z.min;   // `J`
                hz.max_x = z.max;   // `N`
            }
            if (z.axis == "Y" || z.axis == "y") {
                hz.min_y = z.min;   // `P`
                hz.max_y = z.max;   // `W`
            }
            r.hot_zones.push_back(std::move(hz));
        }
    }
    // `<Level Min Max>` wrapper range (`bb.Ajb` L894 via `Zf(a,0,MAX)`, a
    // Min/Max ATTR read; modes.hpp flattens the wrapper into this rule).
    r.power_min = sr.power_min;
    r.power_max = sr.power_max;
    // `pn` RandomRule group/choice (modes.hpp flattens the wrapper; the
    // activation pick is `FightController::rules_begin_round`).
    r.random_group = sr.random_group;
    r.random_choice = sr.random_choice;
    r.random_no_doubles = sr.random_no_doubles;
    r.random_each_round = sr.random_each_round;
    // --- per-rule parse (JS per-class constructors/parse) -----------------
    if (r.kind == FightRuleKind::attributes) {
        // `Zi.parse` (L850): `wB` starts with the `v.wv` align names at 0;
        // every attr except Round/ApplyTo/Eclipse/WarriorPower adds its int.
        static const char* kWvNames[] = {
            "WeaponDamage", "UnarmedDamage", "BodyDefense", "HeadDefense",
            "RangedDamage", "MagicDamage", "EnchantmentResistance"};
        for (const char* n : kWvNames) r.attr_adds[n] = 0;
        for (const auto& kv : sr.attrs) {
            if (kv.first == "Round" || kv.first == "ApplyTo" ||
                kv.first == "Eclipse" || kv.first == "WarriorPower")
                continue;
            int v = 0;
            try { v = static_cast<int>(std::stof(kv.second)); } catch (...) {}
            r.attr_adds[kv.first] += v;
        }
        const auto wp = sr.attrs.find("WarriorPower");
        if (wp != sr.attrs.end()) {
            int v = 0;
            try { v = static_cast<int>(std::stof(wp->second)); } catch (...) {}
            for (const char* n : kWvNames) r.attr_adds[n] += v;
        }
    } else if (r.kind == FightRuleKind::remove_interval) {
        // `lj.parse` (L883): the Type attr -> `a9`.
        const auto it = sr.attrs.find("Type");
        const std::string t = it != sr.attrs.end() ? it->second : std::string();
        r.remove_interval_type = t == "Attack" ? 4 : t == "Block" ? 5
                              : t == "Invulnerable" ? 6 : t == "SelfUninterrupt" ? 3
                              : t == "Uninterrupt" ? 2 : t == "Unstable" ? 1 : 0;
    } else if (r.kind == FightRuleKind::tactic) {
        // `pj.parse` (L911): the Name attr -> `CVa`.
        const auto it = sr.attrs.find("Name");
        if (it != sr.attrs.end()) r.tactic_name = it->second;
    } else if (r.kind == FightRuleKind::resistance) {
        // `mj.parse` (L884): Name -> `eta`, Value -> `vX` (clamped >= 0).
        const auto it = sr.attrs.find("Name");
        if (it != sr.attrs.end()) r.resist_name = it->second;
        r.resist_value = fight_rule_float(sr.attrs, "Value", 0.0f);
        if (r.resist_value < 0.0f) r.resist_value = 0.0f;
        r.resist_set = true;
    } else if (r.kind == FightRuleKind::regeneration) {
        // `kj.parse` (L882): FramesAfterHit/Rate/WeaponStrike.
        r.regen_frames_after_hit =
            fight_rule_int(sr.attrs, "FramesAfterHit", 0);
        r.regen_rate = fight_rule_float(sr.attrs, "Rate", 0.0f);
        r.regen_weapon_strike =
            fight_rule_bool(sr.attrs, "WeaponStrike", false);
    } else if (r.kind == FightRuleKind::life_steal) {
        // `bj.parse` (L865): DamagePart -> `nUa`.
        r.lifesteal_part = fight_rule_float(sr.attrs, "DamagePart", 0.0f);
    } else if (r.kind == FightRuleKind::points) {
        // `gj.parse` (L872-873): Type Contest/Score, Max, the filters.
        const auto tt = sr.attrs.find("Type");
        const std::string t = tt != sr.attrs.end() ? tt->second
                                                   : std::string("Contest");
        r.points_type = t == "Score" ? 1 : 0;
        r.points_max = fight_rule_float(sr.attrs, "Max", 0.0f);
        r.points_has_block = sr.attrs.find("Block") != sr.attrs.end();
        r.points_block = fight_rule_bool(sr.attrs, "Block", false);
        r.points_has_crit = sr.attrs.find("Critical") != sr.attrs.end();
        r.points_crit = fight_rule_bool(sr.attrs, "Critical", false);
        r.points_has_shock = sr.attrs.find("Shock") != sr.attrs.end();
        r.points_shock = fight_rule_bool(sr.attrs, "Shock", false);
        const auto di = sr.attrs.find("Defense");
        const std::string df = di != sr.attrs.end() ? di->second : std::string();
        r.points_defense = df == "BodyDefense" ? 1 : df == "HeadDefense" ? 0 : 2;
    } else if (r.kind == FightRuleKind::combo) {
        // `$m.parse` (L852): `Value` -> `pV`. `De.Zk`/`hh` (L852) derive the
        // per-side `ws` from `NZ < pV` (the `kZ` mutuality, L902).
        r.combo_value = fight_rule_float(sr.attrs, "Value", 0.0f);
    } else if (r.kind == FightRuleKind::crazy) {
        // `an.parse` (L854): `bb.VIa(Type)` -> `Upa`. `hh` (L854) derives
        // `ws` from `xP < Upa`.
        r.crazy_style = fight_rule_style_type(sr.attrs);
    } else if (r.kind == FightRuleKind::win_combo) {
        // `rj.parse` (L912): Value -> `pV`.
        r.win_combo_value = fight_rule_float(sr.attrs, "Value", 0.0f);
    } else if (r.kind == FightRuleKind::win_style) {
        // `tj.parse` (L913) via `bb.VIa` (L891): Type -> `BVa`.
        r.win_style_type = fight_rule_style_type(sr.attrs);
    } else if (r.kind == FightRuleKind::damage_factor) {
        // `bn.parse` (L855-856): `Animation` attr + `Factor`/`RepeatFactor`
        // -> the per-interval charge (`zUa`/`lVa`). Effect OPEN: the native
        // has no per-interval `Cea(side)` setter (`Vm.bp/Rja/JU/KU`, L775) —
        // `damage.cpp:118` keeps `bp=1` (documented at `modes.hpp:562`), so
        // the charge cannot be wired (COMBAT_STATIC A5: no shipped
        // `ERuleDamageFactor` element either).
    }
    // ApplyTo overrides from the `bb.xe` dispatch (L891-893): Points is
    // always All (`new gj(b,3)`) -> split; Darkness always Player
    // (`new $i(b,1)`); Tactic defaults Bot (`new pj(b)`).
    if (r.kind == FightRuleKind::points) r.apply_to = 3;
    else if (r.kind == FightRuleKind::darkness) r.apply_to = 1;
    else if (r.kind == FightRuleKind::tactic) r.apply_to = 2;
    // `qj` ctor (L912): TimeOutWin forces `Li=1` (player wins on timeout;
    // `Yu=false` -> `wfa()` = 1 -> E3a `a=true`).
    if (r.kind == FightRuleKind::timeout_win) r.apply_to = 1;
    // `gn.Fbb` (L863): Invulnerability swaps its side at construction
    // (`Li==1 -> 2`, `Li==2 -> 1`) — the `kZ` pass reads `mc()` on one side
    // and calls `ola` on the OTHER (`Rea(a==1?2:1)`), so the swap makes
    // `ApplyTo=Player` grant the PLAYER the `ws` (shock-immunity) flag.
    // Shipped rules are Player/Bot only (54/54); ApplyTo=All (none shipped)
    // is split by `du.o4` into base `De` copies in JS — not replicated here.
    if (r.kind == FightRuleKind::invulnerability) {
        if (r.apply_to == 1) r.apply_to = 2;
        else if (r.apply_to == 2) r.apply_to = 1;
    }
    return r;
}

// `du.o4` (L909): an ApplyTo=All combat rule is split into two copies with
// `Li=1`/`Li=2` (`b.BLa(1); c.BLa(2)`) so each tracks one fighter.
inline std::vector<FightRule> fight_rules_split(const FightRule& r) {
    std::vector<FightRule> out;
    if (fight_rule_is_combat(r.kind) && r.apply_to == 3) {
        FightRule a = r; a.apply_to = 1;
        FightRule b = r; b.apply_to = 2;
        out.push_back(a);
        out.push_back(b);
    } else {
        out.push_back(r);
    }
    return out;
}

// `du.osb` (L898): `active = kI(cz) && Ti()`. `kI` = the Round attr
// membership (`Lb.kI` L846); `Ti` = the power range `[xFa,wFa]` (`Lb.c_a`
// L846). `power` is `p.o.bb()` = the save's current warrior level
// (`xf.bb` L129409 = `Ca.level`); the native callers pass the player's
// `FighterParams.level` (the port's warrior-level analog).
inline bool fight_rule_gate(const FightRule& r, int round, long power) {
    if (r.has_rounds) {
        bool found = false;
        for (int rr : r.rounds) {
            if (rr == round) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    if (power < r.power_min || power > r.power_max) return false;
    return true;
}

// The battle parameters (JS `Da`, L1418-1424): the fight's config read from
// the stages.xml <Fight> element.
struct BattleParams {
    std::string name;         // "Training" / "BOSS_LYNX"
    std::string type;         // "FightNone" / "FightTutorial" / "FightBosses"...
    std::string location;     // "dojo" / "moon" / ...
    int rounds = 2;           // `pT`  <Fight Rounds="2"> — the ROUNDS-TO-WIN
                              // (the fight ends when a fighter wins this many)
    int round_time = 99;      // `R4`  <Fight RoundTime="99"> (seconds) — the
                              // HUD countdown (JS `$t.gma` / `Sf.iPa`)
    float health_recovery = 1.0f;  // `qDa` <Battle HealthRecovery=".."> (default 1)
    bool timeout_rule = false;     // `ERuleTimeoutWin` — when set, a round ends
                              // when the HUD timer reaches 0 (JS `BT` sets
                              // `ey=2` for the TimeoutWin rule). The shipped
                              // stages use NO timeout rule — rounds end on KO.
    // `ERuleRingout` — the stage fight's `<Ringout Node=".." Axis="X"
    // Min=".." Max=".." ApplyTo=".."/>` rule (stages.xml, e.g. L2238
    // `Min="-600" Max="600"`). JS `nj` (L885-886): `ZG`/`BH` = the Axis=X
    // Min/Max (`of(a,-1E5,1E5)`, `of` def: first=Min, second=Max), `tta` =
    // `SequentionSpeed` (default 3). Absent element => no rule => no marker.
    // Filled by `apply_stage_ringout_rule` from StageFight.rules and fed to
    // set_ringout_rule each round (presentation only).
    bool ringout_rule = false;
    float ringout_min_x = -1.0e5f;  // JS `ZG` (`of` Min default -1E5, L886)
    float ringout_max_x = 1.0e5f;   // JS `BH` (`of` Max default 1E5, L886)
    float ringout_speed = 3.0f;     // JS `tta` (SequentionSpeed default 3, L886)
    // The FULL parsed stage <Rules> set (JS `du.Ae`): every tag dispatched
    // via `bb.M3`/`bb.xe`, ApplyTo=All split into two (`du.o4`), with the
    // Round/Eclipse/Death gating attrs. Filled by `apply_stage_ringout_rule`
    // from StageFight.rules; the controller runs the sim-relevant subset
    // (Ringout/TimeOutWin) with JS-exact result mapping.
    std::vector<FightRule> rules;
    // Fight-level spawn positions. JS `Bf.zjb` L476 stores the ModelsViewer
    // `PlayerPositionX/Y` into `location.Yia` and `EnemyPositionX/Y` into
    // `location.B_`; JS L381 then spawns the player (`kc`) at `Yia` and the
    // enemy (`Zb`) at `B_`. So the fight roles FOLLOW the ModelsViewer
    // labels: dojo player (690,-93) / enemy (973,-105). (The oracle dump's
    // `id` labels are swapped — its 15-bone Punchbag sits at x=973 = the
    // enemy — which is what the deleted comment got wrong.) The caller
    // sources these from LocationScene (screens.cpp); the defaults are the
    // dojo values.
    float player_spawn_x = 690.0f, player_spawn_y = -93.0f;
    float enemy_spawn_x = 973.0f, enemy_spawn_y = -110.0f;
    int max_hp = 100;         // demo HP cap (the game stores HP in the save)
    // JS `ur` L194-195: `Fj = NotAI==null`, `QD = NotAnimation==null`. A
    // stage Warrior with NotAI="1" gets NO AiController (the dummy never
    // selects a move); NotAnimation="1" keeps it at the BIND pose (no
    // stance-idle clip). Resolved from the battle's first <Warrior>
    // (`battle_warrior`, screens.cpp).
    bool enemy_not_ai = false;
    bool enemy_not_animation = false;
    // The player's resolved `UnarmedDamage` (JS `wd.Fm` L811: item bonus
    // `m7a` + group `g8a` + StartingAttributes + level × LevelAttributeGain)
    // — the bCa balance input, not a tuning knob. Dojo default = 15.
    float player_unarmed_damage = 0.0f;
    // The player's FULL resolved attribute map (JS `wd.Fm` L811: the same
    // `m7a`+`g8a`+StartingAttributes+level×LevelAttributeGain sum, computed
    // for EVERY name in `v.eo.attributes`). The bCa inputs `BodyDefense`,
    // `HeadDefense` and `BlockDamageFactor` live here — without them a landed
    // hit is computed against 0 defense and a 1× block factor (JS-exact
    // values from character_progress.xml: BodyDefense 5+10·lvl, HeadDefense
    // 0+10·lvl, BlockDamageFactor −23219 ⇒ 2^(−2.3219)=0.2× on block). The
    // app layer fills this (character_progress.xml + the save's level/items).
    std::map<std::string, float> player_attrs;
    // The fighter's `IY` align-armor rows (JS `xc.IY` -> `damage.hpp`
    // `AlignDelta`), resolved from stages.xml `<AttributesAlign>` (own rows
    // appended after the inherited `Default` template's — see
    // `modes.hpp::stage_warrior_align`). `pAa` reads
    // `(attacker.qb ? defender : attacker).IY`, so in a player-vs-enemy fight
    // the ENEMY's rows always govern the blend; the player set is `Default`'s.
    std::vector<sf2::scene::AlignDelta> player_align;
    std::vector<sf2::scene::AlignDelta> enemy_align;
    // The fighters' `xc.voice` (JS `ur` L186 reads the Warrior's `Voice`
    // attr into `xc.voice`; `Vo` ctor default "" L807). The move actions'
    // `<Sound Voice="Male|Female|MaleLow">` gate compares against it
    // (`fm.fka` L735: `t7 ? true : voice == J8`). The app fills the player's
    // from the save Warrior (`users_default.xml` `<Warrior Voice="Male">`)
    // and the enemy's from the stage Warrior's merged `<Template Voice=..>`
    // (stages.xml). Empty = no voice -> every Voice-gated action silent.
    std::string player_voice;
    std::string enemy_voice;
};

// JS `bb.OE` (L887-888) + `bb.M3`/`bb.xe` (L888-894): parse the stage
// fight's `<Rules>` children into `b.rules` (with the ApplyTo split) and
// mirror the FIRST `<Ringout>` into `b.ringout_*` for the marker config.
// `rules` are the parsed `<Rules>` children (StageRule; `parse_stages`
// modes.hpp stores any child tag + attrs). Axis="X" sets `ZG`/`BH` from
// Min/Max (`of(a,-1E5,1E5)` L886; `of` first=Min, second=Max);
// `SequentionSpeed` defaults 3. JS `f_a` (L896-897) uses the FIRST active
// Ringout rule (`a||(a=g, ...)`), so the first match wins. `TimeOutWin`
// (JS `qj` L912) flips `b.timeout_rule` so the timer end is live.
//
// `<Level Min Max>` wrappers (JS `bb.Ajb` L894, `Zf(a,0,MAX)`): modes.hpp
// flattens the wrapper into its child rules and stamps `power_min`/`power_max`
// on each StageRule, so `parse_fight_rule` copies them onto the FightRule and
// `fight_rule_gate` enforces the range against the player's level.
inline void apply_stage_ringout_rule(BattleParams& b,
                                     const std::vector<StageRule>& rules) {
    // Full dispatch (`bb.OE` L887-888): every child -> kind, ApplyTo=All
    // split into the two `BLa(1)`/`BLa(2)` copies (`du.o4` L909).
    b.rules.clear();
    for (const StageRule& sr : rules) {
        const FightRule r = parse_fight_rule(sr);
        if (r.kind == FightRuleKind::none) continue;
        for (const FightRule& copy : fight_rules_split(r)) {
            b.rules.push_back(copy);
        }
        if (r.kind == FightRuleKind::timeout_win) b.timeout_rule = true;
    }
    // `f_a` (L896-897): the FIRST Ringout rule feeds `H1a(ZG,BH,tta)`. The
    // ApplyTo split means the first copy is the Player-tracking one.
    for (const FightRule& r : b.rules) {
        if (r.kind != FightRuleKind::ringout) continue;
        b.ringout_rule = true;
        b.ringout_min_x = r.min_x;
        b.ringout_max_x = r.max_x;
        b.ringout_speed = static_cast<float>(r.sequention_speed);
        break;
    }
}

// The live round state (JS `$t` L1239).
struct RoundState {
    int number = 0;       // `round` — the current round number (0 before the
                          // first Z2; increments to 1..Rounds)
    // The integer round timer (JS `Sf.iPa` L2036: `--xU`, `NF = xU/60|0`;
    // init `xU = gma*60+1` on reset; the text shows `max(0,NF)`). The HUD
    // and the timeout gate both read NF — the port's old float elapsed
    // timer had NO JS counterpart (`$t.time` is set once in `tx()` and
    // never incremented anywhere in the file).
    int time_xu = 0;      // `xU` — countdown ticks (60/sec)
    int time_nf = 0;      // `NF` — countdown seconds shown + gated
    int length = 0;       // `eL` — Da.pT = Rounds: the ROUNDS-TO-WIN
                          // threshold (JS `Onb` L411: `nB.ng >= round.eL`
                          // ends the battle)
    int gma = 60;         // `gma` — the HUD countdown seconds (Da.R4)
    bool running = false; // `Vt` — the fight timer is running (phase 2)
};

// The fight phases (JS `ca.eu` 0-3).
enum class fight_phase : int {
    idle = 0,          // `eu` 0 — no fight (bob's xF(0) before tx)
    start_stance = 1,  // `eu` 1 — StartStance (fighters at spawn, no input)
    fight = 2,         // `eu` 2 — the round is live
    end_stance = 3,    // `eu` 3 — EndStance (results)
};

// The big center-screen banner (the game's FIGHT!/ROUND N/K.O. overlays —
// JS `Sf`'s round.png labels + the KO slow-mo). A pure PRESENTATION state:
// the banner machine never touches the fight simulation (the pose dump is
// byte-identical with or without it).
// The center-screen banner (JS class `Cr` L2022-2027 — the HUD banner
// state machine with its `type` field):
//   - `round` = JS type 2 (`Cr.tca` L2023, `fu(1.666)` + a 500 ms arm
//     delay): the between-rounds ROUND N plate. Its expiry -> `FNa` (L409)
//     through `ca.vhb` (L410) case 2.
//   - `fight` = the FIGHT! plate. The traced JS takes the
//     `Da.type == "FightNone"` branch of `kg` (L387: `this.xF(2)` straight
//     from the stance end — `oracle_pose.jsonl` shows `phase` 1 -> 2 at
//     f=134), so this plate is display-only for the port's fight; its hold
//     is the JS standard `fu(1.166)` (`Cr.Zy` L2024).
//   - `ko` = the finish plate (JS `Cr.GZ` L2024 type 6/7, `fu(1.166)`).
//   - `victory`/`defeat` = the battle end, no timer (the results screen
//     takes over).
enum class banner_kind : int {
    none = 0,   // no banner
    round,      // "ROUND N" — the round-start intro
    fight,      // "FIGHT!" — the round goes live
    ko,         // "K.O." — a fighter was KO'd (with the slow-mo)
    victory,    // "VICTORY" — the player won the battle
    defeat,     // "DEFEAT" — the player lost the battle
};

// The banner's pending action — the JS `ca.vhb` (L410) dispatch on the
// banner type, plus the port's stand-in for the JS end-stance animation
// gate (`h4a` -> `Ewb` -> `h9`, L387/L404).
enum class banner_action : int {
    none = 0,     // display only (`vhb` has no case for the type; the first
                  // round's plate, the FIGHT! plate, victory/defeat)
    begin_round,  // JS type 2 -> `vhb` case 2 -> `FNa` (L409): phase 1
    next_round,   // the round-end hold -> `NA()` + `Z2()` (L411/L414/L408)
};

// How a round ended (JS `ey` 0-6; the demo fight uses KO=0 and
// TimeoutWin=2).
enum class round_result : int {
    ko = 0,          // `ey` 0 — a fighter was KO'd (hp <= 0)
    points = 1,      // `ey` 1 — points rule (unused here)
    timeout_win = 2, // `ey` 2 — timeout/ringout rule
    defeat = 3,      // `ey` 3 — the player lost (PVP/rule); the ENEMY wins
    ringout = 4,     // `ey` 4 — ringout rule
    survival = 5,    // `ey` 5 — survival rule
    pvp = 6,         // `ey` 6 — PVP HP comparison
};

// HUD style meter (JS `Gr`/`Fr` L2090-2092, `lw`/`v.hu` parse L1159).
// Values verified from internal_settings `<StyleLevels>` 2026-09-04:
// TNa=0.5, tya=0.08, ZIa=2, SNa=1 x6 levels (Start/Hard/Brutal/
// Aggressive/Crazy/Fantastic). Presentation (bars/announce) stays in HUD;
// this is the score state that feeds `Gua` (b6) for the prize DZ term.
// `Y8a(a)`: null->0; per-anim use counts `VS` (first call b=0);
// credit = TNa*SNa[level]*ZIa^(-uses)*RNa. `Vma`: `KDa(bar+credit)`.
// `KDa`: level += trunc, frac carry, clamp to levels-1. Decay `ia()`:
// bar = max(0, bar - tya/60/on()) — bar-only, levels never drop
// (timescale on()=1.0 in the port — no slow-mo).
struct StyleTable {
    double tna = 0.5;
    double tya = 0.08;
    double zia = 2.0;
    std::vector<double> sna{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
};
struct StyleMeter {
    int level = 0;   // `bn`
    double frac = 0.0;  // bar value `Qp.Gb(bn)`
    std::map<std::string, int> vs_counts;  // `VS` per-anim uses
    int best = 0;    // max level seen (`Gua` b6 feed)
};
// `Y8a` credit for one landed hit with the attack move's RNa.
inline double style_credit(const StyleTable& t, StyleMeter& st,
                           const std::string& anim, double rna) {
    int b = 0;
    auto it = st.vs_counts.find(anim);
    if (it != st.vs_counts.end()) {
        b = it->second + 1;
        it->second = b;
    } else {
        st.vs_counts[anim] = 0;
    }
    const double sna =
        !t.sna.empty() ? t.sna[std::min<std::size_t>(
                              static_cast<std::size_t>(std::max(0, st.level)),
                              t.sna.size() - 1)]
                       : 1.0;
    double pow = 1.0;
    for (int i = 0; i < b; ++i) pow /= t.zia;
    return t.tna * sna * pow * rna;
}
// `Vma`/`KDa`: add credit to the bar, carry level-ups (clamped).
inline void style_vma(StyleMeter& st, double credit, int levels) {
    const double total = st.frac + credit;
    int b = st.level + static_cast<int>(total);
    double frac = total - std::floor(total);
    if (b >= levels) {
        b = levels - 1;
        frac = 1.0;
    }
    if (b < 0) {
        b = 0;
        frac = 0.0;
    }
    st.level = b;
    st.frac = frac;
    if (st.level > st.best) st.best = st.level;
}
// Decay `ia()`: bar-only drain, levels never drop.
inline void style_decay(StyleMeter& st, double tya) {
    st.frac = std::max(0.0, st.frac - tya / 60.0);
}

// The per-fighter live state the fight controller owns (JS: the `wd`
// fighter + its `parameters` + the AiController).
struct FightFighter {
    std::string name;         // fighter display name ("Player"/"Enemy")
    bool is_player = false;   // `parameters.qb`
    sf2::scene::Fighter fighter;
    sf2::scene::BodyState body;
    sf2::scene::FighterParams params;
    std::unique_ptr<sf2::scene::AiController> ai;  // null for a manual fighter
    float hp = 0.0f;          // `parameters.gd`
    float max_hp = 0.0f;      // `parameters.Zn`
    int rounds_won = 0;       // `parameters.ng` — rounds won
    bool is_winner = false;   // `zd` — the round/battle winner flag
    // `kh` — the round-over latch (JS `E3a` L413 `a.kh=!0;b.kh=!0`). The
    // per-frame attack pass is gated on the PLAYER's flag
    // (JS `ca.Hnb` L389 `if(!this.kc.kh){...}`) and cleared at the
    // between-round recovery (`NA` L414 `c.parameters.kh=!1`) + the round
    // advance (`Z2` L409).
    bool kh = false;
    sf2::scene::ShockState shock;  // pain/shock/disarm (`sr/vc/sn/Wx/ws`, L490)
    StyleMeter style;  // HUD style meter (`Gr`, feeds prize b6 via best)
    sf2::scene::Vec3 jg{1.0f, 1.0f, 1.0f};  // impulse scale (`wd.JG`;
                                            // `YLa` sets, `gob` resets)
    float qz = 1.0f;  // hit-effect scale (`wd.Qz`; `fob` resets)
    int bullets = 0;  // magic bullets (`wd.bh`; `hZ` adds, cap 1 via `LA`)
    double charge = 0.0;  // magic charge (`wd.my` [0,1]; `Hwa`/`yL`)
    int raid_bullets = 0;  // raid charge bullets (`wd.dO`; `vZa` adds)
    bool collidable = true;  // target hit list (`Nl.oI[].vZ` — `hq.S`)
    // Native cp==7 edge (JS `ca.Lgb` L387 -> `PC(7,side)`): set by
    // `apply_hit` when a Fall reaction starts, consumed by `rules_frame`
    // the same frame (cleared at the top of `update`).
    bool reaction_fall = false;
    std::set<std::string> prev_intervals;  // last tick's intervals (12/13 edge)
    std::vector<sf2::scene::PerkAction> perks;  // equipped perk actions
                                                // (empty until perk-equip
                                                // mapping lands)
    std::vector<sf2::scene::ActiveMod> dots;  // ticking DoTs/HoTs
    std::string weapon = "Fists";  // wielded weapon (disarm identity)
    // JS `wd.K0` (L505): `parameters.ig != null && parameters.ig.Yb ==
    // "NoRanged" ? 1 : -1`. `ig` is the equipped "NoRanged" item (type
    // `I.Vh`, `vzb` L108540); `ranged_available` is its negation. Fed by
    // the equipment setup; defaults true (no NoRanged item -> K0 = -1).
    bool ranged_available = true;
    int hits_landed = 0;
    int hits_taken = 0;
    int combo_run = 0;        // consecutive landed hits (resets when taken)
    int max_combo = 0;        // battle-best run (prize ComboCount factor)
    int shocks_dealt = 0;     // shock hits landed (prize Shock factor)
    int moves_started = 0;
    std::string last_move;    // current move name (for the log/HUD)
    std::string last_decision;  // the AI's last decision (log)
    int last_ai_stage = -1;
    // The fighter's move list is built from the shared move map + weapon.
    std::vector<const sf2::scene::MoveDef*> hb;
    // [fx] The strike capsule's endpoint positions at the END of the previous
    // tick, keyed by capsule (edge) name: `HitCapsule.r1/r2` are the JS
    // `sx.ma`/`Zs.ma` (current world endpoints); this map holds the previous
    // frame's `sx.mf`/`Zs.mf`. JS `Hyb`'s hit direction is
    // `(sx.ma-sx.mf)+(Zs.ma-Zs.mf)` (L395). Snapshotted once per tick
    // (after the hit pass) so `apply_hit` reads the true previous frame.
    std::map<std::string, std::pair<sf2::scene::Vec3, sf2::scene::Vec3>>
        prev_cap_ends;
};

// One finished round's result (for the demo log + the next-round flow).
struct RoundOutcome {
    round_result result = round_result::ko;
    int round_number = 0;     // the round that just ended
    const FightFighter* winner = nullptr;   // the round winner
    const FightFighter* loser = nullptr;    // the round loser (or KO'd)
    std::string reason;       // "KO" / "TIMEOUT" / "RINGOUT"
    float player_hp = 0.0f;   // end-of-round HP (pre-recovery)
    float enemy_hp = 0.0f;
    bool battle_over = false; // true when the battle ended with this round
};

// The one-line-per-second fight log entry (the demo prints these).
struct FightLogLine {
    int frame = 0;
    int phase = 0;
    int round = 0;
    int timer = 0;            // the HUD countdown seconds (NF)
    std::string p_move, e_move;
    float p_hp = 0.0f, e_hp = 0.0f;
};

// The fight camera controller — an exact port of the JS camera chain
// `ql` (L362-371) + `Ut.Al` (L826-827) + `ma.Sya` (L1833-1835):
//
//   - focus: `tyb` (L363) sets the target to the midpoint of the two
//     fighters' Center-Of-Mass nodes (`wd.mea` -> `Dl.mea(a.Eu,b.Eu)`,
//     the native equivalent is the fighter world_x/world_y anchor);
//     `dZa` (L363-365) smooths the focus with the exact JS deltas:
//       $X = target - prevTarget (prediction),
//       bY = prevFocus + $X, aY = bY - focus, IO = (target - focus)*0.15,
//       LO = aY + IO; |LO| > 200 -> clamped to 200 (velocity clamp);
//       focus += LO; |focus - prevFocus| > 50 -> clamped to 50
//       (per-frame delta clamp).
//   - the camera starts at the fight spawn midpoint (`Lb.z9a` L475) and
//     chases the target — the intro ramp the oracle shows
//     (cy -101.5 -> -222 over the first ~60 frames). The VERTICAL target
//     is the arena-floor anchor (the oracle's CoM-mid for its dummy fight
//     settles on the dojo floor line; the native keeps the floor line at
//     0.78 of the view height — the value the pixel-diff verified).
//   - the panorama (`Ut.Al`): Io = arenaWidth/2 - focus is clamped to
//     +/-((arenaWidth - MaxWidthDelta)*Bj*0.5 - nC*0.5), applied to the
//     camera x (the native Io = arena_center_x - center_x).
//   - the zoom (`ma.Sya`): f = viewH / (arenaH*Bj), aspect clamp 0.45..1,
//     the narrow-screen extra clamp, the width-fit min(viewW/(span*f+100),1)
//     (NO +300 — xCa's +300 is the LAYER zoom), the min-zoom
//     0.6..1.3 `0.6+((clamp(c,0.5,1)-0.5)/0.5)*0.7` (BJ=1 at 16:9: -> 1.3),
//     and the portrait vertical shift round((viewH - e*f)/2)/f*0.5.
//
// `framing()` rebuilds center/zoom each fight frame. State lives in the
// struct so the smoothing runs continuously across frames (JS ql state).
struct FightCamera {
    // The smoothed focus (JS `Go.ma`) and the Sya zoom (JS `f`).
    float center_x = 0.0f;
    float center_y = 0.0f;
    float zoom = 1.0f;         // the RENDER zoom (JS `ma.Sya` f — the
                               // camera's tMa): 1.3 at 16:9 with a tight
                               // fighter span
    float zoom_layer = 1.0f;   // the LAYER zoom Bj (JS `Ut.Bj` = `xCa()`:
                               // min(nC/(span+300),1) — 1.0 at the fight
                               // start). This is what the oracle trace
                               // records as "zoom" (the trace hooks
                               // `Ut.Al`, which receives this.Bj), and what
                               // the panorama clamp (Ut.Al `d`) uses.

    // Arena geometry (from the location params).
    float arena_w = 1960.0f;    // the RAW location width (JS `Lb.width`)
    float arena_h = 560.0f;     // JS `Lb.height`
    float wall = 80.0f;
    float floor = 80.0f;        // the visible arena floor line (world y)
                                // (set_bounds feeds the dojo floor anchor)

    // --- JS `ql` dZa/tyb smoothing state (L362-365) ----------------------
    // Du/By = the current target (fighter midpoint), DO = the previous
    // target; Go/Jl = the smoothed focus, iq = the previous focus.
    bool initialized_ = false;
    float go_x_ = 0.0f, go_y_ = 0.0f;            // Go.ma (current focus)
    float go_prev_x_ = 0.0f, go_prev_y_ = 0.0f;  // iq/Go.mf (previous focus)
    float du_x_ = 0.0f, du_y_ = 0.0f;            // By/Du.ma (current target)
    float du_prev_x_ = 0.0f, du_prev_y_ = 0.0f;  // DO/Du.mf (previous target)
    float start_x_ = 0.0f, start_y_ = 0.0f;      // Lb.z9a (spawn midpoint)

    // --- the hit-effects judder + hit-stop (JS `d3a`/`Fnb`/`DL`, L362-371) --
    // The camera latch `DL(a)` (L370): `this.hw=a, this.U1=!0, this.N3=a.YIa,
    // this.wR=!0, this.N5=this.cU=a.jz, this.gh(0,null)` where `a` is the
    // `<HitEffect>` row `ZAa` (L422) selected by hit type. `Fnb` (L364):
    //   `U1 && (N3<=0 && (U1=!1, Bob()), N3--)`   — the pause (hit-stop)
    //   `wR && (cU<=0 && (wR=!1, Cwb()), cU--)`   — the judder
    // `d3a` (L363): while `wR`, the camera offset is
    //   `x = mva*b*sin($za*a*h)*(g-h)/g`, `y = nva*b*sin(aAa*a*h)*(g-h)/g`
    // with `g=N5`, `h=N5-cU` and `a`/`b` the `ce.Bub` trajectory scale —
    // the shipped config (`Bub:{lva:{x:.75,y:.3},Zza:{x:1,y:.75},
    // j_:{x:10,y:30}}`) resolves BOTH interpolations to constants
    // (`50>=e` : `b=lva.y=0.3`, `50>=a` : `a=Zza.y=0.75`) — verified, so no
    // per-effect trajectory data is missing.
    bool hit_effect_valid_ = false;   // `hw != null`
    sf2::scene::HitEffect hw_;        // `hw` — the latched effect row
    bool pause_active_ = false;       // `U1`
    int pause_frames_ = 0;            // `N3`
    int effect_frames_ = 0;           // `cU`
    int effect_total_ = 0;            // `N5`
    bool shake_active_ = false;       // `wR`
    float shake_x_ = 0.0f;            // the `Byb(x,y)` camera-node offset
    float shake_y_ = 0.0f;
    // The peak |offset| reached during the live judder (report only).
    float shake_peak_x_ = 0.0f;
    float shake_peak_y_ = 0.0f;
    // JS `ql.DL(a)` (L370): latch the hit effect + arm the pause/judder.
    void apply_hit_effect(const sf2::scene::HitEffect& e);
    // JS `ql.Fnb()` (L364) + `ql.d3a()` (L363), once per camera update.
    void tick_hit_effect();
    // The latched effect's type (log/report); "" when none.
    const std::string& hit_effect_type() const { return hw_.type; }
    int hit_stop_frames() const { return pause_frames_; }
    bool hit_stop_active() const { return pause_active_; }

    // Recomputes center/zoom from the two fighters' world COM positions
    // (JS `Eu.ma` — the native fighter world_x/world_y anchors) and the
    // view size. An exact port of the JS camera chain (see the struct
    // comment); the `ay`/`by` are the CoM y's (the oracle's CoM-mid
    // vertical target — the native maps it to the floor anchor so the
    // dojo's verified composition holds).
    void framing(float ax, float ay, float bx, float by, float view_w, float view_h);
};

// The fight HUD (JS `Ar` L2016-2019 + `Sf` L2032-2040): HP bars, the round
// timer, the round indicators and the FIGHT!/Round labels. The port renders
// a FUNCTIONAL HUD — HP bars (HealthBar_Full/Empty/Hit), the timer digits
// and the round dots at the JS layout positions — with the fight/ui atlas.
// The exact per-pixel layout of the JS `layout()` (L2036-2037) is
// approximated: the player bar at the left, the enemy bar at the right,
// the timer centered, the round dots between the bars.
class FightHud {
public:
    // Builds the HUD. `atlas_frames` maps an atlas frame NAME to its rect
    // in texture pixels (from the fight/ui.json "filename" fields, e.g.
    // "HealthBar_Full"); `tex_w`/`tex_h` are the atlas texture size;
    // `digits_frames` maps a digit character ('0'..'9') to its frame rect
    // (from digits.fnt); `digits_tex` the digits atlas texture.
    void init(const std::map<std::string, sf2::data::Texture>& /*unused*/,
              const std::map<std::string, sf2::data::atlas_frame>& atlas_frames,
              float tex_w, float tex_h,
              const std::map<char, sf2::data::atlas_frame>& digits_frames,
              float digits_tex_w, float digits_tex_h,
              std::function<GLuint(const std::string&)> texture_lookup);

    // Per-frame HUD update: the timer countdown + the bar damage tween.
    // `running` is the round timer running flag (round.Vt); the countdown
    // decrements 1/sec (JS Sf.iPa L2035).
    void update(float dt, bool running);

    // Recomputes the layout from the view size (JS Sf.layout L2036-2037).
    void layout(float view_w, float view_h);

    // Renders the HUD through the renderer (screen-space).
    void render(sf2::render::Renderer& r);

    // The per-fighter HP + the fight state the HUD reads each frame.
    void set_hp(float player_hp, float player_max, float enemy_hp, float enemy_max);
    void set_round(int round_number, int rounds_total);
    void set_timer(int seconds);          // the displayed countdown
    void set_labels(const std::string& player_name, const std::string& enemy_name);
    void set_phase(fight_phase phase);    // shows FIGHT! / Round N labels

private:
    // One HP bar: the empty (bg), full (current) and hit (trailing) bars.
    struct HpBar {
        bool is_player = true;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float ratio = 1.0f;        // current HP / max
        float hit_ratio = 1.0f;    // trailing (damage) bar
        sf2::scene::Sprite empty, full, hit;
    };
    HpBar player_bar_, enemy_bar_;

    // Round indicators (Round_Done/Round_Undone dots).
    std::vector<sf2::scene::Sprite> round_dots_;

    // Timer digits + the FIGHT!/Round label.
    sf2::scene::Sprite timer_sprite_;      // the timer text (rendered to a
                                           // glyph strip by the demo)
    std::vector<sf2::scene::Sprite> timer_glyphs_;
    sf2::scene::Sprite label_sprite_;

    bool ready_ = false;
    std::function<GLuint(const std::string&)> texture_lookup_;
    std::map<std::string, sf2::data::atlas_frame> frames_;
    std::map<char, sf2::data::atlas_frame> digit_frames_;
    float tex_w_ = 0.0f, tex_h_ = 0.0f;
    float digits_tex_w_ = 0.0f, digits_tex_h_ = 0.0f;
    int timer_seconds_ = 0;
    int round_number_ = 0, rounds_total_ = 2;
    std::string player_name_ = "PLAYER", enemy_name_ = "ENEMY";
    fight_phase phase_ = fight_phase::idle;
    float view_w_ = 1280.0f, view_h_ = 720.0f;
};

// Exact `Fh.lXa` accumulator (JS L2054-2056 + `Kx` L2056, `Fh` ctor):
// `Rva+=prize; PY+=Vk(b); OY+=gems; P3+=Vk(trunc(ceil(a*d)*d6+.5));
// ep+=Vk(trunc(ceil(a*e)*c6+.5)); Ui+=Vk(trunc(ceil(a*f)+.5))*jU;
// DZ+=Vk(trunc(ceil(a*h[b6])+.5)); Ub+=Vk(trunc(ceil(a*g)*e6+.5));
// m6=PY+P3+ep+Ui+DZ+Ub; mOa=OY` with `Vk(a,b=0)=ceil(a/10^(kq-b))`
// (kq = DenominationDigits, absent in seed -> 0).
// Fh counters init 0; `d6`++ only via victory `gXa` (post-lXa, so 0 at
// reward time with fresh-per-battle Fh); `c6`/`e6` via strike flags
// (`cvb`/`yvb`: first-hit/crit-shock of the round); `jU` never set (0);
// `b6` max style (untracked -> Turtle 0). `pk` = EAa order
// (Turtle 0, Hard 3, Brutal 6, Aggressive 9, Crazy 12, Fantastic 15).
struct PrizeFh {
    int d6 = 0, c6 = 0, jU = 0, e6 = 0, b6 = 0;
};
struct PrizeKx {
    double rva = 0, py = 0, oy = 0, p3 = 0, ep = 0, ui = 0, dz = 0, ub = 0;
    double m6 = 0, mOa = 0;
};
inline double prize_vk(double a, int kq = 0) {
    double div = 1.0;
    for (int i = 0; i < kq; ++i) div *= 10.0;
    return std::ceil(a / div);
}
inline void fh_lxa(PrizeKx& oc, const PrizeFh& fh, double prize, double coins,
                   double gems, double Ia, double epF, double UiF, double UbF,
                   const double* pk, int kq = 0) {
    oc.rva += prize;
    oc.py += prize_vk(coins, kq);
    oc.oy += gems;
    oc.p3 += prize_vk(std::trunc(std::ceil(prize * Ia) * fh.d6 + .5), kq);
    oc.ep += prize_vk(std::trunc(std::ceil(prize * epF) * fh.c6 + .5), kq);
    oc.ui += prize_vk(std::trunc(std::ceil(prize * UiF) + .5), kq) * fh.jU;
    oc.dz += prize_vk(std::trunc(std::ceil(prize * pk[fh.b6]) + .5), kq);
    oc.ub += prize_vk(std::trunc(std::ceil(prize * UbF) * fh.e6 + .5), kq);
    oc.m6 = oc.py + oc.p3 + oc.ep + oc.ui + oc.dz + oc.ub;
    oc.mOa = oc.oy;
}

struct PerkSetup {
    const std::map<std::string, sf2::scene::PerkDef>* catalog = nullptr;
    std::vector<sf2::scene::ItemPerkRef> player_refs;
    std::vector<sf2::scene::ItemPerkRef> enemy_refs;
    std::vector<std::string> player_items;  // equipped names (Item conds)
    std::vector<std::string> enemy_items;
    const std::map<std::string, sf2::scene::TacticDef>* tactics = nullptr;
};

// The fight controller (JS `ca` L379-433).
class FightController {
public:
    ~FightController();  // closes the pose dump file if the dump is cut short

    // Loads the fight: builds the two fighters, their move lists, the AI
    // controllers, and sets the spawn positions (JS `o1a` L403 + `ggb`
    // L383).
    void init(const BattleParams& battle,
              const sf2::scene::Model& model,
              const std::map<std::string, sf2::scene::MoveDef>& moves,
              const std::map<std::string, sf2::data::anim_clip>& clips,
              const std::vector<sf2::scene::TacticsFile>& tactics,
              const sf2::scene::TacticDef* tactic,
              const std::string& player_name,
              const std::string& enemy_name,
              float player_x, float player_y,
              float enemy_x, float enemy_y,
              int player_max_hp, int enemy_max_hp,
              std::function<float()> roll01,
              const PerkSetup& perks = PerkSetup());

    // Variant that builds the PLAYER's move list from its OWNED items via
    // the Locks test (JS `ra.Hza` L684-685 — `f.nw(d,b)` against the
    // fighter's items) instead of the TacticWeapon string. `owned` is the
    // player's item list as `OwnedItem` (Type / SubType / Name — the three
    // fields `Hm.he` L758 compares). The enemy stays TacticWeapon-based
    // ("Fists"). Used by the shop/equipment flow: equipping a weapon adds the
    // weapon's Locks-matching moves to the player's move list.
    void init_locks(const BattleParams& battle,
                    const sf2::scene::Model& model,
                    const std::map<std::string, sf2::scene::MoveDef>& moves,
                    const std::map<std::string, sf2::data::anim_clip>& clips,
                    const std::vector<sf2::scene::TacticsFile>& tactics,
                    const sf2::scene::TacticDef* tactic,
                    const std::string& player_name,
                    const std::string& enemy_name,
                    float player_x, float player_y,
                    float enemy_x, float enemy_y,
                    int player_max_hp, int enemy_max_hp,
                    std::function<float()> roll01,
                    const std::vector<sf2::scene::OwnedItem>& player_owned,
                    const PerkSetup& perks = PerkSetup(),
                    // JS `Da.IT` reseed hook for the shared fight stream
                    // (`Da.pg` analog). See `rules_begin_round` (`cl.pmb`).
                    // nullptr = leave the stream untouched.
                    std::function<void(int)> reseed01 = nullptr,
                    // Each fighter's OWN merged model (JS `xc.cM` rebuilds
                    // every warrior from its OWN equipment -> `Yc.load`).
                    // nullptr = the shared `model` for that side. The dojo
                    // Punchbag passes `assets.merged_bag` as `enemy_model`
                    // (its <Items> = PunchingBag + SkeletonPunchingBag,
                    // stages.xml L14-15).
                    const sf2::scene::Model* player_model = nullptr,
                    const sf2::scene::Model* enemy_model = nullptr,
                    // P4b: the PLAYER's roulette tactic. JS `IKa` (L672):
                    //   `this.pb.NT(this.tC);                       // enemy
                    //    this.yb.parameters.Fj && (this.kc.Gc != null ?
                    //        this.yb.NT(this.kc.Gc) : this.yb.s5("Standard"));`
                    // i.e. the ENEMY gets the battle warrior's tactic
                    // (`tC = Zb.Gc`) and the PLAYER gets its OWN save
                    // warrior's `<Tactic>` when it resolves, else "Standard".
                    // `tactic` is the enemy's; nullptr here = fall back to it
                    // (keeps the ai_demo AI-player path unchanged).
                    const sf2::scene::TacticDef* player_tactic = nullptr);

    // Seeds the OWNED fight stream (JS `Da.pg=new Rk(L.seed)`, L67). Every
    // fight draw - rules (`cl.pmb`/`pn.M4`), combat (`Lcb`/`R8a`), the AI
    // (`de.ia`) and conditions (`Random`) - uses this ONE stream unless a
    // `roll01` override was injected (the demo/probe path).
    void set_seed(std::uint32_t seed) { prng_.seed(seed); }

// Perk setup for fight init (`ZOa`/`Pma` analog, §5.4/§5.7): the parsed
// perk catalog (res/perks.xml) + per-side equipped item→perk bindings
// (list.xml `<Perks>`/`<Enchantments>`). Empty refs = no live triggers
// (same as now). Tactic defs by name enable SetTactic (`qpb`/`yZa`).

    // The arena bounds (wall / width-wall / floor) — set by the caller
    // (JS `ca.ggb` L383: v.tFa = location.NU, v.NKa = location.width - NU).
    void set_bounds(float wall, float wall_max, float floor_y);

    // JS `nj` (L885, ERuleRingout): configure the off-screen marker arrows
    // (`sXa` L827-828). The native sim has no ERuleRingout engine (the
    // stages.xml `<Ringout .../>` combat rule is not simulated), so the
    // controller feeds this from the parsed stage rule each round
    // (BattleParams.ringout_*; see apply_stage_ringout_rule): `min_x`/
    // `max_x` are `ZG`/`BH` and `speed` is `tta` (SequentionSpeed default
    // 3). The arrows show while a round is live (JS `f_a` L897) and hide at
    // the round-end cleanup (JS `$_a` L427 -> `onb`/`pnb` L828).
    // Presentation only - never touches the simulation (no RNG).
    void set_ringout_rule(bool enabled, float min_x, float max_x, float speed);

    // Modes setup path (tournament/survival `ModeFight`): rounds/time/
    // recovery, per-side DamageFactor rules, NoBullets flag, enemy
    // rebuild (items/tactic/attrs/perks). Called post-init by the
    // battle flow when a stages.xml battle lands.
    void apply_mode_setup(const ModeSetup& setup);

    // Per-frame fight update (JS `ca.Ea` L385 -> `ia` L388). `dt` is the
    // fixed 60 Hz step (1/60). Runs the phase machine, the per-fighter
    // update (AI or input + move execution + physics), hit detection +
    // damage, the round-end checks and the round/battle transitions.
    void update(float dt);

    // The player's input (buffered into the player fighter; JS HUD buttons
    // -> `ca.cka`/`ca.U4`). The demo auto-attacks, so this is optional.
    void player_input(sf2::scene::key_type key, sf2::scene::press_type press);

    // Simple auto-attack driver (the game's `FightAuto` / the demo's
    // "simple auto-attack"): in phase 2, when no move is playing, the
    // player steps toward the enemy when far and punches when in reach.
    // Uses the same move-start path as the AI (bypasses the key buffer).
    void set_auto_attack(bool on) { auto_attack_ = on; }
    // Fight-rule marker (`ERuleNoBulletsReplenishment`, cj L18E): skips the
    // my->bullet conversion in `la_normalize`. Stream 2 verdict: `replenish`
    // has 0 JS hits (no refill path exists statically; likely a round-start
    // refill in the native-driven flow, or a dead marker — Survival-only).
    // Kept as a plumbed default-off hook.
    void set_no_bullets_replenish(bool on) { no_bullets_replenish_ = on; }

    // Suppresses the fight-entry gong (`rb.Wkb` -> `snd_gong`). The JS fires
    // it from the battle-registration success branch (`sf2.502f0946.js`
    // L1216 `d && rb.Wkb()`), NOT from the `m1a` fight factory the Dojo hub
    // uses (`Tf.init` L1971). Must be set BEFORE `init_locks`. Default off.
    void set_silent_entry(bool on) { silent_entry_ = on; }

    // JS `ca.o1a` L403 (`type=="FightNone" ? a() : ...`) + `ca.kg` L387
    // (`this.Da.type!="FightNone" ? this.Am() : this.xF(2)`): the hub's
    // `FightNone` viewer enters phase 2 DIRECTLY — no StartStance wait, no
    // FIGHT!/ROUND plate, no round timer, no KO/timeout. `xF(2)` (L387-388)
    // also arms the virtual pad (`Za.F().nla(!0)` -> `Za.F().isVisible=true`).
    // Pins the controller in the fight phase: the round-end checks
    // (`Onb` -> `E3a`) never run while this is set.
    void enter_fight_none();

    // [trace, Phase 0] Arms the per-frame pose dump: for the first `frames`
    // fight frames, update() appends one JSONL line to `path` (reference/
    // traces/native_pose.jsonl). Pure trace — the simulation is untouched.
    void set_pose_dump(const std::string& path, int frames);

    // [FIX Phase 4b — fighter color from the location] Sets the fill color
    // BOTH fighters' meshes are drawn with. The game's fighters are
    // silhouettes filled with the LOCATION's Root Color (dojo_params
    // `<Root Color="0x000000">`, JS `Na.cd`) — not a per-fighter team
    // color. The fight screen calls this with the loaded location's
    // root_color() after init_locks; the default is black.
    void set_fighter_color(std::uint32_t rgb) {
        fighter_color_ = rgb;
        player_.fighter.set_color(rgb);
        enemy_.fighter.set_color(rgb);
        // JS `Na.cd(Lb.N2)` (L824/L833): the SAME location Root Color fills
        // every effect sprite, so the hit sparks inherit it at the source
        // (`av` ctor / `ryb` spawn) — not just at the draw site.
        fx_.set_color(rgb);
    }

    // --- fight state accessors -------------------------------------------
    const FightFighter& player() const { return player_; }
    const FightFighter& enemy() const { return enemy_; }
    // Wielded weapons (disarm identity; JS `$b(Au)` vs `ownHd`, L394).
    // Defaults are Fists; the host sets the player's from the save.
    void set_fighter_weapons(const std::string& player_weapon,
                             const std::string& enemy_weapon) {
        player_.weapon = player_weapon;
        enemy_.weapon = enemy_weapon;
    }
    // Battle prize breakdown (JS `v.kD`/`bzb`/`Fh.lXa`, FLOW_STATIC 4.3).
    // Factors from internal_settings `<RewardsPrize>` (verified values):
    // Perfect $Ia=5, FirstStrike ep=2, ComboCount Ui=1, Shock Ub=3,
    // Styles pk (Turtle 0 .. Fantastic 15; port: style untracked -> 0).
    // Totals come from exact `fh_lxa` below (`m6`/`mOa`); `coins_bonus`
    // is the performance part (total minus base) for the results display.
    // `gems_bonus` (`hj.Uo`) has no evidenced fight source (Bonus stays
    // save-driven).
    struct BattlePrize {
        bool perfect = false;      // player took no hits
        bool first_strike = false;  // player landed the battle's first hit
        int max_combo = 0;         // player's best consecutive run
        int shocks = 0;            // player's shock hits
        int style_value = 0;       // style factor (untracked -> Turtle 0)
        int coins_bonus = 0;       // m6 minus base (display)
        int coins_total = 0;       // m6: what the player receives
        int gems_bonus = 0;        // mOa (no fight source evidenced)
    };
    BattlePrize prize(int base_coins) const;
    int phase() const { return static_cast<int>(phase_); }
    const RoundState& round() const { return round_; }
    bool battle_over() const { return battle_over_; }
    // JS: the between-round gate — true from a round's end until the
    // round-break banner (`Cr.tca` L2023) expires into `FNa` (phase 1).
    // The round auto-advances (`Onb` L411 `ZK(); NA(); Z2()`); there is NO
    // host "Next" button in the JS (the old click rect was an invention).
    bool round_wait() const { return round_wait_; }
    // The battle winner (null until the battle ends).
    const FightFighter* winner() const { return winner_; }
    const std::vector<RoundOutcome>& round_history() const { return history_; }
    // The arena bounds.
    float wall_min() const { return wall_min_; }
    float wall_max() const { return wall_max_; }
    float floor_y() const { return floor_y_; }
    // The current camera framing (JS ma.Sya).
    const FightCamera& camera() const { return camera_; }
    // The fight HUD.
    FightHud& hud() { return hud_; }
    // The fight frame counter (JS `ca.frame`).
    int frame() const { return frame_; }
    // The last frame's per-second log line (frame % 60 == 0).
    const FightLogLine& last_log_line() const { return last_log_; }
    // The visual effects layer (hit sparks) — presentation only, never
    // touches the simulation (its RNG is a private LCG, not roll01).
    const EffectSystem& fx() const { return fx_; }
    // The magic/effect containers (JS `tl.Rf`, L842-844) fed by the `Yl`
    // "Effect" trigger action (`tl.Nt` L842; split `Gq`/`Hq`). Presentation.
    const MagicEffects& magic_fx() const { return magic_fx_; }
    // The current center-screen banner (ROUND N / FIGHT! / K.O. /
    // VICTORY / DEFEAT) — presentation only.
    banner_kind banner() const { return cur_banner_; }
    // The banner's display text ("" for none).
    const char* banner_text() const;
    // The banner's progress through its hold, 0..1 (for the fade/scale).
    float banner_progress() const;

private:
    BattleParams battle_;
    sf2::scene::Model model_;
    const std::map<std::string, sf2::scene::MoveDef>* moves_ = nullptr;
    const std::map<std::string, sf2::data::anim_clip>* clips_ = nullptr;
    std::vector<sf2::scene::TacticsFile> tactics_;
    const sf2::scene::TacticDef* tactic_ = nullptr;
    // P4b: the tactic the PLAYER's roulette weighs with (JS `IKa` L672 —
    // the player's own resolved `<Tactic>`, else "Standard"). Kept separate
    // from `tactic_` (the battle warrior's = the ENEMY's) so the player is
    // never weighted by the enemy's tactic. nullptr = `tactic_`.
    const sf2::scene::TacticDef* player_tactic_ = nullptr;
    // External stream override (the demo/probe path). Empty -> the OWNED
    // `prng_` (JS `Da.pg`) is used for every fight draw.
    std::function<float()> roll01_;
    // JS `Da.IT` (L1210444/L2353) reseed hook for an EXTERNAL override
    // stream. `rules_begin_round` calls it once, immediately before the
    // `pn.M4` RandomRule draw pass, mirroring `cl.pmb` (L1413).
    std::function<void(int)> reseed01_;
    // The OWNED fight stream (JS `Da.pg`, L67: `Da.pg=new Rk(L.seed)`;
    // `Xx`+`Rk` L2352/2366). Shared by EVERY fight draw - rules, combat,
    // AI and conditions - like the game's single global `Da.pg`. Default
    // seed 0x5F2 (the native replay-seed analog).
    DaPrng prng_{0x5F2};
    // The shared fight draw (JS `Da.pg.jf()`, L2352): the external override
    // when installed, else the owned `prng_` (`Rk.s4(1)` == `Rk.jf`).
    float draw01();
    // JS `uf.RJa()` (L115) = `Math.random()` — the UNSEEDED stream `wd.R8a`
    // (L531) pulls its two shock rolls from. Deliberately NOT `Da.pg`:
    // routing them through the shared fight stream desynced crit/AI.
    static float math_random01();
    // JS `Da.IT(a)` (L2353: `Da.pg.sL(a)`): reseed the shared stream in
    // place (the `cl.pmb` reseed).
    void reseed_stream(int seed);
    // JS `wd.$db(a,b,c)` (L523: `this.i_.add(a,b,c)`) — the per-hit
    // accumulator fed `(Zi, i6a(SZ), JP)` in `Cgb` (L395). Same call order
    // as the HP spend, so the entries line up with the logged hits.
    struct DamageLogEntry {
        float zi = 0.0f;         // the post-`ws` Zi
        std::string attr;        // `i6a(SZ)` — the max-Shift attack attr
        std::string defense;     // `JP`
    };
    std::vector<DamageLogEntry> i_;

    FightFighter player_;          // JS `kc` (params) + `yb` (fighter)
    FightFighter enemy_;           // JS `Zb` (params) + `pb` (fighter)
    sf2::scene::TrigBus bus_;      // perk trigger bus (`tb`; ZOa registers)
    sf2::scene::PerkSetup perk_setup_;  // stashed for per-round Yka
    bool no_bullets_replenish_ = false;  // `ERuleNoBulletsReplenishment`
    const std::map<std::string, sf2::scene::TacticDef>* tactic_defs_ = nullptr;
    std::vector<std::string> player_items_;  // equipped names (Item conds)
    std::vector<std::string> enemy_items_;
    PrizeFh prize_fh_;             // JS `Fh` (fresh per battle via kD)
    // The fighter mesh fill color (the location Root Color; default black).
    std::uint32_t fighter_color_ = 0x000000u;
    RoundState round_;             // JS `round` ($t L1239)
    fight_phase phase_ = fight_phase::idle;  // JS `eu`
    int frame_ = 0;                // JS `ca.frame`
    bool battle_over_ = false;     // JS `xJ` (battle finished)
    const FightFighter* winner_ = nullptr;
    bool auto_attack_ = false;     // the demo's simple auto-attack driver
    bool round_live_ = false;      // JS `h9` — a round is in progress
    bool dga_ = false;             // JS `Dga` — a hit landed this round
                                   // (`ep` = !Dga, L394; init false L380)
    // Wave log: the last AI decision string, so the `[ai]` diagnostic line
    // (K2/pZ/strike-memory) prints once per change instead of every frame.
    std::string last_ai_log_;
    // Battle prize stats (JS `v.kD`/`bzb` factors, FLOW_STATIC section 4.3):
    // first-strike side, set on the battle's first landed hit (else none).
    bool battle_first_hit_ = false;  // any hit landed yet this battle
    bool battle_first_by_player_ = false;
    bool round_wait_ = false;      // JS: between a round's end and `FNa`
                                   // (the round-break banner holds it)
    // JS `ca.o1a` L403 / `kg` L387: the `FightNone` viewer (the Dojo hub)
    // pins phase 2 and skips the round flow (`Onb`/`E3a`) entirely.
    bool fight_none_ = false;
    // The fight-entry gong is a battle-registration artifact (see
    // `set_silent_entry`), not an `m1a` factory one.
    bool silent_entry_ = false;
    // The StartStance input buffer (JS `wd.WC` L426 + `llb` L429): ONE slot
    // - the FIRST press during phase 1 (StartStance) wins (JS `N0a` L426
    // `b.WC==-1&&(b.WC=a)`); it is replayed when the fight starts
    // (enter_fight) as if the player pressed now.
    sf2::scene::key_type start_buffer_key_ = sf2::scene::key_type::up;
    bool start_buffer_filled_ = false;
    // JS `Cl.ia` one-shot (`dW`, L566-567): last-tested (move, interval)
    // per attacker name - the same attack object never tests twice in a row.
    // Reset on every new move start (JS `wd.x3` -> `Fu.hob()`, dW=null), so
    // repeat swings of the same move re-test.
    std::map<std::string, std::pair<const void*, const void*>> cl_last_;
    std::map<std::string, const void*> cl_move_;
    bool start_stance_done_ = false;  // phase 1 -> 2 gate
    int start_stance_frames_ = 0;  // phase 1 hold counter
    int end_stance_frames_ = 0;    // phase 3 hold (the FIGHT!/KO banner)
    // --- the banner machine (JS class `Cr` L2022-2027) --------------------
    // `Cr.Sc` is a SECONDS countdown and `Cr.wU` the arm flag:
    //   `fu(a){this.Sc=a;this.X(!0);this.wU=!0;...}` (L2026)
    //   `aa(a){...!this.pause&&this.wU&&(this.Sc-=a,this.Sc<=0&&this.ONa())}`
    //   `ONa(){this.X(!1);this.wU=!1;this.yA.Z(this.type)}` (L2026)
    // `Cr.tca` (L2023) additionally holds `wU` false for a 500 ms
    // `wh.delay` before the countdown starts.
    banner_kind cur_banner_ = banner_kind::none;   // JS `Cr.type`
    float banner_time_ = 0.0f;     // JS `Cr.Sc` — hold remaining, SECONDS
    float banner_total_ = 0.0f;    // `Cr.Sc` at `fu` time (progress source)
    bool banner_armed_ = false;    // JS `Cr.wU` — the countdown is running
    float banner_arm_delay_ = 0.f; // JS `wh.delay(...,500)` — the 500 ms arm
    banner_action banner_action_ = banner_action::none;  // the `vhb` dispatch
    int banner_start_ = 0;         // frame_ when the banner was raised
    int banner_round_ = 0;        // the ROUND N number (banner_round_+1 shown)
    // The visual effects layer (hit sparks) — presentation only.
    EffectSystem fx_;
    // The magic/effect containers (JS `tl.Rf` = `Gq`/`Hq`, L842-844): the
    // `Yl` "Effect" trigger action (`Uh(a){a.gwb(this)}` L728 -> fighter `Nt`
    // bus -> `tl.Nt` L842) routes a started effect into `background()`
    // (`Gq`, OnBackground) or `foreground()` (`Hq`). Presentation only.
    MagicEffects magic_fx_;
    // JS `nj` (ERuleRingout) marker state (L885): `ZG`/`BH` axis bounds and
    // `tta` (SequentionSpeed). Set via set_ringout_rule; presentation only.
    bool ringout_rule_ = false;
    float ringout_min_ = -1.0e5f;  // JS `ZG` default (`of(a,-1E5,1E5)` L886)
    float ringout_max_ = 1.0e5f;   // JS `BH` default
    float ringout_speed_ = 3.0f;   // JS `tta` (SequentionSpeed default 3)
    // JS `ca.Iga` (init false L380; reset in `I0a` L409; set by `F1` L897
    // `ERuleInvertJoystick -> this.Oe.Iga=!0`): the input-inversion flag
    // consumed by `ca.LBa` (L399) in `N0a`/`O0a` (L426).
    bool invert_joystick_ = false;
    // --- stage <Rules> engine (JS `du` L894-910) --------------------------
    std::vector<FightRule> rules_;  // parsed rules (JS `du.Ae`)
    // JS `pn.CB` (L879): the chosen RandomRule child per group. The pick is
    // drawn once per fight (`EachFight`) or every round (`EachRound`); it is
    // cached here so `EachFight` groups keep the same choice across rounds.
    std::map<int, int> random_pick_;
    bool random_pick_done_ = false;
    int rule_round_ = 1;            // JS `cz` (`rob(round>0?round:1)`)
    bool rule_pending_ = false;     // JS `ca.Pu != null`
    round_result rule_result_ = round_result::ko;  // JS `ca.ey`
    bool rule_winner_player_ = false;  // JS `Pu.wfa()` winner resolution
    std::vector<RoundOutcome> history_;
    FightLogLine last_log_;
    float wall_min_ = 80.0f, wall_max_ = 1880.0f, floor_y_ = 0.0f;
    FightCamera camera_;
    FightHud hud_;
    float time_since_log_ = 0.0f;

    // --- pose dump (trace infrastructure, no behavior change) --------------
    std::string pose_dump_path_;
    int pose_dump_frames_ = 0;      // frames left to dump (0 = off)
    int pose_dump_written_ = 0;     // lines written so far
    std::FILE* pose_dump_file_ = nullptr;

    // Appends one JSONL line for the current frame to the dump file
    // (lazily opened on the first dumped frame; closed after the last).
    void dump_pose_frame();

    // --- fight helpers (JS ca methods) ------------------------------------
    // JS `xF` (L388): set the fight phase + sync the fighters' stance.
    void set_phase(fight_phase p);
    // JS `tx` (L407): round init (timer = round length, Vt = false).
    void round_init();
    // JS `tx` (L407) / `Z2` (L408-409): the AUTOMATIC round advance. Raised
    // by the round-end epilogue (`Onb` L411 `NA(); Z2()`) and by the
    // round-plate chain `ca.tx` -> `Cr.wca` (L2023) -> `ONa` (L2026) ->
    // `vhb` (L410) case 1. Increments the round counter, applies the
    // round resets and raises the round-break banner (`Cr.tca` L2023).
    void round_start();
    // --- the banner machine (JS class `Cr` L2022-2027) --------------------
    // JS `Cr.fu` (L2026): raise a banner and start its `Sc` countdown.
    void banner_show(banner_kind kind, float seconds, banner_action action,
                     bool arm_after_delay);
    void banner_tick(float dt);   // JS `Cr.aa` (L2027)
    void banner_expire();         // JS `Cr.ONa` (L2026) + the `ca.vhb` (L410)
                                  // dispatch carried by `banner_action_`
    // JS `FNa` (L409) / `Rkb` (L410) / `i4a` (L409): the phase transitions.
    void enter_start_stance();
    void enter_fight();
    void enter_end_stance();
    // JS `Onb` (L411): the round-end check (KO / timeout).
    void check_round_end();
    // --- stage <Rules> engine (JS `du` L894-910) --------------------------
    // JS `ca.F1` L428 -> `du.rob` L900 (`cz`, `osb`, ...): per-round reset +
    // activation by the `kI`/`Ti` gates; then `du.f_a` L896-897 shows the
    // Ringout markers (`ca.H1a` L390 -> `sXa`).
    void rules_begin_round(int round);
    // JS `ca.$_a` L427 -> `du.Iwb` L898 + `ca.onb` L390: round-end cleanup.
    void rules_end_round();
    // JS `ca.ia` L389 `PC(1,3)` -> `du.Ih(1,3,ze)` L896: the per-frame rule
    // detection pass (the Ringout `nj.hh` + the TimeOutWin timer end).
    void rules_frame();
    // JS `nj.hh` (L885-886): the tracked node leaves [ZG,BH]x[dN,HO] ->
    // `setActive(false)` + fire. Returns true when the rule fired.
    bool rules_ringout_detect(FightRule& r);
    // JS `en.ZZa` (L860-861): true iff EVERY HotGround `<Node>` zone has the
    // tracked fighter's node OUTSIDE it (all-outside = "safe"). Resolves each
    // zone's bone by name on `f`.
    bool rules_hot_zones_out(const FightRule& r, const FightFighter& f);
    // JS `jn.hh`/`Rba` (L866-867): LoseFall — arm `tN` from the tracked
    // fighter's current animation (cp==4 `Lba(a.AI)`, L848) or, for a
    // Physical rule, the fall reaction (cp==7), then the node-zone exit.
    // Returns true when the rule fired.
    bool rules_lose_fall(FightRule& r);
    // JS `du.Oob` (L901) + `ca.BT` (L392-393): apply a fired rule's effect
    // (Ringout -> `ey=4`; TimeOutWin -> `ey=2`) and record the winner.
    void rules_fire(FightRule& r);
    // JS `du.F1(a)` (L897) + `du.kZ` (L902) + `du.m_a` (L902-903): the
    // per-round rule apply pass — `Zk` on every active rule (Attributes
    // deltas, RemoveInterval, RechargeMagicEachRound, Tactic, the resets),
    // then the Invulnerability `ola` pass and the Resistance `dta` pass.
    void rules_apply_round_effects();
    // JS `du.kZ` (L902): the per-side `wV` (bit 10) AND over the active
    // Invulnerability / Combo / Crazy `!ws`, then the opposite fighter's
    // `ola(!b)` (`Rea(a==1?2:1)`). `side` 3 -> both.
    void rules_kz(int side);
    // JS `du.Oob` via the hit-scope `Ih(5/6/11,...)` calls (L896 + `ca.Cgb`
    // L396): the landed-hit rule effects — LifeSteal heal, Regeneration
    // counter reset, Points accumulation/fire, WinCombo/WinShock.
    void rules_on_hit(FightFighter& atk, FightFighter& def,
                      const sf2::scene::HitRecord& rec);
    // JS `f_a` L897 `ERuleRingout -> this.Oe.H1a(ZG,BH,tta)` (marker show).
    void rules_show_markers();
    // JS `E3a` L412-413 `wfa()` (L848): the winner for a fired `Pu` rule.
    // `true` = the player (kc) wins; `false` = the enemy (Zb).
    bool rule_winner_is_player(const FightRule& r) const;
    // JS `E3a` (L412): apply a round result (rounds-won, winner flags).
    void apply_round_result(round_result result, const FightFighter& winner,
                            const FightFighter& loser);
    // JS `bea` (L413): the battle end.
    void end_battle(const FightFighter& winner);
    // JS `NA` (L414): the between-round recovery (heal qDa, clear flags).
    void between_rounds_recover();
    // JS `vfa` (L413): the round winner by HP.
    const FightFighter& round_winner_by_hp() const;
    // The per-fighter update (AI / input + move execution + physics).
    void update_fighter(FightFighter& me, FightFighter& foe, float dt);
    // Stamps the condition context's geometry: the two fighter roots, the
    // signed gap, the move facing sign `Ae.Wl` (`Vi.SBa` L704) and the scene
    // wall bounds the `Object="Wall"` Distance refs resolve against (`ee.q9a`
    // L788). `me`/`foe` are the two sides in `To - From` = `foe - me` order.
    void fill_ctx_geometry(FightContext& ctx, const FightFighter& me,
                           const FightFighter& foe) const;
    // Builds one fighter (shared init helper). `weapon_subtype` selects the
    // TacticWeapon-based move list; `owned` (when non-empty) selects the
    // Locks-based list (JS `ra.Hza`). `not_ai` = JS `Fj==false` (NotAI, no
    // AiController); `not_animation` = JS `QD==false` (NotAnimation, no clip
    // attach — the dummy holds its bind pose); `model` = the fighter's own
    // model (nullptr = the shared fight `model`).
    FightFighter make_fighter(const std::string& nm, bool is_player, float x, float y,
                              int max_hp, const std::string& weapon_subtype,
                              const std::vector<sf2::scene::OwnedItem>& owned,
                              bool not_ai = false, bool not_animation = false,
                              const sf2::scene::Model* model = nullptr);
    // The hit test (JS `ca.Enb` -> `wd.tKa` -> `Fu.ia`).
    bool hit_test(FightFighter& atk, FightFighter& def, const sf2::scene::MoveDef& move,
                  int frame, sf2::scene::HitCapsule& hit_cap,
                  sf2::scene::CapsuleHit& ch,
                  const sf2::scene::Interval*& hit_interval,
                  const sf2::scene::HitCapsule*& atk_cap);
    // JS `wd.HZa` gate position (hzaGate L500-501): yD(4) pick + invuln
    // bypass check BEFORE geometry. Returns the interval to test, or null.
    const sf2::scene::Interval* hza_pick(const FightFighter& target,
                                         const sf2::scene::MoveDef& move, int frame);
    // Applies a landed hit (damage + knockback; JS `ca.Cgb` L394-397).
    // `atk_cap` is the attacker's strike capsule (`b.Py`, L395) - the source
    // of the `Hyb` hit direction (`sx/Zs .ma-.mf`); null when the interval
    // has no AttackingParts.
    void apply_hit(FightFighter& atk, FightFighter& def,
                   const sf2::scene::MoveDef& move, const sf2::scene::Interval& iv,
                   const sf2::scene::HitCapsule& hit_cap, const sf2::scene::CapsuleHit& ch,
                   int frame, const sf2::scene::HitCapsule* atk_cap = nullptr);
    // --- move `<Actions>` dispatch (JS `wd.BNa` L523) ----------------------
    // The ported action kinds: `Sound` (`wd.dwb` L519 -> `fm.fka` L735 gate +
    // `ta.ak(name, looped, volume)` L1264), `RandomSound` (`wd.fwb` L519 ->
    // `am.ab()` pick + `ta.ak`), `StopSound` (`wd.ewb` L519 -> `ta.Jwb`),
    // `SetEndStage` (`cm.Uh()` L738 — empty), `ShakeScreen` (`wd.Wvb` L519 ->
    // `Pi.uS` L424 -> `ql.DL` L370 = `FightCamera::apply_hit_effect`),
    // `CameraWeight` (`wd.ANa` L520 -> `Pi.fS` L424 `{debugger}` — a no-op),
    // `EnableBossAbility` (`wd.$vb` L520 -> `Pi.dS` L397 `{debugger}` — a
    // no-op) and `AddBullets` (`wd.Tvb` L519 -> `hZ`+`LA` L505 MagicBullet /
    // `vZa`+`Amb` L524 RaidChargeBullet).
    // `owner` supplies `xc.voice` for the `fka` gate and is MUTATED by
    // `AddBullets` (the JS `wd` IS the fighter — `hZ`/`vZa` write its
    // `bh`/`dO`); `why` tags the log line ("frame", "Strike", "Hit",
    // "AnimationEnd"). `conds` is the condition context used for each
    // action's own `<Conditions>` (JS `cb.Ti` L724, `Fd($c)` -> true when
    // empty).
    void dispatch_move_actions(
        const std::vector<const sf2::scene::MoveAction*>& acts,
        FightFighter& owner, const char* why,
        const sf2::scene::FightContext& conds);
    // JS `uf.sja(a)` (L115): `Math.floor(uf.OKa.RGa() * a)` with
    // `at.Nlb` (L115) = `Math.random()` — an UNSHARED global stream, NOT the
    // fight's `Da.pg`. Native: `math_random01()` (the pinned mulberry32 —
    // see `R8a`'s note), so the pick never perturbs `draw01()`.
    int random_sound_index(int n);
    // Rebuilds a fighter's physics body from its current pose.
    void rebuild_body(FightFighter& f, const FightFighter& foe);

public:
    // --- root `<Triggers>` (JS `Fa.Exb` L708 -> `ra.Dm`) --------------------
    // The GLOBAL trigger set (the parsed root `<Triggers>` block), handed in
    // by the app before `init`; `setup_bus` (inside init, and per round)
    // lock-filters + registers it per side. A setter rather than another
    // `init` parameter keeps the two `init` overloads and their call sites
    // untouched.
    void set_global_triggers(const std::vector<sf2::scene::GlobalTrigger>* triggers) {
        global_triggers_ = triggers;
    }
    // Number of global triggers whose `<Locks>` passed for `side` (0 =
    // player, 1 = enemy). Diagnostics only.
    std::size_t global_trigger_count(int side) const {
        return side == 0 ? global_me_.size() : global_enemy_.size();
    }
    // The parsed global set's action count, for the report.
    std::size_t global_action_kinds() const;
    // Whether the port dispatches this global action kind (`lz.create` name).
    static bool global_kind_dispatched(const std::string& kind);

private:
    // `ra.yz`/`Su.nw`: lock-filter `*global_triggers_` against `conds`.
    void register_global_triggers(const sf2::scene::FightContext& me_ctx,
                                  const sf2::scene::FightContext& enemy_ctx);
    // Evaluate the lock-passing global triggers of BOTH sides whose `<Events>`
    // contain `event` (the MOVE event name, `kz.create`) and whose
    // `<Conditions>` pass, dispatching their supported actions through
    // `dispatch_move_actions` (owner side's fighter + context).
    // `value` is the event payload for the subclasses whose `compare`
    // filters on it (`Sm` ModExpires L770: exact `Ki==data`; `Tm`
    // RoundStageStart L772: `iz.XBa(Ki)==data`; `Km` AnimationStart L766:
    // `Ki==""` or `Ki` in the owner's animation list). An event node with a
    // non-empty `Name` that does not equal `value` is skipped; `nullptr`
    // (Strike/Hit) keeps the name-agnostic behaviour the earlier waves had.
    // `side` = 0/1 restricts the scan to that side's registered set (the
    // per-model publishers: ModExpires/AnimationStart); -1 scans both.
    // `hit` (Hit/Strike only) is the landed-hit context: its `has_last_hit`/
    // `last_hit_type`/`last_hit_animation` (JS `sm.he` reads `a.IL`) are
    // copied into the per-side context so the global `<Hit>` trigger
    // conditions (CriticalEffect/BlockEffect/HitEffect) evaluate.
    void dispatch_global_triggers(const char* event_name, const char* why,
                                 const char* value = nullptr, int side = -1,
                                 const sf2::scene::FightContext* hit = nullptr);
    // The `<Triggers>` EveryFrame publish is per-frame, so its
    // informational line is printed once per side+trigger (the dispatch
    // itself runs every frame — only the log is deduped).
    std::set<std::string> global_logged_;

    const std::vector<sf2::scene::GlobalTrigger>* global_triggers_ = nullptr;
    std::vector<const sf2::scene::GlobalTrigger*> global_me_;
    std::vector<const sf2::scene::GlobalTrigger*> global_enemy_;

    // --- perk trigger bus (`tb`) -----------------------------------------
    // `ZOa` (L398-399): build live trigger sets from the PerkSetup and
    // register both sides (`Gf`).
    void setup_bus(const PerkSetup& perks);
    // Condition context for one side (0 = player, 1 = enemy).
    // `hit_dmg` feeds `?Hit[].Damage` (the in-flight mg.Damage).
    sf2::scene::CondCtx cond_ctx(int side, double hit_dmg = 0.0);
    // Fire a hit-scope slot (6/7) and execute: combat actions fold into
    // `rec`/fighters via `decide_hit_perks`, state actions apply now.
    // `out_has_damage/out_damage` report the last SetHit Damage (`ppb`
    // sets `Zi` directly — the caller bypasses the lethal clamp).
    void run_bus_hit(int slot, const sf2::scene::TrigVars& vars, int fired_side,
                     sf2::scene::HitRecord& rec, FightFighter& atk, FightFighter& def,
                     int depth = 0, bool* out_has_damage = nullptr,
                     float* out_damage = nullptr);
    // Execute one non-hit action (mod registry / vars / logs). `owner_side`
    // is the trigger owner's side; `foe_side` the other.
    void exec_action(const sf2::scene::PerkTrigger& t, const sf2::scene::PerkAction& a,
                     int owner_side, int depth = 0);
    // EveryFrame tick + mod `ia` tick + interval edge detect for one side.
    void tick_bus_side(int side);
    // `ia` mod tick for one side (Uf countdown, JNa, slot-14 publish).
    void tick_mods(int side);
    // Shared JNa state bindings for the mod tick.
    sf2::scene::ModTickCtx mod_tick_ctx();
    // Magic/bullet normalize (`LA` without the link branch) + slot-8 publish.
    void la_normalize(FightFighter& f);
    void fire_slot8(int side);
    // Magic per-fighter init + per-round reset (`Ka`/`yKa`).
    void init_magic();
    // One fighter's magic reset (JS `wd.yKa` L504: `zL(0)`, `yL(
    // InitialCharge)`, `LA`) — shared by `init_magic` and the
    // `ERuleRechargeMagicEachRound` per-side apply (`fmb` L397).
    void reset_magic_fighter(FightFighter& f);
    // Samples the fighter's idle pose (JS: the weapon stance idle).
    void sample_idle(FightFighter& f);
    // The enemy's initial/round pose: the stance idle, or — when the stage
    // Warrior is NotAnimation (JS `QD` L195) — its BIND pose (a 1-frame
    // empty clip, exactly as the dojo hub does at screens.cpp:3510-3512).
    void sample_enemy_idle();
    // The HUD countdown seconds (JS Sf.iPa: gma - round.time).
    int hud_timer() const;
};

} // namespace sf2::scene
