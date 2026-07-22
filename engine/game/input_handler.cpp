// engine/game/input_handler.cpp
//
// InputHandler implementation — input state, double-tap detection,
// move buffer management.

#include "input_handler.hpp"
#include "game.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

extern void debug_log(const char* fmt, ...);

void InputHandler::process_input(
    plat::Platform& platform,
    float /*dt_sec*/, uint32_t dt_ms,
    bool facing_right,
    bool& punch_pressed, bool& kick_pressed,
    bool& key_forward, bool& key_back,
    bool& key_up, bool& key_down,
    bool& fwd_just_pressed, bool& back_just_pressed,
    uint32_t now_ms,
    bool /*in_attack*/,
    bool /*start_stance_playing*/,
    bool& step_min_played_out,
    float& /*dist_to_enemy*/,
    bool& /*past_attack_interval*/,
    int& /*in_attack_out*/,
    uint64_t /*total_frame_count*/
) {
    const auto& input = platform.input();

    // Determine relative directions from absolute keys
    key_up = input.keys_down[(size_t)plat::Key::W] ||
             input.keys_down[(size_t)plat::Key::ArrowUp];
    key_down = input.keys_down[(size_t)plat::Key::S] ||
               input.keys_down[(size_t)plat::Key::ArrowDown];
    bool key_left = input.keys_down[(size_t)plat::Key::A] ||
                    input.keys_down[(size_t)plat::Key::ArrowLeft];
    bool key_right = input.keys_down[(size_t)plat::Key::D] ||
                     input.keys_down[(size_t)plat::Key::ArrowRight];

    // Convert absolute directions to relative (Forward/Back)
    key_forward = facing_right ? key_right : key_left;
    key_back = facing_right ? key_left : key_right;

    // [ORIGINAL] Double-tap detection for DoubleStep/BackHandflip.
    fwd_just_pressed = facing_right ?
        (input.keys_just_pressed[(size_t)plat::Key::D] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowRight]) :
        (input.keys_just_pressed[(size_t)plat::Key::A] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowLeft]);
    back_just_pressed = facing_right ?
        (input.keys_just_pressed[(size_t)plat::Key::A] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowLeft]) :
        (input.keys_just_pressed[(size_t)plat::Key::D] ||
         input.keys_just_pressed[(size_t)plat::Key::ArrowRight]);

    // [ORIGINAL] Double-tap: use move_state_ for direction.
    if (fwd_just_pressed) {
        if (now_ms - last_fwd_tap_ms_ < 300 && move_state_ == 2) {
            double_step_fwd_requested_ = true;
        }
        last_fwd_tap_ms_ = now_ms;
    }
    if (back_just_pressed) {
        if (now_ms - last_back_tap_ms_ < 300 && move_state_ == 1) {
            double_step_back_requested_ = true;
        }
        last_back_tap_ms_ = now_ms;
    }

    // O/P keys for combat
    punch_pressed = input.keys_just_pressed[(size_t)plat::Key::O];
    kick_pressed = input.keys_just_pressed[(size_t)plat::Key::P];
    // Also keep Space/K as fallback for testing
    if (input.keys_just_pressed[(size_t)plat::Key::Space]) punch_pressed = true;
    if (input.keys_just_pressed[(size_t)plat::Key::K]) kick_pressed = true;

    // Step play time tracking
    if (move_state_ == 1 || move_state_ == 2) {
        step_play_time_ += dt_ms;
    } else {
        step_play_time_ = 0;
    }
    step_min_played_out = step_play_time_ >= 400;

    // [HEURISTIC-TODO] fwd_held_ms_/back_held_ms_ latch — same as game.hpp:1316.
    // Invented 200ms latch for direction keys. Original engine has no latch;
    // combos are gated by CurrentAnimation conditions. See game.hpp for details.
    if (key_forward) fwd_held_ms_ = 200;
    else if (fwd_held_ms_ > 0) fwd_held_ms_ -= (int)dt_ms;
    if (key_back) back_held_ms_ = 200;
    else if (back_held_ms_ > 0) back_held_ms_ -= (int)dt_ms;

    // Duck play time tracking
    if (key_down) duck_play_time_ += dt_ms;
    else duck_play_time_ = 0;
}
