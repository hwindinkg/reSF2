// engine/game/animation_player.cpp
#include "animation_player.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace resf2::game {

bool AnimationPlayer::play(
    const std::string& name,
    const std::unordered_map<std::string, AnimationData>& animations,
    float fps, bool loop, int priority
) {
    if (!animations.count(name)) return false;
    // Priority check: a higher-priority animation blocks a lower-priority one —
    // but only while it is still PLAYING. A non-looping animation that has run
    // past its last frame no longer holds the slot (finished_ is set in
    // update()), otherwise nothing could ever follow it.
    //
    // Without that release the fighter froze on the final frame of whatever he
    // last did: the intro stance_2 plays at priority 3, so the stance_idle that
    // is queued when it ends (priority 0) was rejected forever, and the same
    // happened after every attack. Measured with --input-script + --dump-state:
    // anim stayed 'stance_2' from frame 1 to 300 and then 'high_punch' with no
    // idle in between.
    if (!finished_ && priority < priority_) {
        if (name != current_anim_) {
            std::printf("[ANIM] Rejected '%s' (priority %d) — '%s' has higher priority %d\n",
                        name.c_str(), priority, current_anim_.c_str(), priority_);
        }
        return false;
    }
    priority_ = priority;
    finished_ = false;
    current_anim_ = name;
    anim_time_ = 0.0f;
    anim_fps_ = fps;
    anim_loop_ = loop;
    anim_anchor_set_ = false;
    prev_npivot_set_ = false;
    prev_frame_idx_ = -1;
    committed_root_x_ = 0.0f;
    prev_root_offset_x_ = 0.0f;
    prev_root_offset_y_ = 0.0f;
    prev_align_rel_set_ = false;
    return true;
}

void AnimationPlayer::play_for(const AnimationData&, float, float) {}

void AnimationPlayer::update(
    uint32_t dt_ms,
    const std::unordered_map<std::string, AnimationData>& animations,
    const std::vector<std::string>& ordered_node_names
) {
    anim_node_pos_.clear();
    anim_root_dx_ = 0.0f;
    anim_root_dy_ = 0.0f;
    ++total_frame_count_;

    auto it = animations.find(current_anim_);
    if (it == animations.end()) {
        if (!animations.empty()) {
            current_anim_ = animations.begin()->first;
            it = animations.find(current_anim_);
        } else return;
    }
    auto& anim = it->second;
    if (anim.frame_count == 0 || ordered_node_names.empty()) return;

    int npivot_idx = -1;
    for (int i = 0; i < (int)ordered_node_names.size(); ++i)
        if (ordered_node_names[i] == "NPivot") { npivot_idx = i; break; }
    if (npivot_idx < 0) return;

    if (!anim_anchor_set_) {
        float px,py,pz;
        if (anim.get_node_pos(0, npivot_idx, px,py,pz)) {
            anim_root_anchor_x_ = px; anim_root_anchor_y_ = py; anim_anchor_set_ = true;
        }
    }

    anim_time_ += dt_ms / 1000.0f;
    float frame_f = anim_time_ * anim_fps_;
    int frame_idx = (int)frame_f;
    if (anim_loop_ && anim.frame_count > 0) {
        frame_idx %= anim.frame_count;
    } else if (frame_idx >= anim.frame_count) {
        frame_idx = anim.frame_count - 1;
        // Ran past the last frame: release the priority slot so whatever the
        // game queues next (an idle, a follow-up move) can take over.
        finished_ = true;
    }
    if (frame_idx < 0) frame_idx = 0;

    int next_idx = (anim.frame_count > 0 && frame_idx < anim.frame_count-1) ? frame_idx+1 : frame_idx;
    // [FIX] Don't interpolate last frame with frame 0 — prevents NPivot pull-back
    // toward start position during loop wrap. Committed root motion below handles
    // the displacement commit at the wrap point instead.
    float alpha = (next_idx != frame_idx) ? (frame_f - (int)frame_f) : 0.0f;
    if (alpha < 0) alpha = 0; if (alpha > 1) alpha = 1;

    float npx0,npy0,npz0,npx1,npy1,npz1;
    if (!anim.get_node_pos(frame_idx, npivot_idx, npx0,npy0,npz0)) return;
    anim.get_node_pos(next_idx, npivot_idx, npx1,npy1,npz1);
    float npivot_x = npx0 + (npx1-npx0)*alpha;
    float npivot_y = npy0 + (npy1-npy0)*alpha;
    anim_npivot_bin_y_ = npivot_y;

    // === ROOT MOTION (committed displacement pattern) ===
    // Absolute NPivot offset from the animation's start anchor.
    float absolute_offset_x = npivot_x - anim_root_anchor_x_;
    float absolute_offset_y = npivot_y - anim_root_anchor_y_;

    // Wrap detection: the animation looped from the last frame back to frame 0.
    // Commit the completed cycle's total displacement so accumulated root motion
    // doesn't snap back when the animation restarts.
    if (prev_frame_idx_ >= 0 && frame_idx == 0 && prev_frame_idx_ > frame_idx) {
        committed_root_x_ += absolute_offset_x;

        // Re-anchor so absolute_offset restarts from 0 for the new cycle.
        anim_root_anchor_x_ = npivot_x;
        anim_root_anchor_y_ = npivot_y;
        absolute_offset_x = 0.0f;
        absolute_offset_y = 0.0f;
        prev_root_offset_x_ = 0.0f;
        prev_root_offset_y_ = 0.0f;
    }
    prev_frame_idx_ = frame_idx;

    // Per-frame root motion delta = CHANGE in absolute offset since last frame.
    // This gives smooth per-frame displacement without snap-back at wrap.
    anim_root_dx_ = absolute_offset_x - prev_root_offset_x_;

    // [ORIGINAL] A move with an <Align> keeps its anchor node planted.
    //
    // Model::alignAnimation @ 0x101661d0 places the animation so the anchor
    // sits on the model's current node; the fighter's position then has to
    // hold it there, not drift out from under it. Measured in the .bin data,
    // the drift over one playthrough is:
    //
    //     high_punch    NPivot +35.00   NHeel_2 +35.00   (whole body offset)
    //     front_kick    NPivot +156.00  NHeel_2 +51.54
    //     step_forward  NPivot +66.00   NHeel_2 +66.00   (no <Align>)
    //     stance_idle   NPivot   0.00   NHeel_2   0.00
    //
    // Following NPivot blindly handed the fighter the anchor's drift as free
    // travel: every punch moved him 35 units and kept them, so twelve punches
    // crossed the room — measured, -442.6 to -20.5. Pinning the anchor makes
    // the body move around the planted foot instead, which is what an attack
    // looks like. Steps declare no <Align> and are untouched.
    // [M2] Whole-body-translate moves (rolls, dash, flips) must play their
    // authored NPivot root motion RAW: their anchor travels with the body, so
    // per-frame pinning would cancel the locomotion (back_roll measured at
    // 13 px of its authored -350). is_root_motion_travel_anim — see
    // animation_player.hpp.
    if (!align_anchor_.empty() && !is_root_motion_travel_anim(current_anim_)) {
        int anchor_idx = -1;
        for (int i = 0; i < (int)ordered_node_names.size(); ++i)
            if (ordered_node_names[i] == align_anchor_) { anchor_idx = i; break; }
        float ax0, ay0, az0, ax1, ay1, az1;
        if (anchor_idx >= 0 && anim.get_node_pos(frame_idx, anchor_idx, ax0, ay0, az0)) {
            if (!anim.get_node_pos(next_idx, anchor_idx, ax1, ay1, az1)) {
                ax1 = ax0;
            }
            // The anchor's X relative to NPivot: world X of the node is
            // player_pos_x + facing * rel_x, so holding the node still means
            // moving player_pos_x by -d(rel_x).
            const float rel_x = (ax0 + (ax1 - ax0) * alpha) - npivot_x;
            anim_root_dx_ = prev_align_rel_set_ ? -(rel_x - prev_align_rel_x_) : 0.0f;
            prev_align_rel_x_ = rel_x;
            prev_align_rel_set_ = true;
        }
    }
    anim_root_dy_ = absolute_offset_y - prev_root_offset_y_;
    prev_root_offset_x_ = absolute_offset_x;
    prev_root_offset_y_ = absolute_offset_y;

    // Accumulate Y root motion for jump/vertical displacement.
    jump_y_offset_ += anim_root_dy_;

    // Y visual adjustment (smoothed NPivot Y offset for rendering foot placement)
    float target_y_adjust = stance_npivot_y_ - npivot_y;
    if (target_y_adjust < -50) target_y_adjust = -50;
    if (target_y_adjust > 50) target_y_adjust = 50;
    y_adjust_smoothed_ += (target_y_adjust - y_adjust_smoothed_) * 0.3f;

    for (int ni = 0; ni < (int)ordered_node_names.size(); ++ni) {
        float nx,ny,nz, nx1b=0,ny1b=0,nz1b=0;
        if (anim.get_node_pos(frame_idx, ni, nx,ny,nz)) {
            anim.get_node_pos(next_idx, ni, nx1b,ny1b,nz1b);
            float ix = nx + (nx1b-nx)*alpha;
            float iy = ny + (ny1b-ny)*alpha;
            anim_node_pos_[ordered_node_names[ni]] = {ix-npivot_x, iy-npivot_y};
        }
    }
}

bool AnimationPlayer::get_node_pos(const std::string& name, float& x, float& y) const {
    auto it = anim_node_pos_.find(name);
    if (it == anim_node_pos_.end()) return false;
    x = it->second.first; y = it->second.second;
    return true;
}
void AnimationPlayer::set_node_pos(const std::string& name, float x, float y) { anim_node_pos_[name] = {x,y}; }
void AnimationPlayer::clear_node_positions() { anim_node_pos_.clear(); }

} // namespace
