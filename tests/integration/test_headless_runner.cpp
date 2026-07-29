// tests/integration/test_headless_runner.cpp
//
// Smoke test: verifies HeadlessTestRunner can instantiate and run frames
// without crashing. This is the canary that the test harness itself works.
//
// Expected outcome:
// - HeadlessTestRunner::init() succeeds
// - run_frames() completes without crash
// - frame_count() increments correctly
// - Input injection doesn't crash
// - State accessors return valid values (may be default/zero)

#include "../headless_test_runner.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    std::printf("=== HeadlessTestRunner smoke test ===\n");

    // Configure the runner
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;

    // Create and initialize the runner
    resf2::test::HeadlessTestRunner runner(config);
    
    std::printf("Initializing HeadlessTestRunner...\n");
    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: HeadlessTestRunner::init() returned false\n");
        return 1;
    }
    std::printf("OK: init succeeded\n");

    // Verify initial state
    assert(runner.frame_count() == 0);
    std::printf("OK: initial frame_count is 0\n");

    // Run a few frames
    std::printf("Running 10 frames...\n");
    runner.run_frames(10);
    assert(runner.frame_count() == 10);
    std::printf("OK: frame_count is 10\n");

    // Run more frames
    runner.run_frames(5);
    assert(runner.frame_count() == 15);
    std::printf("OK: frame_count is 15\n");

    // Test input injection (should not crash)
    std::printf("Testing input injection...\n");
    runner.inject_key_down(resf2::platform::Key::Space);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::Space);
    runner.run_frames(1);
    std::printf("OK: key injection works\n");

    runner.inject_pointer_down(100.0f, 200.0f);
    runner.run_frames(1);
    runner.inject_pointer_up();
    runner.run_frames(1);
    std::printf("OK: pointer injection works\n");

    // Test state accessors (should not crash, may return default values)
    std::printf("Testing state accessors...\n");
    float player_hp = runner.player_health_frac();
    float enemy_hp = runner.enemy_health_frac();
    std::string outcome = runner.round_outcome();
    int cur = runner.currency();
    bool has = runner.has_item("test_item");
    
    std::printf("  player_health_frac: %.3f\n", player_hp);
    std::printf("  enemy_health_frac: %.3f\n", enemy_hp);
    std::printf("  round_outcome: \"%s\"\n", outcome.c_str());
    std::printf("  currency: %d\n", cur);
    std::printf("  has_item(\"test_item\"): %s\n", has ? "true" : "false");
    std::printf("OK: state accessors work\n");

    // Test run_until
    std::printf("Testing run_until...\n");
    int counter = 0;
    bool found = runner.run_until([&]() {
        counter++;
        return counter >= 5;
    }, 100);
    assert(found);
    assert(counter == 5);
    std::printf("OK: run_until works\n");

    // Test run_until with timeout
    bool timeout = runner.run_until([&]() {
        return false;  // never true
    }, 10);
    assert(!timeout);
    std::printf("OK: run_until timeout works\n");

    std::printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
