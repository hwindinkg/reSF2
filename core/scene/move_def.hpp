#pragma once

// Move definition: one <Move> element of res/moves.xml.
//
// JS study (reference/www/sf2.502f0946.js):
//   - Parse entry point: `Fa.Ueb` (move list) — reads Name/FileName/MidFrames/
//     FirstFrame/EndFrame/Priority/MirrorNode/TacticWeapon/TacticEquivalent/
//     Type/Profile, then `Fa.xbb` merges the parsed sub-objects:
//       `xbb(k,g,l)` reads Conditions (Fa.HS), Locks (Fa.HS), Tactics (djb),
//       Intervals (xjb/LIa), Align (Hib), SetDirection (hjb), Actions (CIa),
//       Transitions (Cxb), Events (GIa/HIa), Shop (Mub).
//   - `Fa.dMa` resolves the Template "A|B|C" string into inherited tag
//     <Template> elements (Fa.kxb) BEFORE parsing the move, so a move's
//     Conditions/Locks/Intervals/Align/SetDirection are the union of its own
//     elements and those of each inherited template tag.
//   - MoveDef class is `jc` (see constructor): name/fileName/XJ=MidFrames/
//     qx=FirstFrame/Lj=EndFrame/priority/type="EAnimationMove"|"EAnimationAttack",
//     `va` holds the parsed content: rb=conditions, Ts=tactics conditions,
//     xb=intervals, locks, Hc=events, p6=transitions, actions, align, vj.
//
// The native port keeps the same structure (own + inherited template tags
// merged, same field names) so the evaluator semantics carry over 1:1.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <pugixml.hpp>

namespace sf2::scene {

// Condition operators (JS `up`, class "2C6"; `wm`, class "132").
enum class cond_op : std::uint8_t {
    leaf = 0,  // a single typed condition, no children
    and_,      // Operator Type="And"  -> all children must pass
    or_,       // Operator Type="Or"   -> any child passes
    not_,      // Operator Type="Not"  -> negated single child (JS: no "Not"
               //                       operator node; the "Not" attribute
               //                       negates the node itself — see below)
};

// The `Not="1"` attribute on any condition node negates its result (JS:
// `Ha.parse` reads `Not` into `cb`; `Nba(a){return this.cb?!a:a}`).
// Modeled as a flag on the node; the evaluator flips the result after
// evaluating the node.
struct Cond;

// One condition node in the condition tree.
// Mirrors JS class `Ha` (base, g="125") + the 25 typed subclasses.
// `type` holds the move's <Conditions> child element name.
struct Cond {
    cond_op op = cond_op::leaf;
    bool not_ = false;   // Not="1" attribute (JS `Ha.cb`)
    int player = 0;      // Player attr: 1=Me, 2=Enemy, 0=default (Nd.ol)
    std::string type;    // element name: "Keys", "Distance", "Operator", ...
    std::string name;    // Name attr (CurrentAnimation/CurrentInterval/...)
    std::string subtype; // SubType/Type/Value attrs, kept as raw text
    std::string value;   // generic attr (e.g. Distance Min/Max, RoundStage)
    int value_int = 0;   // numeric attr (e.g. Random Chance, Step)
    // Range (Distance Min/Max, Health Min/Max, Bullets, PhysicsFrameNumber...)
    bool has_min = false, has_max = false;
    float min = 0.0f, max = 0.0f;
    // Distance axis: 0=X, 1=Y, 2=3D (JS `qm.qdb`, 0=X/1=Y/2=both)
    int axis = 2;
    // Distance From/To object refs (JS `ee`): Player + Object + Part
    int from_player = 1, to_player = 2;  // Me, Enemy
    std::string from_obj = "Pivot", to_obj = "Pivot";  // Nodes/Pivot/Wall/Floor/MapCenter/COM
    std::string from_part, to_part;      // Part attr for Object="Nodes"
    std::string keys;    // Keys: comma-joined "<Type>:<PressType>" list
    // Children (Operator And/Or only).
    std::vector<Cond> children;
};

// Interval of a move (JS `fe`, g="150"; Attack sub-type `Ul`, g="151").
// Start/End are 1-based animation frames (JS `fe.start=u.I(Start)`,
// `finish=End!=null?End:pva+2` where pva = EndFrame of the move).
struct Interval {
    std::string name;      // Interval Name attr (e.g. "Uninterrupt")
    int type = 0;          // fe.G0: 0=other, 2=Uninterrupt, 3=SelfUninterrupt,
                           //         4=Attack, 5=Block, 6=Invulnerable, 7=Invisible
    int start = 0;         // Start frame (1-based)
    int end = 0;           // End frame (inclusive). Default = EndFrame+2.
    // No `End` attribute: the JS `fe.init` finish is `this.pva+2` where
    // `pva` = `jc.Lj` = the move's `EndFrame`, or the LOADED CLIP's frame
    // count when the move carries no `EndFrame` (`jc.Lj` is resolved in
    // `Vlb`/`Cdb`). The parser only sees the XML, so it stores
    // `EndFrame+2` (= 2 for an `EndFrame`-less move) and flags the default
    // here; `Fighter` re-resolves it to `clip_len+2` once the clip is loaded.
    bool end_default = false;
    std::vector<std::string> attacking_parts;  // Attack intervals: Edge names
    // Attack damage block (<Damage Value=..><Damage Type=.. Shift=..>).
    float damage = 0.0f;
    bool no_critical = false;
    // The interval carried an outer `<Damage Value=..>` element at all
    // (JS: `a=this.il.A("Damage"); this.Xb=u.H(a.attributes.get("Value"))` —
    // unguarded, so the node is expected). 3 shipped blocks have no
    // sub-`<Damage>`, which leaves `SZ` empty and `pAa` returning -FLT_MAX.
    bool has_damage = false;
    // JS `Ul.J3` (L775): `this.DL = !u.ka(attrs.get("NoEffect"), false)` —
    // the `<Interval NoEffect="1">` attribute. `DL` gates the hit flash
    // (`Hyb`): `a.Pd.da.yD(4).DL && a.model.lrb(...)` (L395). 118 shipped
    // `<Interval>` entries carry the attribute.
    bool no_effect = false;
    // JS `Ul.J3` (L774-775): `<IgnoresBlock/>` child -> `DDa=true`
    // (+ `hga` names); `<IgnoresInvulnerable Name="Evade|Dash"/>` child ->
    // `jga=true` (+ `iga` bypass names). Both live in shipped moves.xml
    // (159/154 hits) — the strike pre-break (DDa) and the HZa chain gate.
    bool ignores_block = false;
    std::vector<std::string> ignore_block_names;
    bool ignores_invuln = false;
    std::vector<std::string> invuln_bypass_names;
    // SZ (`Ul.qjb` L777-778): EVERY `<Damage Type Shift>` child of the
    // interval's outer `<Damage Value=..>` block, in document order. `pAa`
    // iterates all of them (L1205) and `i6a` (L430) picks the max-`Shift`
    // one. 572 of the 615 shipped outer blocks carry two.
    std::vector<std::pair<std::string, float>> attack_attrs;
    // KP (`Ul.qjb` L778: `e=="Defense"&&this.KP.push(d)`): every `<Defense
    // Type=..>` child. `LAa` (L536) returns `KP[0]` first. 120 shipped
    // blocks carry one.
    std::vector<std::string> defense_names;
    // Legacy mirrors of `attack_attrs[0]` (kept for the probe/demo apps'
    // printouts; `attack_attrs` is the authoritative list the formula uses).
    std::string damage_type;  // e.g. "UnarmedDamage"
    float damage_shift = 0.0f;
    // JS `Ul.Wsa` (L775-777): EVERY `<Hit Name Start End>` child of the
    // Attack interval (`Xu` g="DZ", `f.start = Start ?? interval.start`,
    // `f.end = End ?? interval.finish`). `B8a(frame)` resolves the name
    // whose window contains the attacker's clip frame. 3 of the 618
    // shipped attack intervals carry more than one (most carry one).
    struct HitWindow {
        std::string name;
        int start = -1;
        int end = -1;
    };
    std::vector<HitWindow> hit_windows;
    // JS `Ul.B8a(a)` (L775): `for(d of Wsa) if(d.start<=a&&a<=d.end) return
    // d.name; return ""`. First match in document order, inclusive bounds.
    std::string hit_name_at(int frame) const {
        for (const HitWindow& w : hit_windows) {
            if (w.start <= frame && frame <= w.end) return w.name;
        }
        return "";
    }
    // Legacy mirror of `hit_windows[0].name` (kept for the probe/demo
    // printouts; `hit_name_at` is the authoritative resolver).
    std::string hit_name;     // <Hit Name=..> inside the Attack interval
    float impulse_x = 0.0f, impulse_y = 0.0f, impulse_z = 0.0f;
    bool has_impulse = false;
    // Combo window (JS `Ul.sP`).
    int combo_time = 0;
};

// One lock: <Locks><Item Type SubType Name/> or <Operator Type="Or">...<Item/>
struct Lock {
    std::string type;     // "Weapon", "Skeleton", ...
    std::string subtype;  // "Fists", "Katana", ...
    std::string name;     // optional Name attr (JS `Hm.Ba`)
    // `Not="1"` (JS `tb.init` L763: `this.cb = u.ka(a.attributes.get("Not"),
    // false)` — read for EVERY lock kind). `Hm.he` (L758) returns `!this.cb`
    // when an item matches and `this.cb` when none does, so `Not` INVERTS the
    // whole test. Before this field existed the attribute was parsed away and
    // a `Not="1"` lock passed on an OWNED item — the exact opposite of the JS
    // (HighKick's `Or{<Item Type="Armor" Name="BODY_GATEKEEPER" Not="1"/>}`).
    bool not_ = false;
    bool or_ = false;     // inside an Operator Type="Or" (any of the group)
    // An UNPARSED lock kind (`<Perk Name=..>` and friends). The JS `ra.Hza`
    // tests every lock node against the fighter; the port carries only item
    // ownership here, so such a lock FAILS CLOSED. The old parser dropped the
    // element silently, which made every perk-gated boss ability selectable
    // (e.g. `HermitStormPlayer`, `<Locks>` = PERK_HERMITSTORM + Skeleton,
    // Priority 110, TacticWeapon=None -> it won the Up key and played
    // `hermit_super_attack`).
    bool never = false;
    // A `<Screen Name="..">` lock (JS `Gm`): the UI screen the move is gated
    // to ("ShopWeapon"/"ShopArmor"/"ShopHelm"/"ShopMagic"/"ShopMissile"/
    // "ShopOther"/"Fight"/"Profile"). Stored while `never` stays true, so the
    // FIGHT move list keeps failing it closed exactly as before; the shop's
    // `TryOn` preview (`Fighter::shop_tryon_move`) is the one caller that
    // evaluates it. Empty for the other unmodelled kinds (`<Perk>`).
    std::string screen;
    // A `<Perk Name="..">` lock (JS `Bm`, `Tl.create` case 15, L753-754):
    // `he(a)` scans the fighter's live perk set (`a.rr.parameters.Oa`, built
    // by `Wk`/`Pma` from the save's learned perks + equipped-item perks) for a
    // perk whose `.name` equals the lock's. A move carrying this lock is
    // admitted only while the perk is learned/active — this is the gate that
    // puts `DoubleSweep` (`<Locks><Perk Name="PERK_DOUBLE_SWEEP"/>…`,
    // moves.xml L14391-14397) into the list after the level-2 lesson. `never`
    // stays false for it (modelled), unlike the still-untracked kinds.
    std::string perk;
    // The `<Operator>` block this lock was flattened from (JS: one Or node).
    // Two SEPARATE operators (e.g. a template's `<Screen>` group and the
    // move's own `<Item>` group) are separate groups AND-combined; the old
    // single flat `or_` flag conflated them, so a passing `<Screen
    // Name="Fight"/>` satisfied the move's `<Item>` requirement and every
    // weapon's `StartStance*` entered `hb_` (the wrong intro). -1 = a
    // top-level lock, held to the plain AND.
    int group = -1;
};

// One owned item — the three fields the JS lock test `Hm.he` (L758)
// compares: `uc` (Type), `Zta` (SubType), `Ba` (name):
//   c = (this.uc==""||this.uc==b.type) ? (this.Zta==""||this.Zta==b.Yb) : false;
//   c = c ? (this.Ba==""||this.Ba==b.name) : false;
// A lock's empty field is a wildcard; the item carries whatever list.xml
// declares (e.g. armor `Body` has NO `SubType`, so `Yb` is undefined and a
// lock with `SubType="Body"` can never match it — only the NAME can).
// Lives here (not nested in `Fighter`) because the app layer
// (`PendingBattle`) carries the player's owned list across the
// Map/Dojo -> Fight boundary and must not include `scene/fighter.hpp`.
struct OwnedItem {
    std::string type;
    std::string subtype;
    std::string name;
};

// Align (JS `Ui`, g="109"; parse `Fa.jva` L719-721) — the evaluator fields
// plus the pose-align fields read by `Te.Gub` (L557-559).
struct Align {
    bool has_align = false;
    std::string axis;         // "X|Z" etc.; empty => X|Y|Z (JS L719)
    // JS `jva` axis flags: `cI`=X, `dI`=Y, `MY`=Z. An absent Axis sets all.
    bool axis_x = true, axis_y = true, axis_z = true;
    std::string pivot_object; // Pivot Object ("Nodes"/"Pivot"/"Animation"/"Wall")
    std::string pivot_part;   // Pivot Part (bone name, or Front/Back for Wall)
    std::string pivot_player; // "Me"/"Enemy"
    std::string pos_object;   // Position Object
    std::string pos_part;     // Position Part
    std::string pos_player;   // "Me"/"Enemy"
    float shift_x = 0.0f;     // <Position ShiftX> (JS `dja`)
    float shift_y = 0.0f;     // <Position ShiftY> (JS `eja`)
    std::string shift_model_node;  // <Align ShiftModelNode> (JS `Fla`)
};

// One `<Actions>` child of a move — a frame- or event-triggered action
// (JS base class `cb`, g="110", L724; the 19 concrete kinds `Vl`/`Wl`/`mh`/
// `Xl`/`Yl`/`Zl`/`jg`/`$l`/`am`/`bm`/`cm`/`dm`/`fm`/`gm`/`hm`/`im`/`jm`/`km`
// L725-737, built by the `lz.create` factory L737-739).
//
// Trigger (`cb.parse` L724-725): a `<X Frame="N">` sets `zy.Z5=0`,
// `zy.frame=N`; otherwise `<X Event="Name">` sets `zy.Z5=1`,
// `zy.event=tb.D6a(Name)` (the L763-764 map: RoundStage 1, KeyPressed 2,
// KeyReleased 3, RoundStart 4, RoundEnd 5, Hit 6, Strike 7, WallHit 8,
// AnimationStart 9, AnimationEnd 10, AnimationInterrupted 11,
// IntervalStart 12, IntervalEnd 13, EveryFrame 14, Birth 15, ModExpires 16).
// `Player` (default "Me") -> `pe`; a `<Conditions>` child -> `$c`.
//
// Dispatch: frame actions are collected by `Te.Lwa` (L563-564) —
// `e.$eb(this.ip(), ...) && c.push(e)` — once per completed clip frame and
// fired as the `EActionStart` event (L564 `this.gh("EActionStart", c)`);
// event actions by `Te.CZa(a)` (L555) — `e.afb(a, ...) && c.push(e)`.
// Both reach `wd.mHa` (L530) -> `wd.BNa` (L523) -> each action's `Uh(this)`,
// which routes to the per-kind `wd` handler (L518-520); the sound kinds are
// `wd.dwb` (Sound, L519), `wd.fwb` (RandomSound, L519), `wd.ewb`
// (StopSound, L519).
//
// The port DISPATCHES: Sound / RandomSound / StopSound (audio), SetEndStage
// (`cm.Uh()` L738 is an empty body), ShakeScreen (`wd.Wvb` L519 ->
// `Pi.uS` L424 -> `ql.DL` L370 = the port's `FightCamera::apply_hit_effect`),
// CameraWeight (`wd.ANa` L520 -> `Pi.fS` L424 `{debugger}` = NO-OP),
// EnableBossAbility (`wd.$vb` L520 -> `Pi.dS` L397 `{debugger}` = NO-OP),
// AddBullets (`wd.Tvb` L519 -> `hZ`+`LA` L505 / `vZa`+`Amb` L524) and
// HitEffect (`wd.Xvb` L519 -> `ca.Kla` -> `ql.Kla` L370 -> `Ut.Hyb` L825 =
// the port's `EffectSystem::spawn_hit_flash`; gated on the `lrb` latch). The
// rest are parsed records whose consumer systems are not ported (child
// models, the magic-effect containers, the perk cooldown timers, the intro
// lens); each is reported with its exact missing subsystem — never faked.
struct MoveAction {
    std::string kind;       // element name ("Sound", "RandomSound", ...)
    int js_type = -1;       // JS `cb.type` (0..17); -1 = unknown (never pushed)
    // Trigger (JS `cb.zy`).
    bool frame_trigger = true;  // `zy.Z5 == 0` (a Frame attr was present)
    int frame = 0;              // `zy.frame` (<X Frame=N>)
    std::string event;          // `zy.event` name (<X Event="Strike">)
    int player = 1;             // `pe` (Nd.ol; default "Me")
    std::vector<Cond> conditions;  // `$c` (<Conditions> child)
    // Sound (`fm` L735) / StopSound (`im`) / StopEffect (`gm`) / StopFollowEffect (`hm`).
    std::string name;           // <X Name=..> (JS `fm.name` / `im.name` / ...)
    // Sound only (`fm` L735): `volume`, `ceb` (Looped), `J8`/`t7` (Voice),
    // `ES` (PackName). `PackName` is parsed and NEVER read at play time
    // (`ta.ak` L1264 only consults `ta.WBa(name)`), so it is informational.
    float volume = 1.0f;        // `fm.volume` (default 1)
    bool looped = false;        // `fm.ceb`
    bool has_voice = false;     // JS `!fm.t7` (a Voice attr was present)
    std::string voice;          // `fm.J8`
    std::string pack;           // `fm.ES`
    // RandomSound (`am` L733): the `<Name Name=..>` children (`am.qq`).
    // `am.ab()` picks one at random — JS `uf.sja(n)` (L115) =
    // `floor(Math.random()*n)`, the UNSHARED global stream (`at.Nlb`
    // L115 `return Math.random()`), NOT the fight's `Da.pg`.
    std::vector<std::string> names;

    // --- ShakeScreen (`dm` L734) -----------------------------------------
    // `dm.parse` builds `this.hw = new em` and parses the SAME node into it
    // (`em.parse` L1288): `Type`, `PauseTime`->`YIa`, `EffectTime`->`jz`,
    // `AmplitudeX`->`mva`, `AmplitudeY`->`nva`, `FrequencyX`->`$za`,
    // `FrequencyY`->`aAa`. `wd.Wvb` (L519) passes `a.hw` to the camera's
    // `ql.DL` (L370), the latch that shares its shape with the
    // `<HitEffect>` row (`sf2::scene::HitEffect`) — so these fields map 1:1
    // onto that struct's `pause_time`/`effect_time`/`amplitude_*`/
    // `frequency_*`.
    std::string shake_type;    // `em.type` (<ShakeScreen Type=..>)
    int pause_time = 0;        // `YIa` (PauseTime, frames)
    int effect_time = 0;       // `jz`  (EffectTime, frames; ZoomEffect's too)
    float amplitude_x = 0.0f;  // `mva` (AmplitudeX)
    float amplitude_y = 0.0f;  // `nva` (AmplitudeY)
    float frequency_x = 0.0f;  // `$za` (FrequencyX)
    float frequency_y = 0.0f;  // `aAa` (FrequencyY)
    // --- CameraWeight (`Wl` L726) ----------------------------------------
    // `Wl.parse`: `this.time = u.H(Time)`, `this.$x = u.H(Delay)`. `Uh`
    // either calls `wd.ANa` at once (`$x < .01`) or schedules it after
    // `$x`; `ANa` (L520) ends at `Pi.fS()` — a bare `debugger` (L424), so
    // the whole kind has NO effect in the JS.
    float weight_time = 0.0f;   // `Wl.time`  (<CameraWeight Time=..>)
    float weight_delay = 0.0f;  // `Wl.$x`    (<CameraWeight Delay=..>)
    // --- EnableBossAbility (`Zl` L730) -----------------------------------
    // `this.value = u.ka(Value)`; `Uh` -> `wd.$vb` (L520) -> `Pi.dS()` —
    // also a bare `debugger` (L397), so no effect in the JS either.
    bool bool_value = false;  // `Zl.value`
    // --- AddBullets (`Vl` L725) ------------------------------------------
    // `s6`: `Type=="MagicBullet"` -> 0, `Type=="RaidChargeBullet"` -> 1,
    // anything else leaves `this.s6` UNDEFINED (the ctor never initialises
    // it) and `wd.Tvb` (L519) matches neither branch -> no-op. `value =
    // u.I(Value)`: the `s6!=0&&s6!=1||!v.$aa||(value=0)` guard evaluates
    // `!v.$aa` = `!false` = true and SHORT-CIRCUITS, so `(value=0)` never
    // runs — the shipped `Value` stands.
    int bullet_kind = -1;   // `Vl.s6` (0 MagicBullet / 1 RaidChargeBullet / -1)
    int bullet_value = 0;   // `Vl.value` (<AddBullets Value=..>)
    // --- HitEffect (`jg` L731) -------------------------------------------
    // `jg.parse` (L731): `FileName` -> `vT`, `StartingRotation` -> `ywb`,
    // `ChangeHitEffectScale` -> `aza`. `Uh` (L731) calls `wd.Xvb(this)`
    // (L519), which hands these to `ca.Kla(Vu.bk, Vu.fg, Vu.time, vT,
    // aza>0?aza:Qz, ywb)` -> `ql.Kla` (L370) -> `Ut.Hyb` (L825). `parse`
    // also fills the preload cache `jg.Rza.v[vT]` with the `<vT>_1..N`
    // sprite-frame names (block 24 / effect_shield_hex_hit 16 / critical &
    // hit_blade 29 / others 0) — a PRELOAD-only list; the run length is
    // mirrored by `hit_effect_run_frames` in fight.cpp.
    std::string hit_effect_file;      // `vT` (FileName)
    float hit_effect_scale = 0.0f;    // `aza` (ChangeHitEffectScale; 0 = use Qz)
    float hit_effect_rotation = 0.0f; // `ywb` (StartingRotation, degrees)
    // --- CreatePlayer (`mh` L727) ----------------------------------------
    // JS `mh`: `super(0)`; `parse` sets `this.cacheName = K.T(++mh.dUa)` (a
    // per-authored-element recycle key), `Name` -> `aK`,
    // `StartAnimation` -> `nx`, and each child `<Item CopyParentType
    // CopyParentSubtype Type Name>` -> an `nl` in `this.items`. `Uh` ->
    // `wd.bwb` (L518):
    //   `wd.fya(this.ef(pe), items, aK, cacheName)` pulls a recycled child
    //   from the spawner's `su` cache or builds a new `ih` (parented to the
    //   spawner, inheriting its position/scale), then, when `nx != ""`,
    //   plays the child clip `nx` (`m.find(c.me, d => d.name == nx)` +
    //   `c.NS`). `mh` has NO `fka` (voice) gate.
    std::string create_name;       // `mh.aK`  (<CreatePlayer Name>)
    std::string start_animation;   // `mh.nx`  (<CreatePlayer StartAnimation>)
    std::string create_cache_key;  // `mh.cacheName` (`K.T(++mh.dUa)`)
    // The `nl` children (`nl extends I` L90): the element's OWN `Type`/`Name`
    // plus its `CopyParentType`/`CopyParentSubtype` pair. `wd.ylb` (L268939)
    // resolves each one against the item catalog:
    //   `d = a.name; d != "" && (d = p.items.$b(a.name), d != null &&
    //    (c = d.clone()))`                     — a NAMED list.xml item;
    //   else `d = a.Mxa; d != "" && (d = this.parameters.Fd(a.Mxa, a.Q0a),
    //    d != null && (c = d.clone()))`          — the SPAWNER's item of
    //                                             that type/subtype;
    //   then `c.Geb(a); b.hk(c.type, c)` — the child's `El` item map, which
    //   `El.cM()` turns into the model-name list.
    // The shipped rows use BOTH forms, e.g.
    //   `<Item Type="Skeleton" Name="SkeletonMagic"/>` +
    //   `<Item CopyParentType="Magic" Type="Weapon"/>`,
    // so the child wears its OWN skeleton + the spawner's magic part — NOT
    // the spawner's merged body.
    struct ChildItem {
        std::string type;          // the element's own Type
        std::string name;          // the element's own Name
        std::string copy_type;     // CopyParentType ("" when absent)
        std::string copy_subtype;  // CopyParentSubtype ("" when absent)
    };
    std::vector<ChildItem> child_items;
    // --- Delete (`Xl` L728) ----------------------------------------------
    // `super(1)`; `parse` is only `super.parse(a)`. `Uh` -> `wd.cwb` (L519):
    //   `a = this.ef(a.pe); this.Uza(); this.tK.Z(a)`
    // — signal the model the `Player` selector resolves to; the fight screen
    // (`Pi.Kja` L405) removes it and (for an `ih` with a `cacheName`, JS
    // `wd.pKa` L517) recycles it into the spawner's cache. No extra fields:
    // only `player` (`pe`) matters.
    // --- PlayAnimation (`$l` L732) ---------------------------------------
    // `super(17)`; `parse`: `ChildName` -> `cxa`, `Animation` -> `ova`,
    // `ForcePlay` -> `r4a`. `Uh` -> `wd.awb` (L518):
    //   `pe==4` -> `c = this.Vv(cxa)` (the spawner's child whose
    //             `ab()==cxa`, else its `vd[0]`),
    //   `pe==6` -> `c = this.jb.Vv(cxa)` (the ENEMY's child),
    //   default -> `c = this.ef(pe)` (the owning fighter);
    //   then `b = m.find(c.me, d => d.name == ova)` and, when
    //   `r4a || b.Yz(c)`, `c.NS(b, b.xD(c.Fc, c.da.hd()))` + `c.Ml.clear()`.
    std::string child_name;   // `$l.cxa` (<PlayAnimation ChildName>)
    std::string animation;    // `$l.ova` (<PlayAnimation Animation>)
    bool force_play = false;  // `$l.r4a` (<PlayAnimation ForcePlay>)
    // --- SetCooldown (`bm` L733, type 12) --------------------------------
    // `bm.parse`: `this.duration = u.I(Duration)` and `this.Av = Button ?? ""`.
    // `wd.Zvb` (L520): `slot = sa.HQ(0, Av)` (the `$h` button map:
    // Punch 9 / Kick 10 / Ranged 11 / Magic 12 / RaidCharge 13 / Super 14),
    // then `wKa(slot)` (reset) + `b5(slot, duration)` (arm). `yJa` (L501) is
    // the availability gate that reads the armed timers.
    int duration = 0;          // `bm.duration` (<SetCooldown Duration>)
    std::string button;        // `bm.Av` (<SetCooldown Button>)
    // --- ZoomEffect (`km` L737, type 11) ---------------------------------
    // `km.parse`: `this.Bf = new Wu` then `Bf.jz = u.I(EffectTime)` (reuses
    // `effect_time`) and `Bf.nM = u.H(ZoomScale)`. `wd.Yvb` (L520) hands
    // `Bf` to the camera `ql.Dvb` (L370).
    float zoom_scale = 0.0f;   // `Bf.nM` (<ZoomEffect ZoomScale>)
    // --- Effect (`Yl` L728-730, type 5) ----------------------------------
    // `Yl.parse` (L729): `Name` -> `name` (the shared field), `Sequence` ->
    // `fileName`, `Scale`/`ScaleX`/`ScaleY` -> `scale` (x, y), `TimeScale` ->
    // `NL`, `Looped` -> `wcb`, `OnBackground` -> `Gfb`, `Backwards` -> `lYa`,
    // `StartRotation` -> `Vla`, `PackName` -> `ES`. The child element decides
    // the placement (L730): `<Attach>` (`Vu`, L781-783) sets `FY` and forces
    // `P1=!0`; otherwise `<Position>` (`ee.Ij`, L784-786) fills the anchor and
    // `Follow` -> `P1`.
    //
    // `frames` are NOT stored here: JS resolves `G.qf("magic/<fileName>.json")`
    // at spawn (`cv.lwb` L839), so the port resolves the Sequence against the
    // loaded magic atlas registry at descriptor-build time (magic_effects).
    std::string sequence;             // `Yl.fileName` (<Effect Sequence>)
    float effect_scale_x = 1.0f;      // `Yl.scale.x` (ScaleX ?? Scale)
    float effect_scale_y = 1.0f;      // `Yl.scale.y` (ScaleY ?? Scale)
    float time_scale = 1.0f;          // `Yl.NL` (<Effect TimeScale>)
    bool effect_looped = false;       // `Yl.wcb` (<Effect Looped>)
    bool effect_backwards = false;    // `Yl.lYa` (<Effect Backwards>)
    bool effect_on_background = false;  // `Yl.Gfb` (<Effect OnBackground>)
    float start_rotation = 0.0f;      // `Yl.Vla` (<Effect StartRotation>)
    std::string effect_pack;          // `Yl.ES` (<Effect PackName>)
    // `<Position>` (`ee`, L784-786) — `P1` (Follow) + the named anchor.
    bool effect_follow = false;       // `Yl.P1` (Follow ?? false)
    int stop_follow_frame = -1;       // `ee` StopFollowframe (u.I default -1)
    std::string effect_pos_player;    // `ee.pe` (Player ?? "Null")
    std::string effect_pos_object;    // `ee.object` (Object; HQ(1,..))
    std::string effect_pos_part;      // `ee.part` (Part)
    int effect_pos_frame = 1;         // `ee.frame` (1; 2 when Frame="Previous")
    float effect_shift_x = 0.0f;      // `ee.ix` (<Position ShiftX>)
    float effect_shift_y = 0.0f;      // `ee.jx` (<Position ShiftY>)
    // `<Attach>` (`Vu`, L781-783): the effect rides the RootPoint/AttachPoint
    // pair with `OffsetVector` + `StartRotAngle`. The native port has no
    // attach-point solver (JS `Vu.C7a` carries a `debugger`), so the RootPoint
    // bone is used as the anchor and the offset is applied as a shift.
    bool has_attach = false;          // `<Attach>` present (`Yl.FY != null`)
    std::string attach_root_point;    // `Vu.jta` (<Attach RootPoint>)
    std::string attach_point;         // `Vu.jpa` (<Attach AttachPoint>)
    std::string attach_offset;        // `Vu.kpa` (<Attach OffsetVector> "x;y")
    float attach_start_rot = 0.0f;    // <Attach StartRotAngle> (deg)
};

// One child of the root `<Triggers>` block (JS `Fa.Exb` L708 ->
// `new Su(d)` + `Sl`): the GLOBAL trigger set. Structurally a perk trigger —
// `<Events>` (`Fa.GIa` L708 -> `kz.create` L771), `<Conditions>`/`<Locks>`
// (`Fa.HS` -> `Fa.H3` -> `Tl.create`), `<Actions>` (`Fa.CIa` L718 ->
// `lz.create` L737, i.e. the MOVE action kinds) — plus a `Name`.
//
// JS `Fa.parse` (L708) calls `Fa.Exb(f, e)` with `e = ra.Dm` (the static
// global list, `ra.load` L?); `ra.Z6a`/`ra.yz` then add each `Su` to a
// fight's trigger set per model, gated by its `<Locks>` (`Su.nw` L?).
// res/moves.xml ships 86 `<Trigger>` / 172 actions here (CreatePlayer 12,
// Delete 5, Effect 39, HitEffect 8, PlayAnimation 2, ShakeScreen 10,
// Sound 32, StopEffect 39, StopSound 23, TryOnEnd 2); 81 of the 86 carry
// `<Locks>` (perk/item names), so in a fight without those perks equipped
// they never fire.
//
// EVENT ID SPACE: the global block's events use the MOVE map (`kz.create` /
// `tb.D6a` L763: Hit 6, Strike 7, AnimationStart 9, EveryFrame 14,
// ModExpires 16, RoundStageStart 1) — NOT the perk map in trigger.hpp. The
// native stores the parsed event `Cond`s and matches them by element name.
struct GlobalTrigger {
    std::string name;                 // `<Trigger Name=..>`
    std::vector<Cond> events;         // `<Events>` children (`kz.create`)
    std::vector<Cond> conditions;     // `<Conditions>` children (`Tl.create`)
    std::vector<Cond> locks;          // `<Locks>` children (`Tl.create`)
    std::vector<MoveAction> actions;  // `<Actions>` children (`lz.create`)
};

// <Velocity> (JS `Fa.ykb` L721-722 -> `jc.wub`/`jc.btb`/`jc.jub`).
// `wua` (X/Y/Z) seeds `Te.DM` on move start (`Skb` L551) and `Coa`
// (Ax/Ay/Az) seeds `Te.aV`; `qta` (SaveVelocity) keeps `DM` across moves.
// Reference: `Te` ctor L546 (`j8`/`DM`/`aV`), `Skb` L551-552, `eda` L556.
struct Velocity {
    bool has_velocity = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;    // `wua` (<Velocity X/Y/Z>)
    float ax = 0.0f, ay = 0.0f, az = 0.0f; // `Coa` (<Velocity Ax/Ay/Az>)
    bool save_velocity = false;            // `qta` (SaveVelocity)
};

// <Rotation Angle=".."><Position/></Rotation> (JS `Fa.Yjb` L722 ->
// `jc.hub`/`jc.iub`). `angle` = `jc.zX` (degrees); the `<Position>` is the
// `ee` object-ref `jc.AX` the rotation pivots about (`Te.bYa` L564, called
// from `eda` L556 while `zX!=0`).
struct Rotation {
    bool has_rotation = false;
    float angle = 0.0f;              // `zX` (<Rotation Angle>)
    std::string pos_player;          // <Position Player> (`ee.pe`)
    std::string pos_object;          // <Position Object> (`ee.object`)
    std::string pos_part;            // <Position Part>   (`ee.part`)
    float shift_x = 0.0f;            // `ee.ix` (<Position ShiftX>)
    float shift_y = 0.0f;            // `ee.jx` (<Position ShiftY>)
};

// A move definition (JS `jc`).
struct MoveDef {
    std::string name;
    std::set<std::string> template_tags;  // Template "A|B|C" split on '|'
    // JS `jc.xl` (`lg.vQ` slot selection -> `XH`): the animation-NAME list the
    // `<CurrentAnimation Name=".."/>` gate matches against (`lg.he` L749:
    // `lg.xEa(this.Ba, c)`). `lh.nd` (L368460) + `jc.ava` (`m.bd`) build it as
    // the animation's own name plus every name in its TRANSITIVE `<Template>`
    // chain (`ForwardStep -> Step|Forward`). Filled in move_def.cpp from
    // `collect_templates` (own name + own Template tokens + each collected
    // template's Name).
    std::vector<std::string> anim_names;
    std::string type;                     // "ATTACK"/"MOVE"/empty
    std::string file_name;
    int mid_frames = 0;
    int first_frame = 0;
    // JS `jc.WGa` (`NoInterpolationFrames` attr): when set, `Te.Skb` passes
    // `!WGa` as `Qqa`, which makes `Te.Pka` PREPEND two copies of clip frame
    // `min(len-1, FirstFrame+2)` to the play buffer (`vu.Pka` L340543). When
    // clear, `Qqa` is true and `Te.qrb` (L282683) instead seeds the two
    // prepended slots from the CURRENT posed node ± 1.5·velocity — the
    // clip-start pose blend. Either way `vu.J$a()` (= size) is 2 larger, so
    // the clip plays two extra ranges (the source of the JS 133-vs-128 intro
    // frame count). Absent attr -> false (most moves).
    bool no_interp = false;
    // JS `l.Rha` (L362621: `u.ka(k.attributes.get("NoAnimation"))`) — the
    // move carries no animation. `Gc.DK`'s reaction partition branches on it.
    // ABSENT from the shipped moves.xml (0 occurrences) -> always false; the
    // `g` branch of `DK` is dead with shipped data.
    bool no_animation = false;
    int end_frame = 0;   // EndFrame attr, else 0 (JS `jc.Lj`)
    int priority = 0;
    float style_factor = 1.0f;  // `RNa` (StyleFactor attr, default 1.0)
    std::string tactic_weapon;     // TacticWeapon
    // QX (`jc.Gsb` L800): `TacticWeapon.split("|")` — the move's weapon list
    // `c2a` (L820, called from `bCa` L510 with `e.da.Ua.QX`) tests for
    // "Fists". 101 shipped moves carry a multi-entry list.
    std::vector<std::string> qx;
    std::string tactic_equivalent; // TacticEquivalent
    std::string mirror_node;       // MirrorNode
    // JS `Fa.Ueb` (L712): after the move is parsed,
    //   `k = k.A("Profile"); k != null && u.ka(k.attributes.get("Show"), !1)
    //    && e.push(new Ru(k, l))`
    // - a `<Profile Show="1">` child registers the move in the `Ru` catalog
    // (`ra.Ul`) that `v.uQ()` (L1218) hands to the profile Moves tab. `Ru`
    // ctor (L1253):
    //   `image = Ye.qI(Profile/@Icon)` (`Ye.qI` L1863: first '.' -> '/'),
    //   `v4    = u.I(Profile/@Rank)`   (the `es.uZ` L2239 sort key),
    //   `fFa   = Profile/@KeysDescription` (`ls.ymb` L2238 label lang key).
    // `es.NC` (L2240) builds the `ks` cell -> `ls.init(a.image, a)` (L2235);
    // `Ed.ZL` (L2203) draws `sO` as a frame of atlas id 246
    // (`res/ui/skills.json`, whose frame names ARE these paths, e.g.
    // "Trick1/block"). `Show` is the membership gate - a move without it is
    // not in `ra.Ul` and never appears in the tab.
    bool profile_show = false;        // `<Profile Show="1">`
    int profile_rank = 0;             // `u.I(Profile/@Rank)` (`v4`)
    std::string profile_image;        // `Ye.qI(Profile/@Icon)` -> atlas 246 frame
    std::string profile_keys;         // `Profile/@KeysDescription` (`fFa`)
    // Document (`<Moves><Move>`) index. JS `ra.Ul` (the `Ru` catalog that
    // `v.uQ()` L1218 returns) is filled by `Fa.Ueb` (L712) while it walks
    // `<Move>` in file order, so `ra.Ul` is in document order; `es.uZ`
    // (L2239) then `sort`es by `v4` with a STABLE V8 sort (`pb` L9 returns
    // -1/0/1), so equal-`Rank` moves keep document order. This index is that
    // tie-break (`std::map` iteration would otherwise give alphabetical).
    int profile_order = 0;            // `ra.Ul` push order (L712)
    std::vector<Cond> conditions;      // <Conditions> (own + template)
    std::vector<Cond> tactics;         // <Tactics><Conditions> (own + template)
    // JS `M7.$Q` (`Pu`, L703): the names of the HIGHER-`Priority` moves whose
    // `<Conditions>` KeyPressed spec is a sub-multiset of one of this move's
    // (`ra.c1a` -> `ra.b1a` L683-684). `Pu.Wcb` (L703) is the membership test.
    // Read by `Gc.Pkb`'s mirror-compat filter (L674-675) and by `de.V1`
    // (L601-602: `d=a.M7; if(0<d.$Q.length) ... return !1`). Filled once after
    // the whole move table is parsed.
    std::vector<std::string> mirror_exclusive;
    std::vector<Interval> intervals;   // <Intervals><Interval> (own + template)
    std::vector<Lock> locks;           // <Locks>
    // <Actions> (JS `Fa.CIa` L718 -> `Fa.DIa` L718 -> `lz.create` L737) —
    // own list first, then each inherited template's, in template order
    // (`Fa.DIa(b[a++].A("Actions"), c)`). 980 `<Actions>` blocks ship in
    // res/moves.xml (3276 `<Sound>`, 505 `<RandomSound>`, 467 `<Effect>`,
    // 210 `<Delete>`, 154 `<CreatePlayer>`, 96 `<StopEffect>`, 76
    // `<TryOnEnd>`, 37 `<StopSound>`, 36 `<ShakeScreen>`, 30 `<AddBullets>`,
    // 23 `<StopFollowEffect>`, 23 `<SetCooldown>`, 16 `<CameraWeight>`, 9
    // `<PlayAnimation>`, 8 `<HitEffect>`, 4 `<EnableBossAbility>`,
    // 1 `<SetEndStage>`, 1 `<ZoomEffect>`).
    std::vector<MoveAction> actions;   // <Actions> (own + template)
    Align align;                       // <Align>
    Velocity velocity;                 // <Velocity> (JS `jc.wua`/`Coa`/`qta`)
    Rotation rotation;                 // <Rotation> (JS `jc.zX`/`AX`)
    // Event names (JS `kz.create` L771-772 + `tb.D6a` L763): "KeyPressed"
    // (type 2), "AnimationEnd" (10), "IntervalEnd" (13), ... The fighter's
    // input path (JS `Gc.Vkb` L671 -> `Gc.EZa` L676) only considers moves
    // whose Events contain "KeyPressed".
    std::set<std::string> events;

    bool has_event(const std::string& name) const {
        return events.find(name) != events.end();
    }
};

// Parse res/moves.xml (already-extracted XML text) into name -> MoveDef.
// Mirrors JS `Fa.parse` (static) + `Fa.Ueb`/`Fa.xbb`:
//   - Templates inherit: a <Move Template="X|Y"> inherits the Conditions/
//     Locks/Intervals/Align/SetDirection of <Template Name="X"> and
//     <Template Name="Y"> (Fa.dMa walks the template chain).
//   - Returns false if the <Moves> root is missing; throws std::runtime_error
//     on malformed XML.
// When `global_out` is non-null it also receives the root `<Triggers>` block
// (JS `Fa.Exb` L708 -> `ra.Dm`).
bool parse_moves(const std::string& xml_text, std::map<std::string, MoveDef>& out,
                 std::vector<GlobalTrigger>* global_out = nullptr);

// Debug helper: print one condition tree (for the probe).
std::string cond_to_string(const Cond& c, int depth = 0);

} // namespace sf2::scene
