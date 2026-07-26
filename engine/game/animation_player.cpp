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
    // Priority check: if existing animation has higher priority, reject new one
    if (priority < priority_) {
        if (name != current_anim_) {
            std::printf("[ANIM] Rejected '%s' (priority %d) — '%s' has higher priority %d\n",
                        name.c_str(), priority, current_anim_.c_str(), priority_);
        }
        return false;
    }
    priority_ = priority;
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
    if (anim_loop_ && anim.frame_count > 0) frame_idx %= anim.frame_count;
    else if (frame_idx >= anim.frame_count) frame_idx = anim.frame_count - 1;
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
