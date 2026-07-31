// tests/headless_test_runner.hpp
//
// HeadlessTestRunner — wraps TestPlatform + SoftwareRendererAdapter + Game
// into a convenient test harness for integration tests. Runs the real Game
// class headlessly without GPU/GL, with deterministic time and input injection.

#pragma once

#include "test_platform.hpp"
#include "engine/renderer/software_renderer_adapter.hpp"
#include "engine/game/game_clean.hpp"

#include <memory>
#include <string>

namespace resf2::test {

struct HeadlessTestConfig {
    std::string asset_root = "assets";
    int width = 1280;
    int height = 720;
    std::uint32_t fixed_dt_ms = 16;  // ~60 FPS
    std::string start_scene;  // Optional: "battle", "shop", "map", etc.
    std::string start_location;  // Optional: "dojo", "waterfall_small", ...
    // Hermetic run: read no machine state (no saved profile/inventory) and
    // skip the tutorial check. Set for scripted measurements that must be
    // reproducible on any machine (Game::set_hermetic_run).
    bool hermetic = false;
};

class HeadlessTestRunner {
public:
    explicit HeadlessTestRunner(const HeadlessTestConfig& config = {});
    ~HeadlessTestRunner();

    // Initialize the game with software renderer. Returns false on failure.
    bool init();

    // Run N frames with fixed dt.
    void run_frames(int count);

    // Run until condition is true or max_frames reached. Returns true if condition met.
    template<typename Pred>
    bool run_until(Pred pred, int max_frames) {
        for (int i = 0; i < max_frames; ++i) {
            if (pred()) return true;
            run_frames(1);
        }
        return false;
    }

    // Input injection (delegate to TestPlatform)
    //
    // NOTE on ordering: run_frames() calls poll_events() at the top of each
    // frame, and poll_events() clears keys_just_pressed. A key injected
    // *between* run_frames() calls therefore has its "just pressed" edge wiped
    // before any scene observes it, so scenes that use key_pressed() (an edge
    // test) never see it. Use tap_key() for anything driven by a key press.
    void inject_key_down(platform::Key key);
    void inject_key_up(platform::Key key);

    // Press and release a key so the press edge survives into on_update().
    // Runs `hold_frames` frames with the key down, then releases it.
    void tap_key(platform::Key key, int hold_frames = 1);
    void inject_pointer_down(float x, float y, std::int32_t id = 0);
    void inject_pointer_up(std::int32_t id = 0);

    // State accessors (delegate to Game's host_* methods)
    [[nodiscard]] float player_health_frac() const;
    [[nodiscard]] float enemy_health_frac() const;
    [[nodiscard]] std::string round_outcome() const;
    [[nodiscard]] int currency() const;
    [[nodiscard]] bool has_item(const std::string& id) const;

    // Direct access for advanced assertions
    [[nodiscard]] game::Game& game() { return *game_; }
    [[nodiscard]] const game::Game& game() const { return *game_; }
    [[nodiscard]] TestPlatform& platform() { return *platform_; }
    [[nodiscard]] const TestPlatform& platform() const { return *platform_; }

    [[nodiscard]] int frame_count() const noexcept { return frame_count_; }

    // Access the software renderer adapter (for screenshots, framebuffer access).
    // Valid after init() succeeds.
    [[nodiscard]] renderer::SoftwareRendererAdapter* renderer() noexcept {
        return renderer_ptr_;
    }
    [[nodiscard]] const renderer::SoftwareRendererAdapter* renderer() const noexcept {
        return renderer_ptr_;
    }

private:
    HeadlessTestConfig config_;
    std::unique_ptr<TestPlatform> platform_;
    std::unique_ptr<renderer::SoftwareRendererAdapter> renderer_;
    renderer::SoftwareRendererAdapter* renderer_ptr_ = nullptr;  // non-owning, set before move into game
    std::unique_ptr<game::Game> game_;
    int frame_count_ = 0;
};

}  // namespace resf2::test
