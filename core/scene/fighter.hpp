#pragma once

// Fighter: model + animation sampling + flat-triangle rendering.
//
// Mirrors the game's `wd` fighter (MODEL_FORMAT §2): one merged ragdoll
// body (`Dl`), an animation controller (`Te`) that writes per-bone absolute
// world positions each frame, and a CPU-skinned 2D mesh (z dropped, flat
// color fill, one draw call).
//
// World placement: the fighter's world position anchors the model's
// PivotNode bone (`Dl.jX` = `v.wya`, `internal_settings.xml`
// `<PivotNode Name>`, default "NPivot"; `Dl.oL` L577 offsets all bones so
// the pivot lands on the placement point). Facing negates X (`Te.Qeb`).
// Clip bone i maps to merged model bone i (order-sensitive).

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "scene/conditions.hpp"
#include "scene/model.hpp"
#include "scene/physics.hpp"

namespace sf2::data {
struct anim_clip;
}
namespace sf2::scene {
struct MoveDef;
struct MoveAction;
struct FightContext;
struct TacticDef;       // ai.hpp (tactic_settings.xml <Tactic>)
struct AiFeatureState;  // ai.hpp (the `cc.Gb` weight-curve input)
} // namespace sf2::scene

namespace sf2::scene {

// JS config `v.wya` (`internal_settings.xml` `<PivotNode Name>`; parsed at
// L1155 as `v.wya = b != null ? b : "NPivot"`): the bone name every fighter
// is anchored on. JS `Dl` holds it in `Dl.jX` (`this.jX = v.wya`); `Dl.Trb`
// (L577) resolves `Dl.Ic(v.wya)` into the anchor `Dl.Va.Yd`, and `Dl.oL`
// (L577) offsets every bone so that anchor's `ma` lands on the placement
// point. `Fighter::sample` anchors this bone at (x, y).
const std::string& fighter_pivot_bone();
// Sets the anchor bone from the parsed config (JS assigns `v.wya` once at
// config parse L1155). Empty input is ignored (keeps the shipped default).
void set_fighter_pivot_bone(const std::string& name);

// A rendered fighter: merged model + one animation clip sampled at a frame.
// Phase 3.2b: the fighter is now CONTROLLABLE — it owns a move list (`hb`)
// built from its equipped weapon, selects moves from input via the condition
// evaluator, and plays the move's clip with per-frame interval tracking.
//
// JS execution-path study (sf2.502f0946.js) — the native port mirrors it:
//   - move list:   `wd.jmb()`/`ra.Hza` (L502/L684-685) builds `me` (the
//                  fighter's move set) by testing every parsed move's Locks
//                  against the fighter's items (L685: `f.nw(d,b)`).
//   - candidate test: `de.V1(a)` (L601-602) — the candidate must be in `me`,
//                  `Fc.xK = a.xl` (the candidate's animation-name list),
//                  then `a.Yz(...)` runs the Conditions tree (L602).
//   - input keys:  `wd.yJa(a)` (L501) -> `Kl.Sgb(a)` (zl class, L798) pushes
//                  the key into `zg.sh` (Tap) and fires the KeyPressed event;
//                  `wd.Lea()` (L512) snapshots it into `Fc.keys`.
//   - the `1key` template: the move's Keys condition requires exactly one
//                  buffered Tap of a single key — one press = one move.
//   - play start:  `wd.NS(a, b)` (L506) -> `da.Skb(a, b, ...)` (L550) — the
//                  Te controller starts the clip at `a.qx` (FirstFrame),
//                  sets facing `b` (±1), and `Peb()` (L560) auto-mirrors
//                  when the MirrorNode cross passes (Te.MYa, L566).
//   - clip advance: `da.ia()` (L547-548) — each frame `Xh++` (the playback
//                  frame counter), `fG++` (the physics frame counter);
//                  when `Xh+2 >= clipLen` the clip ends (`KNa()` + lS ->
//                  EStopAnimationEvent, L548). At 60 Hz the fighter
//                  advances one frame per update (HD()==1, L534).
//   - intervals:   `da.vp()` (L562-563) -> `rrb()` (L552) calls
//                  `jc.c7a(frame, active, done)` (L691) which fills the
//                  active-interval list `Te.xj` from the move's Interval
//                  Start/End ranges; the fighter exposes `P0()` (L493) =
//                  `da.xj` — `Fc.xb` (CurrentInterval conditions) reads it
//                  (L680).
//
// JS `wd.Cn` = `tu` (g="DE" L297387) — the strike memory. Per-move
// accumulators (JS `Eu` g="DF" L298900) of dealt damage (`Xb`), strike count
// (`count`) and hits (`tf`), exponentially decayed to the model's strike time
// (`lU`, `++` per landed strike) by the tactic's `<Memory Strikes>` half-life
// (`kfa`) and scaled by `<Memory RoundFactor>` at round end (`$K`).
// `d0(move,b,c,d)` (L298213) writes `{count,Xb,tf}` for the AI's `mQ`
// feature vector; `S5a` (L298213) sums them over body parts.
class StrikeMemory {
public:
    // JS `Eu`: `Yo/gy` are the pending (uncommitted) damage/count;
    // `Xb/tf/count` the committed values; `Yta` the last decay time.
    struct Accum {
        double xb = 0.0;   double yo = 0.0;
        double tf = 0.0;   double gy = 0.0;
        double count = 0.0;
        double yta = 0.0;  // `Yta`
    };
    // JS `Eu.gT` (L298840): `b=2^(-(t-Yta)/half_life)` then scale every
    // field, when `t-Yta > 0`. `half_life <= 0` is a guard (the shipped
    // `<Memory Strikes>` is 3).
    static void decay(Accum& a, double t, double half_life);
    // JS `tu.nY` (L297623) + `Eu.nY` (L298864): buffer `value` (damage).
    void nY(bool mine, const MoveDef* move, double value);
    // JS `tu.rY` (L297684) + `Eu.rY` (L298970): count one strike.
    void rY(bool mine, const MoveDef* move);
    // JS `tu.v_` (L297706) + `Eu.v_` (L298964): commit the buffered Xb/tf.
    void v_(bool mine, const MoveDef* move);
    // JS `tu.d0` (L298213): write `{count, Xb, tf}` for `move`.
    void d0(const MoveDef* move, double& count, double& xb, double& tf);
    // JS `tu.$K` + `Eu.aKa` (L297964/L298976): scale every accumulator.
    void round_factor(double factor);
    // The model's strike time (`wd.lU`, reset per round) — decay clock.
    void set_time(double t) { time_ = t; }
    double time() const { return time_; }
    // The tactic's `<Memory Strikes>` half-life (`Md.KW.f6` via `kfa`).
    void set_half_life(double h) { half_life_ = h; }
    double half_life() const { return half_life_; }
private:
    // `Ysa` = the "mine" map, `bqa` = the "theirs" map (`F0(ky,move)`).
    std::map<const MoveDef*, Accum> mine_;
    std::map<const MoveDef*, Accum> theirs_;
    double time_ = 0.0;       // `model.lU`
    double half_life_ = 3.0;  // `kfa` = `KW.f6` (shipped `<Memory Strikes="3">`)
    Accum& entry(bool mine, const MoveDef* move);
};

class Fighter {
public:
    // Model (already merged, skeleton-first) and rest bind positions.
    void set_model(const Model& model);

    // --- move execution (Phase 3.2b) -------------------------------------

    // Builds `hb` from the equipped weapon: every parsed move whose Locks
    // allow the weapon (JS `ra.Hza` L684-685) — for the Fists demo that is
    // all moves with TacticWeapon="Fists", sorted by Priority desc so
    // `hb[0]` is the highest-priority candidate (JS `Ci` L800 / `Zka`
    // L502 picks `HB[0]`). With `include_universal` the moves with NO
    // TacticWeapon are added too (their Locks — e.g. a Skeleton item —
    // pass for every fighter, so StepForward belongs to `me` in the JS).
    // Pointers point into the caller's stable map (the map must outlive
    // the fighter).
    void build_move_list(const std::map<std::string, MoveDef>& all_moves,
                         const std::string& weapon_subtype,
                         bool include_universal = true);

    // Locks-aware variant (JS `ra.Hza` L684-685 — `f.nw(d,b)` tests the
    // move's <Locks> against the fighter's ITEMS, not the TacticWeapon
    // string). `owned` is the fighter's item list as `OwnedItem` triples
    // (JS `Hm.he` L758: Type / SubType / Name — the Warrior's <Items> +
    // equipment slots resolved through list.xml). A move passes when every
    // Lock group resolves against the owned items: a plain <Item> lock
    // passes when an owned item matches the lock's non-empty Type AND SubType
    // AND Name (`Not` inverts); an Or-group passes when ANY item in the group
    // matches. Moves with no locks are universal (the Skeleton lock passes
    // for every fighter). This is what lets TacticWeapon="Knives|Keris" moves
    // join the list when the fighter equips WEAPON_KNIVES (SubType="Knives").
    // Sorted by Priority desc.
    // One owned item: `sf2::scene::OwnedItem` (move_def.hpp) — the JS `Hm.he`
    // comparison triple. The old (type, subtype) pair overload could not
    // express `Lock::name`, so a NAME-bearing lock could never match and the
    // Map/Dojo path silently dropped every named-lock move (the boot path
    // hardcoded names). The pair shape is gone; every caller passes names.
    using OwnedItem = ::sf2::scene::OwnedItem;
    void build_move_list_locks(const std::map<std::string, MoveDef>& all_moves,
                               const std::vector<OwnedItem>& owned,
                               bool include_universal = true,
                               const std::string& weapon_subtype = std::string());

    // Buffers one key press (JS `Kl.Sgb`/`zl.Sgb`, L798): appends the key to
    // the 2-slot Tap sequence (`zg.sh`), rebuilds the held set (`zg.Fh`),
    // resets the tap age (`dX=0`). `release` (JS `zl.Xgb`, L799) drops the
    // hold and records the release when the key was not tapped.
    void input(sf2::scene::key_type key, sf2::scene::press_type press);

    // Per-frame input aging (JS `zl.ia` L798): drop the Tap sequence at
    // `dX>=15`; at the 30-frame `Qe` cycle clear the holds/releases; rebuild
    // the held set from the currently-down keys (`yLa`).
    void age_keys();

    // Attempts move selection from `hb` (priority order) with the buffered
    // keys + current state (JS `wd.Ykb` L500 -> `nf.ia` L592-594).
    //
    // JS `de.ia` (L594) does NOT take the first passing candidate: it builds
    // the candidate animation list (`h2a` L608: `ld.push(c.animation)`) and
    // then `a = this.jL(this.ld)` — the WEIGHTED ROULETTE (`nf.jL` L597 ->
    // `Md.jL` L640 + `iCa` L640) over ALL passing candidates, drawing from
    // `Da.pg` (`s4`). The tactic's `<AnimationWeights>` table (`Md.$oa`,
    // parsed L638) supplies the weights via `cc.Gb` (L647).
    //
    // 1-arg overload: no tactic/feature state available (probe/demo callers).
    // It keeps the legacy first-passing pick and draws nothing.
    // 3-arg overload (the fight path): full roulette. `tactic` = the tactic
    // (`Md`), `feat` = the `iN` feature state (`mQ` L620). When the tactic is
    // known but the total weight is 0 the JS `jL` returns -1 and `ia` returns
    // null — no move starts (and NO draw is consumed).
    // Returns the started move's name, or "" if none passed.
    std::string try_select_move(sf2::scene::FightContext& ctx);
    std::string try_select_move(sf2::scene::FightContext& ctx,
                                const sf2::scene::TacticDef* tactic,
                                const sf2::scene::AiFeatureState* feat);
    // Hit-reaction pick (JS `Gc.DK` L673-674, d-set first-match): starts the
    // first priority-ordered `hb` move carrying a `Hit` event whose tactics
    // conditions pass (54 such moves in moves.xml: HighHit/MiddleHit/...,
    // PhysicalFall/...). `prefer_fall` (shock knockdown, `Ub`) tries
    // *Fall*-named reactions first — a proxy for the MS/jJa branch (the
    // exact MS mapping is OPEN). The JS `DK` picks the top-`priority` group
    // and then `f[uf.sja(len)]` (uniform, `Math.random`); `rng` is the
    // injected `Math.random` analog (never `Da.pg`). The `Pkb` weighted
    // roulette at reaction time stays OPEN (no tactic weights available).
    // Returns the name or "".
    std::string try_react(sf2::scene::FightContext& ctx, bool prefer_fall,
                          const std::function<float()>& rng = {});

    // Starts `move` if its conditions pass: sets current_move, move_frame=0,
    // loads the clip (FileName -> anim_archive clip), sets facing toward the
    // enemy, arms the move's intervals (JS `Te.Skb` L550 + `jc.c7a` L691).
    bool try_start_move(const MoveDef& move, sf2::scene::FightContext& ctx);

    // AI variant of try_start_move: starts `move` bypassing the Keys
    // conditions (JS `de.V1` L601-602 sets `Fc.gm=!1` so `vm.he` L749
    // returns true for every Keys condition — the AI "simulates" the move's
    // key press via `Kl.Ptb` in `Okb` L506). The other conditions (Distance,
    // CurrentAnimation, CurrentInterval, ...) are still evaluated exactly.
    bool ai_start_move(const MoveDef& move, sf2::scene::FightContext& ctx);

    // Shared implementation of try_start_move / ai_start_move.
    bool start_move_impl(const MoveDef& move, sf2::scene::FightContext& ctx,
                         bool ai);

    // Side-effect-free input-path condition test (JS `de.V1` L601-602 with
    // `gm` left TRUE — the player path). Sets `ctx.candidate_moves` to the
    // move's animation-name list, snapshots the buffered keys, and runs the
    // move's own `<Conditions>` tree. Used by `try_select_move` to collect
    // EVERY passing candidate before the roulette; `start_move_impl` re-runs
    // the same test (with a trace) when the picked move actually starts.
    bool move_conditions_pass(const MoveDef& move, FightContext& ctx,
                              std::string* trace = nullptr) const;

    // Anim timescale (SlowModel `Kvb`/`KT`): apply sets scale (Speed>=1;
    // Speed<1 is a verbatim no-op), revert restores 1.0 (`v.dB` assumed).
    void set_time_scale(float s) { time_scale_ = s; scale_acc_ = 0.0f; }
    float time_scale() const { return time_scale_; }
    // Advances the current clip one frame (60 Hz). Updates active intervals
    // (Start/End), transitions to idle when the clip ends (JS `Te.ia`
    // L547-548: `Xh+2 >= len` -> stop). Samples the pose at move_frame.
    void advance(float dt);
    void advance_step();  // one fixed sub-step (timescale loop calls this)

    // --- move-frame action dispatch (JS `Te.Lwa` L563-564) -----------------
    // JS `xc.voice` (the fighter XML `<Voice>` attr; `Vo` ctor default ""
    // L807, filled by `ur` L186 from the Warrior's Voice). The
    // `<Sound Voice=..>`/`<RandomSound Voice=..>` gate reads it
    // (`wd.dwb`/`wd.fwb` L519 -> `fm.fka` L735 `t7 ? true : voice == J8`).
    // An EMPTY voice therefore silences every Voice-gated action (JS-exact:
    // `"" == "Male"` is false).
    void set_voice(const std::string& v) { voice_ = v; }
    const std::string& voice() const { return voice_; }

    // The current move's FRAME-triggered actions whose `Frame` equals the
    // clip frame the last `advance()` displayed, collected once per frame
    // change (`Te.Lwa` L563-564: `e.$eb(this.ip(), ...) && c.push(e)`).
    // Drained by the caller (the JS `gh("EActionStart", c)` L564 -> `wd.mHa`
    // L530 -> `wd.BNa` L523 path). Empty when no move is playing.
    const std::vector<const MoveAction*>& take_frame_actions();

    // The current move's EVENT-triggered actions with `Event == name`
    // (JS `Te.CZa(a)` L555: `e.afb(a, this.Ua, ...) && c.push(e)`). The
    // caller evaluates each action's `<Conditions>` (`cb.Ti` L724) before
    // firing. Used for the landed-hit `Strike` (7) / `Hit` (6) dispatch.
    std::vector<const MoveAction*> move_actions_for_event(const std::string& event) const;

    // Collects (and clears) the move whose clip ended during the last
    // `advance()` — the `AnimationEnd` (10) action source (JS `Te.lS`
    // L553 -> `wd.kg` -> `Gc.Ih(10,..)` L671 -> `Gnb` L672 -> `CZa(10)`).
    const MoveDef* take_ended_move();

    // [FIX idle-slide — Phase 1] Clears the current move (JS `KNa`): the
    // fighter returns to idle. Used by the fight controller when the
    // StartStance -> Fight transition cuts the intro clip so the fighters
    // don't keep sliding on stance_1/stance_2 into the idle phase.
    void clear_move();

    // --- JS `Vu` (mu g="D0" L249972) — the pending hit-reaction latch -----
    // `lrb(bk,fg,time)` (L523) arms it; `Xvb` (L266159) gates the hit flash
    // on `Vu.Ica`; `eob()` (L523) clears it (the move/round reset `kob`
    // L403). The port's flash spawn now reads this latch instead of
    // re-deriving the gate.
    struct Reaction {
        bool active = false;      // `Ica`
        sf2::scene::Vec3 pos{};   // `bk` — the contact point
        sf2::scene::Vec3 dir{};   // `fg` — the strike direction
        float time = 0.0f;        // `time` — the flash speed (1/60 or 1/120)
    };
    void latch_reaction(const sf2::scene::Vec3& pos, const sf2::scene::Vec3& dir,
                        float time);
    void clear_reaction() { reaction_.active = false; }
    bool has_reaction() const { return reaction_.active; }
    const Reaction& reaction() const { return reaction_; }

    // --- JS `wd.Cn` = `tu` (g="DE" L297387) — the strike memory -----------
    // Per-move accumulators (JS `Eu` g="DF" L298900) of dealt damage (`Xb`),
    // strike count (`count`) and hits (`tf`), exponentially decayed to the
    // model's strike time (`lU`, `++` per strike) by the tactic's
    // `<Memory Strikes>` half-life (`kfa`) and scaled by `<Memory
    // RoundFactor>` at round end (`$K`). `d0(move,b,c,d)` (L298213) writes
    // `{count,Xb,tf}` for the AI's `mQ` feature vector. Type declared at
    // namespace scope (below) so `AiFightState` can carry a pointer.
    void bump_strike_time() { ++strike_time_; }  // `strike`: `++this.lU`
    double strike_time() const { return strike_time_; }
    StrikeMemory& strike_memory() { return strike_memory_; }
    const StrikeMemory& strike_memory() const { return strike_memory_; }

    // --- state accessors (Phase 3.2b) -------------------------------------
    const MoveDef* current_move() const { return current_move_; }
    int move_frame() const { return move_frame_; }
    // JS `Te.M2` — the anim controller move-frame counter. `Te.ia`
    // (L547-548) opens with `this.M2++` and later in the SAME call does
    // `this.Xh++`, so the two counters advance in LOCKSTEP: `M2 == Xh - 4`
    // (`Xqb` L282808 seats `M2 = -4`, `Skb` L280606 seats `Xh = 0`). `Xh` is
    // this port's `playhead_`. `Tba` (L595) = the max `M2` over the enemy's
    // body parts; the port's single body maps to this. NOTE the shipped
    // `Xqb` guards the `M2=-4` seat with `yra` (`this.yra||(...)`) and `yra`
    // is never re-armed, so a literal reading would make `M2` a
    // model-lifetime counter; the port re-seats it per move start (the only
    // reading that keeps `Tba`/`Gea` a move-frame quantity). Reported OPEN.
    int m2() const { return playhead_ - 4; }
    // Subframe state (JS `Te.mo`): `subframe()` is the current subframe
    // index within the clip-frame, `sub()` the subframes per clip-frame.
    // Pose-trace accessors only — no behavior change.
    int subframe() const { return subframe_; }
    int sub() const { return sub_; }
    int facing() const { return facing_; }
    const std::vector<const MoveDef*>& hb() const { return hb_; }
    // The last weighted-roulette outcome (JS `Md.jL` L640 + `iCa` L640) —
    // the JS-exact pick record the `--verify-input` probes assert: the
    // candidate animation list in `jL` order, each candidate's weight, the
    // weight SUM (`d`), the raw shared-stream draw (`Da.pg.jf()`, one draw
    // per pick), the returned index and the move that actually started.
    // `valid` is false until a roulette runs (and resets to false on every
    // `try_select_move` that returns before the roulette).
    struct RouletteRecord {
        bool valid = false;
        std::vector<std::pair<std::string, float>> cands;  // animation -> `iCa`
        float sum = 0.0f;    // JS `Md.jL` `d` (the weight total)
        float roll = 0.0f;   // JS `Da.pg.jf()` (`s4(d)` = `jf()*d`)
        float draw = 0.0f;   // JS `Da.pg.s4(d)` = roll * sum
        int index = -1;      // JS `Md.jL` return (-1 = `if(0<d)` failed)
        std::string picked;  // the move the pick actually started
    };
    const RouletteRecord& last_roulette() const { return roulette_; }
    // Test/trace accessors (no behavior change): live input-buffer counts.
    int buffered_tap_count() const;
    int buffered_hold_count() const;
    const std::set<std::string>& active_intervals() const { return active_intervals_; }
    // Interval names active at `frame` (JS `jc.c7a` L691 semantics).
    std::vector<std::string> intervals_at(int frame) const;
    // `fe.G0` type of the named interval in the CURRENT move (0 when
    // absent — powers `CurrentInterval` conditions + Lj edge vars).
    int interval_type(const std::string& name) const;
    // JS `wd.Nbb` (L514): `qYa()!=null` = a Block interval (`da.yD(5)`)
    // is active at the current frame.
    bool has_block() const;
    // JS `Te.hT(5)` (L554): remove ALL active Block intervals (strike()
    // pre-break when `g.DDa`, L509; `Cgb` post-hit when !block, L394-397).
    void clear_block();
    // JS `Te.hT(type)` / `F4(name)` (L554) + scripted `Yob` (L1294):
    // remove active intervals by TYPE (`G0` id, -1 = any) or NAME
    // ("" = any). Powers perk DisableInterval.
    void clear_intervals(int type, const std::string& name);
    // JS `Te.yD(6)` presence (L553): an Invulnerable interval active now —
    // the `HZa` chain gate (L500-501) consults it on the TARGET.
    bool has_invuln() const;
    // Enemies (for facing). Set by the caller (demo).
    float enemy_x() const { return enemy_x_; }
    void set_enemy_x(float x) { enemy_x_ = x; }
    void set_world_pos(float x, float y) {
        world_x_ = x;
        world_y_ = y;
        sample_current();
    }
    float world_x() const { return world_x_; }
    float world_y() const { return world_y_; }
    // JS `Dl.Eu.ma` — the fighter's Center-Of-Mass BODY. It is NOT the XML
    // `<COM Type="CenterOfMass">` bone (that one has Mass=0 and no LCC — a
    // separate immovable body in JS, `sf2.502f0946.js` L571), and NOT the
    // render root (`Fe().ma` = the NPivot anchor `world_x()`/`world_y()`
    // above). JS derives `Eu.ma` every frame in `Dl.v6()` (L577) as the
    // MASS-WEIGHTED centroid of the whole body:
    //     Eu.ma = Σ(L0()[i].ma · L0()[i].weight) / VR
    // where `weight` = the node's `Mass` attribute (`Yc.Ijb`), `VR = Σ weight`
    // (`Dl.Esb`), and `L0()` = the resolved `<Nodes>` body list (`Du.bca`,
    // filled by `Yc.Mia` → `Du.rWa`). The fight camera targets the midpoint
    // of the two fighters' COMs (`Dl.mea(a.Eu,b.Eu)`, L535/L581). Falls back
    // to the render anchor when the merged model carries no mass.
    float com_x() const { return com_axis(0); }
    float com_y() const { return com_axis(1); }
    // Clamps the fighter's world x to [min_x, max_x] (the arena walls).
    // Called each frame by the fight controller after the root-motion walk.
    void clamp_x(float min_x, float max_x) {
        if (world_x_ < min_x) world_x_ = min_x;
        if (world_x_ > max_x) world_x_ = max_x;
    }
    // Clip lookup callback — the demo supplies the archive.
    void set_clip_lookup(const std::function<const sf2::data::anim_clip*(const std::string&)>& fn) {
        clip_lookup_ = fn;
    }

    // --- existing render path ---------------------------------------------
    // Per-bone world positions at frame `f` of `clip`, anchored so the
    // fighter's PivotNode bone (`fighter_pivot_bone()`) sits at (x, y).
    // Bones beyond the clip's bone count keep their bind position.
    // `mirror_sign` -1 negates the clip buffer x (JS `Te.Qeb` L550 →
    // `vu.Neb` L668 `data[b].x*=-1`); it is the CLIP MIRROR (`Te.FX` /
    // `hd()`), NOT the `b6a` facing lock — see `clip_mirror_`.
    //
    // `interp` selects the JS clip-playback sampling (`Te.Gka` L285802): the
    // interpolation buffer is [slot0, slot1, clip[FirstFrame], ...] where
    // slots 0,1 are the clip-start prepend (JS `vu.Pka`/`Te.qrb`); the sampled
    // control points are buffer [playhead, playhead+1, playhead+2]. With
    // `interp=false` the legacy static mapping (frame, frame+1, frame+2) is
    // kept for the dojo probe/bag poses.
    void sample(const sf2::data::anim_clip& clip, int frame, float x, float y,
                int mirror_sign, bool interp = false, int first_frame = 0,
                int playhead = 0);

    // Flat fill color (RGB, 0..255).
    void set_color(std::uint32_t rgb) {
        color_r_ = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
        color_g_ = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
        color_b_ = static_cast<float>(rgb & 0xFF) / 255.0f;
    }
    float color_r() const { return color_r_; }
    float color_g() const { return color_g_; }
    float color_b() const { return color_b_; }

    const Model& model() const { return model_; }
    const std::vector<float>& positions() const { return pos_; }  // x,y pairs

    // Per-bone knockback offsets (JS `Bl.strike` L582 moves the hit
    // capsule's endpoint BODIES, not the whole fighter). `add_knockback`
    // accumulates the impulse-split vector onto a bone; the offsets ride on
    // top of the clip sample, decay per tick in advance() (`decay_knockback`,
    // OPEN exact rate — the `Vc.sk` integrator is out of scope), and feed
    // the next frame's hit capsules via positions() (emergent correctness).
    void add_knockback(int bone, const sf2::scene::Vec3& v);

    // Fills `out` with the triangle vertex list (screen-space x,y pairs, z
    // dropped). Returns the vertex count (3 * triangle count).
    std::size_t build_vertices(std::vector<float>& out) const;

    // World-space bounding box of the triangle-referenced bones.
    void triangle_bbox(float& min_x, float& min_y, float& max_x,
                       float& max_y) const;

private:
    Model model_;
    std::vector<float> pos_;  // per-bone [x, y] after sampling (world space)
    float color_r_ = 1.0f;
    float color_g_ = 1.0f;
    float color_b_ = 1.0f;

    // --- move execution state (Phase 3.2b) --------------------------------
    std::vector<const MoveDef*> hb_;        // move list (sorted, priority desc)
    RouletteRecord roulette_;               // last `Md.jL` outcome (probe/trace)
    const MoveDef* current_move_ = nullptr; // playing move (JS `da.Ua`)
    const sf2::data::anim_clip* current_clip_ = nullptr; // clip for `current_move_`
    int move_frame_ = 0;                    // clip frame (JS `Te.M0()`) for intervals/cf
    // JS `Vu` (mu L249972) — the pending hit-reaction latch (`lrb`/`eob`).
    Reaction reaction_;
    // JS `wd.Cn` (tu L297387) + `wd.lU` (the strike-time clock).
    StrikeMemory strike_memory_;
    double strike_time_ = 0.0;
    // JS `Te.Xh` (the playback/buffer index). The play buffer is
    // [slot0, slot1, clip[FirstFrame], clip[FirstFrame+1], ...]: slots 0,1
    // are the clip-start prepend (JS `vu.Pka` L340543 copies of clip
    // [FirstFrame+2] when `NoInterpolationFrames`, else `Te.qrb` L282683's
    // `ma ± 1.5·(ma-mf)`). `move_frame_` stays the CLIP frame for the
    // interval/cf consumers; `playhead_` drives sampling (JS `Te.Gka`).
    int playhead_ = 0;
    // Clip-start prepend pose, stride 3 (x,y,z) over the clip bone count
    // (2 slots). Built by `build_prepend` at clip start; cleared when idle.
    std::vector<float> prepend_;
    // [FIX Phase 4a — pacing] The subframe phase within the current
    // clip-frame (JS `Te.mo`). `sub_` = (MidFrames+1) subframes per
    // clip-frame (JS `eda`: `mo += Tx`, `Tx=(XJ+1)*HD`). The sample()
    // interpolates between clip frame `move_frame_` and `move_frame_+1`
    // at `subframe_/sub_`.
    int sub_ = 1;
    int subframe_ = 0;
    // [FIX stretched mesh — ragdoll solver] The game's `Al` Verlet solver
    // state (JS `Vc.ma`/`Vc.mf`): current and previous posed position per
    // bone, in the CLIP's model space (before the COM/world placement).
    // The solver runs once per sample() call (the game's 60 Hz cadence:
    // `Te.eda` applies the clip, `Al.ia` = `sk` integrate + `jE` edge
    // relax x2, `Dl.Qja` re-derives the macros). Replaces the old static
    // bind-offset cloth anchoring (`nearest_clip_`), which stretched the
    // cloth triangles because the cloth's <Edges> constraints bind the
    // cloth nodes to DIFFERENT bones (head macros, knees, ankles) than the
    // bind-nearest skeleton bone.
    std::vector<float> sol_ma_;  // 3*n: current posed positions (JS `ma`)
    std::vector<float> sol_mf_;  // 3*n: previous positions (JS `mf`)
    bool solver_init_ = false;   // ma/mf seeded from the bind pose once
    // [FIX stretched mesh — continuous solver space] The JS solver state
    // (`ma`/`mf`) lives in the fighter's CONTINUOUS space: the whole fighter
    // (skeleton AND cloth) shares one world placement, so switching clips
    // never teleports the cloth. The native solver is authored in raw CLIP
    // coordinates, which jump ~740 units between clips (stance_2 COM x=-502
    // vs an attack clip x=+237); without compensation the cloth is left
    // behind on every clip switch. Translate the persisted state by the COM
    // delta each sample (the native-space equivalent of the JS continuity).
    float sol_prev_com_x_ = 0.0f;
    float sol_prev_com_y_ = 0.0f;
    float sol_prev_com_z_ = 0.0f;
    bool sol_have_prev_com_ = false;
    // [FIX root-motion align — JS `Te.Gub` L557-559 -> `Te.Gla` L550
    // (`jc.shift`)] The move's <Align> offset, applied ONCE at clip start as
    // a shift of the whole clip buffer. Native equivalent: added to every
    // clip-driven bone in sample() before the solver runs. Computed by
    // `compute_align` in start_move_impl; zero while no move is playing.
    float align_x_ = 0.0f;
    float align_y_ = 0.0f;
    float align_z_ = 0.0f;
    // [FIX render anchor — JS `Dl.Fe()` (L575, `Va.Yd` = the `<PivotNode
    // Name>` node) + `Te.Gub`/`Te.Gla` (L557-559/L550)] The JS trace/camera
    // anchor is NPivot's POSED x, and the align shifts the whole clip buffer
    // so the `<Align><Pivot Part>` node (NHeel_2) keeps its world x across a
    // clip switch; NPivot then rides the clip from there:
    //   world_x = clip_interp(NPivot) + (prev_world(Part) - clip(Part, FirstFrame))
    // `render_offset_` is that per-move constant (captured on the first
    // sample; `render_offset_valid_ == false` = recompute). The old code
    // accumulated the clip's root-BONE-0 delta instead, which is a different
    // node's swing and drifted the intro stance ~19u off the JS.
    float render_offset_ = 0.0f;
    bool render_offset_valid_ = true;
    float prev_align_pivot_world_x_ = 0.0f;  // previous frame's world x of the Part
    // [B1 FIX — vertical render anchor] The y analog of `render_offset_`.
    // The JS anchor is a POSED node: `Te.eda` (L556) writes `ma = fq[mo] + j8`
    // for EVERY clip bone, so NPivot's y rides the clip exactly like its x, and
    // `Dl.oL(spawn)` (L577) runs only at init / round reset — it is never
    // re-pinned per frame. `Gla` selects the y shift the same way as x:
    //   `Gla(a.cI?Fk.x:a.dja, a.dI?Fk.y:a.eja, a.MY?Fk.z:0)` (L559)
    // i.e. `Fk.y` when the `<Align><Position>` declares the Y axis (JS `dI`),
    // else `ShiftY` (`eja`, 0 shipped). Captured in `start_move_impl` next to
    // `render_offset_`; 0 while no move plays.
    float render_offset_y_ = 0.0f;
    float prev_align_pivot_world_y_ = 0.0f;  // previous frame's world y of the Part
    int align_pivot_u_ = -1;                 // `<Align><Pivot Part>` bone index (`UE`)
    // [F1/F3] The bone the align actually reads after `Peb`'s `rw` node swap
    // (`Te.Peb` L560 `this.os = model.NQ(this.os)` -> `Gub` L558/559 read `os`,
    // i.e. the mirror PARTNER of `<Align><Pivot Part>` when `rw`). Used by the
    // solver-state translation anchor too (F9).
    int align_ref_u_ = -1;
    // [FIX root motion — JS `Te.j8.x` (L546, `eda` L556)] The authored
    // `<Velocity>` offset accumulated per frame (`Pab`/`Qab` L564:
    // `j8 += DM*sG`). JS adds `j8` to every posed bone, i.e. to the anchor.
    float j8_x_ = 0.0f;
    // [FIX root motion — JS `Te.j8`/`Te.DM`/`Te.aV`] The move's authored
    // <Velocity> (JS `Fa.ykb` L721-722) integrated per frame exactly like
    // the JS controller (`Te` ctor L546; `Skb` L551-552 seeds `DM`/`aV`;
    // `eda` L556 calls `Pab`/`Nab` L564 = `Qab`/`Oab`). `root_dm_x_` and
    // `root_av_x_` are the x components of `DM` (velocity) and `aV`
    // (acceleration); `sG = 1/Tx` (`Gka` L561, Tx = model.HD() = 1). The JS
    // `j8 += DM*sG` accumulation is applied straight onto world_x_ per
    // frame, so no separate `j8` field is kept.
    // `root_active_` is true only when the current move carries <Velocity>;
    // otherwise the COM-delta fallback (the clip's baked root bone) drives
    // world_x_, matching JS where `j8` stays 0 with no <Velocity>.
    float root_dm_x_ = 0.0f;
    float root_av_x_ = 0.0f;
    bool root_active_ = false;
    // [FIX stretched mesh — JS-faithful] The game runs exactly one solver
    // step per frame (`Al.ia()` L582 = `sk(); jE();`); there is no warmup
    // and no cross-clip COM-delta translation of the solver state.
    // (The invented warmup 600 + COM-delta block were removed.)
    // Paired bones _1 ↔ _2 (JS `Dl.Hqb` L580 -> `Dl.v5a` L580, the `Wf.b3`
    // list `Ua.Oeb` L692 consumes). Built in `set_model` from the merged
    // `Va.all` order, exactly like `v5a`.
    std::vector<std::pair<int, int>> mirror_pairs_;
    // [F3] JS `Te.Peb` L560 (`this.rw = Te.MYa(...)`): the mirror swap is
    // decided ONCE at clip start and then applied to the WHOLE buffered clip
    // (`Ua.Oeb` L692 swaps every `_1`/`_2` pair for every buffer slot >= 2; the
    // two `qrb` prepend slots are never swapped). The old port re-derived it
    // per frame from `prev_x_` vs the new `px` order and dropped both JS gates
    // (`!Ua.J2.Vj` = the move carries no `<MirrorNode>`, and the `Ic`+`NE`
    // pair resolving). `mirror_swap_` is set in `start_move_impl`.
    bool mirror_swap_ = false;
    // [F1] JS `Te.Qeb` L550 -> `vu.Neb` L668: with `Te.FX == -1` (facing -1)
    // the whole clip BUFFER x is negated around clip-space 0, from slot
    // `jW ? 2 : 0` up. `jW` is true whenever `qrb` seeded the two prepend
    // slots (`vu.Cbb` L667 sets `this.jW = !0`), i.e. whenever the move does
    // NOT carry `NoInterpolationFrames`; in the `Pka`-prepend case
    // (`no_interp`, `jW == false`) the negation starts at slot 0 and the two
    // prepend slots are negated too.
    bool mirror_x_ = false;        // clip_mirror_ < 0 for the current move
    bool mirror_prepend_ = false;  // mirror_x_ && current_move_->no_interp
    // True once `sample()` has written a real frame into `pos_`. JS `ma` always
    // holds a pose (the bind pose before the first `eda`), so the `lwa` order
    // test in `start_move_impl` falls back to the BIND x until then.
    bool pose_sampled_ = false;
    // [F10] The `b6a` FACING LOCK (movement/orientation), JS L603
    // `b6a(a){...a.ma.x-b.ma.x>=0?1:-1}` — set in `start_move_impl`. This is
    // what `facing()` reports (the pose dump's `fx`).
    int facing_ = 1;
    // [F10] The CLIP-BUFFER MIRROR sign — JS `Te.FX`, read through
    // `Te.hd()` (L547 `hd(){return this.FX}`). A term DISTINCT from the
    // `b6a` lock above: `Te.Skb` L551 runs `this.rub(b)` (L547
    // `rub(a){this.FX=a<0?-1:1}`) with `b` = the animation request's `sign`
    // = `Ae.Wl` (L677), and `Ae.Wl` comes from `Fa.xD` (L697)
    // `xD(a,b){return this.va.vj.mh?this.va.vj.SBa(a):b}` → `Vi.SBa` (L704)
    // `(this.fg!=0 ? … : this.to.OQ(a)-this.from.OQ(a))>=0?1:-1`, i.e.
    // `sign(To - From)` = `sign(enemy_x - me_x)` for the shipped
    // `<From Player="Me" .../><To Player="Enemy" .../>` SetDirection; with no
    // `<SetDirection>` (`vj.mh == false`) `xD` returns the caller's `b` =
    // the previous `hd()`, so the value carries over. Initialized to +1 by
    // the `Te` ctor (L545) and reset to +1 by `Te.reset` (L548).
    // It drives the clip-buffer negation (`Qeb` L550 → `vu.Neb` L668), the
    // `Peb`→`MYa` operand swap (L560), the `Gub` align `hd()` factors
    // (L558-559) and the `<Velocity>` x seed (`Skb` L552 `this.DM.x*=b`).
    int clip_mirror_ = 1;
    float world_x_ = 0.0f, world_y_ = 0.0f; // fighter anchor (pivot world pos)
    float time_scale_ = 1.0f;  // anim timescale (SlowModel KT channel — single; hU noted)
    float scale_acc_ = 0.0f;   // timescale fractional accumulator
    std::vector<sf2::scene::Vec3> kb_;  // per-bone knockback offsets (world)
    std::set<std::string> active_intervals_; // active interval names (JS `Te.xj`)
    // --- move-frame action dispatch (JS `Te.Lwa` / `Te.CZa`) --------------
    // JS `xc.voice` (see `set_voice`).
    std::string voice_;
    // The actions collected by the last `advance()` (frame triggers). JS
    // `Te.Lwa` L563-564 builds `c` and fires `gh("EActionStart", c)`.
    std::vector<const MoveAction*> frame_actions_;
    // JS `Te.cX` (L546 ctor `this.cX=2147483647`; `vp` L563
    // `a != this.cX && (this.cX = a, this.rrb(), this.N9 = !0)`): the last
    // clip frame the action pass ran for. -1 = none yet, so the first frame
    // of a move fires (the JS MAX_INT sentinel).
    int last_action_frame_ = -1;
    // JS `KNa`/`Sca` leaves `Ua` set; the `AnimationEnd` actions read it
    // (`Gnb` L672 `c.model.da.CZa(c.type)` -> `Te.CZa` reads `this.Ua`).
    const MoveDef* ended_move_ = nullptr;
    float enemy_x_ = 0.0f;                  // enemy world X (for facing)
    std::vector<sf2::scene::key_input> keys_; // buffered inputs (JS `Kl.zg`)
    int tap_age_ = 0;                       // frames since last tap (JS `zl.dX`)
    // Keys currently held down (JS `zl.Ff[].sl` -> rebuilt `zg.Fh`): the
    // physical keydown set. `input(tap)` inserts, `input(release)` erases;
    // `rebuild_holds()` projects it into the `keys_` Hold entries.
    std::set<sf2::scene::key_type> held_keys_;
    // JS `zl.Qe` (L798): the 30-frame hold/release clear cycle.
    int hold_age_ = 0;
    std::function<const sf2::data::anim_clip*(const std::string&)> clip_lookup_;

    void rebuild_holds();
    void compute_align(const MoveDef& move);
    // JS `Dl.NQ` (L575) over the `Wf.b3` pair list (`Dl.v5a` L580): the mirror
    // partner bone of `i` (`_1` <-> `_2`), or -1 when `i` is unpaired.
    int mirror_partner(int i) const;
    // JS `Ua.Oeb` (L692): when `rw` (`Te.Peb` L560) is set the buffered clip's
    // `_1`/`_2` pairs are swapped for every slot >= 2, so sampling bone `i`
    // reads the buffer value of its partner. `zclip` guards the JS
    // `a[c].first<e&&a[c].second<e` test (`e` = `Kh(2).size`).
    int mirror_swap_src(int i, std::size_t zclip) const;
    // [FIX prepend-lag / F9] Translate the persisted ragdoll solver state
    // (`sol_ma_`/`sol_mf_`) from the previous clip's raw coordinate space into
    // the new clip's space, anchored at the new clip's `<Align><Pivot Part>`
    // node position `(px,py,pz)` (NOT bone 0 — the align's own reference node
    // is the one whose world continuity `Te.Gub` L558-559 preserves).
    // JS `Te.Skb` L550 builds the play-buffer prepend (`Te.qrb` L282683) from
    // the CURRENT continuous `ma`/`mf` BEFORE the first `eda` sample, so the
    // native prepend must be frozen in the NEW clip space too (otherwise the
    // two prepended slots sit a whole cross-clip COM delta away, and every
    // move start snaps). This is the port's bridge (the JS has no counterpart);
    // it runs ONCE per move start — the per-sample re-application that used to
    // follow inside `sample()` re-stepped the same delta every frame (and used
    // bone 0), which is removed.
    void translate_solver_state(float px, float py, float pz);
    void build_prepend(const MoveDef& move);
    void sample_current();

    // [F4] Mass-weighted centroid of the posed body over the COM child list
    // (JS `Dl.v6` L577: `Eu.ma`). `axis` 0 = x, 1 = y. Falls back to the
    // render anchor when `pos_` is not sampled yet or no listed bone carries
    // mass.
    float com_axis(int axis) const {
        const std::size_t n = model_.bones.size();
        if (pos_.size() < n * 2) {
            return axis == 0 ? world_x_ : world_y_;
        }
        // [F4] JS `Dl.v6` L577 averages `L0()`: `Va.bca` — the resolved
        // `<CenterOfMass NodesCount ChildNodeN>` node list (42 nodes for
        // `mdl_skeleton`) — and only falls back to `Va.all` when that list is
        // empty (`L0()` L575: `this.Va.bca.length>0?this.Va.bca:this.Va.all`).
        // `VR` = `Σ weight` (`Dl.Esb` L576), `weight` = the node's `Mass`.
        // The old code averaged ALL bones unconditionally, i.e. it used
        // neither list.
        auto accum = [&](int idx, float& acc, float& wsum) {
            if (idx < 0 || static_cast<std::size_t>(idx) >= n) return;
            const float w = model_.bones[static_cast<std::size_t>(idx)].mass;
            if (w <= 0.0f) return;
            acc += pos_[static_cast<std::size_t>(idx) * 2 +
                        static_cast<std::size_t>(axis)] *
                   w;
            wsum += w;
        };
        float acc = 0.0f, wsum = 0.0f;
        if (!model_.com_children.empty()) {
            for (const int idx : model_.com_children) accum(idx, acc, wsum);
        } else {
            for (std::size_t i = 0; i < n; ++i) accum(static_cast<int>(i), acc, wsum);
        }
        if (wsum <= 0.0f) {
            return axis == 0 ? world_x_ : world_y_;
        }
        return acc / wsum;
    }
};

} // namespace sf2::scene
