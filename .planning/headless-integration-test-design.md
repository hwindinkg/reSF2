# Headless Integration Test Design

**Companion to ADR-004**  
**Date**: 2026-07-28

## Executive Summary

This document provides the technical design for headless integration tests that run the **real Game class** without OpenGL/GLFW. The solution introduces a renderer abstraction layer allowing the Game class to work with either the GL renderer or the software renderer.

**Key Innovation**: Instead of mocking the Game class, we inject a software renderer, enabling true integration testing of the entire game loop.

---

## 1. Class Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        Test Layer                                │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │          HeadlessTestRunner                               │   │
│  │----------------------------------------------------------│   │
│  │ + init(): bool                                             │   │
│  │ + run_frames(count: int): void                             │   │
│  │ + run_until(cond: function, max: int): void               │   │
│  │ + inject_key_down(key: Key): void                          │   │
│  │ + inject_key_up(key: Key): void                            │   │
│  │ + inject_pointer_down(x: float, y: float): void           │   │
│  │ + inject_pointer_up(): void                                │   │
│  │ + player_health_frac(): float                              │   │
│  │ + enemy_health_frac(): float                               │   │
│  │ + round_outcome(): string                                  │   │
│  │ + currency(): int                                          │   │
│  │ + has_item(id: string): bool                               │   │
│  │ + current_scene(): string                                  │   │
│  │ + save_screenshot(name: string): bool                      │   │
│  │----------------------------------------------------------│   │
│  │ - platform_: unique_ptr<NullPlatform>                     │   │
│  │ - renderer_: unique_ptr<SoftwareRendererAdapter>          │   │
│  │ - game_: unique_ptr<Game>                                 │   │
│  │ - config_: Config                                         │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Game Layer                                │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    Game (game_clean.hpp)                  │   │
│  │----------------------------------------------------------│   │
│  │ + on_init(platform: Platform&): void                      │   │
│  │ + on_update(platform: Platform&, dt: uint32): void        │   │
│  │ + on_render(platform: Platform&): void                    │   │
│  │ + set_renderer(renderer: unique_ptr<IRenderer>): void     │   │
│  │ + host_player_health_frac(): float                        │   │
│  │ + host_enemy_health_frac(): float                         │   │
│  │ + host_round_outcome(): string                            │   │
│  │ + host_get_currency(): int                                │   │
│  │ + host_has_item(id: string): bool                         │   │
│  │ + ... (other host_* methods)                              │   │
│  │----------------------------------------------------------│   │
│  │ - renderer_: unique_ptr<IRenderer>  ◄─── CHANGED          │   │
│  │ - scene_manager_: SceneManager                            │   │
│  │ - player_profile_: PlayerProfile                          │   │
│  │ - assets_: AssetManager                                   │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Renderer Abstraction                         │
│                                                                  │
│  ┌────────────────────────┐         ┌────────────────────────┐  │
│  │    IRenderer           │         │ SoftwareRendererAdapter │  │
│  │  (interface)           │◄────────┤                        │  │
│  │------------------------│  impl   │------------------------│  │
│  │ + init(w, h): bool     │         │ + init(w, h): bool     │  │
│  │ + begin_frame(): void  │         │ + begin_frame(): void  │  │
│  │ + end_frame(): void    │         │ + end_frame(): void    │  │
│  │ + draw_textured_quad() │         │ + draw_textured_quad() │  │
│  │ + draw_filled_rect()   │         │ + draw_filled_rect()   │  │
│  │ + create_texture()     │         │ + create_texture()     │  │
│  │ + ...                  │         │ + framebuffer()        │  │
│  └────────────────────────┘         └────────────────────────┘  │
│              ▲                                │                  │
│              │ implements                     │ wraps            │
│              │                                ▼                  │
│  ┌────────────────────────┐         ┌────────────────────────┐  │
│  │    Renderer (GL)       │         │   soft::Renderer       │  │
│  │  (existing)            │         │   (existing)           │  │
│  │------------------------│         │------------------------│  │
│  │ - OpenGL calls         │         │ - CPU framebuffer      │  │
│  │ - GLFW window          │         │ - No GPU needed        │  │
│  └────────────────────────┘         └────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Platform Layer                              │
│                                                                  │
│  ┌────────────────────────┐         ┌────────────────────────┐  │
│  │   Platform (interface) │         │   NullPlatform         │  │
│  │------------------------│◄────────┤------------------------│  │
│  │ + poll_events(): bool  │  impl   │ + poll_events(): bool  │  │
│  │ + input(): InputState  │         │ + input(): InputState  │  │
│  │ + now_ms(): uint64     │         │ + inject_key_down()    │  │
│  │ + ...                  │         │ + inject_key_up()      │  │
│  └────────────────────────┘         └────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────┐                                      │
│  │   GlfwPlatform         │  (not used in tests)                │
│  │   - GLFW window        │                                      │
│  │   - OpenGL context     │                                      │
│  └────────────────────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. How HeadlessPlatform Works

**Actually, we reuse the existing `NullPlatform`** — no need to create a new one!

`NullPlatform` already provides:
- No-op GL context (`make_gl_current()` returns true but does nothing)
- Controllable time (`now_ms()` uses real time, but we can override)
- Input injection (`inject_key_down`, `inject_pointer_down`, etc.)
- File I/O (reads real files from disk)

**Extension for Tests**: Add a method to inject fixed time:

```cpp
// In NullPlatform (or a TestPlatform subclass)
class TestPlatform : public platform::NullPlatform {
public:
    // Override time to return fixed increments
    void set_fixed_time_ms(uint64_t ms) { fixed_time_ms_ = ms; }
    void advance_time_ms(uint64_t ms) { fixed_time_ms_ += ms; }
    
    [[nodiscard]] uint64_t now_ms() const noexcept override {
        return fixed_time_ms_;
    }
    
private:
    uint64_t fixed_time_ms_ = 0;
};
```

This allows tests to run with **perfectly fixed dt=16ms** instead of relying on real wall-clock time.

---

## 3. How to Run N Frames

### Game Loop Integration

The Game class already implements `rt::IGame` with these methods:
```cpp
void on_init(plat::Platform& platform) override;
void on_update(plat::Platform& platform, uint32_t dt) override;
void on_render(plat::Platform& platform) override;
void on_shutdown(plat::Platform& platform) override;
```

### Running Frames in Tests

```cpp
void HeadlessTestRunner::run_frames(int count) {
    for (int i = 0; i < count; ++i) {
        // 1. Poll events (NullPlatform processes injected input)
        if (!platform_->poll_events()) {
            break;  // quit requested
        }
        
        // 2. Update game logic with fixed dt
        game_->on_update(*platform_, config_.fixed_dt_ms);
        
        // 3. Render frame (software renderer writes to framebuffer)
        game_->on_render(*platform_);
        
        // 4. Advance fixed time
        platform_->advance_time_ms(config_.fixed_dt_ms);
        
        frame_count_++;
        
        // 5. Optionally save screenshot
        if (config_.save_screenshots && (i % 60 == 0)) {
            save_screenshot("frame_" + std::to_string(i));
        }
    }
}
```

### Fixed dt Injection

**Problem**: Real game loop uses wall-clock time (`platform.now_ms()`), which varies.

**Solution**: `TestPlatform` overrides `now_ms()` to return a controlled value:
```cpp
// Initial time
platform_->set_fixed_time_ms(0);

// After each frame
platform_->advance_time_ms(16);  // fixed 16ms = 60fps
```

This ensures **deterministic, reproducible tests**.

---

## 4. How to Access State for Assertions

The Game class already exposes extensive state via `host_*` methods. `HeadlessTestRunner` wraps these for convenient access:

### Health and Combat State

```cpp
float HeadlessTestRunner::player_health_frac() const {
    return game_->host_player_health_frac();
}

float HeadlessTestRunner::enemy_health_frac() const {
    return game_->host_enemy_health_frac();
}

std::string HeadlessTestRunner::round_outcome() const {
    return game_->host_round_outcome();  // "", "victory", "defeat"
}

int HeadlessTestRunner::player_wins() const {
    return game_->host_get_wins();
}
```

### Inventory and Shop State

```cpp
bool HeadlessTestRunner::has_item(const std::string& item_id) const {
    return game_->host_has_item(item_id);
}

int HeadlessTestRunner::currency() const {
    return game_->host_get_currency();
}

std::vector<std::string> HeadlessTestRunner::owned_items() const {
    return game_->host_get_owned_items();
}

std::string HeadlessTestRunner::equipped(const std::string& slot) const {
    return game_->host_get_equipped(slot);
}
```

### Scene State

```cpp
std::string HeadlessTestRunner::current_scene() const {
    // Access SceneManager via Game
    auto scene_id = game_->scene_manager().current_scene_id();
    return scene::scene_name(scene_id);
}

bool HeadlessTestRunner::is_menu_visible() const {
    // Check if menu overlay is open
    // (requires exposing menu state in Game class)
    return game_->is_menu_overlay_visible();
}
```

### Direct Game Access

For advanced assertions, tests can access the Game class directly:

```cpp
// Example: Check player position
auto& game = runner.game();
float player_x = game.player_pos_x();
float player_y = game.player_pos_y();

// Example: Check animation state
std::string anim = game.current_animation();

// Example: Check save data
auto& profile = game.player_profile();
int level = profile.level;
```

---

## 5. File Structure

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
CMakeLists.txt                         # Add software renderer target
tests/CMakeLists.txt                   # Add integration test targets
```

---

## 6. CMake Integration

### Software Renderer Library

```cmake
# engine/renderer/CMakeLists.txt (or top-level CMakeLists.txt)

# Software renderer (no GL dependency)
add_library(resf2_renderer_software STATIC
    software_renderer.cpp
    software_renderer_adapter.cpp
)
target_include_directories(resf2_renderer_software PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(resf2_renderer_software PUBLIC cxx_std_23)
target_link_libraries(resf2_renderer_software PUBLIC resf2_warnings)

# GL renderer (existing, now implements IRenderer)
add_library(resf2_renderer_gl STATIC
    renderer.cpp
    gl_loader.cpp
)
target_include_directories(resf2_renderer_gl PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(resf2_renderer_gl PUBLIC cxx_std_23)
target_link_libraries(resf2_renderer_gl PUBLIC 
    resf2_warnings
    opengl32  # Windows
    # ${OPENGL_LIBRARIES}  # Linux/Mac
)
```

### Integration Tests

```cmake
# tests/CMakeLists.txt

# Helper function to add integration tests
function(add_integration_test test_name)
    add_executable(${test_name}
        integration/${test_name}.cpp
        headless_test_runner.cpp
        test_platform.cpp
    )
    target_link_libraries(${test_name} PRIVATE
        resf2_game
        resf2_renderer_software  # Use software renderer
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

### Build Configuration

```cmake
# Top-level CMakeLists.txt

# Option to disable GL tests (for CI without GPU)
option(RESF2_BUILD_GL_TESTS "Build GL-dependent tests" ON)

if(RESF2_BUILD_GL_TESTS)
    # GL renderer + GL tests
    add_subdirectory(engine/renderer)
    # ... GL test targets ...
endif()

# Software renderer + integration tests (always build)
add_library(resf2_renderer_software STATIC ...)
add_subdirectory(tests)  # Integration tests use software renderer
```

---

## 7. Example Test Pseudocode

### Battle Integration Test

```cpp
// tests/integration/test_battle_integration.cpp

#include "headless_test_runner.hpp"
#include <cassert>
#include <iostream>

int main() {
    // 1. Setup
    resf2::test::HeadlessTestRunner::Config config;
    config.asset_root = "assets";
    config.fixed_dt_ms = 16;
    config.save_screenshots = false;
    
    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) {
        std::cerr << "Failed to initialize\n";
        return 1;
    }
    
    // 2. Configure game state
    auto& game = runner.game();
    game.set_start_scene("Battle");
    game.set_start_location("Dojo");
    
    game::SceneHost::BattleInfo info;
    info.enemy_name = "Kenji";
    info.rounds = 1;
    info.round_time_s = 99;
    game.host_set_battle_info(info);
    
    // 3. Initialize (creates software renderer, loads assets)
    game.on_init(runner.platform());
    
    // 4. Record initial state
    float initial_player_hp = runner.player_health_frac();
    float initial_enemy_hp = runner.enemy_health_frac();
    
    std::cout << "Initial: player=" << initial_player_hp 
              << " enemy=" << initial_enemy_hp << "\n";
    
    // 5. Run simulation
    runner.run_frames(500);  // ~8 seconds at 60fps
    
    // 6. Check results
    float final_player_hp = runner.player_health_frac();
    float final_enemy_hp = runner.enemy_health_frac();
    std::string outcome = runner.round_outcome();
    
    std::cout << "Final: player=" << final_player_hp 
              << " enemy=" << final_enemy_hp 
              << " outcome=" << outcome << "\n";
    
    // 7. Assertions
    assert(final_player_hp < initial_player_hp && "Player should take damage");
    assert(final_enemy_hp < initial_enemy_hp && "Enemy should take damage");
    assert(outcome == "victory" || outcome == "defeat" || outcome.empty());
    assert(!game.quit_requested() && "Game should not crash");
    
    std::cout << "✓ Battle integration test PASSED\n";
    return 0;
}
```

### Shop Integration Test

```cpp
// tests/integration/test_shop_integration.cpp

int main() {
    resf2::test::HeadlessTestRunner runner;
    runner.init();
    
    auto& game = runner.game();
    game.set_start_scene("Shop");
    game.on_init(runner.platform());
    
    // Wait for catalog to load
    runner.run_until([&]() {
        return game.host_get_list_data() != nullptr;
    }, 100);
    
    assert(game.host_get_list_data() != nullptr && "Catalog should load");
    
    // Record initial currency
    int initial_currency = runner.currency();
    std::cout << "Initial currency: " << initial_currency << "\n";
    
    // Try to buy an item (inject input to select and confirm)
    runner.inject_key_down(platform::Key::Enter);
    runner.run_frames(10);
    
    // Check currency decreased (if purchase succeeded)
    int final_currency = runner.currency();
    std::cout << "Final currency: " << final_currency << "\n";
    
    // Check item owned (if purchase succeeded)
    // (depends on which item was selected)
    
    std::cout << "✓ Shop integration test PASSED\n";
    return 0;
}
```

### Menu Integration Test

```cpp
// tests/integration/test_menu_integration.cpp

int main() {
    resf2::test::HeadlessTestRunner runner;
    runner.init();
    
    // Test menu toggle for each scene
    std::vector<std::string> scenes = {"MainMenu", "Map", "Shop", "Results"};
    
    for (const auto& scene_name : scenes) {
        auto& game = runner.game();
        game.set_start_scene(scene_name);
        game.on_init(runner.platform());
        
        // Wait for scene to initialize
        runner.run_frames(30);
        
        // Menu should be hidden initially
        assert(!runner.is_menu_visible() && "Menu should be hidden");
        
        // Press M to toggle menu
        runner.inject_key_down(platform::Key::M);
        runner.run_frames(5);
        
        // Menu should be visible
        assert(runner.is_menu_visible() && "Menu should be visible after M");
        
        // Press M again to close
        runner.inject_key_down(platform::Key::M);
        runner.run_frames(5);
        
        // Menu should be hidden again
        assert(!runner.is_menu_visible() && "Menu should be hidden after second M");
        
        std::cout << "✓ Menu toggle works in " << scene_name << "\n";
    }
    
    std::cout << "✓ Menu integration test PASSED\n";
    return 0;
}
```

### Crash Stability Test

```cpp
// tests/integration/test_crash_stability.cpp

int main() {
    resf2::test::HeadlessTestRunner runner;
    runner.init();
    
    auto& game = runner.game();
    game.set_start_scene("MainMenu");
    game.on_init(runner.platform());
    
    // Run 1000 frames (~16 seconds at 60fps)
    std::cout << "Running 1000 frames...\n";
    runner.run_frames(1000);
    
    // Check no crashes
    assert(!game.quit_requested() && "Game should not crash");
    
    // Check game state is still valid
    assert(runner.player_health_frac() >= 0.0f);
    assert(runner.player_health_frac() <= 1.0f);
    
    std::cout << "✓ Crash stability test PASSED (1000 frames without crash)\n";
    return 0;
}
```

---

## 8. Answers to Design Questions

### Q1: How to make the real Game class work without OpenGL?

**A**: Introduce an `IRenderer` interface. Refactor Game to use `std::unique_ptr<IRenderer>` instead of `std::unique_ptr<ren::Renderer>`. Create a `SoftwareRendererAdapter` that wraps `soft::Renderer` and implements `IRenderer`. Inject the software renderer in tests via `game.set_renderer()`.

### Q2: Should we create a HeadlessPlatform?

**A**: No — reuse the existing `NullPlatform`. Extend it with a `TestPlatform` subclass that overrides `now_ms()` to provide fixed time increments for deterministic tests.

### Q3: How to inject fixed dt=16ms into the game loop?

**A**: Call `game.on_update(platform, 16)` directly with fixed dt. Use `TestPlatform::advance_time_ms(16)` to simulate time passing. This bypasses real wall-clock time and ensures deterministic behavior.

### Q4: How to access game state for assertions?

**A**: The Game class already exposes state via `host_*` methods (e.g., `host_player_health_frac()`, `host_get_currency()`, `host_has_item()`). `HeadlessTestRunner` wraps these for convenient access. For advanced assertions, tests can access the Game class directly via `runner.game()`.

### Q5: Should tests use the existing scene system or bypass it?

**A**: Use the existing scene system. The whole point of integration tests is to test the real game flow. Use `game.set_start_scene("Battle")` to skip the boot sequence and start directly in the desired scene, but let the scene system manage transitions and updates normally.

---

## 9. Implementation Checklist

### Phase 1: Renderer Abstraction (2-3 days)
- [ ] Create `engine/renderer/irenderer.hpp` with `IRenderer` interface
- [ ] Refactor `ren::Renderer` to implement `IRenderer`
- [ ] Create `engine/renderer/software_renderer_adapter.hpp/cpp`
- [ ] Update `Game` class to use `IRenderer`
- [ ] Add `Game::set_renderer()` method
- [ ] Verify GL build still works

### Phase 2: Test Harness (1-2 days)
- [ ] Create `tests/test_platform.hpp/cpp` with fixed time
- [ ] Create `tests/headless_test_runner.hpp/cpp`
- [ ] Implement `run_frames`, `run_until`, input injection
- [ ] Implement state accessors (health, currency, inventory, scene)

### Phase 3: Integration Tests (2-3 days)
- [ ] Implement `test_battle_integration.cpp`
- [ ] Implement `test_shop_integration.cpp`
- [ ] Implement `test_menu_integration.cpp`
- [ ] Implement `test_crash_stability.cpp`
- [ ] Update `tests/CMakeLists.txt`

### Phase 4: Validation (1 day)
- [ ] Run all tests, verify they pass
- [ ] Introduce a known bug, verify tests catch it
- [ ] Fix bug, verify tests pass
- [ ] Document results in `docs/INTEGRATION_TEST_RESULTS.md`

**Total estimated effort**: 6-9 days

---

## 10. Success Criteria

The integration test architecture is successful if:

1. ✅ Tests instantiate the **real Game class** (not mocks)
2. ✅ Tests run N frames with fixed dt=16ms
3. ✅ Tests assert on game state (HP, currency, inventory, scene)
4. ✅ Tests catch visual/behavioral bugs (not just text output)
5. ✅ Tests run on CI without GPU
6. ✅ Tests are deterministic (same result every run)
7. ✅ GL build still works (no regression)
8. ✅ Adding new integration tests is easy (< 50 lines of code)

---

## 11. Future Enhancements

### Visual Regression Testing
Compare software renderer screenshots against reference images:
```cpp
runner.save_screenshot("battle_frame_100");
// Compare against reference: screenshots/reference/battle_frame_100.png
```

### Input Script Replay
Load input scripts (from `tests/data/`) and replay them:
```cpp
runner.load_input_script("tests/data/battle_combo_input.txt");
runner.run_frames(500);
```

### Performance Profiling
Measure frame time in software renderer to catch performance regressions:
```cpp
auto start = std::chrono::high_resolution_clock::now();
runner.run_frames(100);
auto end = std::chrono::high_resolution_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
assert(ms < 5000 && "100 frames should take < 5 seconds");
```

### Coverage Reporting
Track which Game methods are exercised by integration tests:
```bash
cmake -DRESF2_ENABLE_COVERAGE=ON ..
make coverage
```
