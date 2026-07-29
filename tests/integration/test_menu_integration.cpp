// tests/integration/test_menu_integration.cpp
//
// Menu integration test: verifies that the menu overlay toggle (M key)
// works without crashing, and that the game continues running correctly
// after input events.
//
// This test exercises:
// - Key input injection (M key)
// - Menu overlay toggle handler
// - Game stability under input events
// - Frame progression after input

#include "../headless_test_runner.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    std::printf("=== Menu Integration Test ===\n");

    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;

    resf2::test::HeadlessTestRunner runner(config);

    std::printf("Initializing...\n");
    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: init() returned false\n");
        return 1;
    }

    // Let game initialize
    runner.run_frames(10);
    int frames_after_init = runner.frame_count();
    std::printf("Frames after init: %d\n", frames_after_init);
    assert(frames_after_init == 10 && "Frame count should be 10");

    // Record HP before menu toggle (should not change from menu toggle)
    float hp_before = runner.player_health_frac();
    std::printf("HP before menu toggle: %.3f\n", hp_before);

    // Press M to toggle menu overlay
    std::printf("Pressing M key to toggle menu overlay...\n");
    runner.inject_key_down(resf2::platform::Key::M);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::M);
    runner.run_frames(5);

    int frames_after_toggle = runner.frame_count();
    std::printf("Frames after toggle: %d\n", frames_after_toggle);
    assert(frames_after_toggle == 16 && "Frame count should be 16");
    std::printf("OK: Game continued running after M key press\n");

    // HP should not have changed from menu toggle alone
    float hp_after_toggle = runner.player_health_frac();
    std::printf("HP after menu toggle: %.3f\n", hp_after_toggle);
    assert(hp_after_toggle == hp_before &&
           "HP should not change from menu overlay toggle");
    std::printf("OK: HP unchanged after menu toggle\n");

    // Press M again to toggle back
    std::printf("Pressing M key again...\n");
    runner.inject_key_down(resf2::platform::Key::M);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::M);
    runner.run_frames(5);

    int frames_after_second_toggle = runner.frame_count();
    assert(frames_after_second_toggle == 22 && "Frame count should be 22");
    std::printf("OK: Game still running after second M press\n");

    // Rapid M presses — should not crash
    std::printf("Rapid M key presses (stress test)...\n");
    for (int i = 0; i < 20; ++i) {
        runner.inject_key_down(resf2::platform::Key::M);
        runner.run_frames(1);
        runner.inject_key_up(resf2::platform::Key::M);
        runner.run_frames(1);
    }

    int frames_after_stress = runner.frame_count();
    std::printf("Frames after stress test: %d\n", frames_after_stress);
    assert(frames_after_stress == 62 && "Frame count should be 62");
    std::printf("OK: No crash after 20 rapid M presses\n");

    // Test other keys don't crash either
    std::printf("Testing various key inputs...\n");
    runner.inject_key_down(resf2::platform::Key::Escape);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::Escape);
    runner.run_frames(1);

    runner.inject_key_down(resf2::platform::Key::Space);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::Space);
    runner.run_frames(1);

    runner.inject_key_down(resf2::platform::Key::ArrowLeft);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::ArrowLeft);
    runner.run_frames(1);

    runner.inject_key_down(resf2::platform::Key::ArrowRight);
    runner.run_frames(1);
    runner.inject_key_up(resf2::platform::Key::ArrowRight);
    runner.run_frames(1);

    std::printf("OK: Various key inputs processed without crash\n");

    // Final HP check
    float final_hp = runner.player_health_frac();
    assert(final_hp >= 0.0f && final_hp <= 1.0f &&
           "HP must remain in [0, 1] after all input");
    std::printf("OK: Final HP is valid (%.3f)\n", final_hp);

    std::printf("\n=== MENU INTEGRATION TEST PASSED ===\n");
    return 0;
}
