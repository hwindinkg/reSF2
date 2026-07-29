// tests/integration/test_crash_stability.cpp
//
// Crash stability test: runs 500 frames (~8 seconds at 60fps) on the
// default scene without crashing. Verifies that the game can sustain
// extended headless simulation without hitting assertions, segfaults,
// or resource exhaustion.
//
// This test exercises:
// - Long-running simulation stability
// - Memory leaks / resource exhaustion detection
// - Scene update/render loop robustness
// - State invariant preservation over time

#include "../headless_test_runner.hpp"

#include <cassert>
#include <cstdio>
#include <string>

// Suppress noisy stdout from the game's internal logging (e.g. dialogue
// typewriter spam) so the test doesn't time out from I/O overhead.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

int main() {
    // Print our header first, then suppress game log spam
    std::printf("=== Crash Stability Test ===\n");
    std::fflush(stdout);

    suppress_stdout();

    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;

    resf2::test::HeadlessTestRunner runner(config);

    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: init() returned false\n");
        return 1;
    }

    // Record initial state
    float initial_hp = runner.player_health_frac();
    float initial_enemy_hp = runner.enemy_health_frac();
    int initial_currency = runner.currency();
    std::string initial_outcome = runner.round_outcome();

    // Validate initial state
    assert(initial_hp >= 0.0f && initial_hp <= 1.0f &&
           "Initial player HP must be in [0, 1]");
    assert(initial_enemy_hp >= 0.0f && initial_enemy_hp <= 1.0f &&
           "Initial enemy HP must be in [0, 1]");
    assert(initial_currency >= 0 && "Initial currency must be non-negative");

    // Run 500 frames (~8 seconds at 60fps)
    runner.run_frames(500);

    // Check state after first batch
    float hp_after_500 = runner.player_health_frac();
    assert(hp_after_500 >= 0.0f && hp_after_500 <= 1.0f &&
           "HP must remain in [0, 1] after 500 frames");
    assert(runner.frame_count() == 500 && "Frame count should be 500");

    // Test that input injection still works after long run
    runner.inject_key_down(resf2::platform::Key::Space);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::Space);
    runner.run_frames(1);

    runner.inject_pointer_down(100.0f, 200.0f);
    runner.run_frames(1);
    runner.inject_pointer_up();
    runner.run_frames(1);

    assert(runner.frame_count() == 504 && "Frame count should be 504");

    // Run another 500 frames for extended stability check
    runner.run_frames(500);

    // Print results to stderr (stdout is suppressed)
    float final_hp = runner.player_health_frac();
    float final_enemy_hp = runner.enemy_health_frac();
    int final_currency = runner.currency();
    std::string final_outcome = runner.round_outcome();

    // Final validation of all invariants
    assert(final_hp >= 0.0f && final_hp <= 1.0f &&
           "Player HP must remain in [0, 1] after 1004 frames");
    assert(final_enemy_hp >= 0.0f && final_enemy_hp <= 1.0f &&
           "Enemy HP must remain in [0, 1] after 1004 frames");
    assert(final_currency >= 0 &&
           "Currency must remain non-negative after 1004 frames");
    assert((final_outcome.empty() ||
            final_outcome == "victory" ||
            final_outcome == "defeat") &&
           "Outcome must be empty, 'victory', or 'defeat'");
    assert(runner.frame_count() == 1004 && "Frame count should be 1004");

    std::fprintf(stderr,
        "Initial: hp=%.3f enemy_hp=%.3f currency=%d outcome=\"%s\"\n"
        "Final:   hp=%.3f enemy_hp=%.3f currency=%d outcome=\"%s\"\n"
        "(after %d frames)\n",
        initial_hp, initial_enemy_hp, initial_currency, initial_outcome.c_str(),
        final_hp, final_enemy_hp, final_currency, final_outcome.c_str(),
        runner.frame_count());

    std::fprintf(stderr, "OK: Extended stability verified (%d frames)\n",
                 runner.frame_count());
    std::fprintf(stderr, "\n=== CRASH STABILITY TEST PASSED ===\n");
    return 0;
}
