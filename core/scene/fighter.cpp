// Fighter: clip sampling (Te.eda, MODEL_FORMAT §2.2) + macro-node
// computation (Fl.seb, L797) + 2D triangle vertex build (dv.ia, L840).
//
// Phase 3.2b: move execution — input -> move selection -> clip playback,
// intervals, facing/mirror (JS study cited inline in fighter.hpp).

#include "scene/fighter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>

#include "anim_archive.hpp"
#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

void Fighter::set_model(const Model& model) {
    model_ = model;
    pos_.assign(model_.bones.size() * 2, 0.0f);
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

// JS `Te.ia` (L547-548): each 60 Hz update advances `Xh` (playback frame)
// and `fG` (physics frame); when `Xh+2 >= clipLen` the clip ends (`KNa()`
// + lS -> EStopAnimationEvent) and the fighter returns to idle.
// JS `vp` (L562-563) -> `rrb` (L552) -> `jc.c7a` (L691) recomputes the
// active intervals each frame.
void Fighter::advance(float dt) {
    (void)dt;  // fixed 60 Hz — one frame per call (JS 1/60 step)
    if (current_move_ == nullptr || current_clip_ == nullptr) {
        return;
    }

    // Interval update: active when max(start,qx) <= frame <= min(finish,Lj).
    active_intervals_.clear();
    for (const std::string& n : intervals_at(move_frame_)) {
        active_intervals_.insert(n);
    }

    // Root motion (JS `Al.ia` L582): the fighter's world position follows
    // the animation's COM displacement. The JS physics body `oa.Fe().ma.x`
    // is moved by the root-bone delta each frame; the native port applies
    // the COM delta of the clip (scaled by facing) to world_x.
    if (move_frame_ >= 0 && static_cast<std::size_t>(move_frame_) < current_clip_->frames.size()) {
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
            world_x_ += (com_now - com_prev) * (facing_ < 0 ? -1.0f : 1.0f);
        }
    }

    // Clip end (JS `Te.ia` L547-548: `Xh+2 >= len` -> KNa + lS + Sca).
    const int clip_len = static_cast<int>(current_clip_->frames.size());
    if (move_frame_ + 2 >= clip_len) {
        // Transition back to idle (JS: EStopAnimationEvent -> the fight
        // controller re-enters stance idle).
        current_move_ = nullptr;
        current_clip_ = nullptr;
        active_intervals_.clear();
        return;
    }

    ++move_frame_;  // JS `Xh++`
    sample_current();
}

void Fighter::sample_current() {
    if (current_clip_ != nullptr) {
        sample(*current_clip_, move_frame_, world_x_, world_y_, facing_);
    }
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
    const auto& bones = model_.bones;
    const auto& frame_bones = clip.frames[static_cast<std::size_t>(frame)].bones;
    const std::size_t nclip = std::min(frame_bones.size(), n);

    // 1. Per-bone positions: clip absolute world positions for bones 0..nclip-1,
    //    bind position for the rest (the game's eda leaves those at their
    //    current — here initial bind — position).
    std::vector<float> px(n), py(n), pz(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i < nclip) {
            px[i] = frame_bones[i].x;
            py[i] = frame_bones[i].y;
            pz[i] = frame_bones[i].z;
        } else {
            px[i] = bones[i].x;
            py[i] = bones[i].y;
            pz[i] = bones[i].z;
        }
    }

    // 2. MacroNodes not driven by the clip (index >= clip bone count) are the
    //    weighted average of their child bones' current positions (Fl.seb).
    //    Clip-driven macro nodes were explicitly placed (Ega) and skipped.
    //    Children may be skeleton bones or other macros; recursion is bounded
    //    by a per-call visited set (defensive against malformed cycles).
    std::vector<std::uint8_t> visiting(n, 0);
    std::function<void(std::size_t)> compute_macro = [&](std::size_t idx) {
        if (visiting[idx]) {
            return;  // cycle guard
        }
        visiting[idx] = 1;
        const auto& name = bones[idx].name;
        const auto it = model_.macro_children.find(name);
        if (it != model_.macro_children.end()) {
            const MacroChildren& mc = it->second;
            float ax = 0.0f, ay = 0.0f, az = 0.0f;
            for (std::size_t c = 0; c < mc.child_names.size(); ++c) {
                const int ci = model_.bone_by_name(mc.child_names[c]);
                if (ci < 0) {
                    continue;
                }
                const std::size_t u = static_cast<std::size_t>(ci);
                if (u >= n) {
                    continue;
                }
                if (bones[u].is_macro && u >= nclip) {
                    compute_macro(u);
                }
                const float w = c < mc.weights.size() ? mc.weights[c] : 0.0f;
                ax += px[u] * w;
                ay += py[u] * w;
                az += pz[u] * w;
            }
            px[idx] = ax;
            py[idx] = ay;
            pz[idx] = az;
        }
        visiting[idx] = 0;
    };
    for (std::size_t i = nclip; i < n; ++i) {
        if (bones[i].is_macro) {
            compute_macro(i);
        }
    }

    // 3. World placement: the COM bone anchors the fighter at (x, y)
    //    (wd.oL offsets all bones relative to the COM). Facing mirrors X.
    int com = model_.bone_by_name("COM");
    if (com < 0) {
        com = 0;
    }
    const std::size_t com_u = static_cast<std::size_t>(com);
    const float dx = x - px[com_u];
    const float dy = y - py[com_u];
    const float f = facing < 0 ? -1.0f : 1.0f;
    for (std::size_t i = 0; i < n; ++i) {
        pos_[i * 2] = (px[i] + dx) * f;
        pos_[i * 2 + 1] = py[i] + dy;
    }
}

std::size_t Fighter::build_vertices(std::vector<float>& out) const {
    out.clear();
    out.reserve(model_.resolved_tris.size() * 6);
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
