# Headless Integration Tests — Design Summary

**Date**: 2026-07-28  
**Status**: Design Complete, Ready for Implementation

---

## Quick Answers to Your Questions

### Q1: How to make the real Game class work without OpenGL?

**A**: Introduce a **renderer abstraction layer**.

**Current Problem**: Game class has `std::unique_ptr<ren::Renderer> renderer_` which is OpenGL-specific.

**Solution**: 
1. Create `IRenderer` interface in `engine/renderer/irenderer.hpp`
2. Refactor `ren::Renderer` (GL) to implement `IRenderer`
3. Create `SoftwareRendererAdapter` that wraps `soft::Renderer` and implements `IRenderer`
4. Change Game to use `std::unique_ptr<IRenderer>` instead of concrete renderer
5. Add `Game::set_renderer()` method to inject renderer (for tests)

**Code Change**:
```cpp
// Before (game_clean.hpp line 3897):
std::unique_ptr<ren::Renderer> renderer_;

// After:
std::unique_ptr<renderer::IRenderer> renderer_;

// New method:
void Game::set_renderer(std::unique_ptr<renderer::IRenderer> renderer) {
    renderer_ = std::move(renderer);
}
```

**Backward Compatibility**: Default behavior unchanged. If `set_renderer()` not called, `on_init()` creates GL renderer (existing behavior).

---

### Q2: Should we create a HeadlessPlatform that implements Platform interface?

**A**: **No** — reuse the existing `NullPlatform`.

`NullPlatform` already provides:
- No-op GL context (`make_gl_current()` returns true, does nothing)
- Input injection (`inject_key_down`, `inject_pointer_down`, etc.)
- File I/O (reads real files from disk)

**Extension**: Create `TestPlatform` subclass that overrides `now_ms()` for fixed time:

```cpp
class TestPlatform : public platform::NullPlatform {
public:
    void set_fixed_time_ms(uint64_t ms) { fixed_time_ms_ = ms; }
    void advance_time_ms(uint64_t ms) { fixed_time_ms_ += ms; }
    
    [[nodiscard]] uint64_t now_ms() const noexcept override {
        return fixed_time_ms_;
    }
    
private:
    uint64_t fixed_time_ms_ = 0;
};
```

This enables **deterministic tests** with perfectly fixed dt=16ms.

---

### Q3: How to inject fixed dt=16ms into the game loop?

**A**: Call `game.on_update(platform, 16)` directly with fixed dt.

**Game loop in tests**:
```cpp
void HeadlessTestRunner::run_frames(int count) {
    for (int i = 0; i < count; ++i) {
        platform_->poll_events();           // Process injected input
        game_->on_update(*platform_, 16);   // Fixed dt=16ms
        game_->on_render(*platform_);       // Render to software framebuffer
        platform_->advance_time_ms(16);     // Simulate time passing
        frame_count_++;
    }
}
```

**Why this works**: The Game class doesn't care where `dt` comes from. It just uses the value passed to `on_update`. By passing a constant 16ms and advancing the platform's clock by 16ms each frame, we get perfectly deterministic behavior.

---

### Q4: How to access game state for assertions?

**A**: The Game class **already exposes extensive state** via `host_*` methods.

**Examples**:
```cpp
// Health and combat
float player_hp = game.host_player_health_frac();  // 0.0 to 1.0
float enemy_hp = game.host_enemy_health_frac();
std::string outcome = game.host_round_outcome();   // "", "victory", "defeat"
int wins = game.host_get_wins();

// Inventory and shop
int currency = game.host_get_currency();
bool has_item = game.host_has_item("iron_sword");
std::vector<std::string> items = game.host_get_owned_items();
std::string equipped = game.host_get_equipped("weapon");

// Scene state
// (need to expose current scene ID in Game class)
```

**Test harness wraps these**:
```cpp
class HeadlessTestRunner {
public:
    float player_health_frac() const { return game_->host_player_health_frac(); }
    float enemy_health_frac() const { return game_->host_enemy_health_frac(); }
    std::string round_outcome() const { return game_->host_round_outcome(); }
    int currency() const { return game_->host_get_currency(); }
    bool has_item(const std::string& id) const { return game_->host_has_item(id); }
    
    // Direct access for advanced assertions
    game::Game& game() { return *game_; }
};
```

---

### Q5: Should tests use the existing scene system or bypass it?

**A**: **Use the existing scene system**.

The whole point of integration tests is to test the **real game flow**, not a mocked version.

**How to skip boot sequence**: Use `game.set_start_scene("Battle")` to start directly in the desired scene, but let the scene system manage transitions and updates normally.

```cpp
// Skip Boot -> Loading -> MainMenu, start directly in Battle
game.set_start_scene("Battle");
game.set_start_location("Dojo");
game.on_init(platform);

// Now run the Battle scene normally
game.on_update(platform, 16);
game.on_render(platform);
```

**Why this is better**:
- Tests the real scene transitions
- Tests the real scene update/render logic
- Catches integration bugs between Game, Scene, and Renderer
- No code duplication

---

## Architecture Overview

```
Test Harness
    │
    ├─ TestPlatform (extends NullPlatform)
    │   └─ Provides fixed time (now_ms returns controlled value)
    │
    ├─ SoftwareRendererAdapter (implements IRenderer)
    │   └─ Wraps soft::Renderer (CPU rendering to framebuffer)
    │
    └─ HeadlessTestRunner
        ├─ Creates TestPlatform
        ├─ Creates SoftwareRendererAdapter
        ├─ Creates real Game class
        ├─ Injects software renderer via game.set_renderer()
        ├─ Runs N frames with fixed dt=16ms
        └─ Provides state access for assertions

Game Class
    ├─ Uses IRenderer interface (not concrete renderer)
    ├─ Uses Platform interface (TestPlatform for tests)
    ├─ Uses existing Scene system (no bypass)
    └─ Exposes state via existing host_* methods

Renderers
    ├─ ren::Renderer (GL) ─── implements IRenderer ─── uses OpenGL
    └─ SoftwareRendererAdapter ─── implements IRenderer ─── uses soft::Renderer
```

---

## File Structure

### New Files
```
engine/renderer/
├── irenderer.hpp                      # IRenderer interface
├── software_renderer_adapter.hpp      # Adapter for soft::Renderer
└── software_renderer_adapter.cpp

tests/
├── headless_test_runner.hpp           # Test harness
├── headless_test_runner.cpp
├── test_platform.hpp                  # TestPlatform with fixed time
├── test_platform.cpp
└── integration/
    ├── test_battle_integration.cpp    # Battle scene test
    ├── test_shop_integration.cpp      # Shop test
    ├── test_menu_integration.cpp      # Menu toggle test
    └── test_crash_stability.cpp       # 1000 frames stability test
```

### Modified Files
```
engine/game/game_clean.hpp             # Change renderer_ to IRenderer
engine/game/game.cpp                   # Update renderer creation
engine/renderer/renderer.hpp           # Implement IRenderer
engine/renderer/renderer.cpp           # Implement IRenderer methods
tests/CMakeLists.txt                   # Add integration test targets
```

---

## Example Test (Battle Integration)

```cpp
// tests/integration/test_battle_integration.cpp

#include "headless_test_runner.hpp"
#include <cassert>
#include <iostream>

int main() {
    // Setup
    resf2::test::HeadlessTestRunner::Config config;
    config.asset_root = "assets";
    config.fixed_dt_ms = 16;
    
    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) {
        std::cerr << "Failed to initialize\n";
        return 1;
    }
    
    // Configure game
    auto& game = runner.game();
    game.set_start_scene("Battle");
    game.set_start_location("Dojo");
    
    game::SceneHost::BattleInfo info;
    info.enemy_name = "Kenji";
    info.rounds = 1;
    info.round_time_s = 99;
    game.host_set_battle_info(info);
    
    // Initialize
    game.on_init(runner.platform());
    
    // Record initial state
    float initial_player_hp = runner.player_health_frac();
    float initial_enemy_hp = runner.enemy_health_frac();
    
    std::cout << "Initial: player=" << initial_player_hp 
              << " enemy=" << initial_enemy_hp << "\n";
    
    // Run simulation (500 frames = ~8 seconds at 60fps)
    runner.run_frames(500);
    
    // Check results
    float final_player_hp = runner.player_health_frac();
    float final_enemy_hp = runner.enemy_health_frac();
    std::string outcome = runner.round_outcome();
    
    std::cout << "Final: player=" << final_player_hp 
              << " enemy=" << final_enemy_hp 
              << " outcome=" << outcome << "\n";
    
    // Assertions
    assert(final_player_hp < initial_player_hp && "Player should take damage");
    assert(final_enemy_hp < initial_enemy_hp && "Enemy should take damage");
    assert(outcome == "victory" || outcome == "defeat" || outcome.empty());
    assert(!game.quit_requested() && "Game should not crash");
    
    std::cout << "✓ Battle integration test PASSED\n";
    return 0;
}
```

---

## CMake Integration

```cmake
# tests/CMakeLists.txt

function(add_integration_test test_name)
    add_executable(${test_name}
        integration/${test_name}.cpp
        headless_test_runner.cpp
        test_platform.cpp
    )
    target_link_libraries(${test_name} PRIVATE
        resf2_game
        resf2_renderer_software  # Software renderer (no GL)
        resf2_platform
        resf2_scene
        resf2_format
        resf2_reverse
        resf2_warnings
        Threads::Threads
    )
    target_compile_features(${test_name} PRIVATE cxx_std_23)
    add_test(NAME ${test_name} 
             COMMAND ${test_name} 
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

# Integration tests
add_integration_test(test_battle_integration)
add_integration_test(test_shop_integration)
add_integration_test(test_menu_integration)
add_integration_test(test_crash_stability)
```

---

## Implementation Plan

### Phase 1: Renderer Abstraction (2-3 days)
1. Create `IRenderer` interface
2. Refactor GL renderer to implement `IRenderer`
3. Create `SoftwareRendererAdapter`
4. Update Game class to use `IRenderer`
5. Verify GL build still works

### Phase 2: Test Harness (1-2 days)
6. Create `TestPlatform` with fixed time
7. Create `HeadlessTestRunner`
8. Implement `run_frames`, input injection, state accessors

### Phase 3: Integration Tests (2-3 days)
9. Implement `test_battle_integration.cpp`
10. Implement `test_shop_integration.cpp`
11. Implement `test_menu_integration.cpp`
12. Implement `test_crash_stability.cpp`

### Phase 4: Validation (1 day)
13. Run all tests, verify they pass
14. Introduce a known bug, verify tests catch it
15. Fix bug, verify tests pass

**Total**: 6-9 days

---

## Key Benefits

1. ✅ Tests the **real Game class** (not mocks)
2. ✅ Catches **visual/behavioral bugs** (not just text output)
3. ✅ Runs on **CI without GPU** (software renderer)
4. ✅ **Deterministic** (fixed dt=16ms)
5. ✅ **Easy to add new tests** (< 50 lines each)
6. ✅ **No regression** (GL build unchanged)

---

## Deliverables

I've created three documents:

1. **`.planning/adr/ADR-004-headless-integration-tests.md`**  
   Architecture Decision Record with full context, decision, trade-offs, and alternatives

2. **`.planning/headless-integration-test-design.md`**  
   Detailed technical design with class diagrams, implementation details, and examples

3. **`.planning/headless-integration-test-summary.md`** (this file)  
   Quick reference answering your specific questions

---

## Next Steps

1. **Review the design** — Let me know if you have questions or want changes
2. **Approve the ADR** — Once approved, I'll start implementation
3. **Implement Phase 1** — Renderer abstraction (biggest change, most risk)
4. **Implement Phases 2-3** — Test harness and integration tests
5. **Validate** — Introduce bugs, verify tests catch them

Ready to proceed when you are!
