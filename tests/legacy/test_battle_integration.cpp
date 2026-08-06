// tests/integration/test_battle_integration.cpp
//
// Battle integration test: verifies that battle runs for N frames,
// HP values stay in [0, 1] range, and the round completes or reaches
// a valid state (victory/defeat/running).
//
// This test exercises:
// - Battle scene initialization
// - Combat system (HP tracking)
// - Round outcome tracking
// - Scene state management

#include "../headless_test_runner.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    std::printf("=== Battle Integration Test ===\n");

    // Configure to start on Battle scene
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.start_scene = "battle";  // Start directly in Battle scene

    resf2::test::HeadlessTestRunner runner(config);

    std::printf("Initializing with Battle scene...\n");
    if (!runner.init()) {
        std::fprintf(stderr, "FAIL: init() returned false\n");
        return 1;
    }

    // Record initial state
    float initial_player_hp = runner.player_health_frac();
    float initial_enemy_hp = runner.enemy_health_frac();
    std::string initial_outcome = runner.round_outcome();

    std::printf("Initial state:\n");
    std::printf("  player HP: %.3f\n", initial_player_hp);
    std::printf("  enemy HP: %.3f\n", initial_enemy_hp);
    std::printf("  outcome: \"%s\"\n", initial_outcome.c_str());

    // Validate initial state
    assert(initial_player_hp >= 0.0f && initial_player_hp <= 1.0f &&
           "Initial player HP must be in [0, 1]");
    assert(initial_enemy_hp >= 0.0f && initial_enemy_hp <= 1.0f &&
           "Initial enemy HP must be in [0, 1]");
    std::printf("OK: Initial HP values are valid\n");

    // Run simulation (500 frames = ~8 seconds at 60fps)
    std::printf("Running 500 frames of battle...\n");
    runner.run_frames(500);

    // Check final state
    float final_player_hp = runner.player_health_frac();
    float final_enemy_hp = runner.enemy_health_frac();
    std::string final_outcome = runner.round_outcome();

    std::printf("Final state:\n");
    std::printf("  player HP: %.3f\n", final_player_hp);
    std::printf("  enemy HP: %.3f\n", final_enemy_hp);
    std::printf("  outcome: \"%s\"\n", final_outcome.c_str());

    // Validate final HP values
    assert(final_player_hp >= 0.0f && final_player_hp <= 1.0f &&
           "Final player HP must be in [0, 1]");
    assert(final_enemy_hp >= 0.0f && final_enemy_hp <= 1.0f &&
           "Final enemy HP must be in [0, 1]");
    std::printf("OK: Final HP values are valid\n");

    // Validate outcome is one of the expected values
    assert((final_outcome.empty() ||
            final_outcome == "victory" ||
            final_outcome == "defeat") &&
           "Outcome must be empty, 'victory', or 'defeat'");
    std::printf("OK: Outcome is valid\n");

    // Check if damage was dealt (at least one side should take damage)
    // Note: In a real battle, damage should be dealt. If no damage is dealt,
    // it might mean the battle isn't properly initialized or combat isn't running.
    bool player_took_damage = (final_player_hp < initial_player_hp);
    bool enemy_took_damage = (final_enemy_hp < initial_enemy_hp);
    bool damage_dealt = player_took_damage || enemy_took_damage;

    if (damage_dealt) {
        std::printf("OK: Damage was dealt (player: %s, enemy: %s)\n",
                    player_took_damage ? "yes" : "no",
                    enemy_took_damage ? "yes" : "no");
    } else {
        // This is a warning, not a failure. The battle might not have
        // started properly, or both fighters might be at full health.
        std::printf("WARNING: No damage dealt after 500 frames\n");
        std::printf("  (This might indicate battle didn't initialize properly)\n");
    }

    // Verify frame count
    assert(runner.frame_count() == 500 && "Frame count should be 500");
    std::printf("OK: Frame count is correct\n");

    std::printf("\n=== BATTLE INTEGRATION TEST PASSED ===\n");
    return 0;
}
