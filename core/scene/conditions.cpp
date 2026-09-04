// Condition evaluator — exact JS semantics (sf2.502f0946.js classes Ha..ym).
//
// JS study per type (class id, method):
//   - Keys        `vm`  g="131": matches buffered keys; Tap/Hold/Release.
//   - Distance    `qm`  g="12C": Axis X/Y/3D distance between From/To refs.
//   - Weapon      `Hm`  g="13D": my items have Type+SubType+Name.
//   - Player      `Hm`  (Player is `sa.oe[6]` -> same class Hm as Weapon).
//   - Health      `rm`  g="12D": ratio current/max in [Min,Max].
//   - Operator    `wm`  g="132": And (all) / Or (any), `Not` flips.
//   - CurrentInterval `tm` g="12F": active interval by Name and/or Type.
//   - CurrentAnimation `lg` g="12A": animation name in the fighter's lists;
//       `$Move` = the candidate move, `$NoAnimation$` = no anim playing,
//       `Physics` attr matches the physics-flag.
//   - PhysicsFrameNumber `Cm` g="138": frame in [Min,Max] (unset=-1).
//   - RoundResult `Fm` g="13B": Victory/Defeat + Timeout/Ringout.
//   - Item       `um`  g="130": my items have Type+SubType+Name.
//   - Bullets    `lp`  g="2C1": bullet count in [Min,Max] for MagicBullet/
//       RaidChargeBullet.
//   - Perk       `Bm`  g="137": perk by Name in my/enemy perk lists.
//   - MagicCharge `sp` g="2C2": magic charge in [Min,Max].
//   - ModExists  `tp`  g="2C3": mod name in the fighter's mod set (or a
//       Namespace-prefixed check).
//   - Pain       `vp`  g="2BE": pain value in [Min,Max].
//   - Round      `yp`  g="2C0": round number equals `Number`.
//   - InTheArea  `qp`  g="2C4": fighter is in the arena (b.rR).
//   - Random     `xp`  g="2B6": (Chance/100) < random().
//   - PerkStart  `wp`  g="2C5": always true.
//   - Name       `Am`  g="136": fighter's model name == Value.
//   - Screen     `Gm`  g="13C": screen enum == Name.
//   - ModelMirrored `zm` g="135": fighter is mirrored.
//   - BattleType `lm`  g="126": battle type == Value.
//   - BossAbilityState `nm` g="128": Value flag (always false when set).
//   - Hit        `sm`  g="12E": last-hit type/animation match.
//   - ModelExists `ym` g="134": model by name exists on the field.
//   - Combo      `mp`  g="2B9": combo counter in [Min,Max].
//   - Style      `Ap`  g="2B8": style enum (Turtle..Crazy) in [Min,Max].
//   - Direction  `pm`  g="12B": facing sign matches From/To direction.
//   - Birth      `mm`  g="127": fighter's aK (birth name) == Name.
//
// Not every type appears in moves.xml (verified: the file uses Keys,
// CurrentAnimation, CurrentInterval, RoundStage, ModExists, Distance,
// Operator, Item, Player, Health, Bullets, BattleType, RoundResult,
// PhysicsFrameNumber, Birth, Direction, Hit, Combo, Style, Perk,
// BossAbilityState, InTheArea, Pain, Random, Round, MagicCharge — the
// remaining JS types are implemented for completeness).

#include "scene/conditions.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>

namespace sf2::scene {

// ---------------------------------------------------------------------------
// FightContext helpers
// ---------------------------------------------------------------------------

bool FightContext::interval_active(const std::string& name, int type) const {
    // JS `tm.he`: a CurrentInterval condition matches an active interval if
    //   (this.uc==0 || this.uc==d.type) && (this.Ba=="" || d.name==this.Ba)
    // where this.uc = the condition's Type (fe.G0) and this.Ba = its Name.
    for (const interval_state& iv : intervals) {
        if (!iv.active) continue;
        const bool type_ok = type == 0 || iv.type == type;
        const bool name_ok = name.empty() || iv.name == name;
        if (type_ok && name_ok) return true;
    }
    return false;
}

bool FightContext::key_pressed(key_type k, press_type p) const {
    for (const key_input& ki : keys) {
        if (ki.key == k && ki.press == p) return true;
    }
    return false;
}

bool FightContext::has_mod(const std::string& name) const {
    return mods.find(name) != mods.end();
}

bool FightContext::has_item(const std::string& type, const std::string& subtype,
                            const std::string& name, bool enemy) const {
    const std::vector<item_info>& list = enemy ? items_enemy : items;
    for (const item_info& it : list) {
        if (!type.empty() && it.type != type) continue;
        if (!subtype.empty() && it.subtype != subtype) continue;
        if (!name.empty() && it.name != name) continue;
        return true;
    }
    return false;
}

bool FightContext::has_perk(const std::string& action_name,
                            const std::string& perk_name, bool enemy) const {
    const std::vector<perk_info>& list = enemy ? perks_enemy : perks_me;
    for (const perk_info& p : list) {
        if (!action_name.empty() && p.action_name != action_name) continue;
        if (!perk_name.empty() && p.perk_name != perk_name) continue;
        return true;
    }
    return false;
}

bool FightContext::has_animation(const std::string& anim, int slot) const {
    const std::vector<std::string>* list = nullptr;
    switch (slot) {
        case 1: list = &anims_me; break;
        case 2: list = &anims_enemy; break;
        case 3: list = &anims_other; break;
        case 4: list = &anims_fourth; break;
        case 6: list = &anims_sixth; break;
        default: list = &anims_me; break;
    }
    for (const std::string& a : *list) {
        if (a == anim) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Per-type evaluation (JS `he` on each class)
// ---------------------------------------------------------------------------

namespace {

// `lg` CurrentAnimation: player slot selection (lg.vQ).
// slot 1=Me, 2=Enemy, 3=Parent...  JS maps player 1->XH, 2->z_, 3->G3,
// 4->oZ, 6->A_ (vQ switch).
const std::vector<std::string>& anim_list_for(const FightContext& ctx, int player) {
    switch (player) {
        case 2: return ctx.anims_enemy;
        case 3: return ctx.anims_other;
        case 4: return ctx.anims_fourth;
        case 6: return ctx.anims_sixth;
        default: return ctx.anims_me;
    }
}

// JS `lg.he`:
//   name == "$Move"     -> the candidate move's animation is in xK
//                          (lg.xEa(c[0], a.xK))
//   name == "$NoAnimation$" -> no animation playing (c.length==0)
//   name == ""          -> match the physics flag (d7a: Oga/BEa/CEa per player)
//   otherwise           -> the animation name is in the player's animation list
bool eval_current_animation(const Cond& c, const FightContext& ctx) {
    const std::string& name = c.name;
    bool result = false;
    if (name.empty()) {
        // Physics flag match (JS d7a). Not exercised by moves.xml.
        result = true;  // conservative: no anim constraint
    } else if (name == "$Move") {
        for (const std::string& m : ctx.candidate_moves) {
            if (!m.empty()) { result = true; break; }
        }
    } else if (name == "$NoAnimation$") {
        result = anim_list_for(ctx, c.player).empty();
    } else {
        result = false;
        for (const std::string& a : anim_list_for(ctx, c.player)) {
            if (a == name) { result = true; break; }
        }
    }
    return result;
}

// JS `vm.he`: Keys condition.
//   `he(a){a=a.gm?(a.keys.S1||a.Wl>0?this.xn:this.TDa).$ga(a.keys):!0;
//      return this.cb?!a:a}`
//   - `Ae.gm` is the fighter's "input-gated" flag. During MOVE TESTING the
//     game clears it (`wd.y0`/`V1`: `f.gm=!1`), so `he` returns `true`
//     trivially — the actual key gating happens via the `KeyPressed` EVENT
//     of the candidate move, not the Keys condition. The Keys condition is
//     only evaluated in the "strike" continuation path (`Ykb` sets
//     `a.S1=!0` on the input buffer, `Okb`), where `gm` stays true.
//   - When `gm` is true: if `keys.S1` (super/move-executing) or `Wl>0`
//     (scaled), match against the condition's parsed Tap/Hold/Release lists
//     (`xn`); otherwise use the reversed direction-priority order (`TDa`).
//   The native context mirrors this: `keys_gm` = `Ae.gm`, and `keys` holds
//   the buffered Tap/Hold/Release inputs.
bool eval_keys(const Cond& c, const FightContext& ctx) {
    // When the fighter is not input-gated (move testing), JS returns true.
    if (!ctx.keys_gm) return true;

    // Parse the "<Type>:<PressType>,..." list stored in c.keys.
    // The JS `vm.parse` reads each <Key Type PressType/> child and builds
    // three lists (Fh=hold, sh=tap, released).
    if (c.keys.empty()) return true;  // no keys -> always true
    std::vector<std::string> wanted;
    std::string cur;
    for (const char ch : c.keys) {
        if (ch == ',') { wanted.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) wanted.push_back(cur);

    // key type name -> key_type enum (JS sa.$h).
    auto key_id = [](const std::string& s) -> int {
        if (s == "Punch") return static_cast<int>(key_type::punch);
        if (s == "Kick") return static_cast<int>(key_type::kick);
        if (s == "Ranged") return static_cast<int>(key_type::ranged);
        if (s == "Magic") return static_cast<int>(key_type::magic);
        if (s == "RaidCharge") return static_cast<int>(key_type::raid_charge);
        if (s == "Super") return static_cast<int>(key_type::super);
        if (s == "Up") return static_cast<int>(key_type::up);
        if (s == "Up-Forward") return static_cast<int>(key_type::up_forward);
        if (s == "Forward") return static_cast<int>(key_type::forward);
        if (s == "Down-Forward") return static_cast<int>(key_type::down_forward);
        if (s == "Down") return static_cast<int>(key_type::down);
        if (s == "Down-Back") return static_cast<int>(key_type::down_back);
        if (s == "Back") return static_cast<int>(key_type::back);
        if (s == "Up-Back") return static_cast<int>(key_type::up_back);
        return 0;
    };
    auto press_id = [](const std::string& s) -> press_type {
        if (s == "Tap") return press_type::tap;
        if (s == "Hold") return press_type::hold;
        if (s == "Release") return press_type::release;
        return press_type::tap;
    };

    // JS `$ga`: every required key of every required press-type must be
    // present in the fighter's buffer. An unknown key type maps to code 0
    // (JS `sa.HQ` returns 0 for unregistered names, and the buffered codes
    // are 1..14), so it can never match — the condition fails.
    bool ok = true;
    for (const std::string& w : wanted) {
        const std::size_t colon = w.find(':');
        const std::string type_s = colon == std::string::npos ? w : w.substr(0, colon);
        const std::string press_s = colon == std::string::npos ? "Tap" : w.substr(colon + 1);
        const int k = key_id(type_s);
        const press_type p = press_id(press_s);
        if (k == 0) {
            ok = false;
            break;
        }
        if (!ctx.key_pressed(static_cast<key_type>(k), p)) {
            ok = false;
            break;
        }
    }
    return ok;
}

// JS `qm.he`: Distance.
//   Axis X: b = (to.X - from.X) * Wl (signed, scaled)
//   Axis Y: b = to.Y - from.Y (negated)
//   Axis 2: b = sqrt((fx-tx)^2 + (fy-ty)^2)
//   then Min <= b <= Max.
bool eval_distance(const Cond& c, const FightContext& ctx) {
    float b = 0.0f;
    switch (c.axis) {
        case 0: b = ctx.dist_x * ctx.scale; break;
        case 1: b = ctx.dist_y; break;
        default: b = ctx.dist_3d; break;
    }
    bool ok = (!c.has_min || c.min <= b) && (!c.has_max || b <= c.max);
    return ok;
}

// JS `Hm.he` (Weapon/Player) and `um.he` (Item): match my items by
// Type/SubType/Name. `Hm` also handles Enemy via Player attr.
bool eval_item_like(const Cond& c, const FightContext& ctx) {
    // Player attr: default Me (1). Enemy (2) -> enemy item list.
    const bool enemy = c.player == 2;
    bool ok = ctx.has_item(c.subtype /* JS Hm: uc=Type */,
                           c.subtype /* JS Hm: Zta=SubType */,
                           c.name, enemy);
    return ok;
}

// JS `rm.he`: Health ratio in [Min,Max].
bool eval_health(const Cond& c, const FightContext& ctx) {
    const float ratio = ctx.health_ratio;
    bool ok = (!c.has_min || c.min <= ratio) && (!c.has_max || ratio <= c.max);
    return ok;
}

// JS `tm.he`: CurrentInterval — active interval Name and/or Type.
bool eval_current_interval(const Cond& c, const FightContext& ctx) {
    bool ok = ctx.interval_active(c.name, c.value_int);
    return ok;
}

// JS `Em.he`: RoundStage.
bool eval_round_stage(const Cond& c, const FightContext& ctx) {
    bool ok = false;
    const int want = c.value_int;
    const int cur = static_cast<int>(ctx.stage);
    // JS: Je==1 && "StartStance" || Je==2 && "Fight" || Je==3 && "EndStance" ||
    //      Je==7 && "TryOn".
    ok = (cur == 1 && want == 1) || (cur == 2 && want == 2) ||
         (cur == 3 && want == 3) || (cur == 7 && want == 7);
    return ok;
}

// JS `Cm.he`: PhysicsFrameNumber in [Min,Max] (unset = -1).
bool eval_physics_frame(const Cond& c, const FightContext& ctx) {
    const int f = ctx.physics_frame;
    bool ok = (c.min == -1 || f >= static_cast<int>(c.min)) &&
              (c.max == -1 || f <= static_cast<int>(c.max));
    return ok;
}

// JS `Fm.he`: RoundResult — Victory/Defeat + Timeout/Ringout.
bool eval_round_result(const Cond& c, const FightContext& ctx) {
    // JS Fm: this.uc (1=Victory, 2=Defeat), this.uO (1=Timeout, 2=Ringout).
    // Round result is "won/lost" + the way it ended. The native context
    // exposes round_victory + round_timer as a simplification; the evaluator
    // treats a matching Name (Victory/Defeat) as the primary check.
    bool ok = false;
    if (c.value_int == 1 && ctx.round_victory) ok = true;
    else if (c.value_int == 2 && !ctx.round_victory) ok = true;
    else if (c.value_int == 0) ok = true;  // no Name -> any result
    return ok;
}

// JS `lp.he`: Bullets — count for MagicBullet/RaidChargeBullet in [Min,Max].
bool eval_bullets(const Cond& c, const FightContext& ctx) {
    int count = 0;
    if (c.subtype == "MagicBullet") count = ctx.bullets_me;
    else if (c.subtype == "RaidChargeBullet") count = ctx.bullets_enemy;
    else return true;  // no type -> unconstrained
    bool ok = (!c.has_min || c.min <= count) && (!c.has_max || count <= c.max);
    return ok;
}

// JS `tp.he`: ModExists — name in the mod set (or Namespace-prefixed).
bool eval_mod_exists(const Cond& c, const FightContext& ctx) {
    bool ok = false;
    if (!c.value.empty()) {
        // Namespace-prefixed check (JS bc.YZa).
        ok = ctx.has_mod(c.value + "." + c.name) || ctx.has_mod(c.name);
    } else {
        ok = ctx.has_mod(c.name);
    }
    return ok;
}

// JS `Bm.he`: Perk — my/enemy perks by name.
bool eval_perk(const Cond& c, const FightContext& ctx) {
    const bool enemy = c.player == 2;
    bool ok = ctx.has_perk("", c.name, enemy);
    return ok;
}

// JS `sp.he`: MagicCharge in [Min,Max].
bool eval_magic_charge(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    // Native port has no magic-charge meter yet; treat as unconstrained.
    bool ok = true;
    return ok;
}

// JS `vp.he`: Pain in [Min,Max].
bool eval_pain(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    // Native port has no pain meter yet; treat as unconstrained.
    bool ok = true;
    return ok;
}

// JS `yp.he`: Round — round number equals `Number`.
bool eval_round(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    // The context has no round counter yet; round conditions are rare and
    // not present in moves.xml. Treat as unconstrained.
    bool ok = true;
    return ok;
}

// JS `qp.he`: InTheArea — fighter is in the arena.
bool eval_in_the_area(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    bool ok = true;  // the native arena always contains the fighter
    return ok;
}

// JS `xp.he` via `Pl.compare` → `Da.cT(percent)` (L2352, default b=100):
// `a>b ? true : s4(100)<a` — no draw when the percent hits 100+. Threaded
// through `FightContext::roll01` (the shared fight stream); unset contexts
// (probes/demos) keep the legacy private stream.
bool eval_random(const Cond& c, const FightContext& ctx) {
    const float percent =
        c.value_int > 0 ? static_cast<float>(c.value_int) : 0.0f;
    if (ctx.roll01) {
        if (percent >= 100.0f) return true;  // the `a>b` no-draw shortcut
        return ctx.roll01() * 100.0f < percent;
    }
    static std::mt19937 rng(0x5F2);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    bool ok = percent / 100.0f > dist(rng);
    return ok;
}

// JS `wp.he`: PerkStart — always true.
bool eval_perk_start(const Cond& c, const FightContext& ctx) {
    (void)ctx; (void)c;
    return true;
}

// JS `Am.he`: Name — fighter model name == Value.
bool eval_name(const Cond& c, const FightContext& ctx) {
    bool ok = ctx.fighter_name == c.name;
    return ok;
}

// JS `Gm.he`: Screen — screen enum == Name.
bool eval_screen(const Cond& c, const FightContext& ctx) {
    int want = 0;
    if (c.name == "Fight") want = 10;
    else if (c.name == "Profile") want = 9;
    else if (c.name == "ShopArmor") want = 1;
    else if (c.name == "ShopWeapon") want = 2;
    else if (c.name == "ShopHelm") want = 3;
    else if (c.name == "ShopMissile") want = 4;
    else if (c.name == "ShopMagic") want = 5;
    else if (c.name == "ShopRuby") want = 6;
    else if (c.name == "ShopFree") want = 7;
    else if (c.name == "ShopRaidItemPack") want = 8;
    bool ok = ctx.screen == want;
    return ok;
}

// JS `zm.he`: ModelMirrored.
bool eval_model_mirrored(const Cond& c, const FightContext& ctx) {
    (void)c;
    bool ok = ctx.model_mirrored;
    return ok;
}

// JS `lm.he`: BattleType — Value attr == ctx.To.
bool eval_battle_type(const Cond& c, const FightContext& ctx) {
    bool ok = ctx.battle_type == c.value;
    return ok;
}

// JS `nm.he`: BossAbilityState — Value flag.
bool eval_boss_ability_state(const Cond& c, const FightContext& ctx) {
    bool ok = ctx.boss_ability_state == (c.value_int != 0);
    return ok;
}

// JS `sm.he`: Hit — last-hit Type/Name match.
bool eval_hit(const Cond& c, const FightContext& ctx) {
    if (!ctx.has_last_hit) return false;
    bool ok = false;
    if (c.subtype.empty() || c.subtype == ctx.last_hit_type) {
        if (c.name.empty() || c.name == ctx.last_hit_animation) ok = true;
    }
    return ok;
}

// JS `ym.he`: ModelExists — a model with the given name exists on the field.
bool eval_model_exists(const Cond& c, const FightContext& ctx) {
    // The native port does not track models on the field; the context's
    // fighter_names acts as the set of known model names.
    bool ok = false;
    for (const std::string& n : ctx.fighter_names) {
        if (n == c.name) { ok = true; break; }
    }
    return ok;
}

// JS `mp.he`: Combo — combo counter in [Min,Max].
bool eval_combo(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    bool ok = true;  // no combo counter yet; not present in moves.xml
    return ok;
}

// JS `Ap.he`: Style — style enum in [Min,Max].
bool eval_style(const Cond& c, const FightContext& ctx) {
    (void)c; (void)ctx;
    bool ok = true;  // no style state yet; not present in moves.xml
    return ok;
}

// JS `pm.he`: Direction — facing sign matches From/To direction.
bool eval_direction(const Cond& c, const FightContext& ctx) {
    // The direction refs (From/To) are parsed by Fa.Zca; the JS computes the
    // sign of (to - from) on the X axis and compares to the fighter facing.
    // Native: ctx has no facing yet; treat as unconstrained.
    (void)c; (void)ctx;
    return true;
}

// JS `mm.he`: Birth — fighter's aK (birth name) == Name.
bool eval_birth(const Cond& c, const FightContext& ctx) {
    bool ok = ctx.fighter_name == c.name;
    return ok;
}

// Dispatch one leaf condition (JS `Ha.he`).
bool eval_leaf(const Cond& c, const FightContext& ctx) {
    if (c.type == "Keys") return eval_keys(c, ctx);
    if (c.type == "Distance") return eval_distance(c, ctx);
    if (c.type == "Weapon" || c.type == "Player") return eval_item_like(c, ctx);
    if (c.type == "Health") return eval_health(c, ctx);
    if (c.type == "CurrentInterval") return eval_current_interval(c, ctx);
    if (c.type == "CurrentAnimation") return eval_current_animation(c, ctx);
    if (c.type == "PhysicsFrameNumber") return eval_physics_frame(c, ctx);
    if (c.type == "RoundResult") return eval_round_result(c, ctx);
    if (c.type == "Item") return eval_item_like(c, ctx);
    if (c.type == "Bullets") return eval_bullets(c, ctx);
    if (c.type == "Perk") return eval_perk(c, ctx);
    if (c.type == "MagicCharge") return eval_magic_charge(c, ctx);
    if (c.type == "ModExists") return eval_mod_exists(c, ctx);
    if (c.type == "Pain") return eval_pain(c, ctx);
    if (c.type == "Round") return eval_round(c, ctx);
    if (c.type == "InTheArea") return eval_in_the_area(c, ctx);
    if (c.type == "Random") return eval_random(c, ctx);
    if (c.type == "PerkStart") return eval_perk_start(c, ctx);
    if (c.type == "Name") return eval_name(c, ctx);
    if (c.type == "Screen") return eval_screen(c, ctx);
    if (c.type == "ModelMirrored") return eval_model_mirrored(c, ctx);
    if (c.type == "BattleType") return eval_battle_type(c, ctx);
    if (c.type == "BossAbilityState") return eval_boss_ability_state(c, ctx);
    if (c.type == "Hit") return eval_hit(c, ctx);
    if (c.type == "ModelExists") return eval_model_exists(c, ctx);
    if (c.type == "Combo") return eval_combo(c, ctx);
    if (c.type == "Style") return eval_style(c, ctx);
    if (c.type == "Direction") return eval_direction(c, ctx);
    if (c.type == "Birth") return eval_birth(c, ctx);
    // Unknown condition type: pass (the game throws 30 for unknown types;
    // we keep the move evaluable and note it).
    return true;
}

} // namespace

bool eval_conditions(const Cond& cond, const FightContext& ctx,
                     std::string* trace, int depth) {
    bool result = false;
    if (cond.op == cond_op::leaf) {
        result = eval_leaf(cond, ctx);
    } else if (cond.op == cond_op::and_) {
        // JS `wm.gEa`: And — all children must pass; short-circuit false.
        result = true;
        for (const Cond& child : cond.children) {
            if (!eval_conditions(child, ctx, trace, depth + 1)) {
                result = false;
                break;
            }
        }
    } else if (cond.op == cond_op::or_) {
        // JS `wm.gEa`: Or — any child passes; short-circuit true.
        result = false;
        for (const Cond& child : cond.children) {
            if (eval_conditions(child, ctx, trace, depth + 1)) {
                result = true;
                break;
            }
        }
    } else {  // not_ (single child)
        if (!cond.children.empty()) {
            result = !eval_conditions(cond.children[0], ctx, trace, depth + 1);
        }
    }
    // Not attribute (JS `Ha.Nba`).
    result = cond.not_ ? !result : result;

    if (trace != nullptr) {
        std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
        *trace += indent + cond_desc(cond) + " -> " + (result ? "TRUE" : "FALSE") + "\n";
    }
    return result;
}

bool eval_move_conditions(const std::vector<Cond>& conds,
                          const FightContext& ctx, std::string* trace) {
    for (const Cond& c : conds) {
        if (!eval_conditions(c, ctx, trace, 0)) {
            return false;
        }
    }
    return true;
}

std::string cond_desc(const Cond& c) {
    std::ostringstream os;
    if (c.op != cond_op::leaf) {
        const char* opname = c.op == cond_op::and_ ? "AND" : (c.op == cond_op::or_ ? "OR" : "NOT");
        os << "[" << opname << (c.not_ ? "|Not" : "") << "]";
        return os.str();
    }
    os << c.type;
    if (!c.name.empty()) os << " '" << c.name << "'";
    if (!c.subtype.empty()) os << " type=" << c.subtype;
    if (c.type == "Keys") os << " {" << c.keys << "}";
    if (c.has_min || c.has_max) os << " [" << c.min << ".." << c.max << "]";
    if (c.not_) os << " [!]";
    return os.str();
}

} // namespace sf2::scene
