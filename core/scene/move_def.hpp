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
    std::vector<std::string> attacking_parts;  // Attack intervals: Edge names
    // Attack damage block (<Damage Value=..><Damage Type=.. Shift=..>).
    float damage = 0.0f;
    bool no_critical = false;
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
    std::vector<std::string> invuln_bypass_names;    std::string damage_type;  // e.g. "UnarmedDamage"
    float damage_shift = 0.0f;
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
    std::string name;     // optional Name attr
    bool or_ = false;     // inside an Operator Type="Or" (any of the group)
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
    int end_frame = 0;   // EndFrame attr, else 0 (JS `jc.Lj`)
    int priority = 0;
    float style_factor = 1.0f;  // `RNa` (StyleFactor attr, default 1.0)
    std::string tactic_weapon;     // TacticWeapon
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
    std::vector<Interval> intervals;   // <Intervals><Interval> (own + template)
    std::vector<Lock> locks;           // <Locks>
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
bool parse_moves(const std::string& xml_text, std::map<std::string, MoveDef>& out);

// Debug helper: print one condition tree (for the probe).
std::string cond_to_string(const Cond& c, int depth = 0);

} // namespace sf2::scene
