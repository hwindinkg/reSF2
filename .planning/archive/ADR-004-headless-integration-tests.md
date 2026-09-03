> ⚠️ УСТАРЕЛО. Этот документ описывает реверс нативного/Unity билда SF2,
> НЕ веб-версии. Не использовать как источник истины для порта.
> Валидный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.

# ADR-004: Headless Integration Test Architecture

**Status**: Proposed  
**Date**: 2026-07-28  
**Supersedes**: Current text-based test approach

## Context

The current test suite (27 tests) passes but provides weak coverage:
- Tests parse textual traces (`[STATE]`, `[SCENE]`) but don't verify visual/behavioral correctness
- No tests run the actual game loop with real scenes
- `headless_main.cpp` exists but is a **separate** Game implementation for screenshots, not integration with the real Game class
- The real Game class (`engine/game/game_clean.hpp`, 4391 lines) has a hard dependency on the OpenGL renderer (`std::unique_ptr<ren::Renderer>`)

**Problem**: We need integration tests that:
1. Instantiate the **real Game class** (not a mock)
2. Run N frames with fixed dt=16ms
3. Assert on game state (HP changes, round completion, catalog loaded, menu visibility)
4. Catch visual/behavioral bugs, not just text output

**Constraints**:
- C++23, CMake, ctest
- No OpenGL/GLFW for headless tests
- Must work on CI without GPU
- Software renderer exists at `engine/renderer/software_renderer.hpp` but Game class doesn't use it

## Decision

### Architecture Overview

We will introduce a **renderer abstraction layer** that allows the Game class to work with either the GL renderer or the software renderer. This enables headless integration tests to use the real Game class with the software renderer.

```
┌─────────────────────────────────────────────────────────────┐
│                    Test Harness                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  HeadlessTestRunner                                   │   │
│  │  - Creates NullPlatform                               │   │
│  │  - Creates SoftwareRenderer (wrapped as IRenderer)    │   │
│  │  - Creates real Game class                            │   │
│  │  - Runs N frames with fixed dt=16ms                   │   │
│  │  - Provides state access for assertions               │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Game Class                                │
│  - Uses IRenderer interface (not concrete ren::Renderer)    │
│  - Uses Platform interface (NullPlatform for tests)         │
│  - Uses existing Scene system (no bypass)                   │
│  - Exposes state via existing host_* methods                │
└─────────────────────────────────────────────────────────────┘
                            │
              ┌─────────────┴─────────────┐
              ▼                           ▼
    ┌─────────────────┐         ┌─────────────────┐
    │  GL Renderer    │         │ Software Renderer│
    │  (ren::Renderer)│         │ (soft::Renderer) │
    │  - OpenGL       │         │ - CPU framebuffer│
    │  - GLFW window  │         │ - No GPU needed  │
    └─────────────────┘         └─────────────────┘
```

### Key Design Decisions

#### 1. Renderer Abstraction (IRenderer Interface)

**Problem**: Game class currently uses `std::unique_ptr<ren::Renderer>` (GL-specific).

**Solution**: Create an abstract `IRenderer` interface that both GL and Software renderers implement.

```cpp
// engine/renderer/irenderer.hpp
namespace resf2::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;
    
    virtual bool init(int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(int width, int height) = 0;
    
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    
    virtual void set_clear_color(float r, float g, float b, float a = 1.0f) = 0;
    
    // Texture operations (polymorphic)
    virtual void* create_texture(int w, int h, const uint8_t* pixels) = 0;
    virtual void destroy_texture(void* tex) = 0;
    
    // Drawing operations (use variant/any for texture handle)
    virtual void draw_textured_quad(
        void* texture,
        float x, float y, float w, float h,
        float u0, float v0, float u1, float u1,
        Color4B color
    ) = 0;
    
    virtual void draw_textured_quad_screen(
        void* texture,
        float x, float y, float w, float h,
        float u0, float v0, float u1, float v1,
        Color4B color
    ) = 0;
    
    // Primitives
    virtual void draw_filled_rect_screen(float x, float y, float w, float h, Color4B color) = 0;
    virtual void draw_filled_circle_screen(float cx, float cy, float radius, Color4B color) = 0;
    virtual void draw_line_screen(float x0, float y0, float x1, float y1, Color4B color) = 0;
    virtual void draw_line_world(float x0, float y0, float x1, float y1, Color4B color) = 0;
    
    // Camera
    virtual void camera_set_target(float x, float y) = 0;
    virtual void camera_set_zoom(float zoom) = 0;
    
    // State query (for tests)
    virtual int width() const = 0;
    virtual int height() const = 0;
};

} // namespace resf2::renderer
```

**Trade-offs**:
| Benefit | Cost |
|---------|------|
| Game class works with both GL and software renderers | Requires refactoring existing GL renderer to implement interface |
| Headless tests can use real Game class | Slight performance overhead from virtual calls (~1-2%) |
| Cleaner separation of concerns | Need to handle texture handles polymorphically (void* or variant) |

**Alternatives Considered**:
- **Option A**: Create a `HeadlessGame` subclass that overrides rendering — rejected because it duplicates Game logic
- **Option B**: Compile-time switch via templates — rejected because it requires two builds and complicates CMake
- **Option C**: Runtime polymorphism via IRenderer — **CHOSEN** for flexibility and minimal code duplication

#### 2. Software Renderer Adapter

**Problem**: `soft::Renderer` has a different API than `ren::Renderer`.

**Solution**: Create a `SoftwareRendererAdapter` that wraps `soft::Renderer` and implements `IRenderer`.

```cpp
// engine/renderer/software_renderer_adapter.hpp
namespace resf2::renderer {

class SoftwareRendererAdapter : public IRenderer {
public:
    SoftwareRendererAdapter();
    ~SoftwareRendererAdapter() override;
    
    bool init(int width, int height) override;
    void shutdown() override;
    void resize(int width, int height) override;
    
    void begin_frame() override;
    void end_frame() override;
    
    void set_clear_color(float r, float g, float b, float a) override;
    
    void* create_texture(int w, int h, const uint8_t* pixels) override;
    void destroy_texture(void* tex) override;
    
    void draw_textured_quad(void* texture, float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, Color4B color) override;
    
    // ... other IRenderer methods
    
    // Test-specific accessors
    const std::vector<uint8_t>& framebuffer() const;
    bool save_screenshot(const std::string& path) const;
    
private:
    soft::Renderer impl_;
    std::unordered_map<void*, std::unique_ptr<soft::Texture>> textures_;
};

} // namespace resf2::renderer
```

**Texture Handle Strategy**: Use `void*` as an opaque handle. The adapter maps `void*` to `soft::Texture*` internally. This keeps the interface simple and avoids exposing software-specific types to Game.

#### 3. Game Class Refactoring

**Changes Required**:

```cpp
// engine/game/game_clean.hpp
class Game final : public rt::IGame, public scene::SceneHost {
public:
    // Constructor signature unchanged
    explicit Game(std::string asset_root, bool replay_mode = false, bool dump_state = false);
    
    // on_init now accepts renderer type via platform or explicit injection
    void on_init(plat::Platform& platform) override;
    
    // New: inject renderer explicitly (for tests)
    void set_renderer(std::unique_ptr<renderer::IRenderer> renderer);
    
    // ... rest unchanged
    
private:
    // Changed from: std::unique_ptr<ren::Renderer> renderer_;
    std::unique_ptr<renderer::IRenderer> renderer_;
};
```

**Backward Compatibility**: The default `on_init` creates a GL renderer (existing behavior). Tests inject a software renderer via `set_renderer` before calling `on_init`.

#### 4. HeadlessTestRunner

**Purpose**: Encapsulates test setup and frame execution.

```cpp
// tests/headless_test_runner.hpp
namespace resf2::test {

class HeadlessTestRunner {
public:
    struct Config {
        std::string asset_root = "assets";
        int width = 1280;
        int height = 720;
        uint32_t fixed_dt_ms = 16;  // 16ms = 60fps
        bool save_screenshots = false;
        std::string screenshot_dir = "screenshots_test";
    };
    
    explicit HeadlessTestRunner(Config config = {});
    ~HeadlessTestRunner();
    
    // Initialize the game with software renderer
    bool init();
    
    // Run N frames with fixed dt
    void run_frames(int count);
    
    // Run until a condition is met or max_frames reached
    void run_until(std::function<bool()> condition, int max_frames = 10000);
    
    // Inject input events
    void inject_key_down(platform::Key key);
    void inject_key_up(platform::Key key);
    void inject_pointer_down(float x, float y);
    void inject_pointer_up();
    
    // State access (delegates to Game's host_* methods)
    float player_health_frac() const;
    float enemy_health_frac() const;
    std::string round_outcome() const;
    int player_wins() const;
    int enemy_wins() const;
    int currency() const;
    bool has_item(const std::string& item_id) const;
    std::string current_scene() const;
    bool is_menu_visible() const;
    
    // Direct Game access for advanced assertions
    game::Game& game() { return *game_; }
    const game::Game& game() const { return *game_; }
    
    // Screenshot (for debugging)
    bool save_screenshot(const std::string& name) const;
    
private:
    Config config_;
    std::unique_ptr<platform::NullPlatform> platform_;
    std::unique_ptr<renderer::SoftwareRendererAdapter> renderer_;
    std::unique_ptr<game::Game> game_;
    int frame_count_ = 0;
};

} // namespace resf2::test
```

#### 5. Test Structure

**File Layout**:
```
tests/
├── headless_test_runner.hpp          # Test harness
├── headless_test_runner.cpp
├── integration/
│   ├── test_battle_integration.cpp   # Battle scene with AI
│   ├── test_shop_integration.cpp     # Shop catalog, buy/sell
│   ├── test_menu_integration.cpp     # Menu toggle per scene
│   └── test_crash_stability.cpp      # 1000 frames without crash
└── CMakeLists.txt                    # Updated to build integration tests
```

**Example Test Pseudocode**:

```cpp
// tests/integration/test_battle_integration.cpp
#include "headless_test_runner.hpp"
#include <cassert>
#include <iostream>

int main() {
    resf2::test::HeadlessTestRunner::Config config;
    config.asset_root = "assets";
    config.fixed_dt_ms = 16;
    
    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) {
        std::cerr << "Failed to initialize test runner\n";
        return 1;
    }
    
    // Navigate to Battle scene (skip menus via set_start_scene)
    runner.game().set_start_scene("Battle");
    runner.game().set_start_location("Dojo");
    
    // Set battle info (1 round, 99s timer)
    game::SceneHost::BattleInfo info;
    info.enemy_name = "Kenji";
    info.rounds = 1;
    info.round_time_s = 99;
    runner.game().host_set_battle_info(info);
    
    // Re-initialize to apply scene override
    runner.game().on_init(*runner.platform());
    
    // Record initial HP
    float initial_player_hp = runner.player_health_frac();
    float initial_enemy_hp = runner.enemy_health_frac();
    
    std::cout << "Initial HP: player=" << initial_player_hp 
              << " enemy=" << initial_enemy_hp << "\n";
    
    // Run 500 frames (~8 seconds at 60fps)
    runner.run_frames(500);
    
    // Check that combat happened
    float final_player_hp = runner.player_health_frac();
    float final_enemy_hp = runner.enemy_health_frac();
    
    std::cout << "Final HP: player=" << final_player_hp 
              << " enemy=" << final_enemy_hp << "\n";
    
    // Assertions
    assert(final_player_hp < initial_player_hp && "Player should take damage");
    assert(final_enemy_hp < initial_enemy_hp && "Enemy should take damage");
    
    // Check round outcome
    std::string outcome = runner.round_outcome();
    std::cout << "Round outcome: " << outcome << "\n";
    assert(outcome == "victory" || outcome == "defeat" || outcome == "");
    
    // Check no crashes, game still running
    assert(!runner.game().quit_requested());
    
    std::cout << "Battle integration test PASSED\n";
    return 0;
}
```

#### 6. CMake Integration

```cmake
# tests/CMakeLists.txt

# Integration test target
add_executable(test_battle_integration
    integration/test_battle_integration.cpp
    headless_test_runner.cpp
)
target_link_libraries(test_battle_integration PRIVATE
    resf2_game
    resf2_platform
    resf2_renderer_software  # New target for software renderer
    resf2_scene
    resf2_warnings
)
target_compile_features(test_battle_integration PRIVATE cxx_std_23)
add_test(NAME test_battle_integration 
         COMMAND test_battle_integration 
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Repeat for other integration tests...
```

### Implementation Plan

#### Phase 1: Renderer Abstraction (Wave 4)
1. Create `engine/renderer/irenderer.hpp` with `IRenderer` interface
2. Refactor `ren::Renderer` to implement `IRenderer` (GL backend)
3. Create `engine/renderer/software_renderer_adapter.hpp/cpp`
4. Update `Game` class to use `IRenderer` instead of concrete `ren::Renderer`
5. Verify GL build still works (no regression)

#### Phase 2: Test Harness (Wave 4)
6. Create `tests/headless_test_runner.hpp/cpp`
7. Implement `HeadlessTestRunner` with software renderer injection
8. Add `run_frames`, `run_until`, input injection, state accessors

#### Phase 3: Integration Tests (Wave 4)
9. Implement `test_battle_integration.cpp`
10. Implement `test_shop_integration.cpp`
11. Implement `test_menu_integration.cpp`
12. Implement `test_crash_stability.cpp`
13. Update `tests/CMakeLists.txt`

#### Phase 4: Validation
14. Run all integration tests, verify they fail when they should
15. Introduce a known bug (e.g., break HP calculation), verify test catches it
16. Fix bug, verify test passes
17. Document test results in `docs/INTEGRATION_TEST_RESULTS.md`

## Trade-offs

| Benefit | Cost |
|---------|------|
| Real Game class tested, not mocks | Requires refactoring renderer to use interface |
| Catches visual/behavioral bugs | Slight performance overhead from virtual calls |
| Software renderer provides framebuffer for screenshot comparison | Need to maintain two renderer implementations |
| Tests run on CI without GPU | Initial setup effort (~2-3 days) |
| Existing scene system reused (no bypass) | Game class must expose state for assertions (already does via host_* methods) |

## Alternatives Considered

### Option A: Mock Game Class
**Approach**: Create a `MockGame` that simulates Game behavior without running real logic.

**Why Rejected**: 
- Doesn't test real Game class
- Duplicates Game logic
- Can't catch integration bugs between Game, Scene, and Renderer

### Option B: Compile-Time Renderer Switch
**Approach**: Use templates or `#ifdef` to switch between GL and software renderer at compile time.

**Why Rejected**:
- Requires two builds (GL and headless)
- Complicates CMake
- Can't switch at runtime for different test scenarios

### Option C: Screen Scraping GL Output
**Approach**: Use OpenGL in headless mode (e.g., Mesa llvmpipe) and screenshot the output.

**Why Rejected**:
- Requires OpenGL drivers on CI (not always available)
- Slower than software renderer
- More complex setup (EGL/OSMesa)

### Option D: Extend headless_main.cpp
**Approach**: Make `headless_main.cpp` use the real Game class instead of its own implementation.

**Why Rejected**:
- `headless_main.cpp` is designed for screenshots, not integration testing
- Doesn't provide state access for assertions
- No test harness or framework

## Consequences

### What Becomes Easier
- Running integration tests on CI without GPU
- Debugging game logic by inspecting software renderer framebuffer
- Adding new integration tests (just use `HeadlessTestRunner`)
- Catching regression bugs in combat, shop, menu systems

### What Becomes Harder
- Initial refactoring effort to abstract renderer (~2-3 days)
- Maintaining two renderer implementations (GL and software)
- Ensuring software renderer matches GL behavior (visual parity)

### Risks
- **Visual Parity**: Software renderer may not perfectly match GL renderer → Mitigate by comparing screenshots
- **Performance**: Software renderer is slower than GL → Mitigate by limiting test frame count (1000 frames max)
- **Texture Handle Management**: Using `void*` for texture handles is error-prone → Mitigate with strict ownership rules in adapter

## Migration Plan

### Backward Compatibility
- Existing GL build unchanged (Game still uses GL renderer by default)
- New `set_renderer` method is optional; if not called, Game creates GL renderer in `on_init`
- No changes to scene code (scenes use SceneHost interface, not renderer directly)

### Rollout Strategy
1. Implement renderer abstraction in feature branch
2. Verify GL build still works (no regression)
3. Add software renderer adapter
4. Add test harness
5. Add integration tests one by one
6. Merge to main after all tests pass

## References

- Current Game class: `engine/game/game_clean.hpp` (4391 lines)
- GL Renderer: `engine/renderer/renderer.hpp`
- Software Renderer: `engine/renderer/software_renderer.hpp`
- Platform Interface: `engine/platform/platform.hpp`
- NullPlatform: `engine/platform/platform.cpp` (lines 240-290)
- Existing Tests: `tests/` directory (27 tests, text-based)
- headless_main.cpp: Separate Game implementation for screenshots (1526 lines)
