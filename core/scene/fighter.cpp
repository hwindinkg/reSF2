// Fighter: clip sampling (Te.eda, MODEL_FORMAT §2.2) + macro-node
// computation (Fl.seb, L797) + 2D triangle vertex build (dv.ia, L840).
//
// Phase 3.2b: move execution — input -> move selection -> clip playback,
// intervals, facing/mirror (JS study cited inline in fighter.hpp).

#include "scene/fighter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <set>

#include "anim_archive.hpp"
#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

void Fighter::set_model(const Model& model) {
    model_ = model;
    pos_.assign(model_.bones.size() * 2, 0.0f);

    // [FIX stretched mesh — ragdoll solver] The game keeps the body/head
    // cloth nodes attached to the skeleton with a Verlet ragdoll solver
    // (JS `Al`, L296179: `ia()` = `sk()` integrate + `jE()` edge relax x
    // `IterativeProcess`=2, internal_settings.xml Physics). The old native
    // anchored each cloth node at its bind offset from the bind-nearest
    // skeleton bone — but the cloth's <Edges> constraints (rest lengths
    // 10-45) bind the cloth to DIFFERENT bones (the head cloth to the
    // HEAD-MacroNode* weighted averages, the body cloth to knees/ankles/
    // hips), which move independently of the bind-nearest bone. The static
    // offset left cloth+mesh triangles stretched tens of units (median
    // aspect 2.1, max 462.9 in the idle pose). The solver state below is
    // seeded from the bind pose (JS `Vc` ctor: ma = mf = p8) and stepped
    // once per sample() call — the game's 60 Hz cadence.
    const std::size_t n3 = model_.bones.size() * 3;
    sol_ma_.assign(n3, 0.0f);
    sol_mf_.assign(n3, 0.0f);
    for (std::size_t i = 0; i < model_.bones.size(); ++i) {
        const Bone& b = model_.bones[i];
        sol_ma_[i * 3] = b.x;
        sol_ma_[i * 3 + 1] = b.y;
        sol_ma_[i * 3 + 2] = b.z;
        sol_mf_[i * 3] = b.x;
        sol_mf_[i * 3 + 1] = b.y;
        sol_mf_[i * 3 + 2] = b.z;
    }
    // [FIX stretched mesh — continuity seed] Seed the continuity reference
    // with the BIND COM so the FIRST sample also translates the cloth from
    // bind into the first clip's frame (the JS solver space is continuous
    // from the very first frame). Without this the first frames leave the
    // cloth behind the posed skeleton.
    if (!model_.bones.empty()) {
        sol_prev_com_x_ = model_.bones[0].x;
        sol_prev_com_y_ = model_.bones[0].y;
        sol_prev_com_z_ = model_.bones[0].z;
        sol_have_prev_com_ = true;
    }
    solver_init_ = true;
    // JS `Vc` ctor (L793-794): ma = mf = the bind position (`p8`). The
    // solver then runs exactly one `Al.ia()` step per frame (L582) — the
    // game has NO warmup and NO cross-clip state translation.
    align_x_ = align_y_ = align_z_ = 0.0f;

    // Build mirror swap pairs for _1 ↔ _2 (JS Te.Peb L560 → Ua.Oeb L692).
    // When facing -1 the buffered clip frames are negated (vu.Neb L668) and
    // left/right paired bones are swapped so the skeleton's left stays left.
    mirror_pairs_.clear();
    for (std::size_t i = 0; i < model_.bones.size(); ++i) {
        const std::string& nm = model_.bones[i].name;
        if (nm.size() < 3) continue;
        if (nm.compare(nm.size() - 2, 2, "_1") != 0) continue;
        std::string other = nm.substr(0, nm.size() - 2) + "_2";
        int j = model_.bone_by_name(other);
        if (j < 0) continue;
        if (static_cast<std::size_t>(j) <= i) continue;  // avoid double
        mirror_pairs_.emplace_back(static_cast<int>(i), j);
    }
}

// ---------------------------------------------------------------------------
// Move execution (Phase 3.2b) — JS `wd`/`Te`/`de` semantics
// ---------------------------------------------------------------------------

// JS `ra.Hza` (L684-685): the fighter's move set `me` is built by testing
// every parsed move's Locks against the fighter's items. Task contract:
// the equipped weapon (Fists) contributes all moves tagged
// `TacticWeapon == weapon_subtype` (JS `Fa.Ueb` L711 reads TacticWeapon
// into `QX`). Sorted by Priority desc so `hb[0]` = the highest-priority
// candidate (`Ci` L800, `Zka` L502).
void Fighter::build_move_list(const std::map<std::string, MoveDef>& all_moves,
                              const std::string& weapon_subtype,
                              bool include_universal) {
    hb_.clear();
    for (const auto& kv : all_moves) {
        const MoveDef& m = kv.second;
        if (!m.tactic_weapon.empty()) {
            // Weapon-locked move: only the matching weapon contributes it.
            if (m.tactic_weapon != weapon_subtype) {
                continue;
            }
        } else if (!include_universal) {
            continue;
        }
        // No TacticWeapon -> universal (Skeleton lock passes for all).
        hb_.push_back(&m);
    }
    std::sort(hb_.begin(), hb_.end(),
              [](const MoveDef* a, const MoveDef* b) { return a->priority > b->priority; });
}

// JS `ra.Hza` (L684-685): the move set `me` is built by testing every move's
// Locks against the fighter's items (`f.nw(d,b)`). A lock group passes when:
//   - a plain <Item Type SubType> matches an owned (type, subtype) item;
//   - an <Operator Type="Or"> group passes when ANY member item matches;
// moves with no locks are universal (every fighter has the Skeleton).
// This mirrors the game exactly: a WEAPON_KNIVES (SubType="Knives") owner
// gets KnivesSlash (Locks: Or{Weapon Knives, Weapon Keris}).
void Fighter::build_move_list_locks(
    const std::map<std::string, MoveDef>& all_moves,
    const std::vector<std::pair<std::string, std::string>>& owned,
    bool include_universal) {
    auto owned_item = [&owned](const Lock& l) {
        for (const auto& o : owned) {
            // Lock Type/SubType both match (JS `nw`: `b.type==a.type &&
            // b.Yb==a.Yb`; an empty lock SubType matches any owned subtype).
            if (o.first != l.type) continue;
            if (!l.subtype.empty() && o.second != l.subtype) continue;
            return true;
        }
        return false;
    };
    hb_.clear();
    for (const auto& kv : all_moves) {
        const MoveDef& m = kv.second;
        if (m.locks.empty()) {
            // No locks -> universal (JS: every move's Skeleton lock passes).
            if (include_universal) hb_.push_back(&m);
            continue;
        }
        // Evaluate the lock list: every top-level lock must pass.
        // Or-group locks (l.or_) pass when ANY member passes — the parser
        // flattens the Or group into one lock per member item with or_=true.
        bool all_pass = true;
        bool or_group = false;
        bool any_or = false;
        for (const Lock& l : m.locks) {
            if (l.or_) {
                or_group = true;
                if (owned_item(l)) any_or = true;
            } else if (!owned_item(l)) {
                all_pass = false;
                break;
            }
        }
        if (!all_pass) continue;
        if (or_group && !any_or) continue;
        hb_.push_back(&m);
    }
    std::sort(hb_.begin(), hb_.end(),
              [](const MoveDef* a, const MoveDef* b) { return a->priority > b->priority; });
}

void Fighter::age_keys() {
    // JS `zl.ia` (L798): after 30 frames the tap buffer is cleared.
    if (tap_age_ > 0) {
        ++tap_age_;
        if (tap_age_ > 30) {
            tap_age_ = 0;
            for (auto it = keys_.begin(); it != keys_.end();) {
                if (it->press == press_type::tap) it = keys_.erase(it);
                else ++it;
            }
        }
    }
}

// JS `Kl.Sgb` (L798): press buffers the key as a Tap and marks Hold.
// `Xgb` (L799) removes it from Hold on release.
void Fighter::input(sf2::scene::key_type key, sf2::scene::press_type press) {
    key_input ki{key, press};
    // Replace an existing entry for the same key (Tap replaces Tap).
    for (auto& k : keys_) {
        if (k.key == key && k.press == press) {
            k = ki;
            return;
        }
    }
    keys_.push_back(ki);
    // Tap also implies Hold is active (JS `zl.yLa` L799 builds Hold from
    // the pressed keys).
    if (press == press_type::tap) {
        tap_age_ = 1;
        bool held = false;
        for (const auto& k : keys_) {
            if (k.key == key && k.press == press_type::hold) held = true;
        }
        if (!held) keys_.push_back({key, press_type::hold});
    }
    if (press == press_type::release) {
        // Remove the hold.
        for (auto it = keys_.begin(); it != keys_.end();) {
            if (it->key == key && it->press == press_type::hold) it = keys_.erase(it);
            else ++it;
        }
    }
}

// JS `jc.c7a` (L691): an interval is active when
//   max(start, qx) <= frame <= min(finish, Lj).
std::vector<std::string> Fighter::intervals_at(int frame) const {
    std::vector<std::string> out;
    if (current_move_ == nullptr) return out;
    for (const Interval& iv : current_move_->intervals) {
        const int s = std::max(iv.start, current_move_->first_frame);
        const int e = iv.end;  // parse already applied EndFrame+2 default
        if (s <= frame && frame <= e) {
            out.push_back(iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name);
        }
    }
    return out;
}

int Fighter::interval_type(const std::string& name) const {
    if (current_move_ == nullptr) return 0;
    for (const Interval& iv : current_move_->intervals) {
        const std::string key =
            iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name;
        if (key == name) return iv.type;
    }
    return 0;
}

// JS `wd.qYa`/`Nbb` (L514): a Block interval (`yD(5)`) active now.
bool Fighter::has_block() const {
    if (current_move_ == nullptr) return false;
    for (const Interval& iv : current_move_->intervals) {
        if (iv.type != 5) continue;  // `fe.G0`: Block = 5 (L774)
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= iv.end) return true;
    }
    return false;
}

// JS `Te.yD(6)` presence (L553): Invulnerable active now (HZa gate).
bool Fighter::has_invuln() const {
    if (current_move_ == nullptr) return false;
    for (const Interval& iv : current_move_->intervals) {
        if (iv.type != 6) continue;  // `fe.G0`: Invulnerable = 6 (L774)
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= iv.end) return true;
    }
    return false;
}

// JS `Te.hT(5)` (L554, reverse loop): drop every active Block interval.
void Fighter::clear_block() { clear_intervals(5, ""); }

// JS `Te.hT(type)` / `F4(name)` (L554) + scripted `Yob` (L1294): drop every
// active interval matching TYPE (-1 = any) or NAME ("" = any). Interval
// identity is the active-set key (name, or "type<N>" for nameless).
// Per-bone knockback feed (JS `Bl.strike` L582 moves the endpoint bodies).
void Fighter::add_knockback(int bone, const sf2::scene::Vec3& v) {
    if (bone < 0 || model_.bones.empty()) return;
    const std::size_t n = model_.bones.size();
    if (kb_.size() != n) kb_.assign(n, sf2::scene::Vec3{});
    kb_[static_cast<std::size_t>(bone)] =
        kb_[static_cast<std::size_t>(bone)] + v;
}

void Fighter::clear_intervals(int type, const std::string& name) {    if (current_move_ == nullptr) {
        active_intervals_.clear();
        return;
    }
    for (const Interval& iv : current_move_->intervals) {
        if (type >= 0 && iv.type != type) continue;
        if (!name.empty() && iv.name != name) continue;
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= iv.end) {
            active_intervals_.erase(iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name);
        }
    }
}

// JS `wd.NS` (L506) -> `Te.Skb` (L550): start the move's clip.
//   - conditions tested by the caller (try_select_move)
//   - `Mq = a.qx` (FirstFrame) — native: move_frame = FirstFrame
//   - facing `b` = ±1 toward the enemy (JS `b6a`, L603: sign of enemyX - meX)
//   - `Peb()` (L560) auto-mirrors when the MirrorNode flips — the native
//     pose mirror is the render `facing` (x flip in Fighter::sample).
//
// Keys gating: the fighter's OWN context keeps `gm` true (only the AI
// move-finder `de.V1` clears it: `f.gm=!1` L601). With gm=true the Keys
// condition matches the buffered keys (`vm.he` L749:
// `(a.keys.S1||a.Wl>0?this.xn:this.TDa).$ga(a.keys)` — the move's required
// keys must be a subset of the buffered keys). So `keys_gm` is set TRUE
// here and the caller must have buffered the required keys.
bool Fighter::try_start_move(const MoveDef& move, FightContext& ctx) {
    return start_move_impl(move, ctx, /*ai=*/false);
}

// JS `de.V1` (L601-602): the AI tests a candidate with `Fc.gm=!1`, which
// makes every Keys condition pass (`vm.he` L749 returns true). The native
// port mirrors this with `keys_gm=false`.
bool Fighter::ai_start_move(const MoveDef& move, FightContext& ctx) {
    return start_move_impl(move, ctx, /*ai=*/true);
}

bool Fighter::start_move_impl(const MoveDef& move, FightContext& ctx, bool ai) {
    if (ai) {
        // JS `de.V1` (L601-602): the AI's candidate test evaluates ONLY the
        // move's TACTICS conditions (`a.Yz(this.model,null,a.FQ(2))` = the
        // `Ts` list). The main Conditions tree (Keys/CurrentAnimation/etc)
        // is NOT re-checked when the move starts — `Ykb` (L500) goes
        // straight to `Okb` -> `NS` -> `Skb`. The native AI path therefore
        // evaluates the tactics conditions here (the same ones `de.V1`
        // ran) and skips the input-gated main conditions.
        ctx.candidate_moves = {move.name};
        ctx.keys_gm = false;  // gm=false: every Keys condition passes
        if (!eval_move_conditions(move.tactics, ctx)) {
            return false;
        }
    } else {
        // Input path (JS `wd.NS` L506): the caller (try_select_move) has
        // already tested the main Conditions; here they are re-checked with
        // the buffered keys (gm=true).
        ctx.candidate_moves = {move.name};
        ctx.keys.clear();
        for (const auto& k : keys_) {
            ctx.keys.push_back({k.key, k.press});
        }
        ctx.keys_gm = true;
        std::string trace;
        if (!eval_move_conditions(move.conditions, ctx, &trace)) {
            return false;
        }
    }

    current_move_ = &move;
    move_frame_ = std::max(0, move.first_frame);  // JS `Mq = a.qx`
    active_intervals_.clear();
    // [FIX Phase 4a — pacing] Subframes per clip-frame (JS `Te.Gka`:
    // `Tx = model.model.HD()`, `rpa.initialize((Ua.XJ+1)*Tx)`; `eda`
    // advances `mo` by `Tx` per step with HD()==1). MidFrames=2 -> 3.
    sub_ = std::max(1, (move.mid_frames + 1) * 1);
    subframe_ = 0;

    // Consume the buffered tap (JS `Okb` L506: `Kl.reset()` + `Kl.Ptb(a)`
    // sets the current key as the move's trigger).
    tap_age_ = 0;
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (it->press == press_type::tap) it = keys_.erase(it);
        else ++it;
    }

    // Clip lookup: FileName -> anim_archive entry (JS `jc.uja` L693 loads
    // the clip by `Eza` = FileName minus ".bytes").
    if (clip_lookup_) {
        std::string clip_name = move.file_name;
        const std::string suffix = ".bytes";
        if (clip_name.size() > suffix.size() &&
            clip_name.compare(clip_name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            clip_name = clip_name.substr(0, clip_name.size() - suffix.size());
        }
        current_clip_ = clip_lookup_(clip_name);
    }

    // Facing toward the enemy (JS `b6a` L603: `enemy.x - my.x >= 0 ? 1 : -1`).
    facing_ = (enemy_x_ - world_x_) >= 0.0f ? 1 : -1;
    // JS `Te.Skb` (L551) runs `Gub()` (align, L557-559) right after loading
    // the clip and before the first `ia()` sample.
    compute_align(move);

    // [FIX root motion — JS `Skb` L551-552] Seed the authored root-motion
    // state exactly as the JS controller does when a move starts:
    //   `a=this.j8; a.x=0` (always reset the applied offset),
    //   `this.Ua.qta || (this.DM = Qfa() = wua; this.DM.x*=hd())`
    //     (DM = <Velocity X/Y/Z>, x mirrored by facing; kept when
    //      SaveVelocity/`qta` is set),
    //   `this.aV = t9a() = Coa; this.aV.x*=hd()`
    //     (aV = <Velocity Ax/Ay/Az>, x mirrored by facing — always).
    // Reference: `Fa.ykb` L721-722 (parse), `Te.Qfa`/`Te.t9a` L699,
    // `Te.jub` L722 (`SaveVelocity` -> `qta`).
    const float fsign = facing_ < 0 ? -1.0f : 1.0f;
    if (move.velocity.has_velocity) {
        root_active_ = true;
        if (!move.velocity.save_velocity) {
            root_dm_x_ = move.velocity.x * fsign;
        }
        root_av_x_ = move.velocity.ax * fsign;
    } else {
        root_active_ = false;
        root_dm_x_ = 0.0f;
        root_av_x_ = 0.0f;
    }
    sample_current();
    return true;
}

// JS `wd.Lea` (L512) snapshots the buffered keys into `Fc.keys`; the move
// selection tests each `hb` candidate in priority order (JS `Zka` L502 +
// `de.V1` L601). The FIRST passing move starts. The `1key` template means
// one buffered Tap of a single key.
std::string Fighter::try_select_move(FightContext& ctx) {
    // A move can only start when no clip is playing (JS: `tKa` L499 guards
    // `da.Ua==null` for strike checks; the KeyPressed event handler only
    // acts when the fighter is not busy).
    if (current_move_ != nullptr) {
        return "";
    }
    for (const MoveDef* m : hb_) {
        if (m == nullptr) continue;
        // Input-selectable moves are those whose Events contain
        // "KeyPressed" (JS `Gc.Vkb` L671 -> `Gc.EZa` L676: the KeyPressed
        // event walks `d.Su.dea(2)` = the KeyPressed-indexed move list).
        if (!m->has_event("KeyPressed")) {
            continue;
        }
        if (try_start_move(*m, ctx)) {
            return m->name;
        }
    }
    return "";
}

// Hit-reaction pick (JS `Gc.DK` L673-674, d-set first-match; see
// fighter.hpp for the roulette caveat).
std::string Fighter::try_react(FightContext& ctx, bool prefer_fall) {
    auto try_pass = [&](bool falls_only) -> std::string {
        for (const MoveDef* m : hb_) {
            if (m == nullptr) continue;
            if (!m->has_event("Hit")) continue;
            const bool is_fall = m->name.find("Fall") != std::string::npos;
            if (falls_only != is_fall) continue;
            if (ai_start_move(*m, ctx)) return m->name;
        }
        return "";
    };
    if (prefer_fall) {
        const std::string f = try_pass(true);
        if (!f.empty()) return f;
    }
    return try_pass(false);
}

// JS `Te.ia` (L547-548): each 60 Hz update advances `Xh` (playback frame)
// and `fG` (physics frame); when `Xh+2 >= clipLen` the clip ends (`KNa()`
// + lS -> EStopAnimationEvent) and the fighter returns to idle.
// JS `vp` (L562-563) -> `rrb` (L552) -> `jc.c7a` (L691) recomputes the
// active intervals each frame.
//
// [FIX Phase 4a — pacing] The game does NOT play 1 clip-frame per 60 Hz
// step. `Te.Gka` (L285802) sets `Tx = model.model.HD()` (=1) and the
// interpolator `rpa` is initialized with `(Ua.XJ+1)*Tx` subframes per
// clip-frame; `Te.eda` (L282908) advances the subframe index `mo` by
// `Tx` per step. With `MidFrames=XJ=2` (moves.xml: `MidFrames="2"` on the
// fists/stance moves) that is (XJ+1)=3 subframes per clip-frame, i.e. the
// anim lasts (clipLen - FirstFrame)*3 + 1 fight-frames. Evidence (oracle
// reference/traces/console.log, real game at 60 Hz):
//   FistsStartStance-Left: clip stance_1 = 46 frames, FirstFrame=2
//     -> trace F2..F134 = 133 frames = (46-2)*3+1            [exact]
//   HighKick: clip high_kick = 21 frames, FirstFrame=3
//     -> trace F336..F390 = 55 frames = (21-3)*3+1           [exact]
// The old code advanced move_frame_ 1 per step => 3x TOO FAST. The native
// now advances a subframe counter and samples the interpolated pose.
void Fighter::advance(float dt) {
    (void)dt;  // fixed 60 Hz - one frame per call (JS 1/60 step)
    // Knockback offsets decay every tick (JS Vc.sk friction - OPEN rate).
    if (!kb_.empty()) sf2::scene::decay_knockback(kb_);
    // Timescale steps (SlowModel KT): scale>=1 verbatim (Speed<1 no-ops
    // at apply); fractional remainder carries to the next tick.
    scale_acc_ += time_scale_;
    int steps = static_cast<int>(scale_acc_);
    if (steps < 1) steps = 1;
    if (steps > 4) steps = 4;
    scale_acc_ -= static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) advance_step();
}

void Fighter::advance_step() {
    if (current_move_ == nullptr || current_clip_ == nullptr) {
        return;
    }

    const int clip_len = static_cast<int>(current_clip_->frames.size());
    if (clip_len <= 0) {
        return;
    }

    // Subframes per clip-frame: (XJ+1)*HD with HD=1 (JS `Gka` +
    // `eda`: `mo += Tx`, `Tx = (Ua.XJ+1)`). MidFrames=2 -> 3.
    const int sub = std::max(1, (current_move_->mid_frames + 1) * 1);

    // Interval update: active when max(start,qx) <= frame <= min(finish,Lj).
    // The intervals use the CLIP frame (Xh = move_frame_) — the subframe
    // phase is a render detail.
    active_intervals_.clear();
    for (const std::string& n : intervals_at(move_frame_)) {
        active_intervals_.insert(n);
    }

    // Root motion (JS `Te.eda` L556 + `Te.j8`/`DM`/`aV` L546/564). Two paths:
    //
    //  (a) AUTHORED <Velocity> (JS `Fa.ykb` L721-722; `Skb` L551-552 seeds
    //      `DM`=`wua`, `aV`=`Coa`). `eda` L556 runs `Pab` (`Qab(DM)`:
    //      `j8 += DM*sG`) at frame start and `Nab` (`Oab(aV)`:
    //      `DM += aV*sG`) at frame end; the `j8` offset is added to EVERY
    //      posed bone (`d.x+=c.x; d.y+=c.y; d.z+=c.z`) — i.e. the whole
    //      fighter shifts, so the native anchor world_x_ takes the same
    //      per-frame delta. `sG = 1/Tx`, `Tx = model.model.HD()` (`Gka`
    //      L561) = 1.
    //  (b) FALLBACK (clip-baked root): a move with no <Velocity> leaves
    //      `wua`/`Coa` = 0, so `DM`/`aV`/`j8` stay 0 (JS) and the clip's own
    //      root-bone (bone 0) displacement moves the pose. The native
    //      reproduces that as the COM-x delta per clip frame spread over the
    //      `sub` subframes (one clip-frame delta over `sub` `eda` calls).
    //      Retained ONLY here, for moves without authored <Velocity>.
    // NOTE: every shipped fighter/locomotion move takes (b) — all 62 live
    // <Velocity> elements are on projectile/magic moves (summary).
    constexpr float kSG = 1.0f;  // JS `Gka` L561: sG = 1/Tx, Tx = HD() = 1
    if (root_active_) {
        const float d = root_dm_x_ * kSG;  // `Pab`/`Qab`: j8 += DM*sG
        world_x_ += d;                     // the j8 delta lands on the COM anchor
        root_dm_x_ += root_av_x_ * kSG;    // `Nab`/`Oab`: DM += aV*sG
    } else if (move_frame_ >= 0 &&
               static_cast<std::size_t>(move_frame_) < current_clip_->frames.size()) {
        const float com_now = current_clip_->frames[static_cast<std::size_t>(move_frame_)].bones.empty()
                                  ? 0.0f
                                  : current_clip_->frames[static_cast<std::size_t>(move_frame_)]
                                        .bones[0]
                                        .x;
        if (move_frame_ > 0 && static_cast<std::size_t>(move_frame_ - 1) <
                                   current_clip_->frames.size()) {
            const float com_prev =
                current_clip_->frames[static_cast<std::size_t>(move_frame_ - 1)].bones.empty()
                    ? 0.0f
                    : current_clip_->frames[static_cast<std::size_t>(move_frame_ - 1)].bones[0].x;
            world_x_ += (com_now - com_prev) * (facing_ < 0 ? -1.0f : 1.0f) /
                        static_cast<float>(sub);
        }
    }

    // Clip end (JS `Te.ia` L547-548: `Xh+2 >= len` -> KNa + lS + Sca).
    // With the subframe pacing the anim plays the playable range
    // [FirstFrame..len-1] at `sub` subframes each + the extra hold frame
    // (the Pka `CT` duplicates the last key, JS `vu.Pka` L340694). The
    // observed duration = (len - FirstFrame)*sub + 1 fight-frames; the
    // clip-frame (Xh) counter advances once every `sub` steps.
    if (move_frame_ >= clip_len - 2 && subframe_ >= sub - 1) {
        // The clip has fully played (the last playable frame's subframes).
        current_move_ = nullptr;
        current_clip_ = nullptr;
        active_intervals_.clear();
        subframe_ = 0;
        align_x_ = align_y_ = align_z_ = 0.0f;
        // JS `stop()`/`KNa()` call `jc.reset()`; `Skb` L551 zeroes `j8`.
        root_active_ = false;
        root_dm_x_ = root_av_x_ = 0.0f;
        return;
    }

    ++subframe_;
    if (subframe_ >= sub) {
        subframe_ = 0;
        ++move_frame_;  // JS `Xh++` (once per `sub` steps)
    }
    sample_current();
}

void Fighter::sample_current() {
    if (current_clip_ != nullptr) {
        sample(*current_clip_, move_frame_, world_x_, world_y_, facing_);
    }
}

void Fighter::clear_move() {
    current_move_ = nullptr;
    current_clip_ = nullptr;
    active_intervals_.clear();
    subframe_ = 0;
    align_x_ = align_y_ = align_z_ = 0.0f;
    // JS `stop()` -> `jc.reset()` / `Skb` L551: the authored root state is
    // per-move; clear it so a later move starts from a zero `j8`.
    root_active_ = false;
    root_dm_x_ = root_av_x_ = 0.0f;
}

// [FIX root-motion align] JS `Te.Gub` (L557-559) + `Te.Gla` (L550):
// compute the move's <Align> offset and store it as a clip-buffer shift
// (`Gla` -> `jc.shift` L550). Called once at clip start — JS `Skb` (L551)
// runs `Gub()` right after loading the clip, before the first `ia()`.
// Object mapping (JS `Fa.jva` L719-720): Pivot Object = `VE`, Position
// Object = `JK`; the axis flags (`cI`=X, `dI`=Y, `MY`=Z) select which
// components shift (`Gla(cI?Fk.x:dja, dI?Fk.y:eja, MY?Fk.z:0)`).
//
// The shipped Fists stance moves are
// `<Pivot Object="Nodes" Part="NHeel_2"/><Position Object="Pivot" ShiftX=..>`
// -> d = the clip's NHeel_2 at FirstFrame (`jc.Kh(2)`), e = the posed
// NHeel_2 (`currentNode.ma`) + facing*ShiftX, so the clip is shifted to
// keep the pivot bone where the previous pose left it.
void Fighter::compute_align(const MoveDef& move) {
    align_x_ = align_y_ = align_z_ = 0.0f;
    if (!move.align.has_align || current_clip_ == nullptr ||
        current_clip_->frames.empty()) {
        return;
    }
    const Align& al = move.align;
    const std::size_t n = model_.bones.size();
    // JS `Gub` L558/559 switch on the resolved `VE`/`JK` enum.
    const auto map_object = [](const std::string& s) -> int {
        if (s == "Nodes") return 1;      // EObjectNodes
        if (s == "Animation") return 2;  // EObjectAnimation
        if (s == "Wall") return 3;       // EObjectWall
        if (s == "Pivot") return 4;      // EObjectPivot
        return 0;                        // EObjectNone
    };
    const int ve = map_object(al.pivot_object);
    const int jk = map_object(al.pos_object);

    // Reference frame = FirstFrame (JS `Pka(jc, Mq)` loads it before `Gub`;
    // `this.jc.Kh(2)` is that buffer, `Kh(2).data[node]` = the raw clip pos).
    const std::size_t f0 = static_cast<std::size_t>(
        std::max(0, std::min(move.first_frame,
                             static_cast<int>(current_clip_->frames.size()) - 1)));
    const auto& fb = current_clip_->frames[f0].bones;
    const int pivot_idx = model_.bone_by_name(al.pivot_part);  // UE / `this.os`
    const float f = facing_ < 0 ? -1.0f : 1.0f;

    // d = the Pivot object's position (JS `Gub` L558).
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (ve == 1 || ve == 4) {  // EObjectNodes / EObjectPivot: clip buffer node
        if (pivot_idx >= 0 && static_cast<std::size_t>(pivot_idx) < fb.size()) {
            const std::size_t u = static_cast<std::size_t>(pivot_idx);
            dx = fb[u].x;
            dy = fb[u].y;
            dz = fb[u].z;
        }
    }
    // ve == 2 (EObjectAnimation) -> d = 0. ve == 3 (EObjectWall) needs the
    // scene wall bounds `yu`/`zu` (not owned by Fighter) — OPEN, d stays 0.

    // e/f/g = the Position object's position (JS `Gub` L559), in solver
    // (clip) space: `currentNode.ma` / `L7a(i).ma` are the posed positions.
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    auto posed = [&](int idx, float& ox, float& oy, float& oz) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= n) return;
        if (sol_ma_.size() != n * 3) return;
        const std::size_t u = static_cast<std::size_t>(idx);
        ox = sol_ma_[u * 3];
        oy = sol_ma_[u * 3 + 1];
        oz = sol_ma_[u * 3 + 2];
    };
    if (jk == 1) {        // EObjectNodes: posed Position-Part bone
        posed(model_.bone_by_name(al.pos_part), ex, ey, ez);
    } else if (jk == 4) { // EObjectPivot: posed pivot node (`currentNode.ma`)
        posed(pivot_idx, ex, ey, ez);
    }
    // jk == 2 (EObjectAnimation) -> e = this.Fk = 0 at clip start.
    // jk == 3 (EObjectWall) needs the wall bounds — OPEN, e stays 0.
    ex += f * al.shift_x;  // JS `e += this.hd()*a.dja`
    ey += al.shift_y;      // JS `f += a.eja`

    // `Fk = e - d` (JS L559 `c.x=e-d.x; ...`); `Gla` selects the per-axis
    // component (X/Z here; `ShiftY` when Y is not an align axis).
    align_x_ = al.axis_x ? (ex - dx) : al.shift_x;
    align_y_ = al.axis_y ? (ey - dy) : al.shift_y;
    align_z_ = al.axis_z ? (ez - dz) : 0.0f;
}

void Fighter::sample(const sf2::data::anim_clip& clip, int frame, float x,
                     float y, int facing) {
    if (clip.frames.empty()) {
        return;
    }
    frame = std::max(0, std::min(frame, static_cast<int>(clip.frames.size()) - 1));
    const std::size_t n = model_.bones.size();
    if (n == 0) {
        return;
    }
    // Snapshot this frame's input world-x as the NEXT frame's `ma`-analog
    // for the MYa/lwa swap check (JS compares stale posed order vs the new
    // buffer). Updated every sample so prev_x_ always lags one frame.
    if (pos_.size() == n * 2) {
        if (prev_x_.size() != n) prev_x_.assign(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) prev_x_[i] = pos_[i * 2];
    }
    const auto& bones = model_.bones;
    const auto& frame_bones = clip.frames[static_cast<std::size_t>(frame)].bones;
    const std::size_t nclip = std::min(frame_bones.size(), n);

    // [FIX root-motion] JS `wu` Bezier (Te.Gka/wu.f6a L1284-1286):
    // P0=mid(a,b), P1=b, P2=mid(b,c) at t=(mo+1)/UM. Linear over-scales.
    const int sub_i = std::max(1, sub_);
    const float t_bez = (static_cast<float>(subframe_) + 1.0f) / static_cast<float>(sub_i);
    const float omt = 1.0f - t_bez;
    const float w0 = omt * omt;
    const float w1 = 2.0f * omt * t_bez;
    const float w2 = t_bez * t_bez;
    const auto& next_bones = clip.frames[std::min(frame + 1, static_cast<int>(clip.frames.size()) - 1)].bones;
    const std::size_t nnext = std::min(next_bones.size(), n);
    const auto& next2_bones = clip.frames[std::min(frame + 2, static_cast<int>(clip.frames.size()) - 1)].bones;
    const std::size_t nnext2 = std::min(next2_bones.size(), n);
    std::vector<float> px(n), py(n), pz(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i < nclip) {
            if (i < nnext && i < nnext2 && sub_i > 1) {
                const float ax = frame_bones[i].x, ay = frame_bones[i].y, az = frame_bones[i].z;
                const float bx = next_bones[i].x, by = next_bones[i].y, bz = next_bones[i].z;
                const float cx = next2_bones[i].x, cy = next2_bones[i].y, cz = next2_bones[i].z;
                const float p0x = (ax + bx) * 0.5f, p0y = (ay + by) * 0.5f, p0z = (az + bz) * 0.5f;
                const float p2x = (bx + cx) * 0.5f, p2y = (by + cy) * 0.5f, p2z = (bz + cz) * 0.5f;
                px[i] = w0 * p0x + w1 * bx + w2 * p2x;
                py[i] = w0 * p0y + w1 * by + w2 * p2y;
                pz[i] = w0 * p0z + w1 * bz + w2 * p2z;
            } else if (i < nnext) {
                const float t_lin = static_cast<float>(subframe_) / static_cast<float>(sub_i);
                px[i] = frame_bones[i].x + (next_bones[i].x - frame_bones[i].x) * t_lin;
                py[i] = frame_bones[i].y + (next_bones[i].y - frame_bones[i].y) * t_lin;
                pz[i] = frame_bones[i].z + (next_bones[i].z - frame_bones[i].z) * t_lin;
            } else {
                px[i] = frame_bones[i].x; py[i] = frame_bones[i].y; pz[i] = frame_bones[i].z;
            }
        } else { px[i] = bones[i].x; py[i] = bones[i].y; pz[i] = bones[i].z; }
    }

    // [FIX root-motion align — JS `Te.Gub` L557-559 -> `Te.Gla` L550]
    // `Gla` shifts the whole clip buffer (`jc.shift`) by the move's align
    // offset once at clip start; every later `eda` (JS L556) reads the
    // shifted buffer. Native equivalent: add the stored shift to every
    // clip-driven bone before the solver. Only `fq`-sized (clip) bones are
    // shifted in JS, so the cloth/macro bones keep their own state.
    if (align_x_ != 0.0f || align_y_ != 0.0f || align_z_ != 0.0f) {
        for (std::size_t i = 0; i < nclip; ++i) {
            px[i] += align_x_;
            py[i] += align_y_;
            pz[i] += align_z_;
        }
    }

    // 2. [FIX stretched mesh — ragdoll solver] The game's per-frame pose
    //    pipeline (JS fighter `ia` L253769: `da.ia()` [Te.eda applies the
    //    clip] -> `Nd.ia()` [Al.ia = sk + jE] -> `oa.Qja()` [macros]):
    //    (see the step comments inside; n3 = the solver state stride)
    //
    //    a) `Te.eda` (L282908): every clip-driven bone gets `f4()` (mf =
    //    ma — the previous SOLVED position) then `XA(clip)` (ma = the
    //    interpolated clip position). Bones past the clip bone count keep
    //    their solved state (the cloth ragdoll).
    //    b) `Al.sk` (L296832, `Vc.sk` L405734): Verlet integrate every
    //    non-immovable bone — new = ma + (ma - mf) * (cloth ? 1-Att :
    //    1) + (0, grav, 0); grav = `xd.fDa` = 0.4 (internal_settings.xml
    //    Physics Gravitation). Immovable (`NG`): Fixed="1" bones and ALL
    //    MacroNodes (JS `Fl` ctor `QMa(1)` -> nh=false -> NG=true).
    //    c) `Al.jE` (L296592): `IterativeProcess`=2 passes over every
    //    <Edges> entry; `yu.bFa` (L403731) relaxes the pair toward the
    //    rest Length, mass-weighted (`f=(1-len/dist)/(w1+w2)`), moving
    //    only the non-immovable endpoints. This is what keeps the cloth
    //    at its constraint lengths around the posed skeleton — the
    //    static bind-offset anchoring it replaces stretched the cloth
    //    triangles (the cloth's edges bind to head macros / knees /
    //    ankles, not the bind-nearest bone).
    //    d) `Dl.Qja` (L294688) -> `Fl.seb` (L406288): every macro not
    //    clip-posed this frame is re-derived as the weighted average of
    //    its children's SOLVED positions (mf = ma — velocity zeroed).
    //    The solver state (sol_ma_/sol_mf_) persists across sample()
    //    calls — one step per call matches the game's 60 Hz cadence.
    const std::size_t n3 = n * 3;
    if (solver_init_ && sol_ma_.size() == n3) {
        // [FIX stretched mesh — continuous solver space] The JS `ma`/`mf`
        // live in the fighter's ONE continuous space (skeleton and cloth
        // share the world placement), so a clip switch never teleports the
        // cloth. The native solver is authored in raw CLIP coordinates,
        // which jump ~740 units between clips; translate the persisted
        // state by the COM delta each sample so the cloth stays continuous
        // with the (align-shifted) skeleton. This is the native-space
        // equivalent of the JS continuity, NOT new JS behavior.
        if (nclip > 0) {
            const float com_x = px[0], com_y = py[0], com_z = pz[0];
            if (sol_have_prev_com_) {
                const float dx = com_x - sol_prev_com_x_;
                const float dy = com_y - sol_prev_com_y_;
                const float dz = com_z - sol_prev_com_z_;
                if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
                    for (std::size_t i = 0; i < n; ++i) {
                        sol_ma_[i * 3] += dx;
                        sol_ma_[i * 3 + 1] += dy;
                        sol_ma_[i * 3 + 2] += dz;
                        sol_mf_[i * 3] += dx;
                        sol_mf_[i * 3 + 1] += dy;
                        sol_mf_[i * 3 + 2] += dz;
                    }
                }
            }
            sol_prev_com_x_ = com_x;
            sol_prev_com_y_ = com_y;
            sol_prev_com_z_ = com_z;
            sol_have_prev_com_ = true;
        }
        // [FIX stretched mesh — JS-faithful] `Al.ia()` (L582) is exactly
        // `sk(); jE();` per frame. `eda` (JS L556) sets each clip bone's
        // mf = ma (the previous solved position) then ma = the (align-shifted)
        // clip pose; the cloth bones keep their prior state. The native's raw
        // clip-space state is kept continuous by the COM translation above
        // (the JS solver space is inherently continuous).
        // (a) eda: clip bones mf = solved, ma = interpolated clip pose.
        for (std::size_t i = 0; i < nclip; ++i) {
            sol_mf_[i * 3] = sol_ma_[i * 3];
            sol_mf_[i * 3 + 1] = sol_ma_[i * 3 + 1];
            sol_mf_[i * 3 + 2] = sol_ma_[i * 3 + 2];
            sol_ma_[i * 3] = px[i];
            sol_ma_[i * 3 + 1] = py[i];
            sol_ma_[i * 3 + 2] = pz[i];
        }
        // [FIX stretched mesh — JS-faithful] `Al.ia()` (L582) runs exactly
        // ONE solver step per 60 Hz frame: `sk(); jE();`. The invented
        // 600-step warmup (PORT_AUDIT_RENDER §2.4 candidate 1; absent from
        // JS) is removed.
        // (b) sk: Verlet integrate (grav 0.4 = `xd.fDa`).
        // [FIX stretched mesh — cloth-only] JS `Al.sk` @296832 gates EVERY
        // body on `this.nk` (ragdoll-active, set ONLY by `Al.start` @296449
        // from `Lwb` @260135 = ragdoll start): `... && (this.nk || c.jy ||
        // ...) && c.sk(...)`. For a NORMAL animated fighter `nk` is false, so
        // only cloth bodies (`jy`) integrate; the clip-driven skeleton bodies
        // are NOT moved by the solver (they keep their `Te.eda` pose). The
        // old native integrated every non-fixed non-macro bone, dragging the
        // posed skeleton off the clip (the stretched mesh).
        constexpr float kGrav = 0.4f;
        for (std::size_t i = 0; i < n; ++i) {
            const Bone& b = bones[i];
            if (!b.cloth || b.fixed || b.is_macro) continue;  // cloth-only (JS nk=false)
            const std::size_t i3 = i * 3;
            float vx = sol_ma_[i3] - sol_mf_[i3];
            float vy = sol_ma_[i3 + 1] - sol_mf_[i3 + 1];
            float vz = sol_ma_[i3 + 2] - sol_mf_[i3 + 2];
            if (b.cloth) {
                const float k = 1.0f - b.attenuation;  // `Vc.bI` damp
                vx *= k;
                vy *= k;
                vz *= k;
            }
            sol_mf_[i3] = sol_ma_[i3];
            sol_mf_[i3 + 1] = sol_ma_[i3 + 1];
            sol_mf_[i3 + 2] = sol_ma_[i3 + 2];
            sol_ma_[i3] += vx;
            sol_ma_[i3 + 1] += vy + kGrav;
            sol_ma_[i3 + 2] += vz;
        }
        // (c) jE: 2 edge-relaxation passes (`yu.bFa` mass-weighted).
        constexpr int kEdgeIters = 2;  // `xd.jE` IterativeProcess
        for (int it = 0; it < kEdgeIters; ++it) {
            for (const EdgeDef& e : model_.edges) {
                const int bi1 = model_.bone_by_name(e.end1);
                const int bi2 = model_.bone_by_name(e.end2);
                if (bi1 < 0 || bi2 < 0) continue;
                const std::size_t i1 = static_cast<std::size_t>(bi1);
                const std::size_t i2 = static_cast<std::size_t>(bi2);
                if (i1 >= n || i2 >= n) continue;
                // [FIX stretched mesh — cloth-only] JS `Al.jE` @296592:
                // `d.cA = d.nh && !d.NG && (this.nk || d.jy || a && d.vc)`.
                // `nh` = the body participates, `NG` = immovable (Fixed /
                // MacroNode), `jy` = cloth. With `nk` false a body is a
                // relaxation endpoint ONLY when it is cloth. Non-cloth clip
                // bones are NOT relaxed (they stay exactly at the clip pose).
                const bool cA1 = bones[i1].cloth && !bones[i1].fixed && !bones[i1].is_macro;
                const bool cA2 = bones[i2].cloth && !bones[i2].fixed && !bones[i2].is_macro;
                if (!cA1 && !cA2) continue;
                const std::size_t u1 = i1 * 3;
                const std::size_t u2 = i2 * 3;
                const float ex = sol_ma_[u2] - sol_ma_[u1];
                const float ey = sol_ma_[u2 + 1] - sol_ma_[u1 + 1];
                const float ez = sol_ma_[u2 + 2] - sol_ma_[u1 + 2];
                const float dist = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (dist < 1e-9f) continue;
                const float r = e.length / dist;
                const float w1 = bones[i1].mass;
                const float w2 = bones[i2].mass;
                const float fk = (1.0f - r) / (w1 + w2);
                const float g1 = w1 * fk;
                const float g2 = w2 * fk;
                const float bx = sol_ma_[u1] * g1 + sol_ma_[u2] * g2;
                const float by = sol_ma_[u1 + 1] * g1 + sol_ma_[u2 + 1] * g2;
                const float bz = sol_ma_[u1 + 2] * g1 + sol_ma_[u2 + 2] * g2;
                if (cA1) {
                    sol_ma_[u1] = sol_ma_[u1] * r + bx;
                    sol_ma_[u1 + 1] = sol_ma_[u1 + 1] * r + by;
                    sol_ma_[u1 + 2] = sol_ma_[u1 + 2] * r + bz;
                }
                if (cA2) {
                    sol_ma_[u2] = sol_ma_[u2] * r + bx;
                    sol_ma_[u2 + 1] = sol_ma_[u2 + 1] * r + by;
                    sol_ma_[u2 + 2] = sol_ma_[u2 + 2] * r + bz;
                }
            }
        }
        // (d) Qja/seb: macros re-derived from the solved children.
        std::vector<std::uint8_t> visiting(n, 0);
        std::function<void(std::size_t)> compute_macro = [&](std::size_t idx) {
            if (visiting[idx]) {
                return;  // cycle guard
            }
            visiting[idx] = 1;
            const auto it2 = model_.macro_children.find(bones[idx].name);
            if (it2 != model_.macro_children.end()) {
                const MacroChildren& mc = it2->second;
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                for (std::size_t c = 0; c < mc.child_names.size(); ++c) {
                    const int ci = model_.bone_by_name(mc.child_names[c]);
                    if (ci < 0) continue;
                    const std::size_t u = static_cast<std::size_t>(ci);
                    if (u >= n) continue;
                    if (bones[u].is_macro && u >= nclip) {
                        compute_macro(u);
                    }
                    const float w = c < mc.weights.size() ? mc.weights[c] : 0.0f;
                    ax += sol_ma_[u * 3] * w;
                    ay += sol_ma_[u * 3 + 1] * w;
                    az += sol_ma_[u * 3 + 2] * w;
                }
                sol_ma_[idx * 3] = ax;
                sol_ma_[idx * 3 + 1] = ay;
                sol_ma_[idx * 3 + 2] = az;
                sol_mf_[idx * 3] = ax;
                sol_mf_[idx * 3 + 1] = ay;
                sol_mf_[idx * 3 + 2] = az;
            }
            visiting[idx] = 0;
        };
        for (std::size_t i = nclip; i < n; ++i) {
            if (bones[i].is_macro) {
                compute_macro(i);
            }
        }
        // The solved pose becomes this frame's positions.
        for (std::size_t i = 0; i < n; ++i) {
            px[i] = sol_ma_[i * 3];
            py[i] = sol_ma_[i * 3 + 1];
            pz[i] = sol_ma_[i * 3 + 2];
        }
    } else {
        // No solver state (defensive): fall back to the bind pose for the
        // non-clip bones (the pre-solver behavior minus the cloth anchor).
        for (std::size_t i = nclip; i < n; ++i) {
            if (bones[i].is_macro) {
                continue;
            }
            px[i] = bones[i].x;
            py[i] = bones[i].y;
            pz[i] = bones[i].z;
        }
    }

    // [FIX root motion — JS `Te.bYa` L564, invoked from `eda` L556 while
    // `Ua.zX != 0`] The move's <Rotation Angle> + <Position> pivot rotates
    // EVERY posed bone about the pivot by `Angle` degrees in the local
    // model (x,y) plane: `e = zX*0.017453292519943295; h=f.x-b.x;
    // f=f.y-b.y; f=(cos(e)*h-sin(e)*f+b.x, sin(e)*h+cos(e)*f+b.y, 0, 1)`.
    // The pivot `b = Ua.AX.nt(model.Fc)` (`ee.nt` L786): for
    // Object="Nodes" it is the posed node `Part`, with `c.x += ix*Wl`
    // (Wl = fighter scale = 1) and `c.y -= jx`. Only the shipped
    // Object="Nodes" case is handled; Object=Animation/Pivot/Wall are OPEN
    // (none of the 6 shipped <Rotation> moves use them). Applies to px/py
    // after the solver — the JS `bYa` runs in `eda` before `Al.ia`, so the
    // native cloth solver state (`sol_ma_`) is NOT rotated (OPEN, cloth-only
    // on rotation moves).
    if (current_move_ != nullptr && current_move_->rotation.has_rotation &&
        current_move_->rotation.angle != 0.0f &&
        current_move_->rotation.pos_object == "Nodes") {
        const int pit = model_.bone_by_name(current_move_->rotation.pos_part);
        if (pit >= 0 && static_cast<std::size_t>(pit) < n) {
            const std::size_t pu = static_cast<std::size_t>(pit);
            const float ox = px[pu] + current_move_->rotation.shift_x;  // `c.x += ix*Wl`
            const float oy = py[pu] - current_move_->rotation.shift_y;  // `c.y -= jx`
            const float rad = current_move_->rotation.angle * 0.017453292519943295f;
            const float cs = std::cos(rad);
            const float sn = std::sin(rad);
            for (std::size_t i = 0; i < n; ++i) {
                const float hx = px[i] - ox;
                const float hy = py[i] - oy;
                px[i] = cs * hx - sn * hy + ox;
                py[i] = sn * hx + cs * hy + oy;
            }
        }
    }

    // JS mirror swap (Te.Peb L560 -> Te.MYa/lwa -> Ua.Oeb L692): when
    // facing -1 the buffered clip frames have x negated (Qeb/Neb) and
    // paired _1/_2 bones are swapped ONLY when the stale world-x order
    // disagrees with the new buffer order (`lwa`: with facing -1 the
    // comparison operands are swapped, i.e. fire iff
    // world-order(a,b) != buffer-order(a,b)). Unconditional swapping
    // flips already-correct symmetric poses, so the port keeps the
    // previous sample's world x (`ma` analog) for the check.
    if (facing < 0 && !mirror_pairs_.empty()) {
        const bool have_prev = prev_x_.size() == n;
        for (auto& pr : mirror_pairs_) {
            int a = pr.first, b = pr.second;
            if (a < 0 || b < 0) continue;
            std::size_t ua = static_cast<std::size_t>(a);
            std::size_t ub = static_cast<std::size_t>(b);
            if (ua >= n || ub >= n) continue;
            if (ua >= nclip || ub >= nclip) continue;  // order check needs buffer pos
            bool disagree = true;
            if (have_prev) {
                const bool world_ge = prev_x_[ua] >= prev_x_[ub];
                const bool buf_ge = px[ua] >= px[ub];
                disagree = (world_ge != buf_ge);
            }
            if (disagree) {
                std::swap(px[ua], px[ub]);
                std::swap(py[ua], py[ub]);
                std::swap(pz[ua], pz[ub]);
            }
        }
    }

    // 3. World placement: the fighter's (x, y) anchors the model's COM at
//    (x, y) — matching the JS oracle where world_y is the COM (Dl.mea),
//    not the feet. The oracle trace shows world_y -93 while feet are at
//    ~5 (delta ~98): the COM is ~98 below the feet. The old native anchored
//    the feet at (x, y) (dy = y - ground, ground = max py), which placed
//    the COM ~98 above the oracle and left the world_y gap ~310. Facing
//    mirrors X (Te.Qeb).
    int com = model_.bone_by_name("COM");
    if (com < 0) {
        com = 0;
    }
    const std::size_t com_u = static_cast<std::size_t>(com);
    const float com_y = py[com_u];
    const float dy = y - com_y;
    const float f = facing < 0 ? -1.0f : 1.0f;
    for (std::size_t i = 0; i < n; ++i) {
        // [FIX Phase 4a] The facing mirror (JS `Te.Qeb` L550: `jc.Neb()`
        // flips the CLIP BUFFER x) applies to the LOCAL pose only: the
        // clip x is offset by the COM, mirrored, then the world x is added.
        // The old `(px+dx)*f` (or `px*f+dx`) misplaced the fighter when
        // facing -1 (the world x was mirrored off-screen / doubled).
        pos_[i * 2] = (px[i] - px[com_u]) * f + x;
        pos_[i * 2 + 1] = py[i] + dy;
        // Knockback ride: the impulse-split offsets displace the hit bones
        // on top of the clip pose (JS endpoint-body moves persist into the
        // next frame's `ma`).
        if (i < kb_.size()) {
            pos_[i * 2] += kb_[i].x;
            pos_[i * 2 + 1] += kb_[i].y;
        }
    }
    // JS `dv.ia` (L840) drops z when it skins the mesh (`Xg[a++] = d.x;
    // Xg[a++] = d.y`), so no per-bone depth is retained for drawing — the
    // triangles draw in XML document order (see build_vertices).
}

std::size_t Fighter::build_vertices(std::vector<float>& out) const {
    out.clear();
    const std::size_t ntri = model_.resolved_tris.size();
    out.reserve(ntri * 6);

    // JS `dv.ia` (L840) emits the triangles in XML DOCUMENT order: `zU` is
    // pushed in `dv.DXa` resolution order and `Xg` copies each node's
    // `ma.x/ma.y` (z dropped) — there is NO depth sort. The previous native
    // painter's sort by mean pose z re-ordered overlapping limbs and read as
    // "some triangles wrong" against the oracle (PORT_AUDIT_RENDER §3.3/§4.1,
    // ranked P0 #1): restore the document order exactly.
    for (const TriResolved& tri : model_.resolved_tris) {
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2 + 1]);
    }
    return out.size() / 2;
}

void Fighter::triangle_bbox(float& min_x, float& min_y, float& max_x,
                            float& max_y) const {
    min_x = min_y = max_x = max_y = 0.0f;
    bool first = true;
    for (const TriResolved& tri : model_.resolved_tris) {
        const int idx[3] = {tri.i1, tri.i2, tri.i3};
        for (int k = 0; k < 3; ++k) {
            const float vx = pos_[static_cast<std::size_t>(idx[k]) * 2];
            const float vy = pos_[static_cast<std::size_t>(idx[k]) * 2 + 1];
            if (first) {
                min_x = max_x = vx;
                min_y = max_y = vy;
                first = false;
            } else {
                min_x = std::min(min_x, vx);
                max_x = std::max(max_x, vx);
                min_y = std::min(min_y, vy);
                max_y = std::max(max_y, vy);
            }
        }
    }
}

} // namespace sf2::scene
