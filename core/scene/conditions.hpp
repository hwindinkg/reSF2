#pragma once

// Condition evaluator for the move system (JS classes Ha..ym, g="125".."134").
//
// The game evaluates a move's <Conditions> tree against the fighter's live
// state (the `Ae` class, g="13F"): current animations, active intervals,
// round stage, keys pressed, mods, items, distance to the enemy, etc.
// Every condition type in the JS switch `ec.create` / `Tl.create` is
// implemented below with EXACT JS semantics; see the per-type comments.
//
// The native port models the state it reads as `FightContext`, and evaluates
// the tree with `eval_conditions` (JS `Ha.he` + `Ha.Nba` Not handling +
// Operator And/Or from `wm.gEa`).

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "scene/move_def.hpp"

namespace sf2::scene {

// Key types (JS `sa.$h`): Punch=9, Kick=10, Ranged=11, Magic=12, ...
enum class key_type : int {
    up = 1, up_forward = 2, forward = 3, down_forward = 4, down = 5,
    down_back = 6, back = 7, up_back = 8,
    punch = 9, kick = 10, ranged = 11, magic = 12, raid_charge = 13,
    super = 14,
};

// Press type of a buffered key (JS `vm` Keys condition: Tap/Hold/Release).
enum class press_type : int {
    tap = 1, hold = 2, release = 3,
};

// One buffered input.
struct key_input {
    key_type key;
    press_type press;
};

// Round stages (JS `iz.XBa`): 1=StartStance, 2=Fight, 3=EndStance, 4=PeacefulStart,
// 5=ShopPurchase, 6=PeacefulRestore, 7=TryOn, 0=unknown.
enum class round_stage : int {
    unknown = 0, start_stance = 1, fight = 2, end_stance = 3,
    peaceful_start = 4, shop_purchase = 5, peaceful_restore = 6, try_on = 7,
};

// Fighter state the conditions read (JS `Ae` fields):
//   XH/z_/G3/oZ/A_ = animation name lists per player slot (lg.vQ)
//   xb = active intervals (tm.he)
//   Je = round stage (Em.he)
//   keys = buffered inputs (vm.he)
//   Eib/F3a = "items" lists for Item/Weapon conditions (um/Hm.he)
//   aK = fighter name (Am.he), rr = model ref for Perk/ModelExists
//   IL = last hit info (sm.he Hit condition)
//   EA/lz = perk lists (Bm.he Perk condition)
//   Xda = physics frame number (Cm.he)
//   qb = "is mirrored" flag (Dm.he ModelMirrored)
//   a.zda = round timer (Fm.he RoundResult)
//   ul = screen (Gm.he Screen), cl/Glb = bullets (om.he Bullets)
//   To = battle type (lm.he BattleType), state = boss ability state (nm.he)
//   yDa/zDa = current/param health (rm.he Health)
//   Oga/BEa/CEa = physics flags per player (lg.d7a)
//   Mla/Nla = enemy facing/direction (pm.he Direction)
//   sign = player facing (pm.he)
//   Wl = the move's `<SetDirection>` sign (qm.he Distance + vm.he Keys)
struct FightContext {
    // --- animation names currently active on the fighter/player slots ----
    std::vector<std::string> anims_me;     // XH (slot 1) — "my" animations
    std::vector<std::string> anims_enemy;  // z_ (slot 2) — enemy animations
    std::vector<std::string> anims_other;  // G3 (slot 3) — third party
    std::vector<std::string> anims_fourth; // oZ (slot 4)
    std::vector<std::string> anims_sixth;  // A_ (slot 6)
    // The candidate move being evaluated ($Move in CurrentAnimation). The
    // game sets `Ae.xK` to the candidate's animation-name list while testing
    // (wd.V1: `c.xK=a.xl`).
    std::vector<std::string> candidate_moves;

    // --- active intervals on this fighter (JS `Ae.xb`) -------------------
    // name -> (type, active).  Keyed by interval Name where present, else
    // by type string.  JS tm.he: `this.uc==0||this.uc==d.type` and
    // `this.Ba==""||d.name==this.Ba` — either Name or Type can match.
    struct interval_state {
        std::string name;  // Interval Name attr (may be "")
        int type = 0;      // fe.G0 type
        bool active = false;
    };
    std::vector<interval_state> intervals;

    // The OPPONENT's active intervals: `<CurrentInterval Player="Enemy" ...>`.
    // JS `tm.he` selects the per-player interval list via the condition's
    // `player` field (`Nd.ol` L705; Me=1, Enemy=2). The shipped `Throw`
    // template gates on `<CurrentInterval Player="Enemy" Name="Throwable"/>`
    // (moves.xml:553/565), so without this the throw gate tested the
    // ATTACKER's own intervals and could never pass.
    std::vector<interval_state> intervals_enemy;

    // --- round stage (JS `Ae.Je`, iz.XBa) --------------------------------
    round_stage stage = round_stage::unknown;

    // --- buffered keys (JS `Ae.keys` = zd) -------------------------------
    std::vector<key_input> keys;
    bool keys_gm = false;  // `Ae.gm`: "hold mode" — if set, only Held keys match
    // `Ae.keys.S1` (JS `zd.S1`): the move-executing flag `Ykb` sets when a
    // move starts (`a.S1=!0` L499). `vm.he` (L749) picks the normal key list
    // when `S1 || Wl>0`, else the direction-reversed `TDa`.
    bool keys_s1 = false;

    // --- items (Weapon/Item/Player conditions) ---------------------------
    // Each item: (type, subtype, name).
    struct item_info {
        std::string type;
        std::string subtype;
        std::string name;
    };
    std::vector<item_info> items;            // my items (um.he/Hm.he)
    std::vector<item_info> items_enemy;      // enemy items (Hm.he Enemy slot)
    std::vector<std::string> fighter_names;  // "me" fighter names (Am.he)

    // --- mods present (ModExists, xm.he) ----------------------------------
    // JS `xm.he` reads `a.Sia` (Me) / `a.S3` (Enemy) — filled per side by
    // `yma` (`c.tb.Qea(b,a.Sia); c.tb.Qea(d,a.S3)` L28651-28654). `Sia` is
    // the mod set of THIS ctx's fighter, `S3` the opponent's; the condition's
    // `Player` (`dl`) picks case 1=Me / 2=Enemy / 5=Both.
    std::set<std::string> mods;         // `Sia` — my live mod names
    std::set<std::string> mods_enemy;   // `S3` — the opponent's mod names

    // --- perks (Bm.he Perk condition) ------------------------------------
    // JS `Bm.he` switch on `dl` (Player):
    //   case 1 -> a.EA      (my perks)              case 2 -> a.lz (enemy)
    //   case 3 -> a.rr.lb.parameters.Oa  (Parent)   case 4 -> a.rr.hBa().Oa (Child)
    //   case 5 -> b(a.EA) && b(a.lz)  (Both)        case 6 -> a.rr.jb.hBa().Oa
    //   case 7 -> a.rr.Kf().parameters.Oa (SuperParent)
    // Each perk carries the action name + owning perk name; the `Perk`
    // condition matches `this.name` against the element's `.name` (the perk
    // name), so the port keeps that in `perk_name`.
    struct perk_info {
        std::string action_name;
        std::string perk_name;
    };
    std::vector<perk_info> perks_me;            // `EA`
    std::vector<perk_info> perks_enemy;         // `lz`
    // Cases 3/4/6/7 reference OTHER fighters' perk lists (the spawner / a
    // spawned child / the spawner's opponent / the super-parent). The port
    // builds a ctx only for top-level fighters (whose `lb` is null), so these
    // stay EMPTY — JS `b(null)` -> `Fd(null)` -> false, matching the shipped
    // top-level semantics exactly.
    std::vector<perk_info> perks_parent;        // case 3 (`a.rr.lb`)
    std::vector<perk_info> perks_child;         // case 4 (`a.rr.hBa()`)
    std::vector<perk_info> perks_enemy_child;   // case 6 (`a.rr.jb.hBa()`)
    std::vector<perk_info> perks_super_parent;  // case 7 (`a.rr.Kf()`)

    // --- distance to enemy (qm.he Distance) ------------------------------
    // Precomputed distance between the From/To object refs. JS computes it
    // per axis from the two `ee` object refs; the native context fills the
    // 3 values (dx, dy, dist3d) and the evaluator picks by `axis`.
    // `dist_x` is `To.OQ(a) - From.OQ(a)` (JS L744) — for every shipped move
    // the pair is `<From Player="Me" Object="Nodes" Part="NPivot"/>` /
    // `<To Player="Enemy" Object="Nodes" Part="NPivot"/>` (moves.xml, e.g.
    // ShortUpwardElbowStrike / DoublePunch / DashBackwards / WallDashForward_50
    // / WallJump_200 each carry that exact `<SetDirection>`), i.e. `enemy - me`.
    float dist_x = 0.0f;   // signed X delta (`To - From` = enemy - me)
    float dist_y = 0.0f;   // Y delta
    float dist_3d = 0.0f;  // Euclidean 2D distance (JS qm case 2)
    // JS `ee.nt(a)` (L786): resolve a Distance From/To object ref to its world
    // (x, y) — `Object` = Nodes/Pivot/Wall/Floor/MapCenter/COM, `part` = the
    // node name for Nodes, `player` 1=Me/2=Enemy, `frame_prev` = the `Frame`
    // attr (`mf` = previous frame). When set, `eval_distance` computes the
    // delta from the two refs JS-exact (case 0 `(to.x-from.x)*Wl`, case 1
    // `from.y-to.y` via `bfa = -y`, case 2 the 2D norm). When unset (probes/
    // demos that only fill the roots) it falls back to `dist_x`/`dist_y`.
    std::function<bool(const std::string& obj, const std::string& part,
                       int player, bool frame_prev, float& x, float& y)>
        ref_pos;
    // JS `ee.j8a` (L33329) / `ee.nt` case 2 (L33250): `Object="Pivot"` reads
    // `this.CBa(ctx).Yd` — the node `jc.vF`/`ee.iB` (L29378/L33309) stamps
    // from the move whose `vF` last ran (`e = a.Va.IF[this.va.align.UE]`, i.e.
    // the CANDIDATE's `<Align><Pivot Part>`). When the candidate has no align
    // node (`align.UE < 0`, or an absolute `Object="Animation"`) `j8a` falls
    // back to `ctx.cJa` = `da.Fe()` (`Te.Mqb` L23543) = the CURRENT move's
    // node, which `ref_pos` resolves. `de.V1` binds this per candidate.
    std::function<bool(int player, bool frame_prev, float& x, float& y)>
        candidate_pivot_pos;
    // The two fighter roots, kept separately so `Object="Wall"` Distance refs
    // can resolve their non-wall end (`ee.nt` L786). `dist_x == enemy_x - me_x`.
    float me_x = 0.0f;     // this fighter's world X (`Me/...` refs)
    float enemy_x = 0.0f;  // the opponent's world X (`Enemy/...` refs)
    // The scene's two wall X bounds (JS `b.yu`/`b.zu`, `ee.q9a` L788) — filled
    // from `FightController::set_bounds` (`wall_min_`/`wall_max_`). `q9a` picks
    // by `(facing>0)==(Part=="Back") ? yu : zu`, and `yu` is the LEFT bound:
    // facing right puts the fighter's back to the min-X wall.
    float wall_min = 80.0f;
    float wall_max = 1880.0f;
    // JS `Ae.Wl`. Set once per candidate move at L677
    //   `var l = b.jb!=null ? f.xD(this.Ek[d], b.da.hd()) : 1; this.Ek[d].Wl = l;`
    // `xd(a,b){return this.va.vj.mh?this.va.vj.SBa(a):b}` (L697) with
    // `Vi.SBa(a){return (this.fg!=0 ? … : this.to.OQ(a)-this.from.OQ(a))>=0?1:-1}`
    // (L704) — so `Wl` is the sign of the move's `<SetDirection>` `To - From`,
    // falling back to the model facing `hd()`. `qm.he` case 0 multiplies the X
    // delta by it (L744 `b=this.GK.OQ(a)-this.FK.OQ(a);b*=a.Wl;`), which turns
    // the raw signed delta into the FORWARD distance the gates are authored
    // around (the shipped `Me -> Enemy` SetDirection makes this `|dist_x|`).
    // `vm.he` (L749) also reads it: `a.keys.S1||a.Wl>0 ? this.xn : this.TDa`.
    float direction = 1.0f;  // `Ae.Wl` (`sb.SBa` = sign(enemy - me))
    // The OPPONENT's own `Wl` (JS `Ae.Mla` = `d.hd()`, L679). For a
    // `Me -> Enemy` SetDirection the other side's sign is the negation of ours,
    // so it is derived rather than tracked separately.
    float enemy_direction = -1.0f;

    // --- health (rm.he) ---------------------------------------------------
    float health_ratio = 1.0f;  // yDa / zDa (current/max)

    // --- physics frame number (Cm.he) -------------------------------------
    int physics_frame = 0;
    // `<CurrentAnimation>` with NO `Name` (JS `lg.he` else-branch):
    //   `b = this.d7a(a); b = b == this.PG`
    // `d7a` returns the per-player physics flag (`a.Oga`/`a.BEa`/`a.CEA`),
    // `PG` = the condition's `Physics` attr (`u.ka`). The port carries the
    // three flags; an absent-Name condition compares by `player`.
    bool physics_me = false;     // `Oga`
    bool physics_enemy = false;  // `BEa`
    bool physics_other = false;  // `CEa`

    // --- misc (not used by moves.xml but present in the JS) --------------
    bool model_mirrored = false;  // zm.he ModelMirrored (`sa.oe.set(...19)` L706)
    // JS `Dm.he` Player condition source: `qb` = "this fighter is the
    // CONTROLLED one" (`parameters.qb`; set true at L1207, false for the AI
    // at L806 `this.qb=!1;this.Fj=!0`; copied into the event ctx at L680
    // `a.qb=b.parameters.qb`). true = player, false = enemy.
    bool qb = false;
    // Random source for `Random` conditions (JS `Da.pg` shared stream).
    // When set, `eval_random` draws from it with the `Da.cT` shortcut
    // (`value>=100` -> true, no draw); when unset (probes/demos), the
    // legacy private stream is used (behavior unchanged).
    std::function<float()> roll01;
    int round_timer = 0;          // Fm.he RoundResult (zd/Jq = Victory/Defeat)
    bool round_victory = false;   // Fm.zd — "victory round" flag
    // JS `Fm.he` reads `a.kh` (the round-result LATCH — set only once the round
    // has ended; `E3a` L413 `a.kh=!0;b.kh=!0`) and `a.Iq` (the round-end code).
    // The old port ignored both, so `<RoundResult Type="Timeout"/>` (which has
    // no `Name`) evaluated `value_int==0 -> true` on EVERY frame — the getup
    // `Standup` gate's `<Or><RoundResult Name="Defeat" Not="1"/><RoundResult
    // Type="Timeout"/></Or>` was therefore always satisfied and a KO'd loser
    // could snap upright instead of playing `Loss_fall`.
    bool round_result_set = false;  // `a.kh`
    bool round_end_timeout = false; // `a.Iq` in {2,3,4,5} (`Fm.n_a`)
    int screen = 0;               // Gm.he Screen (0=Fight,10=...)
    int bullets_me = 0;           // `lp` Bullets: my `bh` (`Lh(Ob==1).bh`)
    int bullets_enemy = 0;        // `lp` Bullets: enemy `bh` (`Lh(Ob==2).bh`)
    int raid_me = 0;              // `lp` Bullets: my `dO` (RaidChargeBullet)
    int raid_enemy = 0;           // `lp` Bullets: enemy `dO`
    double charge_me = 0.0;       // `sp` MagicCharge: my `my` [0,1]
    double charge_enemy = 0.0;    // `sp` MagicCharge: enemy `my` [0,1]
    std::string battle_type;      // lm.he BattleType ("FightNone" default)
    bool boss_ability_state = false;  // nm.he BossAbilityState (Value)
    std::string fighter_name;     // Am.he Name — the fighter's model name
    // Hit condition (sm.he): type + hit animation name of last hit.
    std::string last_hit_type;      // "Critical"/"Shock"/"" — matches by name
    std::string last_hit_animation; // hit animation name
    bool has_last_hit = false;
    // JS `sm.he` / `Nm.compare` read `a.IL.se` (critical) and `a.IL.Ub`
    // (shock) as SEPARATE booleans — a hit can be both. The old single
    // `last_hit_type` string collapsed them (a Critical+Shock hit matched
    // only "Critical"), so `<Hit Type="Shock"/>` reactions/conditions were
    // missed on a crit-shock.
    bool last_hit_critical = false;  // `a.IL.se`
    bool last_hit_shock = false;     // `a.IL.Ub`

    // Helpers.
    bool interval_active(const std::string& name, int type = 0,
                         int player = 1) const;
    bool key_pressed(key_type k, press_type p) const;
    bool has_mod(const std::string& name) const;
    bool has_item(const std::string& type, const std::string& subtype,
                  const std::string& name, bool enemy = false) const;
    bool has_perk(const std::string& action_name, const std::string& perk_name,
                  bool enemy = false) const;
    bool has_animation(const std::string& anim, int slot = 1) const;
};

// Evaluate a condition tree against a context. Returns true iff the move
// passes. Mirrors JS: `Ha.Nba` (Not flip) + `wm.gEa` (And/Or short-circuit).
bool eval_conditions(const Cond& cond, const FightContext& ctx,
                     std::string* trace = nullptr, int depth = 0);

// Convenience: evaluate a whole move's <Conditions> list (JS `jc.nw`).
bool eval_move_conditions(const std::vector<Cond>& conds,
                          const FightContext& ctx, std::string* trace = nullptr);

// Debug: one-line human description of a condition node (for the probe trace).
std::string cond_desc(const Cond& c);

} // namespace sf2::scene
