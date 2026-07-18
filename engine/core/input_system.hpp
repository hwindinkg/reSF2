#pragma once

#include "input.hpp"
#include "../platform/platform.hpp"

namespace resf2::core {

// Bridges platform::Platform input to core::InputState.
// Reads raw keyboard/mouse/gamepad from Platform and produces
// the derived directional state needed for move selection.

class InputProcessor {
public:
    void poll(platform::Platform& plat, InputState& state);

private:
    void update_directions(InputState& state);
    int last_mouse_x_ = 0;
    int last_mouse_y_ = 0;
};

} // namespace resf2::core
