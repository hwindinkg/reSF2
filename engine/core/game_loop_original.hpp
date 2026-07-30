#pragma once

// engine/core/game_loop_original.hpp
//
// [ORIGINAL] 1:1 port of the S3E main loop at game+0x64400.
//
// Replaces the float-accumulator model in game_loop.hpp, which does not match
// the original. Verified against the live ARM binary on 2026-07-30; see
// reverse/analysis/RUNTIME_MAP.md §5 and reverse/analysis/PORT_GAPS.md GAP-1.
//
// Original disassembly (runtime addresses of that session in brackets):
//
//   game+0x64400 [0x8F0BB400]  push {r4-r8,lr}; sub sp,#0x10
//                              if (this->vtable[0x08]() == 0) return 0
//   game+0x64420  frame top:   t0 = s3eTimerGetMs()      ; 64-bit -> r6:r7
//                              s3eDeviceYield(0)          ; BL @0x64430
//                              s3eKeyboardUpdate()
//                              s3ePointerUpdate()
//                              ctx = game+0x143C8()
//                              r4 = ctx->vtable[0x50]()
//                              ax,ay,az = s3eAccelerometerGetX/Y/Z()
//                              step(r4, ax, ay, az, t0)   ; game+0x64230
//                              if (s3eDeviceCheckQuitRequest()) goto +0x6450C
//   game+0x644B0               ctx->vtable[0x2C]()        ; render
//   game+0x644C0  wait:        elapsed  = s3eTimerGetMs() - t0
//                              interval = this->[0x08]    ; ldrd r4,r5
//                              if (interval <= elapsed) goto frame top  ; bls
//                              remaining = (t0 + interval) - s3eTimerGetMs()
//                              if (remaining < 0) goto frame top        ; bmi
//                              s3eDeviceYield(remaining)  ; BL @0x644EC
//                              goto wait                  ; inner spin loop
//
// Three properties the old accumulator model got wrong:
//
//  1. The interval is an *integer* count of milliseconds, read from this+0x08
//     as int64. Captured live: 16. Since 1000/60 truncates to 16, the real cap
//     is 62.5 fps, not 60. internalSettings.xml's <FrameRate Value="60"/> is
//     the nominal rate, not the loop period.
//
//  2. delta is `interval_ms / 1000.0` in *double* (game+0x64230 does
//     vcvt.f64.f32 then vdiv.f64 by a literal verified to be exactly 1000.0 at
//     0x8F0BB2B0). dt is therefore quantised to whole milliseconds; a float
//     1/60 accumulator emits values the original never produces.
//
//  3. The wait at +0x644C0 is an inner spin loop that re-reads both the clock
//     and the interval on every pass and branches back to itself, not to the
//     frame top. No fractional remainder is carried between frames.

#include <cstdint>
#include <functional>

namespace resf2::core {

// Platform clock + yield, so the loop can be driven headlessly in tests.
// In the real build these map onto s3eTimerGetMs / s3eDeviceYield.
struct LoopPlatform {
    std::function<std::int64_t()> timer_ms;         // s3eTimerGetMs
    std::function<void(std::int32_t)> yield_ms;     // s3eDeviceYield
    std::function<bool()> quit_requested;           // s3eDeviceCheckQuitRequest
    std::function<void()> pump_input;               // Keyboard/PointerUpdate
};

// The per-frame state the original writes in game+0x64230: four doubles, all
// in seconds. reSF2 previously passed a single float dt and dropped the rest.
struct FrameState {
    double dt = 0.0;         // this+0x08  — frame delta, seconds
    double accel_y = 0.0;    // this+0x10
    double accel_x = 0.0;    // this+0x18
    double timestamp = 0.0;  // this+0x20  — from a second 64-bit time source
};

class OriginalGameLoop {
public:
    using StepFn = std::function<void(const FrameState&)>;
    using RenderFn = std::function<void()>;

    // [ORIGINAL] 16 ms, read live from this+0x08. Not 1000/60.0.
    static constexpr std::int64_t kDefaultIntervalMs = 16;

    // [ORIGINAL] the literal at 0x8F0BB2B0 (0x408F4000_00000000).
    static constexpr double kMsPerSecond = 1000.0;

    explicit OriginalGameLoop(LoopPlatform platform,
                              std::int64_t interval_ms = kDefaultIntervalMs)
        : platform_(std::move(platform)), interval_ms_(interval_ms) {}

    void set_step(StepFn f) { step_ = std::move(f); }
    void set_render(RenderFn f) { render_ = std::move(f); }

    // Mirrors the original's return values: 0 from the startup gate, -1 on quit.
    int run(std::int64_t max_frames = -1) {
        if (start_gate_ && !start_gate_()) return 0;

        while (true) {
            if (max_frames >= 0 && frame_count_ >= max_frames) return 0;

            // ---- frame top (game+0x64420) ----
            const std::int64_t t0 = now();
            platform_.yield_ms(0);            // yield(0): pump events
            if (platform_.pump_input) platform_.pump_input();

            FrameState st;
            // [ORIGINAL] integer ms / 1000.0 as double — quantised to 1 ms.
            st.dt = static_cast<double>(interval_ms_) / kMsPerSecond;
            st.timestamp = static_cast<double>(t0) / kMsPerSecond;
            if (step_) step_(st);
            last_dt_ = st.dt;

            if (platform_.quit_requested && platform_.quit_requested()) {
                return -1;                    // game+0x6450C -> +0x644F4
            }

            // ---- render (game+0x644B0) ----
            if (render_) render_();

            // ---- inner wait loop (game+0x644C0) ----
            spins_last_frame_ = 0;
            while (true) {
                const std::int64_t elapsed = now() - t0;
                // bls: next frame once the budget is spent (note: <=).
                if (interval_ms_ <= elapsed) break;
                const std::int64_t remaining = (t0 + interval_ms_) - now();
                // bmi: a negative remainder also ends the frame.
                if (remaining < 0) break;
                platform_.yield_ms(static_cast<std::int32_t>(remaining));
                ++spins_last_frame_;
            }

            ++frame_count_;
        }
    }

    // The interval is re-read every spin in the original, so changing it
    // mid-frame takes effect immediately.
    void set_interval_ms(std::int64_t ms) { interval_ms_ = ms; }
    std::int64_t interval_ms() const { return interval_ms_; }

    // dt the original would hand to the step function.
    double frame_dt() const {
        return static_cast<double>(interval_ms_) / kMsPerSecond;
    }

    void set_start_gate(std::function<bool()> g) { start_gate_ = std::move(g); }

    std::int64_t frame_count() const { return frame_count_; }
    int spins_last_frame() const { return spins_last_frame_; }
    double last_dt() const { return last_dt_; }

private:
    std::int64_t now() const { return platform_.timer_ms ? platform_.timer_ms() : 0; }

    LoopPlatform platform_;
    std::int64_t interval_ms_;
    StepFn step_;
    RenderFn render_;
    std::function<bool()> start_gate_;
    std::int64_t frame_count_ = 0;
    int spins_last_frame_ = 0;
    double last_dt_ = 0.0;
};

}  // namespace resf2::core
