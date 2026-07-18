#include "input_system.hpp"
#include <cstring>

namespace resf2::core {

void InputProcessor::poll(platform::Platform& plat, InputState& state) {
    const auto& pinput = plat.input();

    // Clear edges (just_pressed/just_released) that persist from last frame
    // We need to keep the edges until polled, so don't clear them here.
    // Instead, clear after use in the game loop.

    // Copy keyboard
    for (int i = 0; i < 256; i++) {
        state.keys_down[i] = pinput.keys_down[i];
        state.keys_just_pressed[i] = pinput.keys_just_pressed[i];
        state.keys_just_released[i] = pinput.keys_just_released[i];
    }

    // Copy pointers
    state.pointer_count = 0;
    for (int i = 0; i < InputState::kMaxPointers; i++) {
        auto& src = pinput.pointers[i];
        auto& dst = state.pointers[i];
        dst.id = src.id;
        dst.x = src.x;
        dst.y = src.y;
        dst.down = src.pressed;
        dst.just_pressed = src.just_pressed;
        dst.just_released = src.just_released;
        if (src.id >= 0) state.pointer_count++;
        if (src.just_pressed || src.pressed) {
            state.pointer_pos = Vec2(src.x, src.y);
        }
    }

    update_directions(state);
}

void InputProcessor::update_directions(InputState& state) {
    // Reset directions
    memset(state.dir_down, 0, sizeof(state.dir_down));
    memset(state.action_pressed, 0, sizeof(state.action_pressed));

    // Map keyboard to directions (facing-relative handled later)
    bool up = state.keys_down[(int)platform::Key::W] ||
              state.keys_down[(int)platform::Key::ArrowUp];
    bool down = state.keys_down[(int)platform::Key::S] ||
                state.keys_down[(int)platform::Key::ArrowDown];
    bool left = state.keys_down[(int)platform::Key::A] ||
                state.keys_down[(int)platform::Key::ArrowLeft];
    bool right = state.keys_down[(int)platform::Key::D] ||
                 state.keys_down[(int)platform::Key::ArrowRight];

    // Actions
    state.action_pressed[(int)InputAction::Punch] =
        state.keys_just_pressed[(int)platform::Key::O] ||
        state.keys_just_pressed[(int)platform::Key::Space];
    state.action_pressed[(int)InputAction::Kick] =
        state.keys_just_pressed[(int)platform::Key::P] ||
        state.keys_just_pressed[(int)platform::Key::K];
    state.action_pressed[(int)InputAction::Block] =
        state.keys_down[(int)platform::Key::ShiftLeft];
    state.action_pressed[(int)InputAction::Confirm] =
        state.keys_just_pressed[(int)platform::Key::Enter];
    state.action_pressed[(int)InputAction::Back] =
        state.keys_just_pressed[(int)platform::Key::Escape];

    // Absolute directions (will be converted to relative by the move selector)
    if (up && right) state.dir_down[4] = true; // UpForward
    else if (up && left) state.dir_down[5] = true; // UpBack
    else if (down && right) state.dir_down[6] = true; // DownForward
    else if (down && left) state.dir_down[7] = true; // DownBack
    else if (up) state.dir_down[0] = true;
    else if (down) state.dir_down[1] = true;
    else if (left) state.dir_down[2] = true;
    else if (right) state.dir_down[3] = true;
    else state.dir_down[8] = true; // Central (no direction)
}

} // namespace resf2::core
