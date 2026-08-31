#pragma once

// Fighter: model + animation sampling + flat-triangle rendering.
//
// Mirrors the game's `wd` fighter (MODEL_FORMAT §2): one merged ragdoll
// body (`Dl`), an animation controller (`Te`) that writes per-bone absolute
// world positions each frame, and a CPU-skinned 2D mesh (z dropped, flat
// color fill, one draw call).
//
// World placement: the fighter's world position anchors the COM bone
// (`wd.oL` offsets all bones relative to the COM). Facing negates X
// (`Te.Qeb`). Clip bone i maps to merged model bone i (order-sensitive).

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "scene/conditions.hpp"
#include "scene/model.hpp"

namespace sf2::data {
struct anim_clip;
}
namespace sf2::scene {
struct MoveDef;
struct FightContext;
} // namespace sf2::scene

namespace sf2::scene {

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
    // string). `owned` is the fighter's item list as (type, subtype) pairs
    // (the Warrior's <Items> + equipment slots). A move passes when every
    // Lock group resolves against the owned items: a plain <Item> lock
    // passes when an owned item matches Type AND SubType; an Or-group
    // passes when ANY item in the group matches. Moves with no locks are
    // universal (the Skeleton lock passes for every fighter). This is what
    // lets TacticWeapon="Knives|Keris" moves join the list when the fighter
    // equips WEAPON_KNIVES (SubType="Knives"). Sorted by Priority desc.
    void build_move_list_locks(const std::map<std::string, MoveDef>& all_moves,
                               const std::vector<std::pair<std::string, std::string>>& owned,
                               bool include_universal = true);

    // Buffers one key press (JS `Kl.Sgb`, L798): Tap (and Hold if
    // `held`). Clears on `release` (JS `Xgb`, L799).
    void input(sf2::scene::key_type key, sf2::scene::press_type press);

    // Per-frame tap buffer aging (JS `zl.ia` L798: clears after 30 frames).
    void age_keys();

    // Attempts move selection from `hb` (priority order) with the buffered
    // keys + current state. The FIRST passing move starts (JS `Zka` picks
    // `HB[0]`; `de.V1` tests each candidate in priority order).
    // Returns the started move's name, or "" if none passed.
    std::string try_select_move(sf2::scene::FightContext& ctx);

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

    // Advances the current clip one frame (60 Hz). Updates active intervals
    // (Start/End), transitions to idle when the clip ends (JS `Te.ia`
    // L547-548: `Xh+2 >= len` -> stop). Samples the pose at move_frame.
    void advance(float dt);

    // --- state accessors (Phase 3.2b) -------------------------------------
    const MoveDef* current_move() const { return current_move_; }
    int move_frame() const { return move_frame_; }
    int facing() const { return facing_; }
    const std::vector<const MoveDef*>& hb() const { return hb_; }
    const std::set<std::string>& active_intervals() const { return active_intervals_; }
    // Interval names active at `frame` (JS `jc.c7a` L691 semantics).
    std::vector<std::string> intervals_at(int frame) const;
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
    // Clip lookup callback — the demo supplies the archive.
    void set_clip_lookup(const std::function<const sf2::data::anim_clip*(const std::string&)>& fn) {
        clip_lookup_ = fn;
    }

    // --- existing render path ---------------------------------------------
    // Per-bone world positions at frame `f` of `clip`, anchored so the
    // fighter's COM bone sits at (x, y). Bones beyond the clip's bone count
    // keep their bind position. Facing -1 mirrors X.
    void sample(const sf2::data::anim_clip& clip, int frame, float x, float y,
                int facing);

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
    const MoveDef* current_move_ = nullptr; // playing move (JS `da.Ua`)
    const sf2::data::anim_clip* current_clip_ = nullptr; // clip for `current_move_`
    int move_frame_ = 0;                    // playback frame (JS `Te.Xh`)
    int facing_ = 1;                        // ±1 (JS `Te.FX` / `hd()`)
    float world_x_ = 0.0f, world_y_ = 0.0f; // fighter anchor (COM world pos)
    std::set<std::string> active_intervals_; // active interval names (JS `Te.xj`)
    float enemy_x_ = 0.0f;                  // enemy world X (for facing)
    std::vector<sf2::scene::key_input> keys_; // buffered inputs (JS `Kl.zg`)
    int tap_age_ = 0;                       // frames since last tap (JS `zl.Qe`)
    std::function<const sf2::data::anim_clip*(const std::string&)> clip_lookup_;

    void sample_current();
};

} // namespace sf2::scene
