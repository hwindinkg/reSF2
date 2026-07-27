// tests/test_input_handler.cpp
//
// Unit tests for InputHandler's double-tap detection. These verify the
// timing logic that drives DoubleStepForward (dash) and DoubleStepBack
// (handstand retreat).
//
// [ORIGINAL] Double-tap window: 300 ms from Model::step @ 0x10161ad0.
//
// [BEFORE FIX] Detection required move_state_ == 2 (already walking
// forward). Double-tapping from idle was silently ignored.
//
// [AFTER FIX] Detection fires when move_state_ == 0 (idle) OR the
// matching walk state (1 for back, 2 for forward).

#include "../engine/game/input_handler.hpp"
#include "../engine/platform/platform.hpp"
#include "check.hpp"

#include <cstdio>

using resf2::test::check;
namespace plat = resf2::platform;

namespace {

// Test helper: build a NullPlatform and drive InputHandler frame-by-frame.
struct Harness {
    plat::NullPlatform plat;
    InputHandler ih;

    // Reset all input state then optionally hold D and/or A for one frame.
    // D_just/A_just simulate a "just pressed" tap.
    void tap_frame(bool d_just, bool a_just) {
        // Release everything first (clears keys_down).
        for (std::size_t i = 0; i < plat::kMaxKeys; ++i) {
            plat.inject_key_up(static_cast<plat::Key>(i));
        }
        // Now press — inject_key_down sets keys_just_pressed when the key
        // was previously up, which it is after the release loop above.
        if (d_just) plat.inject_key_down(plat::Key::D);
        if (a_just) plat.inject_key_down(plat::Key::A);
    }

    struct Output {
        bool fwd_just = false;
        bool back_just = false;
        bool fwd_held = false;
        bool back_held = false;
        bool up_held = false;
        bool down_held = false;
        bool punch = false;
        bool kick = false;
        bool step_min = false;
    };

    // Drive one InputHandler frame.
    Output step(uint32_t now_ms) {
        Output o;
        float dist = 500.0f;
        int in_out = 0;
        bool past_attack = false;
        ih.process_input(plat,
                         /*dt_sec=*/0.016f, /*dt_ms=*/16,
                         /*facing_right=*/true,
                         o.punch, o.kick,
                         o.fwd_held, o.back_held,
                         o.up_held, o.down_held,
                         o.fwd_just, o.back_just,
                         now_ms,
                         /*in_attack=*/false,
                         /*start_stance=*/false,
                         o.step_min, dist, past_attack,
                         in_out, /*total_frame=*/0);
        return o;
    }
};

}  // namespace

int main() {
    // ------------------------------------------------------------------
    // Test 1: Double-tap from idle (move_state_ == 0) triggers
    //         DoubleStepForward. This is the primary fix under test —
    //         it would have FAILED on the old code.
    // ------------------------------------------------------------------
    {
        Harness h;
        // Tap 1 at t=1000 ms
        h.tap_frame(true, false);
        auto o1 = h.step(1000);
        check(o1.fwd_just, "tap 1: fwd_just_pressed is true");
        check(!h.ih.double_step_fwd_requested(),
              "tap 1: no double-step yet (only one tap)");

        // Tap 2 at t=1150 ms — 150 ms later, inside 300 ms window
        h.tap_frame(true, false);
        auto o2 = h.step(1150);
        check(o2.fwd_just, "tap 2: fwd_just_pressed is true");
        check(h.ih.double_step_fwd_requested(),
              "tap 2 from IDLE: double_step_fwd IS requested "
              "[would have FAILED before fix]");
    }

    // ------------------------------------------------------------------
    // Test 2: 400 ms gap does NOT trigger double-tap (outside window).
    // ------------------------------------------------------------------
    {
        Harness h;
        h.tap_frame(true, false);
        h.step(2000);

        h.tap_frame(true, false);
        h.step(2400);
        check(!h.ih.double_step_fwd_requested(),
              "400 ms gap: double_step_fwd NOT requested (outside window)");
    }

    // ------------------------------------------------------------------
    // Test 3: 299 ms boundary — just inside, SHOULD trigger.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.tap_frame(true, false);
        h.step(3000);

        h.tap_frame(true, false);
        h.step(3299);
        check(h.ih.double_step_fwd_requested(),
              "299 ms gap (boundary): double_step_fwd IS requested");
    }

    // ------------------------------------------------------------------
    // Test 4: Double-tap while already walking (move_state_ == 2) —
    //         regression guard: this always worked.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.ih.set_move_state(2);

        h.tap_frame(true, false);
        h.step(4000);

        h.tap_frame(true, false);
        h.step(4100);
        check(h.ih.double_step_fwd_requested(),
              "double-tap while walking: triggers (regression guard)");
    }

    // ------------------------------------------------------------------
    // Test 5: Backward double-tap from idle.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.tap_frame(false, true);
        h.step(5000);

        h.tap_frame(false, true);
        h.step(5120);
        check(h.ih.double_step_back_requested(),
              "double-tap back from idle: triggers DoubleStepBack");
    }

    // ------------------------------------------------------------------
    // Test 6: Single tap — no double-step.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.tap_frame(true, false);
        h.step(6000);
        check(!h.ih.double_step_fwd_requested(),
              "single tap: no double_step_fwd");
    }

    // ------------------------------------------------------------------
    // Test 7: Double-tap while walking backward (move_state_ == 1)
    //         triggers BACKWARD double-step, NOT forward.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.ih.set_move_state(1);  // walking back
        h.tap_frame(false, true);
        h.step(7000);
        h.tap_frame(false, true);
        h.step(7100);
        check(h.ih.double_step_back_requested(),
              "walking back + double-tap A: triggers DoubleStepBack");
        check(!h.ih.double_step_fwd_requested(),
              "walking back + double-tap A: does NOT trigger DoubleStepFwd");
    }

    // ------------------------------------------------------------------
    // Test 8: Exactly 300 ms gap — boundary edge case.
    // The window check is `now_ms - last_tap_ms < kDoubleTapWindowMs`,
    // so exactly 300 ms is NOT < 300 → should NOT trigger.
    // ------------------------------------------------------------------
    {
        Harness h;
        h.tap_frame(true, false);
        h.step(8000);

        h.tap_frame(true, false);
        h.step(8300);  // exactly 300 ms
        check(!h.ih.double_step_fwd_requested(),
              "exactly 300 ms gap: double_step_fwd NOT requested "
              "(window is strictly < 300 ms)");
    }

    return resf2::test::summary();
}
