# Phase 2: Test Harness — Implementation Summary

## Overview
Implemented a headless test harness that wraps the real Game class with a software renderer, enabling integration tests to run without GPU/GL. The harness provides deterministic time control and input injection for reproducible tests.

## Files Created

### 1. `tests/test_platform.hpp` & `tests/test_platform.cpp`
- **TestPlatform** extends `platform::NullPlatform` (removed `final` keyword)
- Overrides `now_ms()` to return controlled, deterministic time
- Provides `set_fixed_time_ms()` and `advance_time_ms()` for time control
- Inherits all input injection methods from NullPlatform:
  - `inject_key_down(Key)`, `inject_key_up(Key)`
  - `inject_pointer_down(int32_t id, float x, float y)`
  - `inject_pointer_up(int32_t id)`
  - `inject_pointer_move(int32_t id, float x, float y)`

### 2. `tests/headless_test_runner.hpp` & `tests/headless_test_runner.cpp`
- **HeadlessTestRunner** wraps TestPlatform + SoftwareRendererAdapter + Game
- Provides convenient API for integration tests:
  - `init()` — initializes platform, renderer, and game
  - `run_frames(int count)` — runs N frames with fixed dt
  - `run_until(Pred, int max_frames)` — runs until condition or timeout
  - Input injection methods (delegate to TestPlatform)
  - State accessors (delegate to Game's host_* methods):
    - `player_health_frac()`, `enemy_health_frac()`
    - `round_outcome()`, `currency()`, `has_item()`
  - Direct access to `game()` and `platform()` for advanced assertions

### 3. `tests/integration/` directory
- Created for Phase 3 integration tests

### 4. `tests/integration/test_headless_runner.cpp`
- Smoke test that verifies the harness works:
  - Instantiates HeadlessTestRunner
  - Runs frames without crash
  - Tests input injection
  - Tests state accessors
  - Tests `run_until()` with success and timeout cases

## Files Modified

### 1. `engine/platform/platform.hpp`
- **Change:** Removed `final` keyword from `NullPlatform` class declaration
- **Reason:** Allow `TestPlatform` to extend `NullPlatform`
- **Impact:** None — `NullPlatform` is still safe to use directly

### 2. `engine/game/game.cpp` (line 282-286)
- **Change:** Added guard to skip GL renderer creation if custom renderer already injected
- **Before:**
  ```cpp
  renderer_ = std::make_unique<ren::Renderer>();
  if (!renderer_->init(platform.window_width(), platform.window_height())) {
      renderer_.reset(); return;
  }
  ```
- **After:**
  ```cpp
  if (!renderer_) {
      renderer_ = std::make_unique<ren::Renderer>();
      if (!renderer_->init(platform.window_width(), platform.window_height())) {
          renderer_.reset(); return;
      }
  }
  ```
- **Reason:** Allow `set_renderer()` to inject a software renderer for headless testing
- **Impact:** None — existing behavior unchanged when `set_renderer()` is not called

### 3. `tests/CMakeLists.txt`
- **Added:** `add_integration_test()` helper function
  - Links against full game + renderer + platform stack
  - Sets 120-second timeout
  - Runs from project root (for asset access)
- **Added:** `test_headless_runner` integration test
- **Added:** Commented-out placeholders for Phase 3 tests:
  - `test_battle_integration`
  - `test_shop_integration`
  - `test_menu_integration`
  - `test_crash_stability`

## Key Design Decisions

### 1. Deterministic Time Control
- TestPlatform overrides `now_ms()` to return a controlled value
- `advance_time_ms()` increments the clock by a fixed delta
- Ensures tests are reproducible regardless of wall-clock time

### 2. Input Injection
- Reuse NullPlatform's existing injection methods
- HeadlessTestRunner provides a clean wrapper API
- Uses `platform::Key` enum for type safety

### 3. Software Renderer
- Uses `SoftwareRendererAdapter` from Phase 1
- No GPU/GL required — all rendering happens in CPU memory
- Allows tests to run in CI without display

### 4. State Accessors
- Delegate to Game's `host_*` methods
- No need to add new methods to Game — all required methods already exist:
  - `host_player_health_frac()`, `host_enemy_health_frac()`
  - `host_round_outcome()`, `host_get_currency()`, `host_has_item()`

## Verification

### Build
```bash
cmake --build build --target test_headless_runner
```
✅ Build succeeds with 0 errors

### Test Results
```bash
ctest -C Debug
```
✅ **28/28 tests pass** (27 existing + 1 new)
- All existing tests still pass (no regressions)
- New `test_headless_runner` smoke test passes

### Smoke Test Output
```
=== HeadlessTestRunner smoke test ===
Initializing HeadlessTestRunner...
OK: init succeeded
OK: initial frame_count is 0
Running 10 frames...
OK: frame_count is 10
OK: frame_count is 15
Testing input injection...
OK: key injection works
OK: pointer injection works
Testing state accessors...
  player_health_frac: 1.000
  enemy_health_frac: 1.000
  round_outcome: ""
  currency: 0
  has_item("test_item"): false
OK: state accessors work
Testing run_until...
OK: run_until works
OK: run_until timeout works

=== ALL TESTS PASSED ===
```

## Issues Encountered & Resolved

### Issue 1: NullPlatform was `final`
- **Problem:** TestPlatform couldn't extend NullPlatform
- **Solution:** Removed `final` keyword from NullPlatform declaration
- **Impact:** None — NullPlatform is still safe to use directly

### Issue 2: Game::on_init always created GL renderer
- **Problem:** Even after calling `set_renderer()`, `on_init()` would overwrite with a GL renderer
- **Solution:** Added guard `if (!renderer_)` before creating GL renderer
- **Impact:** None — existing behavior unchanged when `set_renderer()` is not called

### Issue 3: [[nodiscard]] warning
- **Problem:** `poll_events()` returns bool marked [[nodiscard]], but we discarded it
- **Solution:** Cast to void: `(void)platform_->poll_events();`
- **Impact:** None — we intentionally ignore the return value in test harness

## Next Steps (Phase 3)

The test harness is ready for Phase 3 integration tests:
1. `test_battle_integration` — full battle flow (enter, fight, win/lose, rewards)
2. `test_shop_integration` — buy items, verify inventory and currency
3. `test_menu_integration` — navigate menu, verify scene transitions
4. `test_crash_stability` — run many frames, verify no crashes

Each test will use `HeadlessTestRunner` to drive the real Game class headlessly.

## Constraints Satisfied

✅ C++23
✅ CMake integration
✅ ctest compatibility
✅ No GPU/GL required
✅ Deterministic execution
✅ All existing tests still pass
✅ Style: trailing_underscore_ for members, kPascalCase for constants, [[nodiscard]] on getters
✅ Namespace: `resf2::test` for test harness classes
