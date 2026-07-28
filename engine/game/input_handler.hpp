#pragma once

#include <cstdint>
#include <string>

#include "engine/platform/platform.hpp"

namespace plat = resf2::platform;

// ---------- Input handler ----------
//
// Encapsulates input state and processing for the Game class.
// Processes keyboard/mouse input, move buffer management,
// and double-tap detection.

class InputHandler {
public:
    InputHandler() = default;

    // State getters
    int move_state() const { return move_state_; }
    void set_move_state(int s) { move_state_ = s; }
    bool double_step_fwd_requested() const { return double_step_fwd_requested_; }
    void set_double_step_fwd_requested(bool v) { double_step_fwd_requested_ = v; }
    void clear_double_step_fwd() { double_step_fwd_requested_ = false; }
    bool double_step_back_requested() const { return double_step_back_requested_; }
    void set_double_step_back_requested(bool v) { double_step_back_requested_ = v; }
    void clear_double_step_back() { double_step_back_requested_ = false; }
    // [ORIGINAL] Double-tap window: 300 ms.
    // From Model::step @ 0x10161ad0 — the model stores a timestamp at +0x84/0x88
    // and compares (now_ms - last_tap_ms) against 300. PORT_PLAN D8 confirms
    // the constant; JS does not expose it by name (obfuscated), but the binary
    // comparison at 0x1015c0a0 (step timing) uses 300 as the threshold.
    static constexpr uint32_t kDoubleTapWindowMs = 300;
    // [ORIGINAL] Step frame counting — original uses animation frame count,
    // not wall-clock time, to gate when a step can be interrupted.
    // From Model::step pipeline (0x10161ad0): animation must play N frames
    // before another input is accepted.
    uint32_t step_frames() const { return step_frames_; }
    void set_step_frames(uint32_t v) { step_frames_ = v; }
    void increment_step_frames() { ++step_frames_; }
    void reset_step_frames() { step_frames_ = 0; }
    // [HEURISTIC-TODO] 12 frames = 200ms at 60fps. Exact threshold unknown;
    // needs tracing from binary's movement entries or moves.xml animation data.
    static constexpr uint32_t kMinStepFrames = 12;
    uint32_t duck_play_time() const { return duck_play_time_; }
    void set_duck_play_time(uint32_t t) { duck_play_time_ = t; }

    // [ORIGINAL] Direction key latch removed.
    // The original binary (Model::step 0x10161ad0) has no direction latch;
    // combos are gated by CurrentAnimation conditions in moves.xml, not by
    // key history. fwd_held_ms_/back_held_ms_ were invented and caused
    // incorrect combo behavior (sticky controls for ~13 frames after key up).

    // Double-tap timing
    uint32_t last_fwd_tap_ms() const { return last_fwd_tap_ms_; }
    void set_last_fwd_tap_ms(uint32_t v) { last_fwd_tap_ms_ = v; }
    uint32_t last_back_tap_ms() const { return last_back_tap_ms_; }
    void set_last_back_tap_ms(uint32_t v) { last_back_tap_ms_ = v; }

    // Seen/last press timestamps (read-only)
    uint32_t last_kick_press_ms() const { return last_kick_press_ms_; }
    uint32_t last_punch_press_ms() const { return last_punch_press_ms_; }
    uint32_t last_punch_seen_ms() const { return last_punch_seen_ms_; }
    uint32_t last_kick_seen_ms() const { return last_kick_seen_ms_; }

    // Process all input for the current frame (called from host_update_gameplay)
    void process_input(
        plat::Platform& platform,
        float dt_sec, uint32_t dt_ms,
        bool facing_right,
        bool& punch_pressed, bool& kick_pressed,
        bool& key_forward, bool& key_back,
        bool& key_up, bool& key_down,
        bool& fwd_just_pressed, bool& back_just_pressed,
        uint32_t now_ms,
        bool in_attack,
        bool start_stance_playing,
        bool& step_min_played_out,
        float& dist_to_enemy,
        bool& past_attack_interval,
        int& in_attack_out,
        uint64_t total_frame_count
    );

private:

    // Movement state
    // 0=IDLE, 1=MOVING_LEFT, 2=MOVING_RIGHT, 10=special, 11=block
    int move_state_ = 0;

    // Timing state
    uint32_t step_frames_ = 0;
    uint32_t duck_play_time_ = 0;

    // [ORIGINAL] Double-tap detection
    uint32_t last_fwd_tap_ms_ = 0;
    uint32_t last_back_tap_ms_ = 0;
    bool double_step_fwd_requested_ = false;
    bool double_step_back_requested_ = false;
    uint32_t last_kick_press_ms_ = 0;
    uint32_t last_punch_press_ms_ = 0;
    uint32_t last_punch_seen_ms_ = 0;
    uint32_t last_kick_seen_ms_ = 0;
};
