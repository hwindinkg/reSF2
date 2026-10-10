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
    // [weapon drop] JS `xc.P2a` (L417709) -> the dropped weapon item's
    // `isActive=false`: stop drawing the named source part's triangles. The
    // bones stay (clip bone indices unchanged); only the mesh hides.
    void hide_model_part(const std::string& model_name) {
        model_.hide_part(model_name);
    }
    // [weapon drop, probe] Number of triangles still drawn.
    std::size_t active_tri_count() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < model_.resolved_tris.size(); ++i) {
            if (i >= model_.tri_active.size() || model_.tri_active[i]) ++n;
        }
        return n;
    }
    // [weapon drop, probe] True when the named part has any hidden triangle.
    bool has_hidden_part(const std::string& model_name) const {
        for (std::size_t i = 0; i < model_.tri_active.size() &&
                                i < model_.tri_part.size(); ++i) {
            const int pi = model_.tri_part[i];
            if (pi >= 0 && static_cast<std::size_t>(pi) < model_.part_names.size() &&
                model_.part_names[static_cast<std::size_t>(pi)] == model_name &&
                !model_.tri_active[i]) {
                return true;
            }
        }
        return false;
    }

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

    // The stance move the idle auto-play settles into, resolved from THIS
    // fighter's own unlocked list (`hb_`, JS `ra.Hza` L684-685) instead of a
    // hardcoded weapon name. `templates` = the candidate Template tags
    // (`StanceLeft`/`StanceRight` for the phase-1 intro, `StartIdleStance`
    // for the phase-2 loop, moves.xml). JS `Aua` (L673) keeps the
    // MAX-`<Priority>` group; within it the variant matching the controlled
    // side wins (`-Left` for the player, `-Right` otherwise — the
    // `Player Number=1` gate, `Dm.he` L755), and an unsuffixed variant
    // (`KnivesStartStanceIdle`) serves both. nullptr when `hb_` has none.
    const MoveDef* stance_move(const std::vector<std::string>& templates,
                               bool is_player) const;

    // JS `Gc` stage re-selection (`xF(3)` -> `Gj(..,1)`): at the EndStance
    // stage the model re-picks its move from the `EndStance`-template family by
    // evaluating each candidate's own `<Conditions>` (the `<RoundStage
    // Name="EndStance"/>` + `<RoundResult Name="Victory"/>` / `<Health Max="0"/>`
    // gates resolve the winner's `Win_Fists` and the KO'd loser's `Loss_fall`).
    // The port had no stage re-selection, so a fighter that died STANDING kept
    // its previous clip ("he kept standing"). Returns the max-`<Priority>`
    // passing candidate, or nullptr. `ctx.stage` must be EndStance and
    // `ctx.round_victory` set to this fighter's `is_winner`.
    const MoveDef* end_stance_move(sf2::scene::FightContext& ctx);

    // --- shop `TryOn` preview (JS `Pi.Ex` L2301; `iz.XBa("TryOn")=7` L444) --
    // The item's TryOn move from the loaded table: a move whose Template
    // carries "ShopTryOn" and whose locks pass for `shop_screen` + `worn`.
    // The `<Screen Name>` lock (`Lock::screen`) is evaluated against
    // `shop_screen` (the JS `Gm` — the fight path keeps failing it closed);
    // the modelled `<Item>` locks run through the same `Hm.he` test the fight
    // move list uses. A `<Perk>` (never, no screen) fails the move closed.
    // The max-`priority` passing group wins (JS `Aua` L673). `shop_screen` is
    // the list.xml type mapped by the caller (`shop_screen_for_type`:
    // Weapon->ShopWeapon, Armor->ShopArmor, Helm->ShopHelm, Ranged->
    // ShopMissile, Magic->ShopMagic, else ShopOther). nullptr when none.
    static const MoveDef* shop_tryon_move(
        const std::map<std::string, MoveDef>& all_moves,
        const std::vector<OwnedItem>& worn, const std::string& shop_screen);

    // Buffers one key press (JS `Kl.Sgb`/`zl.Sgb`, L798): appends the key to
    // the 2-slot Tap sequence (`zg.sh`), rebuilds the held set (`zg.Fh`),
    // resets the tap age (`dX=0`). `release` (JS `zl.Xgb`, L799) drops the
    // hold and records the release when the key was not tapped.
    void input(sf2::scene::key_type key, sf2::scene::press_type press);

    // Per-frame input aging (JS `zl.ia` L798): drop the Tap sequence at
    // `dX>=15`; at the 30-frame `Qe` cycle clear the holds/releases; rebuild
    // the held set from the currently-down keys (`yLa`).
    void age_keys();

    // JS `zl.reset()` (L798): the round-boundary input clear — empties the
    // buffered Tap/Hold/Release rows (`zg.sh`/`zg.Fh`/`zg.released`), the
    // physical held set (`Ff[].sl=!1`) and the tap/hold ages. Reached through
    // `wd.ctb(!0)` (`if(this.sN=a)this.Kl.reset(),this.Mka(0)`) from
    // `ca.Eaa(!0)` — called by `Rkb` (L410) at EVERY phase-2 round start, so a
    // key held (or buffered) in the previous round must NOT leak into the next.
    void reset_input();

    // JS `lg.vQ` keeps the animator's CURRENT animation after a clip ends
    // (`KNa` L548 leaves `Ua` set). The port's `current_move_` goes null at
    // clip end, so the last played move's animation names persist here — the
    // EndStance re-selection (`Gc` -> `Gj(..,1)`) reads them via
    // `<CurrentAnimation Name="PhysicalLying"/>` to pick the ground death
    // (`Loss_1`/`Loss_2`) for an already-ragdolled loser instead of `Loss_fall`.
    const std::vector<std::string>& last_anim_names() const {
        return last_anim_names_;
    }

    // Attempts move selection from `hb` (document order, matching the JS
    // `ra.Lk` order `Gc.EZa` L676 walks) with the buffered keys + current
    // state. This is the PLAYER's path — the JS `Gc.DK` `c == false`
    // else-branch (L673-674), NOT the tactic roulette.
    //
    // Hop-by-hop (sf2.502f0946.js):
    //   ca.N0a (L426)  `this.eu==2 && b.yJa(a)`      -- fight-phase key press
    //   wd.yJa (L501)  `... || !this.sN || this.Kl.Sgb(a)`  -- buffer the id
    //   zl.Sgb (L798)  push the tap, `rwa()` fires event 0
    //   wd.BHa (L507)  `this.mS.Z(this.Vb)`          -- model event, NO eb
    //   Gc.mS  (L672)  `this.Ih(2,a)`                -- `eb` stays FALSE
    //   Gc.Gnb (L672)  `Rwa` -> `EZa` -> `dxa`       -- build + dispatch
    //   Gc.EZa (L676)  candidates = `ru.iQ` (moves whose <Events> carry
    //                  <KeyPressed/>, i.e. Template `1key` -> `Controlled`)
    //                  that pass their own <Conditions> (incl. <Keys>)
    //   Gc.DK  (L673)  `c==false`: `c||!h.eb||h.Rha||d.push(h)` -> `d`
    //                  EMPTY (eb is false), so the `d.length>0` branch —
    //                  and therefore all of `Gc.Pkb` (the `M7.Wcb` mirror
    //                  filter, the `va.Ts` <Tactics><Conditions> filter and
    //                  the `Gc.jL` -> `de.jL` -> `Md.jL` WEIGHTED ROULETTE)
    //                  — is NOT reached.
    //   Gc.DK  (L674)  the else branch: `e = f[uf.sja(f.length)]` — the
    //                  `Aua` max-`priority` group of the non-`Rha` candidates
    //                  picked UNIFORMLY from `Math.random` (`uf.sja` L115 =
    //                  `floor(uf.OKa.RGa()*n)`, `uf.OKa.RGa() = Math.random`,
    //                  L114/L2471), then `Gc.Nsb` (L674) -> `wd.fJa` (L506)
    //                  -> `Ml.animation` -> `wd.Bnb` (L507) -> `wd.NS` (L505)
    //                  -> `Te.Skb` (L550).
    //   `g` (the `Rha` group) goes to `a.Ukb(g[sja].animation)` (L674) which
    //   only parks the name in `wd.P9` for `Mnb` (L507) to clear — no clip.
    //
    // The tactic `Md` (`parameters.Gc`) and the `mQ` feature state are NOT
    // consulted here: the JS only reaches them through `Pkb`/`de.ia`, and
    // `de.ia` is gated to AI control by `de.R0()` (L608:
    // `de.tY ? (this.Ca.Fj ? true : P.fP) : false`) and `wd.Anb` (L499:
    // `(this.parameters.Fj||P.fP) && this.Je==2`).
    //
    // `event` selects the JS event-type candidate set (`d.Su.dea(type)`, the
    // list `Gc.EZa` L676 walks):
    //   "" / "KeyPressed" — the press edge (`Gc.mS` L672 <- `wd.BHa` L507 <-
    //     `zl.rwa` L799). Runs ONLY on a fresh press edge: `zl.rwa` fires the
    //     `gh(0, zg)` event once per `zl.Sgb` (L798) `!a.sl` edge.
    //   "AnimationEnd" — the clip-end event (`Gc.kg` L671 <- `wd.eIa` L508 <-
    //     `Te.lS` L553). This is the HELD-move re-fire: `StepForward`'s
    //     `<Keys>Forward:Hold</Keys>` (moves.xml L10658-10662) stays true
    //     while the key is down (`zl.yLa` L799 rebuilds `zg.Fh` on every
    //     `zl.ia` L798), so the step restarts at every clip end — the
    //     continuous walk. On release the Hold drops and nothing passes.
    // Returns the started move's name, or "".
    // `iv_name`/`iv_type` carry the ended interval for `event ==
    // "IntervalEnd"` (JS `Om.compare` against the `fe` interval in the
    // type-13 event's `data`). Ignored for the other events.
    std::string try_select_move(sf2::scene::FightContext& ctx,
                                const std::string& event = std::string(),
                                const std::string& iv_name = std::string(),
                                int iv_type = 0);
    // Hit-reaction pick (JS `Gc.DK` L673-674 + `Gc.EZa` L676-677): the
    // fighter's moves carrying a `<Hit>` event (`Su.dea(6)`) whose
    // `<Hit>`-event name matches the attacker's hit name (`Nm.compare`,
    // L767) and whose `<Conditions>` pass (`f.Yz`, L677); `Gc.DK` then keeps
    // the max-`priority` group and picks uniformly (`f[uf.sja]`). The JS `DK`
    // tail branches on the picked animation's `MS` (the `Physics` attr,
    // L362442): `MS ? jJa : Nsb` (L674) — `last_react_physics()` reports
    // which branch this pick ran. `rng` is the injected `Math.random` analog
    // (never `Da.pg`). `Gc.Pkb` (its `M7.Wcb` mirror filter, its `va.Ts`
    // `<Tactics><Conditions>` filter and its `this.jL` -> `de.jL` -> `Md.jL`
    // weighted roulette) is CONFIRMED unreachable here: the reaction event is
    // dispatched `this.Bg.Ih(6,a)` (L201588), `Ih` leaves `eb` FALSE, so
    // `Gc.DK`'s `d` (`c||!h.eb||h.animation.Rha||d.push(h)`) is EMPTY and the
    // tail starts the `f[uf.sja]` pick directly (L674). No roulette is needed.
    // Returns the name or "".
    std::string try_react(sf2::scene::FightContext& ctx,
                          const std::function<float()>& rng = {});

    // Per-frame auto-move pick (JS `Gc.ia` L671 -> `Gnb` L672 -> `Rwa`/`EZa`
    // L676 -> `dxa`/`DK` L673-674) for the `<Events><EveryFrame/></Events>`
    // event (`Gc.nr` L672 `Ih(14)`). The JS fires one type-14 event per
    // fighter per frame (`wd.ia` L499 `this.nr.Z(this.Vb)`); `Gc.EZa` collects
    // the fighter's `EveryFrame` moves whose `<Conditions>` pass, `Gc.DK`
    // keeps the max-`priority` non-`Rha` group and picks uniformly
    // (`f[uf.sja(f.length)]`, L674), then starts it on the `MS` branch
    // (`e.animation.MS ? jJa : Nsb`). This is the knockdown recovery chain:
    // `PhysicalFall -> PhysicalGroundHit -> PhysicalLying -> Standup`, where
    // the final non-physics `Standup` (`GetUp|AfterPhysics`, FileName
    // standup.bytes) stops the ragdoll (`wd.Bnb` L507 `Nd.stop()`) and starts
    // the getup clip. Returns the started move's name or "". `wall_min`/
    // `wall_max` are the ragdoll solver bounds for a physics pick.
    std::string try_every_frame_move(sf2::scene::FightContext& ctx,
                                     float wall_min, float wall_max);

    // JS `wd.qs`/`wd.Ml` (L257400 `fJa`, L257798 `jJa`) — the reaction
    // QUEUES. `wd.ia` (L253545) drains them at the TOP of the frame:
    //   `Qnb() ? (this.Ml.clear(), this.GM=!1)
    //          : (this.Mnb(), this.Bnb() && (this.GM=!1))`
    // `Qnb` (`j$a()` = `qs.animation != null`) starts the queued GETUP/PHYSICS
    // reaction (`Mwb` -> `Lwb` -> `Nd.start` + `da.etb`); `Bnb`
    // (`KCa()` = `Ml.animation != null`) stops the ragdoll (`Nd.nk &&
    // Nd.stop()`) and starts the ordinary clip (`NS`). A pick from
    // `try_react`/`try_every_frame_move` is stashed here and STARTED on the
    // next frame — the JS order (`ia`'s Qnb/Bnb run before `Ax`'s every-frame
    // event). `qs` (physics) wins and clears `Ml`.
    void queue_reaction(const MoveDef* m, bool physics);
    std::string process_reaction_queues(sf2::scene::FightContext& ctx,
                                        float wall_min, float wall_max);
    bool reaction_pending() const {
        return qs_move_ != nullptr || ml_move_ != nullptr;
    }

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

    // Side-effect-free input-path condition test (JS `f.Yz(b,null,g)` L677,
    // the player path — `gm` left TRUE so the `<Keys>` Tap requirement
    // really matches the buffered keys). Sets `ctx.candidate_moves` to the
    // move's animation-name list, snapshots the buffered keys, and runs the
    // move's own `<Conditions>` tree. Used by `try_select_move` to collect
    // EVERY passing candidate before the `Aua` priority split;
    // `start_move_impl` re-runs the same test (with a trace) when the picked
    // move actually starts.
    bool move_conditions_pass(const MoveDef& move, FightContext& ctx,
                              std::string* trace = nullptr) const;

    // Hit-reaction candidate gate (JS `Gc.EZa` L676-677): `f.Yz(b,null,g)` —
    // the candidate reaction's own `<Conditions>` tree, tested BEFORE the
    // `Gc.DK` (L673-674) priority partition. The event pass runs with `gm`
    // false (`vm.he` L749 returns true for every Keys condition — only the
    // type-2 KeyPressed event clears `gm` in `EZa`), so `<CurrentAnimation>`,
    // `<ModExists>`, `<CurrentInterval>`, `<Hit>`, ... are what admit a
    // reaction. Without this, a higher-Priority reaction whose `<Conditions>`
    // are false (e.g. `ShroudFakeRecoil`, Priority 600,
    // `<CurrentAnimation Name="ShroudFakeStance"/>`) dominates the `Aua`
    // max-priority group and wins the pick. Sets `ctx.candidate_moves` to the
    // candidate's animation-name list (JS `Ek[d].xK = f.xl` L677) and
    // snapshots the fighter's live intervals (JS `Ek[d].xb = b.P0()`).
    bool react_conditions_pass(const MoveDef& move, FightContext& ctx) const;

    // Anim timescale (SlowModel `Kvb`/`KT`): apply sets scale (Speed>=1;
    // Speed<1 is a verbatim no-op), revert restores 1.0 (`v.dB` assumed).
    // NOTE: JS `KT` (char 271499) also writes the MODEL scale via `dba`
    // (char 271571) -> `NMa` (char 251149) -> `Ita`, but that write is gated
    // by `this.lb==null` and no shipped fighter model carries a scale attr, so
    // `HD()` (char 251101) is 1 for shipped data. The timescale channel and
    // the gravity model scale are therefore kept SEPARATE here (`model_hd_`).
    // [FIX slow-mo re-set — JS `ePa` `@285802`] The JS `ePa(a){this.Tx=a;
    // this.sG=1/this.Tx}` sets the timescale and keeps NO reset-on-set
    // accumulator (`mo` is the JS subframe counter, reset only by `Gka` at a
    // clip-frame boundary, never by a timescale change). Resetting
    // `scale_acc_` here discarded the fractional subframe remainder whenever
    // the SlowModel `KT` channel re-fired, so a per-frame re-set froze the
    // clip (`acc` pinned at the fraction, `steps=0`, no advance).
    void set_time_scale(float s) { time_scale_ = s; ++ts_set_calls_; }
    float time_scale() const { return time_scale_; }
    // [probe, authorised] `SF2_TIMESCALE_PROBE=1`: the fractional subframe
    // accumulator (`scale_acc_`) and the lifetime count of `set_time_scale`
    // calls. The JS `ePa` (`@285802`) sets `Tx` with NO accumulator reset;
    // this exposes whether the port's reset is hit every frame.
    float scale_acc() const { return scale_acc_; }
    int ts_set_calls() const { return ts_set_calls_; }
    // JS `this.oa.model.HD()` (`Ita`), used by `Al.O9a` (char 296359):
    // `xd.fDa/(HD()*HD())`. 1.0 for every shipped fighter (no model Scale
    // attr; the JS `KT`->`NMa` writer does not fire on shipped data), so the
    // divisor is inert: `kGravitation/1^2 == 0.4`.
    float model_hd() const { return model_hd_; }
    // JS `wd.y5(a)`/`Dfa()` (L251005/L251095): `wd.xpa` — the ACT-ALLOWED
    // flag. `ca.uhb` calls `a.model.y5(!1)` on the finishing blow (Dfa() then
    // false, so `uhb` cannot re-arm), and the `cu` timer restores it after
    // `v.iNa`/`v.jNa` seconds. It is NOT an animation freeze: the clip keeps
    // advancing (which is how the type-4 interval end — `rgb` — still fires).
    void set_disabled(bool d) { action_disabled_ = d; }
    bool disabled() const { return action_disabled_; }
    // JS `de.ia` (L248219): `let a=1/v.on(); ... P7[b].ia(a)` — the GLOBAL
    // timescale `v.on()` divides the clip advance rate. 1.0 when no slow-mo;
    // `set_slowmo` sets it to `1/v.kNa` (the finishing-blow slow-mo).
    void set_anim_rate(float r) { anim_rate_ = r; }
    float anim_rate() const { return anim_rate_; }
    // Advances the current clip one frame (60 Hz). Updates active intervals
    // (Start/End), transitions to idle when the clip ends (JS `Te.ia`
    // L547-548: `Xh+2 >= len` -> stop). Samples the pose at move_frame.
    void advance(float dt);
    void advance_step();  // one fixed sub-step (timescale loop calls this)

    // --- display preview playback (`Pi.Ex` L2301 -> `wd.ia` -> `Te.ia`) -----
    // Seats a DISPLAY-ONLY clip so the caller drives it through the SAME
    // `advance()` (the `Te.ia` subframe pacing) the fight uses. The shop
    // `TryOn` (`Oa.Fhb` L2300 -> `Ex(a,7)`) and the profile move preview
    // (`vb.DK` -> `Pi.kg` -> `wd.fJa`) both play on a `Pi` model whose
    // animation is `Pi.ia` -> `wd.ia` -> `Te.ia`; a raw `++frame` per 60 Hz
    // tick was `(MidFrames+1)` = 3x TOO FAST (same bug the fight's Phase 4a
    // fixed). Seats the `Te.Skb` counters (`playhead_=0`, `subframe_=0`,
    // `sub_=(MidFrames+1)`) and samples the first pose; the caller then calls
    // `advance()` once per fixed step and reads `move_frame()` (JS `Te.M0()`).
    // `preview_active()` goes false when the clip ends (`Te.KNa` clears the
    // move), the `Ad.kg` animation-end.
    // `node_x`/`node_y` = the `Pi` model node placement (JS `Pi.J9 = (0,-93)`,
    // L439, applied via `wd.oL` L577 at `Pi.job`). The JS `Pi` pins the
    // fighter's render anchor (NPivot) at J9 ONCE (`oL` shifts every `ma`), and
    // every later `eda` lets the clip ride from there; the port's `sample`
    // reproduces that as `render_offset = node - clip[anchor]` captured on the
    // first sample (`render_offset_valid_ == false`). Without this the preview
    // was re-anchored to the clip pivot (the armor/helm try-on sank and the
    // profile idle legs floated).
    void start_preview_clip(const MoveDef& move, const sf2::data::anim_clip& clip,
                            float node_x = 0.0f, float node_y = -93.0f);
    bool preview_active() const { return current_clip_ != nullptr; }

    // --- move-frame action dispatch (JS `Te.Lwa` L563-564) -----------------
    // JS `xc.voice` (the fighter XML `<Voice>` attr; `Vo` ctor default ""
    // L807, filled by `ur` L186 from the Warrior's Voice). The
    // `<Sound Voice=..>`/`<RandomSound Voice=..>` gate reads it
    // (`wd.dwb`/`wd.fwb` L519 -> `fm.fka` L735 `t7 ? true : voice == J8`).
    // An EMPTY voice therefore silences every Voice-gated action (JS-exact:
    // `"" == "Male"` is false).
    void set_voice(const std::string& v) { voice_ = v; }
    const std::string& voice() const { return voice_; }

    // JS `ur` L195 `QD = NotAnimation==null` (`st.fighter.parameters.QD`): a
    // NotAnimation fighter has NO animation attach — `ia` (L499) gates the
    // WHOLE clip advance on `this.parameters.QD && (this.da.ia(), ...)`, so a
    // NotAnimation dummy NEVER advances a clip (the Punchbag holds its bind
    // pose / solver pose); only `this.Nd.ia()` (the `Al` cloth solver) runs.
    void set_not_animation(bool v) { not_animation_ = v; }
    bool not_animation() const { return not_animation_; }

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
    // JS `wd.ip()` (L264899) = `Nd.nk ? Nd.frameCount : (Sj()!=null ?
    // da.ip() : -1)`, and `da.ip()` (L279031) = `Ua.MS ? fG : M0()`. `M0()`
    // is the clip frame (`move_frame_`); `fG` is the physics-frame counter
    // (incremented alongside `Xh` in `Te.ia`'s normal branch, so it is the
    // `Xh` domain — the port's `playhead_`). Powers the `CurrentAnimation`
    // Min/Max frame bound (`np.isEqual` L671284 `this.xE(b.ip())`).
    int anim_ip() const {
        if (ragdoll_active()) return ragdoll_frame_count_;
        if (current_move_ == nullptr) return -1;
        return current_move_->physics ? playhead_ : move_frame_;
    }
    // JS `jc.c7a` (L691): an interval is active iff
    //   `(g.start>=qx?g.start:qx) <= frame <= (g.finish<=Lj?g.finish:Lj)`.
    // The end term is `h`, i.e. `finish` CLAMPED DOWN to `Lj`. `fe.init`
    // (L773) makes a no-`End` interval's `finish = pva+2` and `OWa` (L694)
    // sets `pva = Lj`, so its raw finish is always `Lj+2` — unclamped the
    // interval stays live two frames past the move's end frame.
    int interval_last(const Interval& iv) const;
    // JS `Te.rw` (L560): the per-clip mirror flag. `Te.xqb` (L553) swaps
    // every AttackingParts edge name ending `_1`/`_2` while it is set.
    bool mirror_swap() const { return mirror_swap_; }
    // The LOGICAL playback counter (JS `Te.Xh`): advanced once per
    // `(MidFrames+1)` sub-steps (`Te.ia`'s normal branch `Xh++`). NOT the
    // quantity `kJ()` returns.
    int playhead() const { return playhead_; }
    // [FIX pose-dump `cf`] The JS `Te.Xh` — the value the oracle trace dumps
    // as `cf` (`trace.js` L179 `da.Xh`), NOT `Te.M0()` (`move_frame_`). After
    // the JS `eda` order fix (`sample` then `mo++`) the port's `playhead_` is
    // the play-buffer span index `S` (`sample()`'s `play = playhead_`) and
    // `subframe_` is the post-increment `mo` (1,2 then the wrap 0). `Te.Xh`
    // increments ONCE per span on the span's FIRST frame (the `Gka` branch
    // `this.Xh++`), so `Xh == S+1` on every frame of the span: `S+1` while
    // `mo != 0`, and the already-bumped `playhead_ == S+1` on the wrap frame
    // (`mo == 0`). Dumping this (instead of `move_frame_ = M0`) aligns the
    // native `cf` with the oracle's `Xh` sequence.
    int playhead_xh() const { return playhead_ + (subframe_ != 0 ? 1 : 0); }
    // JS `Te.kJ` (L279097, @278950): `kJ(){return this.Pe?this.lq:0}` — `lq`
    // is the PER-SUB-STEP counter, incremented on EVERY `Te.ia` tick while
    // playing (buffer branch `lq++`; normal branch `lq++,fG++,Xh++`). The
    // port's `advance_step()` runs once per `Te.ia` tick, so the number of
    // sub-steps since the move start is `playhead_*sub_ + subframe_` (the
    // `subframe_` wrap carries one `playhead_`). `Fl`/`q7` are built from
    // THIS (sub-frame) domain, not from `Xh`.
    int played_steps() const { return playhead_ * sub_ + subframe_; }
    // Per-clip-start serial: JS `Te.Skb` -> `x3` -> `Fu.hob()` clears the
    // `Cl` one-shot at EVERY move start, including a repeat of the same
    // move (whose pointer is unchanged).
    int move_start_count() const { return move_start_count_; }
    // Incremented when a move's clip ends (JS `Te.lS` -> the `EStopAnimation
    // Event`). The EndStance gate uses the edge to know the KO/lying chain (or
    // the `Loss_fall`/`Win_Fists` clip) has finished.
    int move_end_count() const { return move_end_count_; }
    // JS `Te.M2` — the anim controller move-frame counter. `Te.ia`
    // (L547-548) opens with `this.M2++` and later in the SAME call does
    // `this.Xh++`, so the two counters advance in LOCKSTEP: `M2 == Xh - 4`
    // (`Xqb` L282808 seats `M2 = -4`, `Skb` L280606 seats `Xh = 0`). `Xh` is
    // this port's `playhead_`. `Tba` (L595) = the max `M2` over the enemy's
    // spawned CHILD models (`a.vd`, filled by `zWa`/`fya`); a single-part
    // fighter's `vd` is empty so `Tba` is 0 (computed in fight.cpp, not from
    // this body's `M2`). NOTE the shipped
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
    // [FIX finishing-blow slow-mo] The FRACTIONAL subframe position in
    // [0, 1): the sub-frame progress toward the next integer subframe
    // (`sub_frac_`). 0 at a JS-exact integer sample; during the slow-mo
    // (`anim_rate_ < 1`) it carries the fractional part the old integer-only
    // `steps` accumulator threw away. `anim_time()` is the resulting
    // continuous clip position in clip frames (the probe ramp).
    float subframe_frac() const { return sub_frac_; }
    float anim_time() const;
    int facing() const { return facing_; }
    // JS `Te.FX` / `hd()` (L547): the CLIP MIRROR (±1). Distinct from the
    // `b6a` facing lock above; the impulse mirror (`wd.Kwb` L509) uses THIS.
    int clip_mirror() const { return clip_mirror_; }
    const std::vector<const MoveDef*>& hb() const { return hb_; }
    // The fighter's LIVE perk set (JS `parameters.Oa`, built by `Wk`/`Pma`):
    // the names `Bm.he` (L753-754) scans when a move carries a `<Perk
    // Name=..>` lock. Fed from the save's learned perks (`Bt.KS.Oa`) +
    // equipped-item perks. `build_move_list_locks` admits a perk-locked move
    // only while its perk is present here (the `DoubleSweep` gate).
    void set_perks(std::vector<std::string> perks) { perks_ = std::move(perks); }
    const std::vector<std::string>& perks() const { return perks_; }
    // The last player move decision (the JS `Gc.DK` `c == false` branch,
    // L673-674) — the record the `--verify-input` probes assert: the
    // candidate set with each candidate's `priority`, the `Aua` max-priority
    // non-`Rha` group (`f`), the `g` (`Rha`) `Ukb` name, the `uf.sja` draw,
    // and the move that actually started. `valid` is false until a decision
    // runs (and resets on every `try_select_move` call).
    struct MoveDecision {
        bool valid = false;
        // `hb_`-order passing candidates: move name + `priority`.
        std::vector<std::pair<std::string, int>> cands;
        // The `Aua` non-`Rha` max-`priority` group (`f`), in order.
        std::vector<std::string> f_group;
        // `a.Ukb(g[sja].animation)` (L674) — the `Rha` group pick, if any.
        std::string ukb;
        bool ukb_set = false;
        // `uf.sja(f.length)`: whether `uf.OKa.RGa()` was drawn, its value,
        // and the resulting index. No draw when `|f| <= 1` — `floor(r*1)`
        // is 0 for every `r`, so the pick is value-exact without one.
        bool drew = false;
        float draw = 0.0f;
        int index = -1;
        std::string picked;  // the move the decision actually started
    };
    const MoveDecision& last_decision() const { return decision_; }
    // Whether the move `try_react` most recently STARTED carries `MS` (the
    // `Physics` knockdown attr, L362442). `Gc.DK`'s tail (L674) branches
    // `e.animation.MS ? jJa(-> ragdoll) : Nsb(ordinary clip)`; `apply_hit`
    // reads this to start the ragdoll ONLY on the `jJa` branch.
    bool last_react_physics() const { return react_physics_; }
    // Test/trace accessors (no behavior change): live input-buffer counts.
    int buffered_tap_count() const;
    int buffered_hold_count() const;
    const std::set<std::string>& active_intervals() const { return active_intervals_; }
    // The ended move's still-active intervals, snapshotted at clip end.
    // JS `KNa()` (L548) does NOT clear `Ua`/`Te.xj`, so the `AnimationEnd`
    // pass sees `Ae.xb = b.P0()` = the last played frame's intervals.
    const std::vector<std::pair<std::string, int>>& ended_intervals() const {
        return ended_intervals_;
    }
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
    // [cross-fighter align — JS `Te.BBa` L563 + `Gub` L557-559] The
    // opponent-controller link (JS `Te.cQ`). `<Align><Position Player="Enemy"
    // Object="Animation"/>` — the throw victim's `?V` move — reads the OTHER
    // controller's `Fk`: `e = opponent.Fk`, `d = 0`, so `self.Fk =
    // opponent.Fk` and `Gla(Fk.x, eja, Fk.z)` shifts the victim's whole clip
    // onto the thrower's clip-space origin. Set by the fight each round.
    void set_opponent(Fighter* o) { opponent_ = o; }
    const Fighter* opponent() const { return opponent_; }
    // [probe] Throw diagnostics (--verify-place): dump the align/Fk/root state.
    void debug_throw_probe(const char* tag) const;
    void set_world_pos(float x, float y) {
        world_x_ = x;
        world_y_ = y;
    }
    // Test hook: move the anchor so it STICKS. `set_world_pos` alone is undone
    // by the next `sample()`: with an active move the anchor is rebuilt as
    // `world_x_ = px[anchor] + render_offset_ + j8_x_` (fighter.cpp:1663), so
    // the per-move constant must absorb the delta (and the `<Align>`
    // continuity target `prev_align_pivot_world_*` shifts with it).
    void teleport(float x, float y) {
        const float dx = x - world_x_;
        const float dy = y - world_y_;
        render_offset_ += dx;
        render_offset_y_ += dy;
        render_offset_valid_ = true;
        j8_x_ = 0.0f;
        prev_align_pivot_world_x_ += dx;
        prev_align_pivot_world_y_ += dy;
        world_x_ = x;
        world_y_ = y;
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
    // JS `ee.nt` case 6 (`Pg.Eu` = the `_CenterOfMass_` node, L401300): its
    // `ma`/`mf` — the CURRENT and PREVIOUS solver-frame world position.
    // `prev_pos_` is snapshotted at the top of `advance()` (the JS node `mf`
    // carries the previous frame's `ma`). `Distance ... Frame="Previous"`
    // refs read these (`Standup`/`PhysicalLying` getup gates).
    float com_prev_x() const { return com_axis_in(prev_pos_, 0); }
    float com_prev_y() const { return com_axis_in(prev_pos_, 1); }
    // JS `ee.nt` case 1 (`MQ(a).ma/mf`, L786): the posed world (x,y) of a
    // named node, current (`pos_`) or previous frame (`prev_pos_`).
    bool node_world_xy(const std::string& name, bool prev, float& x,
                       float& y) const {
        const int i = model_.bone_by_name(name);
        const std::vector<float>& p = prev ? prev_pos_ : pos_;
        if (i < 0 || p.size() < static_cast<std::size_t>(i) * 2 + 2) return false;
        x = p[static_cast<std::size_t>(i) * 2];
        y = p[static_cast<std::size_t>(i) * 2 + 1];
        return true;
    }
    // Clamps the fighter's world x to [min_x, max_x] (the arena walls).
    // Called each frame by the fight controller after the root-motion walk.
    void clamp_x(float min_x, float max_x) {
        if (world_x_ < min_x) world_x_ = min_x;
        if (world_x_ > max_x) world_x_ = max_x;
    }
    // JS `Te.yu`/`Te.zu` (set by `zLa(a,b,..)` L279707, reached from
    // `pMa`/`qMa` L212992: `qMa(v.tFa, v.NKa, ..)` where `v.tFa = location.NU`
    // = the `<Root Wall>` and `v.NKa = location.width - location.NU`). They are
    // the ARENA WALLS, distinct from the ragdoll `Al.NO/MO` clamp. Consumed by
    // `Te.Gub`'s `EObjectWall` align (L558/559) and `Te.Iub`'s repulsion.
    void set_arena_walls(float min_x, float max_x) {
        arena_wall_min_ = min_x;
        arena_wall_max_ = max_x;
    }
    // JS `Te.yu`/`Te.zu` getters (read by `de.oxb` L618 `b.da.yu`/`b.da.zu`).
    float arena_wall_min() const { return arena_wall_min_; }
    float arena_wall_max() const { return arena_wall_max_; }
    // [probe, authorised] Max x-extent of the rendered pose (`pos_`) — the
    // "stretched across the arena" metric for `--wall-probe`.
    float debug_bone_span_x() const {
        if (pos_.size() < 2) return 0.0f;
        float lo = pos_[0], hi = pos_[0];
        for (std::size_t i = 1; i * 2 < pos_.size(); ++i) {
            const float v = pos_[i * 2];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        return hi - lo;
    }
    // JS `sI` (L491/L498/L511): the number of landed hits this fighter has
    // TAKEN (`Bb.ep = (sI==0)` then `sI++`). `ca.Cgb` L396 gates the
    // Punchbag's forced reaction on `a.model.sI == v.Qxa`
    // (`<CounterPunches>` = 50, internal_settings.xml; JS L1157).
    void note_hit_taken() { ++hits_taken_; }
    int hits_taken() const { return hits_taken_; }
    int hits_taken_ = 0;  // JS `sI`
    // Clip lookup callback — the demo supplies the archive.
    void set_clip_lookup(const std::function<const sf2::data::anim_clip*(const std::string&)>& fn) {
        clip_lookup_ = fn;
    }
    // JS `uf.OKa.RGa()` = `Math.random()` (`at.Nlb` L114; `uf.OKa=new at`
    // L2471) — the UNSHARED stream `uf.sja` (L115:
    // `Math.floor(uf.OKa.RGa()*(a-0))+0`) draws from for `Gc.DK`'s
    // `e = f[uf.sja(f.length)]` (L674). Installed by the fight controller
    // with the pinned `FightController::math_random01()` (never `Da.pg`).
    // Unset -> index 0, which is VALUE-EXACT for a single-element `Aua`
    // group (`floor(r*1) == 0`) — the case at every probe frame.
    void set_math_random(std::function<float()> fn) { math_random_ = fn; }

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
    // [FIX finishing-blow slow-mo] `sub_frac` is the fractional sub-frame
    // position in [0,1) (JS: the animated lists get a fractional dt
    // `1/60 * 1/v.on()`; `Qi.ia` L248219 / `WD.WL` L427772). It shifts the
    // Bezier parameter continuously between the JS integer samples so the
    // slow-mo pose ramps instead of stepping.
    void sample(const sf2::data::anim_clip& clip, int frame, float x, float y,
                int mirror_sign, bool interp = false, int first_frame = 0,
                int playhead = 0, float sub_frac = 0.0f);

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
    // [FIX phase-1 f=0 = bind] Overwrite the DRAWN pose (`pos_`) without
    // touching the solver state (`sol_ma_`/`sol_mf_`). `FNa` (JS L409) shows
    // the held bind pose at phase-1 f=0 while the first `wd.ia` step (f=1)
    // seeds the solver; the port re-samples on the transition frame, so the
    // drawn pose is restored to the held one while the solver keeps its step.
    void set_drawn_positions(std::vector<float> p) {
        pos_ = std::move(p);
        pose_sampled_ = true;
    }

    // JS `Te.Ic(name, facing)` (L549) consumed by `de.Wea` (L600): the world
    // x of the named skeleton node in MY current posed frame (the node `ma.x`
    // analog, i.e. `positions()`). The node's `NE` neighbour is the trailing
    // `1`<->`2` name flip (`Ou.Grb` L702, the same partner rule as `Peb`);
    // when it resolves, `facing` (±1 = the `de.t0` pivot-x order, L618)
    // selects the left/right node (`NHeel_1`/`NHeel_2`). Returns `kNoBoneX`
    // (`Wea`'s `3.4028234663852886E38`) exactly when `Ic` returns null.
    float bone_world_x(const std::string& name, int facing) const;
    static constexpr float kNoBoneX = 3.4028234663852886E38f;

    // JS `oa.V_a()` (L517): releases the model's `Weak="1"` figures -
    // `let a=0,b=this.oa.Va.all;for(;a<b.length;){let c=b[a];++a;c.UEa&&c.kla(!1)}`
    // i.e. every node carrying `Vc.UEa` gets `kla(false)` -> `MG=false`
    // (`kla(a){this.NG=(this.MG=a)||!this.nh}`, L795). The only shipped Weak
    // node is `mdl_skeleton_punching_bag` Node12 (`Weak="1" Fixed="1"`); the
    // Punchbag's forced reaction calls it (JS `ca.Cgb` L396 tail, fight.cpp).
    void release_weak() {
        for (Bone& b : model_.bones) {
            if (b.weak) b.fixed = false;
        }
    }

    // JS `Al.oa.vc` (the model's shock latch; `ca.Cgb` L394 `a.model.vc=!0`,
    // mirrored by `FightFighter::shock.shocked_vc`). Read by the `Al.sk`/`jE`
    // participation gate (`!NG && (nk || jy || oa.vc && c.vc)`): a non-cloth
    // node with `Shock="1"` integrates/relaxes while the model is shocked.
    void set_shock_latch(bool v) { shock_latch_ = v; }
    bool shock_latch() const { return shock_latch_; }

    // --- JS `Al` (L582) — the ragdoll solver latch ------------------------
    // `Al` ctor (L582): `frameCount=0; names=[]; nk=false`. `Al.start(a)`
    // (L582): `nk=true; frameCount=0; names=[]; a!=null&&addRange(names,a);
    // this.oa.BKa()`. `Al.stop()` (L582) clears `nk`. `Al.ia()` (L582) runs
    // `sk(); jE(); nk&&frameCount++`. While `nk` is set, `Al.sk`/`Al.jE`
    // (L583) integrate EVERY non-immovable node
    // (`!NG && (nk || jy || oa.vc && c.vc)`), and a node's `ma` is WORLD
    // space and is NOT overwritten by the per-frame clip apply — that is
    // what makes a hit reaction persist (no snap-back).
    //
    // `wall_min`/`wall_max`/`floor_y` are the arena bounds `Al.fha` (L582)
    // clamps every body node to (x in [wall, width-wall], y >= 0 in JS).
    void ragdoll_start(const std::string& reaction, float wall_min,
                       float wall_max, float floor_y);
    void ragdoll_stop();
    bool ragdoll_active() const { return nk_; }
    int ragdoll_frame_count() const { return ragdoll_frame_count_; }
    const std::vector<std::string>& ragdoll_names() const { return ragdoll_names_; }

    // JS `Bl.strike` (L587-588): `a.sx.XA(l)` / `a.Zs.XA(c)` add the
    // impulse-split displacement to the endpoint nodes' WORLD `ma`.
    // Persistent across frames while the ragdoll is active.
    void strike_node(int bone, const sf2::scene::Vec3& v);

    // JS `wd.Wqb` (L268496) — the shock-impulse weapon fling. For EVERY body
    // node carrying `Shock="1"` (`Vc.vc`, the weapon-attachment nodes
    // `Weapon-Node{1..4}_{1,2}` in mdl_skeleton.xml), the JS adds
    //   `e.ma.x += v.Ub.kw/e.weight; e.ma.y += v.Ub.gR/e.weight;
    //    e.ma.z += v.Ub.hR/e.weight`
    // to the node's WORLD position (`sol_ma_` = JS `Vc.ma`). The impulse
    // becomes Verlet velocity on the next `Al.sk` step and the weapon mesh
    // (skinned to those nodes) leaves the hand. `weight` = `Bone::mass`.
    // The clip apply must NOT re-pose these nodes while the shock latch is
    // set (`Al.eda` L282908 gate `!(model.vc && e.vc)`), so the fling
    // persists — see `sample`.
    void weapon_fling(float ix, float iy, float iz);

    // [probe, authorised] The struck endpoint node's solver `ma` component
    // (JS `Vc.ma` = the node `ma` `Bl.strike` writes).
    float solver_ma_x(int bone) const;
    float solver_ma_y(int bone) const;
    // [probe, authorised] Arm the NEXT `sample()` to report the DRAWN pose
    // delta (per-node + capsule bbox) since this call — the per-hit motion of
    // the render pose (`pos_`). One `[bagmove]` line on that sample.
    void arm_strike_move_probe();

    // World-space bbox of every <Edges> capsule endpoint, inflated by the
    // capsule radius (the meshless Punchbag has no triangles; the capsule
    // list still measures it). Returns the number of resolved edges.
    int capsule_bbox(float& min_x, float& min_y, float& max_x,
                     float& max_y) const;

    // --- JS `ju` (g="D3" L545) — `wd.Ja`, the ability cooldown/reload state --
    // Slots (`sa.$h` L706): Punch 9, Kick 10, Ranged 11, Magic 12,
    // RaidCharge 13, Super 14. Only 9/10/11/14 carry timers (`wKa`/`b5`/`MOa`
    // L523-533). Field names keep the JS minified names for the cite.
    struct AbilityCooldown {
        float super_reload_ = 500.0f;  // `UNa`
        float super_target_ = 1.0f;    // `pU`
        float kick_reload_ = 0.0f;     // `hFa`
        float super_elapsed_ = 0.0f;   // `iu`
        float kick_target_ = 1.0f;     // `DR`
        float punch_reload_ = 0.0f;    // `rlb`
        float kick_elapsed_ = 0.0f;    // `eA`
        float punch_target_ = 1.0f;    // `aT`
        float ranged_reload_ = 0.0f;   // `wGa`
        float punch_elapsed_ = 0.0f;   // `JA`
        float ranged_target_ = 1.0f;   // `TR`
        float punch_duration_ = 0.0f;  // `teb`
        float ranged_elapsed_ = 0.0f;  // `mA`
        bool super_active_ = true;     // `oU`
        bool ranged_active_ = false;   // `SR`
        bool punch_active_ = false;    // `m4`
        bool kick_active_ = false;     // `i2`
    };
    AbilityCooldown ability_cooldowns_;  // JS `wd.Ja` (`new ju`, L491)
    // JS `wd.wKa(a)` (L523): reset the slot's cooldown (inactive + elapsed 0)
    // and emit the ability-animation event `yd(slot, 0, 0)` onto `this.yp`
    // (the port has no ability-animation bus — it logs).
    void ability_cooldown_reset(int slot);
    // JS `wd.b5(a, b)` (L524): arm the slot's cooldown for `b` (`b <= 0 -> 1`).
    void ability_cooldown_start(int slot, float duration);
    // JS `wd.MOa()` (L532-533), once per fight frame: advance each live
    // cooldown and emit `yd(slot, elapsed, 1)`. `game_speed` is `v.on()`
    // (the JS global time-scale; the port passes the fight frame speed).
    void tick_ability_cooldowns(float game_speed);
    // The cooldown terms of the JS `wd.yJa(a)` availability gate (L501):
    //   `a==11 && SR && mA<TR` / `a==10 && i2 && eA<DR` /
    //   `a==9 && m4 && JA<aT` / `a==14 && iu<pU`.
    // True while the named slot's cooldown is still running. `yJa`'s other
    // terms (`bh`/`$aa` magic gate, `sN`, `Kl.Sgb`) have no port consumer.
    bool ability_cooldown_running(int slot) const;

    // JS `Bl.s2a()` (L588) — the pre-strike midpoint smoothing: for every
    // solver body, `mf = (mf + ma) * 0.5` (per x,y,z). `Bl.strike` calls it
    // before splitting the impulse onto the hit bodies.
    void strike_midpoint_smooth();

    // Fills `out` with the triangle vertex list (screen-space x,y pairs, z
    // dropped). Returns the vertex count (3 * triangle count).
    std::size_t build_vertices(std::vector<float>& out) const;

    // World-space bounding box of the triangle-referenced bones.
    void triangle_bbox(float& min_x, float& min_y, float& max_x,
                       float& max_y) const;

private:
    Model model_;
    std::vector<float> pos_;  // per-bone [x, y] after sampling (world space)
    // The previous frame's `pos_` (JS node `mf`). Snapshotted at the top of
    // `advance()`; the `Distance ... Frame="Previous"` refs read it.
    std::vector<float> prev_pos_;
    float color_r_ = 1.0f;
    float color_g_ = 1.0f;
    float color_b_ = 1.0f;

    // --- move execution state (Phase 3.2b) --------------------------------
    std::vector<const MoveDef*> hb_;        // move list (document order, JS `ra.Lk`)
    // Live perk names for the `<Perk Name=..>` lock test (JS `Bm.he` L753).
    std::vector<std::string> perks_;
    MoveDecision decision_;                 // last player decision (probe/trace)
    // `MS` of the move `try_react` last started (see `last_react_physics`).
    bool react_physics_ = false;
    // `uf.sja`'s `Math.random` mirror (`set_math_random`); unset -> no draw is
    // needed because the `Aua` group is a singleton (value-free).
    std::function<float()> math_random_;
    const MoveDef* current_move_ = nullptr; // playing move (JS `da.Ua`)
    const sf2::data::anim_clip* current_clip_ = nullptr; // clip for `current_move_`
    int move_frame_ = 0;                    // clip frame (JS `Te.M0()`) for intervals/cf
    // JS `jc.Lj` (`fe.init`'s `pva`): the move's `EndFrame`, or the LOADED
    // clip's frame count when `EndFrame` is absent. Resolved in
    // `start_move_impl` right after the clip lookup. Finishes every interval
    // whose `<Interval>` carried no `End` (`Interval::end_default`), so a
    // `StartIdleStance`-family move with no `EndFrame` keeps its `Stance`
    // template's `Throwable` interval live for the whole clip instead of the
    // dead `0..2` window (which is why the throw gate never resolved).
    int move_end_frame_ = 0;
    // Incremented in `start_move_impl` (JS `Te.Skb` L551 -> `x3` -> `hob`).
    int move_start_count_ = 0;
    // Incremented when a clip ends (`ended_move_` set in `advance_step`).
    int move_end_count_ = 0;
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
    // [FIX finishing-blow slow-mo] Fractional sub-frame progress in [0,1)
    // toward the next integer subframe (see `subframe_frac()`/`anim_time()`).
    // 0 at every JS-exact integer sample; carries the slow-mo remainder.
    float sub_frac_ = 0.0f;
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
    // [perf] Solver index caches (built once per model). The JS `Al.jE`/`Qja`
    // hold DIRECT node references, so its per-frame solve never resolves a
    // name. The old port called `Model::bone_by_name`
    // (`std::unordered_map<std::string,int>`) twice per edge per iteration and
    // `macro_children.find(name)` per macro per frame — hundreds of string
    // hashes EVERY frame during a ragdoll, plus a fresh `visiting` vector and
    // a `std::function` heap allocation. These caches remove all of it while
    // keeping the solve O(bones·iterations) and value-identical.
    // `edge_bone_idx_[i]` = the resolved (i1,i2) of `model_.edges[i]`.
    std::vector<std::pair<int, int>> edge_bone_idx_;
    // Per-bone macro children, indexed directly by bone index (empty unless
    // the bone is a MacroNode).
    struct MacroChildIdx {
        bool present = false;
        std::vector<int> child;
        std::vector<float> weight;
    };
    std::vector<MacroChildIdx> macro_child_idx_;
    // Reused recursion scratch for the macro re-derivation (was a fresh
    // `std::vector<std::uint8_t> visiting(n,0)` allocated every frame).
    std::vector<std::uint8_t> macro_visiting_;
    // Build `edge_bone_idx_`/`macro_child_idx_` for the current model (no-op
    // when already current).
    void ensure_solver_caches();
    // [probe, authorised] per-hit drawn-pose evidence (arm_strike_move_probe).
    bool strike_probe_pending_ = false;
    std::vector<float> strike_probe_pose_;
    // JS `Al.oa.vc`: the model's shock latch (see `set_shock_latch`). Starts
    // false; the fight sets it when a shock lands (`ca.Cgb` L394).
    bool shock_latch_ = false;
    // JS `wd.qs.animation` (physics/getup queue, `jJa` L257798) and
    // `wd.Ml.animation` (ordinary-clip queue, `fJa` L257400). Drained by
    // `process_reaction_queues` at the top of the next frame (`wd.ia`
    // L253545 `Qnb`/`Bnb`).
    const MoveDef* qs_move_ = nullptr;
    const MoveDef* ml_move_ = nullptr;
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
    // True while a display-only preview clip is seated (`start_preview_clip`):
    // the first sample then captures the y render offset from the `Pi` node
    // placement too. Fight moves keep the y anchor rule of `start_move_impl`.
    bool preview_mode_ = false;
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
    // what `facing()` reports. NOTE: the pose dump's `fx` is NOT this — it is
    // the `Te.FX` clip mirror (`clip_mirror()` / JS `da.hd()`), see
    // fight.cpp's `dump_pose_frame`.
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
    float model_hd_ = 1.0f;    // JS `KT`->`NMa` model scale (`HD()`, `Al.O9a`)
    float scale_acc_ = 0.0f;   // timescale fractional accumulator
    int ts_set_calls_ = 0;     // [probe] lifetime `set_time_scale` calls
    // JS `wd.xpa` (`y5`): false while the finishing-blow disable holds.
    bool action_disabled_ = false;
    // JS `1/v.on()` (the global timescale divisor `de.ia` feeds the animator).
    float anim_rate_ = 1.0f;
    // --- JS `Al` solver latch (see `ragdoll_start`) -----------------------
    // `parameters.QD == false` (warrior `NotAnimation="1"`). Set by
    // `make_fighter`; gates the clip advance in `advance_step` (JS `ia`
    // L499) so the NotAnimation dummy never plays a reaction/desync clip.
    bool not_animation_ = false;
    bool nk_ = false;                         // JS `Al.nk` (ragdoll active)
    int ragdoll_frame_count_ = 0;             // JS `Al.frameCount`
    std::vector<std::string> ragdoll_names_;  // JS `Al.names`
    // [ragdoll recovery probe] World `pos_` snapshot taken in
    // `ragdoll_stop()`; the next `sample()` logs the per-bone delta between
    // the released solver pose and the resumed clip pose (`[ragdoll] RECOVER`).
    // `ragdoll_stop_logs_` caps the probe output.
    std::vector<float> ragdoll_recover_from_;
    int ragdoll_recover_log_ = 0;
    int ragdoll_stop_logs_ = 0;
    // The solver state (`sol_ma_`/`sol_mf_`) is promoted to WORLD space while
    // the ragdoll is active (the JS node `ma` is world). `solver_base_*` is
    // the world->clip placement base captured at start; `ragdoll_stop`
    // subtracts it so the resuming clip apply stays continuous.
    bool solver_world_ = false;
    float solver_base_x_ = 0.0f;
    float solver_base_y_ = 0.0f;
    // Arena bounds for the per-frame body clamp (JS `Al.fha` L582).
    float ragdoll_wall_min_ = 0.0f;
    float ragdoll_wall_max_ = 0.0f;
    float ragdoll_floor_y_ = 0.0f;
    // JS `Te.yu`/`Te.zu`: the ARENA walls (see `set_arena_walls`). Used by the
    // `EObjectWall` align (`Gub` L558/559) and `Iub`'s repulsion.
    float arena_wall_min_ = 0.0f;
    float arena_wall_max_ = 0.0f;
    // JS `Te.jc` buffer shift from `Te.Iub` (wall repulsion): per play-buffer
    // slot x offset subtracted at read time (`b.data[d++].x -= f`). Sized to
    // the buffer (`2 + clip_len - FirstFrame`) and reset at every clip start.
    std::vector<float> wall_shift_;
    // JS `Te.Hla` (`this.Hla = f = ...`): the last repulsion amount, read by a
    // parent's `Iub` when `AlignOnParentWallCollision` (`Te.F3.Hla`).
    float wall_rep_last_ = 0.0f;
    // `this.bQa` = `xd.bAa` (per-location `FrictionForce`, char 241113).
    float ragdoll_friction_ = kFrictionForce;
    std::set<std::string> active_intervals_;
    // Clip-end snapshot of the above (JS `Ae.xb` at the `AnimationEnd` pass).
    std::vector<std::pair<std::string, int>> ended_intervals_; // active interval names (JS `Te.xj`)
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
    // The last played move's animation names, kept after the clip ends (the
    // `Te.KNa` `Ua` persistence). Read by `anim_names_of` when no move is
    // current — the EndStance ground-death `<CurrentAnimation Name="Physical
    // Lying"/>` selection depends on it.
    std::vector<std::string> last_anim_names_;
    float enemy_x_ = 0.0f;                  // enemy world X (for facing)
    // [cross-fighter align — JS `Te.BBa` L563 + `Gub` L557-559] The opponent
    // controller (JS `Te.cQ`) and this controller's `Te.Fk` vector (ctor L546
    // `this.Fk = new H(0,0,0,1)`): the last `Gub` result `(e - d)` in CLIP
    // space. A `<Position Player="Enemy" Object="Animation"/>` align of the
    // OTHER controller reads it verbatim (`e = c.Fk`). `Gub` runs on every
    // clip start, so a move with no `<Align>` resets it to 0 (JS default `Ui`).
    Fighter* opponent_ = nullptr;
    float fk_x_ = 0.0f, fk_y_ = 0.0f, fk_z_ = 0.0f;
    std::vector<sf2::scene::key_input> keys_; // buffered inputs (JS `Kl.zg`)
    int tap_age_ = 0;                       // frames since last tap (JS `zl.dX`)
    // JS `zl.rwa` (L799) fires the `KeyPressed` event (`gh(0, zg)`) exactly
    // ONCE, on the `zl.Sgb` (L798) `!a.sl` press edge. The old selection ran
    // on every frame a Tap lingered in `keys_` (the 15-frame `dX` window) and
    // consumed it at the first started move, so a 2-Tap move
    // (`DoubleStepForward`, moves.xml L12467-12470) could never see tap 1 and
    // tap 2 together. This flag is that one-frame edge.
    bool key_edge_ = false;
    // Keys currently held down (JS `zl.Ff[].sl` -> rebuilt `zg.Fh`): the
    // physical keydown set. `input(tap)` inserts, `input(release)` erases;
    // `rebuild_holds()` projects it into the `keys_` Hold entries.
    std::set<sf2::scene::key_type> held_keys_;
    // JS `zl.Qe` (L798): the 30-frame hold/release clear cycle.
    int hold_age_ = 0;
    std::function<const sf2::data::anim_clip*(const std::string&)> clip_lookup_;

    void rebuild_holds();
    void compute_align(const MoveDef& move);
    // JS `Te.Iub` (L~285930): the per-frame wall-repulsion clip shift. Reads
    // the pivot node's buffered x at slot `playhead_+2` and, when the move is
    // not `NoWallRepulsion`, subtracts the wall overflow from every later
    // buffer slot (`b.data[d++].x -= f`). No-op while no clip plays.
    void wall_repulsion_step();
    // JS `Dl.NQ` (L575) over the `Wf.b3` pair list (`Dl.v5a` L580): the mirror
    // partner bone of `i` (`_1` <-> `_2`), or -1 when `i` is unpaired.
    int mirror_partner(int i) const;
    // JS `Ua.Oeb` (L692): when `rw` (`Te.Peb` L560) is set the buffered clip's
    // `_1`/`_2` pairs are swapped for every slot >= 2, so sampling bone `i`
    // reads the buffer value of its partner. `zclip` guards the JS
    // `a[c].first<e&&a[c].second<e` test (`e` = `Kh(2).size`).
    int mirror_swap_src(int i, std::size_t zclip) const;
    void build_prepend(const MoveDef& move);
    void sample_current();
    // JS `ia` (L499) clip-less cadence: `Nd.ia()` (the ragdoll solver) runs
    // every frame even when `parameters.QD` skips the clip advance. Samples
    // the 1-frame, bone-less bind clip so the solver/placement path of
    // `sample()` runs (`nclip == 0`) — the NotAnimation bag's drawn pose.
    void sample_bind_pose();

    // [F4] Mass-weighted centroid of the posed body over the COM child list
    // (JS `Dl.v6` L577: `Eu.ma`). `axis` 0 = x, 1 = y. Falls back to the
    // render anchor when `pos_` is not sampled yet or no listed bone carries
    // mass.
    float com_axis(int axis) const {
        return com_axis_in(pos_, axis);
    }
    // Shared body: mass-weighted centroid of the posed body in `p` (JS
    // `Dl.v6` L577 `Eu.ma = Σ L0()[i].ma·weight / VR`). Used for both the
    // current (`pos_`) and previous (`prev_pos_`, the `mf` ref) frames.
    float com_axis_in(const std::vector<float>& p, int axis) const {
        const std::size_t n = model_.bones.size();
        if (p.size() < n * 2) {
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
            acc += p[static_cast<std::size_t>(idx) * 2 +
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
