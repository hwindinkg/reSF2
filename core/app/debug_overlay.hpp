#pragma once

// In-game debug overlay (port side of the oracle-comparison kit).
//
// The user asked for a debug system where every subsystem can be shown in BOTH
// the native port and the JS oracle, toggled by digit keys:
//   1 = AI (branch/tactic/zone/candidates/chosen)
//   2 = Quests (story step, zone, hanging dialogs, modal depth)
//   3 = Collision (per-side intervals, block state, hit counters)
//   4 = Physics (ragdoll state, COM, worst stretched cloth edge)
//   5 = Damage (HP of both sides + damage dealt, for the final stats)
//   6 = Fight timing (frame/phase/round/timer/input taps)
//   7 = Save/profile (money/level/weapon)
//   8 = Screens (top screen id + stack depth)
//   9 = Frame time (ms + fps)
//   0 = clear all
// The same fields are emitted by the oracle tracer (`trace_oracle.js`, keys
// 1..9 there too), so a divergence can be read on both sides and diffed from
// the logs (`reference/tools/compare_debug.py`).

#include <array>

namespace sf2::app {

class App;

struct DebugOverlay {
    // Panel state, index 1..9 (0 unused).
    std::array<bool, 10> on{};

    static DebugOverlay& get();

    // Digit key (GLFW_KEY_0..GLFW_KEY_9). Returns true when consumed.
    bool handle_key(int glfw_key);

    bool any() const {
        for (int i = 1; i < 10; ++i) {
            if (on[static_cast<std::size_t>(i)]) return true;
        }
        return false;
    }

    void clear() { on.fill(false); }
};

// Draws the enabled panels over the current frame (called from
// `App::render_frame`, after every screen and the global toasts).
void draw_debug_overlay(App& app);

// One-line status for the console (the harness/GUI can log the toggles).
const char* debug_overlay_status();

}  // namespace sf2::app
