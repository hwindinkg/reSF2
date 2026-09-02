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
#include <limits>
#include <set>

#include "anim_archive.hpp"
#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

void Fighter::set_model(const Model& model) {
    model_ = model;
    pos_.assign(model_.bones.size() * 2, 0.0f);

    // [FIX Phase 4a — stretched cloth] The merged model appends the body
    // cloth nodes (BODY-Node*) and the head nodes AFTER the skeleton; the
    // animation clips drive ONLY the 67 skeleton bones (clip bone i =
    // merged bone i). The remaining bones keep their model-space bind
    // position (x≈0, y≈-90..-160) while the clip pose sits at x≈-360 —
    // the cloth mesh (body tris mixing cloth + skeleton) exploded ~360
    // world units. The game's ragdoll solver (Al) keeps the cloth attached
    // to the skeleton; without it the native anchors each non-clip bone to
    // its NEAREST clip-driven bone (bind-space) and carries the bind
    // offset along with that bone's clip position.
    nearest_clip_.assign(model_.bones.size(), 0);
    for (std::size_t i = 0; i < model_.bones.size(); ++i) {
        if (i < 67) {  // the skeleton bones (clip-driven; see anim clip bone count)
            nearest_clip_[i] = static_cast<int>(i);
            continue;
        }
        // [FIX Phase 4a — stretched cloth] Anchor each non-clip bone to its
        // ragdoll-connected skeleton bone (the model's <Edges> End1/End2 —
        // the game's Al solver keeps the cloth at the edge endpoints). This
        // is correct where the bind-nearest fails: the HEAD-Node* cloth
        // nodes' bind positions sit at the FIST height (the weapon
        // placeholder rig), so the bind-nearest picked the hand/weapon bones
        // and the head mesh exploded. The edges connect the head nodes to
        // the head macros (which follow the skeleton head bones).
        int ref = -1;
        for (const EdgeDef& e : model_.edges) {
            const int ei = model_.bone_by_name(e.end1);
            const int ej = model_.bone_by_name(e.end2);
            const int other = (ei == static_cast<int>(i)) ? ej : (ej == static_cast<int>(i) ? ei : -1);
            if (other < 0) continue;
            if (other < 67) {  // the clip-driven skeleton bone
                ref = other;
                break;
            }
        }
        if (ref < 0) {
            // [FIX Phase 4a] The HEAD-Node* cloth nodes' edges connect only
            // to each other + the head macros (no direct skeleton edge), so
            // the edge lookup above fails and the bind-nearest picks the
            // fist/weapon bones (the head nodes' bind positions sit at the
            // fist height). Anchor the head nodes to the skeleton HEAD bone
            // cluster (the head macros' children) instead.
            const std::string& nm = model_.bones[i].name;
            if (nm.rfind("HEAD-Node", 0) == 0) {
                static const char* kHeadBones[4] = {"NTop", "NHeadF", "NHeadS_1", "NHeadS_2"};
                int hbest = 0;
                float hd = std::numeric_limits<float>::max();
                for (int hb = 0; hb < 4; ++hb) {
                    const int hj = model_.bone_by_name(kHeadBones[hb]);
                    if (hj < 0 || hj >= 67) continue;
                    const float dx = model_.bones[i].x - model_.bones[static_cast<std::size_t>(hj)].x;
                    const float dy = model_.bones[i].y - model_.bones[static_cast<std::size_t>(hj)].y;
                    const float dz = model_.bones[i].z - model_.bones[static_cast<std::size_t>(hj)].z;
                    const float d = dx * dx + dy * dy + dz * dz;
                    if (d < hd) {
                        hd = d;
                        hbest = hj;
                    }
                }
                ref = hbest;
            }
        }
        if (ref < 0) {
            // Fallback: the bind-space nearest non-weapon clip-driven bone.
            int best = 0;
            float best_d = std::numeric_limits<float>::max();
            for (std::size_t j = 0; j < 67; ++j) {
                if (j >= 19 && j <= 26) continue;  // the weapon placeholder rig
                const float dx = model_.bones[i].x - model_.bones[j].x;
                const float dy = model_.bones[i].y - model_.bones[j].y;
                const float dz = model_.bones[i].z - model_.bones[j].z;
                const float d = dx * dx + dy * dy + dz * dz;
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(j);
                }
            }
            ref = best;
        }
        nearest_clip_[i] = ref;
    }

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
    (void)dt;  // fixed 60 Hz — one frame per call (JS 1/60 step)
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

    // Root motion (JS `Al.ia` L582 + Te.eda j8): the old native added the
    // full clip-frame COM delta every 60 Hz tick — for sub=3 that counted
    // the delta 3x per clip frame (2.4x/3.6x over-application). Fix: distribute
    // the delta over subframes (single-application per clip frame).
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

    // [FIX Phase 4a — stretched cloth] Non-macro bones not driven by the
    // clip (the body/head cloth nodes) keep their BIND position in the
    // model space (x≈0), while the clip-driven skeleton sits at x≈-360 —
    // the body mesh exploded ~360 world units. Anchor each cloth node at
    // its bind offset from the nearest clip-driven bone (precomputed in
    // set_model): the cloth follows the skeleton's motion.
    // [FIX Phase 4d — cloth side under mirror] The bind offset
    // (bones[i].x - bones[r].x) is in the AUTHORED (right-facing) space.
    // The clip pose is authored once and mirrored by facing at the final
    // projection (below, `* f`). If the cloth offset is added UNMIRRORED
    // to the clip anchor and then the whole pose is mirrored, the cloth
    // nodes CROSS sides: the left-leg cloth (BODY-Node16) lands on the
    // fighter's right and the right-leg cloth (BODY-Node12) on the left,
    // so the body-mesh legs overlap into a thin column (the oracle's
    // ragdoll solver keeps each cloth node on its own side). Mirror the
    // offset by the facing here so the two mirror operations cancel and
    // the cloth node stays on the correct side of its anchor.
    for (std::size_t i = nclip; i < n; ++i) {
        if (bones[i].is_macro) {
            continue;  // macros are computed from the (anchored) children
        }
        const int ref = nearest_clip_[i];
        if (ref < 0 || static_cast<std::size_t>(ref) >= nclip) {
            continue;
        }
        const std::size_t r = static_cast<std::size_t>(ref);
        const float f_cloth = facing < 0 ? -1.0f : 1.0f;
        px[i] = (bones[i].x - bones[r].x) * f_cloth + px[r];
        py[i] = bones[i].y - bones[r].y + py[r];
        pz[i] = bones[i].z - bones[r].z + pz[r];
    }

    // JS mirror swap (Te.Peb L560 -> Ua.Oeb L692): when facing -1 the
    // buffered clip frames have x negated (Qeb/Neb) and paired _1/_2 bones
    // swapped. Native mirrors at projection, but also swaps paired bones so
    // left stays left (otherwise shoulder/leg crossing ~100px, bone mean 140+).
    if (facing < 0) {
        for (auto& pr : mirror_pairs_) {
            int a = pr.first, b = pr.second;
            if (a < 0 || b < 0) continue;
            std::size_t ua = static_cast<std::size_t>(a);
            std::size_t ub = static_cast<std::size_t>(b);
            if (ua >= n || ub >= n) continue;
            std::swap(px[ua], px[ub]);
            std::swap(py[ua], py[ub]);
            std::swap(pz[ua], pz[ub]);
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
