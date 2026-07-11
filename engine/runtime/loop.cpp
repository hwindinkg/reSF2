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

        // Frame timing: variable-step, clamped to 200 ms
        const auto now = platform.now_ms();
        const auto raw_dt = now > last_ms ? static_cast<std::uint32_t>(now - last_ms) : 0u;
        const auto dt = std::min<std::uint32_t>(raw_dt, 200u);
        last_ms = now;

        // Update
        game.on_update(platform, dt);

        // Render
        game.on_render(platform);
        platform.swap_buffers();
    }

    game.on_shutdown(platform);
    return 0;
}

}  // namespace resf2::runtime
