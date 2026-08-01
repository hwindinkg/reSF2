#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "types.hpp"

namespace resf2::game {

// [M2] Whole-body-translate moves (rolls, dash, flips): the authored NPivot
// trajectory IS the locomotion — measured from the .bin data:
//     forward_roll +404 (26f), back_roll -350 (31f), back_handflip -346 (34f),
//     double_step_forward +220 (17f), front_flip +366 (35f), back_flip -335 (28f).
// Their <Align> anchor travels WITH the body (anchor-relative offset stays
// constant), so per-frame anchor pinning would cancel the motion entirely
// (measured: back_roll pinned to 13 px — the soak's "~10 px" crawl) and the
// one-shot placement would snap the fighter at roll start (forward_roll
// snapped -38 px from idle). The original plants the anchor once at the
// transition and then follows the animation data. These moves must play RAW.
// jump is NOT here: it is a vertical hop whose X wobble is noise (zeroed in
// game.cpp, M3); jump_away is authored mirrored, needing its pinning.
inline bool is_root_motion_travel_anim(const std::string& name) {
    return name == "forward_roll" || name == "back_roll" ||
           name == "back_handflip" || name == "double_step_forward" ||
           name == "front_flip" || name == "back_flip";
}

// ---------- Animation player ----------
//
// Encapsulates animation playback state and interpolation logic.
// Handles .bin keyframe loading, frame interpolation, root motion.

class AnimationPlayer {
public:
    AnimationPlayer() = default;

    // Getters
    const std::string& current_anim() const { return current_anim_; }
    float anim_time() const { return anim_time_; }
    float anim_fps() const { return anim_fps_; }
    bool anim_loop() const { return anim_loop_; }
    int prev_frame_idx() const { return prev_frame_idx_; }
    float prev_npivot_x() const { return prev_npivot_x_; }
    bool prev_npivot_set() const { return prev_npivot_set_; }
    float prev_npivot_y() const { return prev_npivot_y_; }
    bool prev_npivot_y_set() const { return prev_npivot_y_set_; }
    float anim_root_dx() const { return anim_root_dx_; }
    float anim_root_dy() const { return anim_root_dy_; }
    float anim_root_anchor_x() const { return anim_root_anchor_x_; }
    float anim_root_anchor_y() const { return anim_root_anchor_y_; }
    bool anim_anchor_set() const { return anim_anchor_set_; }
    float jump_y_offset() const { return jump_y_offset_; }
    float prev_root_offset() const { return prev_root_offset_; }
    float committed_root_x() const { return committed_root_x_; }
    float prev_root_offset_x() const { return prev_root_offset_x_; }
    float prev_root_offset_y() const { return prev_root_offset_y_; }
    float step_start_player_x() const { return step_start_player_x_; }
    bool anim_facing_right() const { return anim_facing_right_; }
    float y_adjust_smoothed() const { return y_adjust_smoothed_; }
    float stance_npivot_y() const { return stance_npivot_y_; }
    float anim_npivot_bin_y() const { return anim_npivot_bin_y_; }
    uint64_t total_frame_count() const { return total_frame_count_; }
    const std::string& last_logged_anim() const { return last_logged_anim_; }
    const std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos() const { return anim_node_pos_; }

    // Mutable accessors (for Game reference aliasing — same pattern as Combat)
    std::string& mutable_current_anim() { return current_anim_; }
    float& mutable_anim_time() { return anim_time_; }
    float& mutable_anim_speed() { return anim_speed_; }
    bool& mutable_anim_loop() { return anim_loop_; }
    float& mutable_anim_fps() { return anim_fps_; }
    std::unordered_map<std::string, std::pair<float, float>>& mutable_anim_node_pos() { return anim_node_pos_; }
    float& mutable_anim_root_dx() { return anim_root_dx_; }
    float& mutable_anim_root_dy() { return anim_root_dy_; }
    float& mutable_anim_root_anchor_x() { return anim_root_anchor_x_; }
    float& mutable_anim_root_anchor_y() { return anim_root_anchor_y_; }
    bool& mutable_anim_anchor_set() { return anim_anchor_set_; }
    float& mutable_prev_npivot_x() { return prev_npivot_x_; }
    bool& mutable_prev_npivot_set() { return prev_npivot_set_; }
    float& mutable_prev_npivot_y() { return prev_npivot_y_; }
    bool& mutable_prev_npivot_y_set() { return prev_npivot_y_set_; }
    int& mutable_prev_frame_idx() { return prev_frame_idx_; }
    float& mutable_jump_y_offset() { return jump_y_offset_; }
    float& mutable_prev_root_offset() { return prev_root_offset_; }
    float& mutable_committed_root_x() { return committed_root_x_; }
    float& mutable_prev_root_offset_x() { return prev_root_offset_x_; }
    float& mutable_prev_root_offset_y() { return prev_root_offset_y_; }
    float& mutable_step_start_player_x() { return step_start_player_x_; }
    bool& mutable_anim_facing_right() { return anim_facing_right_; }
    float& mutable_y_adjust_smoothed() { return y_adjust_smoothed_; }
    float& mutable_stance_npivot_y() { return stance_npivot_y_; }
    float& mutable_anim_npivot_bin_y() { return anim_npivot_bin_y_; }
    uint64_t& mutable_total_frame_count() { return total_frame_count_; }
    std::string& mutable_last_logged_anim() { return last_logged_anim_; }

    void set_current_anim(const std::string& a) { current_anim_ = a; }
    void set_anim_time(float t) { anim_time_ = t; }
    void set_anim_fps(float f) { anim_fps_ = f; }
    void set_anim_loop(bool l) { anim_loop_ = l; }
    void set_anim_root_dx(float dx) { anim_root_dx_ = dx; }
    void set_anim_root_dy(float dy) { anim_root_dy_ = dy; }
    void set_anim_root_anchor_x(float x) { anim_root_anchor_x_ = x; }
    void set_anim_root_anchor_y(float y) { anim_root_anchor_y_ = y; }
    void set_anim_anchor_set(bool s) { anim_anchor_set_ = s; }
    void set_prev_npivot_x(float x) { prev_npivot_x_ = x; }
    void set_prev_npivot_set(bool s) { prev_npivot_set_ = s; }
    void set_prev_npivot_y(float y) { prev_npivot_y_ = y; }
    void set_prev_npivot_y_set(bool s) { prev_npivot_y_set_ = s; }
    void set_prev_frame_idx(int i) { prev_frame_idx_ = i; }
    void set_jump_y_offset(float o) { jump_y_offset_ = o; }
    void set_prev_root_offset(float o) { prev_root_offset_ = o; }
    void set_committed_root_x(float v) { committed_root_x_ = v; }
    void set_prev_root_offset_x(float v) { prev_root_offset_x_ = v; }
    void set_prev_root_offset_y(float v) { prev_root_offset_y_ = v; }
    void set_step_start_player_x(float x) { step_start_player_x_ = x; }
    void set_anim_facing_right(bool r) { anim_facing_right_ = r; }
    void set_y_adjust_smoothed(float y) { y_adjust_smoothed_ = y; }
    void set_stance_npivot_y(float y) { stance_npivot_y_ = y; }
    void set_anim_npivot_bin_y(float y) { anim_npivot_bin_y_ = y; }

    // Priority
    int anim_priority() const { return priority_; }
    // True once a non-looping animation has run past its last frame. While it
    // is true the animation no longer holds its priority slot.
    bool anim_finished() const { return finished_; }
    void clear_anim_finished() { finished_ = false; }
    void set_priority(int p) { priority_ = p; }
    int& mutable_priority() { return priority_; }

    // [ORIGINAL] The <Align> anchor of the move being played, or "" when the
    // move declares none. An aligned move keeps that node pinned; an
    // unaligned one (the steps) carries its motion in the animation data.
    void set_align_anchor(const std::string& node) { align_anchor_ = node; }
    const std::string& align_anchor() const { return align_anchor_; }
    // Seed the pinning with the pose the one-shot placement was computed for,
    // so the first frame's delta is measured against it rather than skipped.
    void seed_align_rel(float rel_x) {
        prev_align_rel_x_ = rel_x;
        prev_align_rel_set_ = true;
    }

    // Play a named animation (looked up in the animations map)
    bool play(
        const std::string& name,
        const std::unordered_map<std::string, AnimationData>& animations,
        float fps,
        bool loop,
        int priority = 0  // priority level; higher = more important
    );

    // Play an arbitrary animation for a specific duration
    void play_for(const AnimationData& anim, float duration, float fps);

    // Main update: advance animation time by dt_ms, interpolate frames,
    // compute root motion displacement, populate anim_node_pos_
    void update(
        uint32_t dt_ms,
        const std::unordered_map<std::string, AnimationData>& animations,
        const std::vector<std::string>& ordered_node_names
    );

    // Get node position from animation
    bool get_node_pos(const std::string& name, float& x, float& y) const;
    void set_node_pos(const std::string& name, float x, float y);
    void clear_node_positions();

private:
    // Current animation state
    std::string current_anim_;
    float anim_time_ = 0.0f;
    float anim_speed_ = 30.0f;
    bool anim_loop_ = true;
    float anim_fps_ = 20.0f;
    int priority_ = 0;
    // A non-looping animation stops holding its priority slot once it has run
    // past its last frame. See play() / update().
    bool finished_ = false;

    // Interpolated node positions (name -> (x, y))
    std::unordered_map<std::string, std::pair<float, float>> anim_node_pos_;

    // Root motion
    float anim_root_dx_ = 0.0f;
    float anim_root_dy_ = 0.0f;
    float anim_root_anchor_x_ = 0.0f;
    float anim_root_anchor_y_ = 0.0f;
    bool anim_anchor_set_ = false;
    float prev_npivot_x_ = 0.0f;
    bool prev_npivot_set_ = false;
    float prev_npivot_y_ = 0.0f;
    bool prev_npivot_y_set_ = false;
    int prev_frame_idx_ = -1;
    float jump_y_offset_ = 0.0f;
    float prev_root_offset_ = 0.0f;
    float committed_root_x_ = 0.0f;       // accumulated X displacement from completed animation cycles
    float prev_root_offset_x_ = 0.0f;     // previous frame's absolute offset X (for delta computation)
    float prev_root_offset_y_ = 0.0f;     // previous frame's absolute offset Y (for delta computation)
    float step_start_player_x_ = 0.0f;
    bool anim_facing_right_ = true;
    float y_adjust_smoothed_ = 4.0f;
    float stance_npivot_y_ = 106.0f;
    float anim_npivot_bin_y_ = 106.0f;
    std::string align_anchor_;        // node the current move pins, or empty
    float prev_align_rel_x_ = 0.0f;   // its X relative to NPivot, last frame
    bool prev_align_rel_set_ = false;
    uint64_t total_frame_count_ = 0;
    std::string last_logged_anim_;
};

} // namespace resf2::game
