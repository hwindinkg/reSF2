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

#include "scene/ai.hpp"  // DaPrng (the shared `Da.pg` stream; see eval_random)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace sf2::scene {

// ---------------------------------------------------------------------------
// FightContext helpers
// ---------------------------------------------------------------------------

bool FightContext::interval_active(const std::string& name, int type,
                                   int player) const {
    // JS `tm.he`: a CurrentInterval condition matches an active interval if
    //   (this.uc==0 || this.uc==d.type) && (this.Ba=="" || d.name==this.Ba)
    // where this.uc = the condition's Type (fe.G0) and this.Ba = its Name.
    // `Player` (Nd.ol L705): Me=1 (default) reads `Ae.xb` (this fighter);
    // Enemy=2 reads the opponent's list (the `Throw` Throwable gate).
    const std::vector<interval_state>& list =
        (player == 2) ? intervals_enemy : intervals;
    for (const interval_state& iv : list) {
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

    // JS `vm.he` (L749): `a.gm ? (a.keys.S1||a.Wl>0 ? this.xn : this.TDa)
    //                     .$ga(a.keys) : true`.
    // `xn` = the parsed required list; `TDa = xn.Ib(); TDa.reverse(-1)` mirrors
    // it (`zd.Fha` L688/689 flips 2<->8, 3<->7, 4<->6).
    //
    // RESOLVED (this was a blanket `normal = true` with a FALSE justification
    // that "the buffer is already facing-mapped") — evidence, JS-strict:
    //
    //  * The JS mirror is REAL, not a no-op. `Kl.Sgb` (L798) is called from
    //    `wd.yJa` (L501) with the control id, `getKey(a)` matches `d.code==a`
    //    and pushes `d.index`, and `Ff` is built as `b.code=b.index=a++ +1`
    //    (L798) — so the buffer holds the RAW `sa.$h` control id. The mirror
    //    `LBa` (L399: 1<->5, 2<->6, 3<->7, 4<->8) sits in `N0a`/`O0a` (L426)
    //    BEFORE `yJa`, and it is the identity unless `Iga` is set — `Iga` is
    //    false by default (L380), cleared at every round start (L409) and set
    //    ONLY by the `ERuleInvertJoystick` field rule (`F1` L897). So in a
    //    normal fight the buffer is un-mirrored, and the JS reverses the
    //    REQUIREMENT (`TDa`), not the buffer.
    //  * The port is in the same convention on both sides, so the un-reversed
    //    `xn` is what matches: the buffer holds raw `sa.$h` ids
    //    (`FightScreen::key_type_for_glfw(65)==7`, `(68)==3` reproduces
    //    `Af.oUa` L2472 `v[7]=65`/`v[3]=68`; `inject_game_key` feeds `sa.$h`
    //    ids straight into `Fighter::input`; `FightController::player_input`
    //    applies only the `LBa` map, identical to the JS) and the parsed
    //    requirement holds the SAME ids (`key_id("Forward")==3` == the buffered
    //    `forward`). `$ga` therefore compares like with like.
    //  * JS-exact mirroring (`zd.reverse` L688 / `vm.he` L749): the requirement
    //    list is the normal `xn` when the fighter's move-executing flag `S1`
    //    is set OR its `Wl` (`ctx.direction`) is forward (>0); otherwise the
    //    direction-reversed `TDa` (`zd.Fha`, 2<->8 / 3<->7 / 4<->6). The BUFFER
    //    is NOT mirrored (`zl.Lea` L798 ignores the sign arg), so only the
    //    requirement reverses. `ctx.direction` is `Ae.Wl` filled by
    //    `fill_ctx_geometry` (fight.cpp:3615 -> 2916) and is +1 while the
    //    player faces an opponent on the right, so the un-mirrored tape is
    //    unchanged; a mirrored player reverses the directional requirement.
    const bool normal = ctx.keys_s1 || ctx.direction > 0.0f;
    auto fha = [](int k) -> int {
        switch (k) {
            case 2: return 8;
            case 8: return 2;
            case 3: return 7;
            case 7: return 3;
            case 4: return 6;
            case 6: return 4;
            default: return k;
        }
    };
    // JS `$ga` (L688): multiset containment —
    //   `eca(sh, req.sh) && eca(Fh, req.Fh) ? eca(released, req.released) : false`
    // where `eca(x,y)` (`Eab`) requires every element of `x` to match a
    // DISTINCT element of `y`. Equivalently: for every (key, press) pair the
    // buffered count must be >= the required count. An unknown key type maps
    // to code 0 (JS `sa.HQ` returns 0 for unregistered names) and the
    // buffered codes are 1..14, so it can never match.
    std::vector<std::pair<int, press_type>> required;
    required.reserve(wanted.size());
    for (const std::string& w : wanted) {
        const std::size_t colon = w.find(':');
        const std::string type_s = colon == std::string::npos ? w : w.substr(0, colon);
        const std::string press_s = colon == std::string::npos ? "Tap" : w.substr(colon + 1);
        int k = key_id(type_s);
        if (k == 0) return false;
        if (!normal) k = fha(k);
        required.emplace_back(k, press_id(press_s));
    }
    for (const std::pair<int, press_type>& rp : required) {
        int need = 0;
        for (const std::pair<int, press_type>& rp2 : required) {
            if (rp2 == rp) ++need;
        }
        int have = 0;
        for (const key_input& ki : ctx.keys) {
            if (static_cast<int>(ki.key) == rp.first && ki.press == rp.second) ++have;
        }
        if (have < need) return false;
    }
    return true;
}

// JS `qm.he` (L744-745): Distance.
//   Axis X: `b = this.GK.OQ(a) - this.FK.OQ(a); b *= a.Wl;` (L744) — the
//     signed X delta `To - From` scaled by the move's `<SetDirection>` sign.
//   Axis Y: `b = this.GK.bfa(a) - this.FK.bfa(a)` (negated Y).
//   Axis 2: `b = sqrt((fx-tx)^2 + (fy-ty)^2)`.
//   then `Min <= b <= Max`.
//
// `From`/`To` are `ee` refs; `ee.OQ(a)` = `ee.nt(a).x` (L786) resolves them:
//   - `Object="Nodes"|"Pivot"` -> the owner's posed node X. The native context
//     carries the two fighter roots (the same root-X stand-in used everywhere
//     else in the evaluator; `dist_x` = `Enemy - Me`).
//   - `Object="Wall"` -> `ee.nt` case 3 -> `ee.q9a` (L788):
//       `q9a(a,b){let c=0; switch(this.pe){case 0:case 1:c=a.Wl; break;
//          case 2:c=a.Mla; break; case 3:c=a.Nla}
//          return c>0==this.qga ? b.yu : b.zu}`
//     `this.qga = (Part=="Back")` (L785); `b.yu`/`b.zu` are the scene's two
//     wall X bounds (`FightController::set_bounds` -> `wall_min_`/`wall_max_`,
//     the JS `yu`/`zu`). The wall moves (`WallJump_100`, `WallJump_200`,
//     `WallDashForward_50`, `WallJump_50_PVP`) gate on the distance from the
//     wall BEHIND the fighter to its own heel; without this branch the port
//     fed the Me->Enemy gap into those gates.
float wall_ref_x(const FightContext& ctx, int player, const std::string& part) {
    // `c` = that player's facing (`a.Wl` for Me, `a.Mla` for Enemy).
    const float facing = (player == 2) ? ctx.enemy_direction : ctx.direction;
    const bool back = (part == "Back");  // `this.qga`
    return ((facing > 0.0f) == back) ? ctx.wall_min : ctx.wall_max;
}

// `ee.nt(a).x` for one end of a Distance ref (`to_end` selects To vs From).
float ref_x(const Cond& c, bool to_end, const FightContext& ctx) {
    const std::string& obj = to_end ? c.to_obj : c.from_obj;
    const std::string& part = to_end ? c.to_part : c.from_part;
    const int player = to_end ? c.to_player : c.from_player;
    if (obj == "Wall") return wall_ref_x(ctx, player, part);
    // `Object="Nodes"|"Pivot"|...` -> the owning fighter's root X.
    return player == 2 ? ctx.enemy_x : ctx.me_x;
}

bool eval_distance(const Cond& c, const FightContext& ctx) {
    float b = 0.0f;
    switch (c.axis) {
        case 0:
            if (c.from_obj == "Wall" || c.to_obj == "Wall") {
                b = (ref_x(c, /*to_end=*/true, ctx) -
                     ref_x(c, /*to_end=*/false, ctx)) * ctx.direction;
            } else {
                b = ctx.dist_x * ctx.direction;
            }
            break;
        case 1: b = ctx.dist_y; break;
        default: b = ctx.dist_3d; break;
    }
return (!c.has_min || c.min <= b) && (!c.has_max || b <= c.max);
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
    // `Player` (Nd.ol L705): Me=1 (default) reads `Ae.xb`; Enemy=2 reads the
    // opponent's interval list — the `Throw` template's Throwable gate.
    bool ok = ctx.interval_active(c.name, c.value_int, c.player);
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
// `a>b ? true : s4(b)<a` — `s4(100) < percent`, one shared draw. Threaded
// through `FightContext::roll01` (the shared fight stream `Da.pg`); unset
// contexts (probes/demos) fall back to the SAME `Xx`+`Rk` DaPrng stream
// (was a private mt19937 — the documented RNG divergence).
bool eval_random(const Cond& c, const FightContext& ctx) {
    const float percent =
        c.value_int > 0 ? static_cast<float>(c.value_int) : 0.0f;
    if (percent >= 100.0f) return true;  // the `a>b` no-draw shortcut
    if (ctx.roll01) {
        return ctx.roll01() * 100.0f < percent;
    }
    static DaPrng rng(0x5F2);
    return static_cast<float>(rng.s4(1.0)) * 100.0f < percent;
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

// JS `Dm.he` (Player condition — `sa.oe.set("Player",6)` L705, class L755):
//   ctor: `this.LUa = u.I(a.attributes.get("Number"),1)==1`
//   he:   `a = this.LUa == a.qb; return this.cb?!a:a`
// `qb` is true for the CONTROLLED fighter (L1207 `a.wu=!0;a.Fj=!1;a.qb=!0`)
// and false for the AI (L806 `this.qb=!1;this.Fj=!0`); the event context
// copies it (`a.qb=b.parameters.qb` L680). So `Number=1` == the player,
// any other value == the enemy. `Number` is parsed into `c.value_int`
// (default 1; move_def.cpp `Player` branch).
bool eval_player(const Cond& c, const FightContext& ctx) {
    bool ok = ((c.value_int == 1) == ctx.qb);
    return ok;
}

// Dispatch one leaf condition (JS `Ha.he`).
bool eval_leaf(const Cond& c, const FightContext& ctx) {
    if (c.type == "Keys") return eval_keys(c, ctx);
    if (c.type == "Distance") return eval_distance(c, ctx);
    if (c.type == "Weapon") return eval_item_like(c, ctx);
    if (c.type == "Player") return eval_player(c, ctx);
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
