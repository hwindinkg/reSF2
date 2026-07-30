// engine/runtime/loop.cpp
//
// Implementation of the main loop.

#include "loop.hpp"
#include "../platform/platform.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>

namespace resf2::runtime {

namespace plat = resf2::platform;

struct Loop::Impl {
    std::atomic<bool> exit_requested{false};
};

Loop::Loop() : impl_(new (impl_storage_) Impl()) {}
Loop::~Loop() { impl_->~Impl(); }

void Loop::request_exit() noexcept {
    impl_->exit_requested.store(true, std::memory_order_relaxed);
}

int Loop::run(plat::Platform& platform, IGame& game) {
    if (!platform.make_gl_current()) {
        std::fprintf(stderr, "Loop: make_gl_current() failed\n");
        return 2;
    }

    game.on_init(platform);

    auto last_ms = platform.now_ms();
    bool was_paused = false;

    while (!impl_->exit_requested.load(std::memory_order_relaxed)) {
        // Poll events (updates input state, processes window events)
        if (!platform.poll_events()) {
            break;  // platform requested quit
        }
        if (platform.should_quit()) {
            break;
        }

        // Handle pause/resume
        bool is_paused = platform.is_paused();
        if (is_paused && !was_paused) {
            game.on_pause(platform);
            was_paused = true;
        } else if (!is_paused && was_paused) {
            game.on_resume(platform);
            was_paused = false;
            last_ms = platform.now_ms();  // reset dt to avoid huge jump
        }

        if (is_paused) {
            // While paused: still poll events, but skip update + render
            platform.sleep_ms(100);
            continue;
        }

        // ---- Frame timing ----
        // [ORIGINAL] game+0x64400. Verified against the live ARM binary and
        // confirmed by Ghidra decompilation; see reverse/analysis/RUNTIME_MAP.md
        // section 5 and PORT_GAPS.md GAP-1.
        //
        // This was previously a variable-step loop (dt = now - last_ms, clamped
        // to 200 ms) with no frame limiter at all. The original is a FIXED
        // timestep:
        //
        //   * the interval is an integer number of milliseconds, read from the
        //     loop object at this+0x08 as int64. Captured live: 16. Note that
        //     1000/60 truncates to 16, so the real cap is 62.5 fps -- the
        //     <FrameRate Value="60"/> in internalSettings.xml is nominal only.
        //   * every frame receives exactly that interval, never the measured
        //     wall-clock delta, so behaviour is deterministic under load.
        //   * the leftover time is slept off in an inner spin loop that
        //     re-reads the clock each pass and carries no remainder into the
        //     next frame.
        const auto t0 = platform.now_ms();

        // Update with the fixed interval (never the measured delta).
        game.on_update(platform, static_cast<std::uint32_t>(frame_interval_ms_));

        // Render
        game.on_render(platform);
        platform.swap_buffers();

        // ---- inner wait loop (game+0x644C0) ----
        // Break when interval <= elapsed. The original's encoding is
        // `cmp/cmpeq/bls`, i.e. the next frame is entered as soon as the budget
        // is spent, including the exact-boundary case.
        while (true) {
            const auto elapsed = platform.now_ms() - t0;
            if (static_cast<std::int64_t>(frame_interval_ms_) <=
                static_cast<std::int64_t>(elapsed)) {
                break;
            }
            const auto remaining = static_cast<std::int64_t>(t0 + frame_interval_ms_) -
                                   static_cast<std::int64_t>(platform.now_ms());
            if (remaining < 0) break;   // bmi -> frame top
            if (remaining == 0) break;
            platform.sleep_ms(static_cast<std::uint32_t>(remaining));
        }

        last_ms = t0;
    }

    game.on_shutdown(platform);
    return 0;
}

}  // namespace resf2::runtime
