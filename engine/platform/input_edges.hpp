// engine/platform/input_edges.hpp
//
// Shared per-frame key-poll contract.
//
// The two input producers — the Win32 GetAsyncKeyState poll path
// (glfw_platform.cpp poll_events) and the test injection path
// (NullPlatform::inject_key_down/up) — must produce the SAME InputState
// timeline from the same physical press/release sequence, because the
// game and every integration test read only platform.input().
//
// poll_key_frame() is the edge-decision function the Win32 poll loop
// calls with the OS's per-key down-state. The input-contract test
// (tests/test_input_contract.cpp) drives this same function with scripted
// OS state and asserts the result matches TestPlatform injection for the
// M1 roll orders, P7 held duck, D4 dialogue advance and J/U weapon cycle
// scenarios — so the real input path and the test path share one
// implementation of the only logic either of them has.
//
// Contract (identical to NullPlatform::inject_key_down/up semantics):
//   just_pressed  = is_down && !was_down
//   just_released = !is_down && was_down
//   keys_down     = is_down
// A key whose down+up both fall between two polls produces NO edges —
// the documented fast-tap gap of poll-based input (INPUT_PATH_AUDIT.md).

#pragma once

#include "platform.hpp"

#include <cstddef>
#include <span>

namespace resf2::platform {

// Applies one frame of OS-level key state to an InputState.
//
//   input      - the InputState for this frame; keys_just_pressed and
//                keys_just_released MUST already be cleared (the callers
//                clear them before sampling, exactly like poll_events()).
//   prev_down  - previous frame's down-state per Key index; updated in
//                place to this frame's state.
//   os_down    - physical key state per Key index sampled this frame;
//                false for keys the backend cannot sample (unmapped).
//
// Covers every key in the Key enum (0..AltRight). Keys outside the enum
// are never touched.
inline void poll_key_frame(InputState& input, std::span<bool> prev_down,
                           std::span<const bool> os_down) {
    const int last = static_cast<int>(Key::AltRight);
    for (int i = 0; i <= last; ++i) {
        const bool down = os_down[i];
        const bool was_down = prev_down[i];
        input.keys_just_pressed[i] = down && !was_down;
        input.keys_just_released[i] = !down && was_down;
        input.keys_down[i] = down;
        prev_down[i] = down;
    }
}

}  // namespace resf2::platform
