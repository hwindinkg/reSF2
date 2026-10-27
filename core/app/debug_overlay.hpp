#pragma once

// In-game debug overlay (port side of the oracle-comparison kit).
//
// The oracle has nine panels (keys 1..9 there). The port folds them into FOUR
// keys so nothing overlaps and the hand never leaves 1..4:
//   1 = fight & AI   -> AI (branch/tactic/zone/chosen) + Damage + Fight timing
//   2 = world        -> Quest + Collision (intervals/capsules) + Physics
//   3 = session      -> Save/profile + Screens (id + stack)
//   4 = performance  -> FPS (current / average / 0.1% low), frame-time graph,
//                       CPU + GPU load, fixed steps, and the [perf] log lines
//   0 = clear all
// The panels are laid out in anchored flows and never share pixels. Every
// panel also owns a translucent plate + 1px border.
//
// The same per-panel fields are emitted by the oracle tracer
// (`trace_oracle.js`, keys 1..9), so a divergence can be read on both sides
// and diffed from the logs (`reference/tools/compare_debug.py`).

#include <array>

namespace sf2::app {

class App;

struct DebugOverlay {
    // Panel state, index 1..9 (0 unused) - derived from `group` when a digit
    // key toggles a group.
    std::array<bool, 10> on{};
    // The four toggle groups (see the header comment).
    std::array<bool, 5> group{};

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
