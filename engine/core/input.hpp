#pragma once

#include <cstdint>
#include "math.hpp"

namespace resf2::core {

// Input state matching JS `Is` / `Yd` / `Es` classes.
// Tracks keyboard, mouse, touch, and gamepad.

enum class InputAction : uint8_t {
    // Directions (for move selection)
    Up, Down, Left, Right,
    UpForward, UpBack, DownForward, DownBack,
    Central,

    // Buttons
    Punch, Kick, Block, Special,
    Confirm, Back,

    COUNT
};

struct InputState {
    // Keyboard: USB HID key codes, 1=down this frame
    bool keys_down[256] = {};
    bool keys_just_pressed[256] = {};
    bool keys_just_released[256] = {};

    // Mouse / touch pointers
    static constexpr int kMaxPointers = 16;
    struct Pointer {
        int id = -1;
        float x = 0, y = 0;
        bool down = false;
        bool just_pressed = false;
        bool just_released = false;
    };
    Pointer pointers[kMaxPointers];
    int pointer_count = 0;

    // Gamepad
    struct Gamepad {
        bool connected = false;
        float left_stick_x = 0, left_stick_y = 0;
        float right_stick_x = 0, right_stick_y = 0;
        bool buttons[16] = {};
        bool buttons_just_pressed[16] = {};
    };
    Gamepad gamepad;

    // Derived: virtual directions/buttons for move selection
    // Updated by the input processor
    bool dir_down[9] = {};  // Up=0, Down=1, Left=2, Right=3, UpF=4, UpB=5, DownF=6, DownB=7, Central=8
    bool action_pressed[6] = {};  // Punch, Kick, Block, Special, Confirm, Back
    Vec2 pointer_pos;

    void clear_edges() {
        for (auto& k : keys_just_pressed) k = false;
        for (auto& k : keys_just_released) k = false;
        for (int i = 0; i < kMaxPointers; i++) {
            pointers[i].just_pressed = false;
            pointers[i].just_released = false;
        }
        for (auto& b : gamepad.buttons_just_pressed) b = false;
    }
};

} // namespace resf2::core
