// engine/runtime/loop.hpp
//
// Main loop abstraction.
//
// Stage 7.1 implementation. The Loop owns the main loop and drives
// the game's update + render callbacks. It is responsible for:
//   - Frame timing (variable-step, clamped to 200 ms max)
//   - Pause/resume (skips updates when platform is paused)
//   - Quit handling (exits when platform requests quit)
//
// The IGame interface is implemented by the actual game code
// (Stage 7.x). The Loop calls its on_init / on_update / on_render /
// on_shutdown methods.

#pragma once

#include <cstdint>

namespace resf2::platform { class Platform; }

namespace resf2::runtime {

// Interface implemented by game code.
class IGame {
public:
    virtual ~IGame() = default;

    // Called once at startup, after the platform is initialized.
    virtual void on_init(platform::Platform& platform) = 0;

    // Called once per frame with the delta time in milliseconds (clamped
    // to [0, 200]). The InputState for this frame is available via
    // platform.input().
    virtual void on_update(platform::Platform& platform, std::uint32_t dt_ms) = 0;

    // Called once per frame to render. The platform's GL context is
    // already current.
    virtual void on_render(platform::Platform& platform) = 0;

    // Called when the platform is paused (window minimized, Android
    // Activity paused). Game should release non-essential resources.
    virtual void on_pause(platform::Platform& platform) { (void)platform; }

    // Called when the platform is resumed. Game should re-acquire
    // resources and reload any GPU-side state that may have been lost.
    virtual void on_resume(platform::Platform& platform) { (void)platform; }

    // Called once at shutdown, before the platform is destroyed.
    virtual void on_shutdown(platform::Platform& platform) { (void)platform; }
};

// Main loop owner.
class Loop {
public:
    Loop();
    ~Loop();

    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;

    // Run the loop with the given game. Returns the process exit code
    // (0 = success, non-zero = error).
    [[nodiscard]] int run(platform::Platform& platform, IGame& game);

    // Request the loop to exit on the next iteration. Safe to call
    // from any thread.
    void request_exit() noexcept;

private:
    struct Impl;
    // Inline storage to avoid heap allocation; the Impl is small.
    alignas(64) char impl_storage_[64];
    Impl* impl_;
};

}  // namespace resf2::runtime
