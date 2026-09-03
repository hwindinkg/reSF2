> ⚠️ УСТАРЕЛО. Этот документ описывает реверс нативного/Unity билда SF2,
> НЕ веб-версии. Не использовать как источник истины для порта.
> Валидный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.

# Plan: Headless Integration Tests for reSF2

## Overview

Enable headless integration tests that instantiate the **real Game class**, run N frames with fixed dt, and assert on observable game state (HP, currency, inventory, scene). The current 27 text-trace tests miss every visual/behavioral bug because they never run the real game loop. The fix: introduce an `IRenderer` abstraction so Game can use the existing software renderer (`soft::Renderer`) instead of the GL-only `ren::Renderer`, then wrap Game in a `HeadlessTestRunner` that drives frames, injects input, and exposes state for assertions.

## Background

- ADR: `.planning/adr/ADR-004-headless-integration-tests.md`
- Design: `.planning/headless-integration-test-design.md`
- Quick reference: `.planning/headless-integration-test-summary.md`
- Waves 1–3 of Phase 3 (gameplay feel) are complete and merged. Wave 4 (Battles) is next — these integration tests are the safety net that lets us tackle Wave 4 without regressing earlier work.

## Requirements

| ID | Requirement | Verification |
|----|-------------|--------------|
| R1 | Tests instantiate the real `Game` class, not mocks | `test_battle_integration.cpp` calls `game::Game` ctor |
| R2 | Tests run N frames with fixed dt = 16 ms | `TestPlatform::now_ms()` returns controlled value; `run_frames(count)` advances by `count * 16` |
| R3 | Tests assert on HP, currency, inventory, scene | `runner.player_health_frac()`, `runner.currency()`, `runner.has_item(id)`, `runner.current_scene()` |
| R4 | Tests catch visual/behavioral bugs (not just text output) | Phase 4 validation: inject a known HP bug, verify battle test fails |
| R5 | Tests run on CI without a GPU | Build configures with `-DRESF2_BUILD_HEADLESS=ON`; no GL calls |
| R6 | Tests are deterministic | Same binary, same seed → same outcome across 10 runs |
| R7 | GL build still works (no regression) | `cmake --build build` on Windows/GL succeeds; `resf2_app.exe` launches |
| R8 | Adding a new integration test is cheap (< 50 LOC) | Demonstrate by writing `test_crash_stability.cpp` |

## Architecture Changes

### New files

| File | Purpose |
|------|---------|
| `engine/renderer/itexture.hpp` | `ITexture` interface (`width`, `height`, `pixels()`) |
| `engine/renderer/irenderer.hpp` | `IRenderer` interface (frame lifecycle, draw, camera) |
| `engine/renderer/software_renderer_adapter.hpp/cpp` | Adapter wrapping `soft::Renderer` to implement `IRenderer` |
| `tests/test_platform.hpp/cpp` | `TestPlatform : NullPlatform` with fixed-time control |
| `tests/headless_test_runner.hpp/cpp` | Test harness wrapping `TestPlatform` + `Game` |
| `tests/integration/test_battle_integration.cpp` | Battle scene: HP changes, round outcome |
| `tests/integration/test_shop_integration.cpp` | Shop scene: catalog loaded, buy/sell affects currency + inventory |
| `tests/integration/test_menu_integration.cpp` | Menu toggle per scene |
| `tests/integration/test_crash_stability.cpp` | 1000 frames without crash |

### Modified files

| File | Change | Risk |
|------|--------|------|
| `engine/renderer/renderer.hpp/cpp` | `Texture2D : ITexture`; adds `pixels_` field; populate in `init_*`; `Renderer : IRenderer`; camera method renames | Medium |
| `engine/renderer/software_renderer.hpp` | `soft::Texture : ITexture` | None |
| `engine/renderer/CMakeLists.txt` | Add `software_renderer_adapter.cpp` | Low |
| `engine/game/game_clean.hpp` | `unique_ptr<ren::Renderer>` → `unique_ptr<IRenderer>`; 2 camera call-site changes | Medium |
| `engine/game/game.cpp` | Add `Game::set_renderer(...)` | Low |
| `tests/CMakeLists.txt` | Add `add_integration_test()` helper + 4 test targets | Low |

## Dependency Graph

```
    1.1 ITexture
        │
    ┌───┼────────────┐
    ▼   ▼            ▼
  1.2  1.3         1.4 IRenderer
  T2D  soft::Tex      │
    │   │             │
    └─┬─┘             │
      ▼               ▼
    1.5 GL Renderer   1.6 Soft Adapter
      (refactor)        
        │               │
        └───────┬───────┘
                ▼
        1.7/1.8 Game uses IRenderer + set_renderer
                │
                ▼
        1.9/1.10 Gate: GL build green, 27/27 ctest
                │
       ┌────────┼────────┐
       ▼        ▼        ▼
      2.1      2.2      2.3     ← parallelizable
  TestPlatform  Runner   CMake
       └────────┼────────┘
                ▼
    3.1 Battle  3.2 Shop  3.3 Menu  3.4 Crash    ← parallelizable
                │
                ▼
           4. Validation (sequential)
```

## Implementation Steps

### Phase 1 — Renderer Abstraction (est. 2–3 days, highest risk)

**Step 1.1 — Define `ITexture` interface** (15 min)
- New file: `engine/renderer/itexture.hpp`
- Virtual methods: `width()`, `height()`, `pixels() -> span<const uint8_t>`

**Step 1.2 — `Texture2D : ITexture` + keep CPU pixels** (45 min)
- Files: `engine/renderer/renderer.hpp/cpp`
- Add `std::vector<uint8_t> pixels_` field; populate in every `init_*` before/after GL upload
- Implement 3 ITexture overrides (2 are just `override` on existing methods)
- *Risk:* Memory cost of keeping CPU pixels. Mitigation: can add `release_cpu_pixels()` later

**Step 1.3 — `soft::Texture : ITexture`** (30 min)
- File: `engine/renderer/software_renderer.hpp`
- Add `override` width/height/pixels accessors
- *Note:* requires renaming existing `width`/`height` public fields → `width_`/`height_` (4 refs in `software_renderer.cpp`)

**Step 1.4 — Define `IRenderer` interface** (30 min)
- New file: `engine/renderer/irenderer.hpp`
- Frame lifecycle, drawing (all taking `const ITexture&`), camera accessors (flat methods: `camera_set_target`, `camera_set_zoom`, `camera_world_to_screen`)
- Uses existing `resf2::renderer::Color4B`

**Step 1.5 — Refactor `ren::Renderer : IRenderer`** (2 hours)
- Files: `engine/renderer/renderer.hpp/cpp`
- Change draw signatures from `const Texture2D&` to `const ITexture&`; inside, `static_cast<const Texture2D&>`
- Rename camera access points; keep `Camera2D& camera()` as non-virtual accessor for internal use
- Implement `create_texture`/`destroy_texture`/`camera_world_to_screen` overrides

**Step 1.6 — Create `SoftwareRendererAdapter`** (2 hours)
- New files: `engine/renderer/software_renderer_adapter.hpp/cpp`
- Wraps `soft::Renderer`; implements IRenderer
- Key: `resolve(ITexture&)` method that lazily synthesizes a `soft::Texture` from any `ITexture`'s CPU pixels — handles both `Texture2D` (from AssetManager) and `soft::Texture` (ad-hoc)

**Step 1.7 — Game uses `IRenderer`** (2 hours)
- Files: `engine/game/game_clean.hpp/cpp`
- Change field type from `unique_ptr<ren::Renderer>` → `unique_ptr<IRenderer>`
- Update 2 camera call sites (game.cpp lines 2824–2825)
- ~40 `renderer_->draw_textured_quad(*img.texture, ...)` sites need **no change** — `Texture2D` is-a `ITexture`

**Step 1.8 — Add `Game::set_renderer`** (10 min, bundled with 1.7)

**Step 1.9 — CMake: add adapter source** (15 min)
- File: `engine/renderer/CMakeLists.txt`
- Add `software_renderer_adapter.cpp` to `RESF2_RENDERER_SOURCES`

**Step 1.10 — Regression gate** (30 min)
- Must pass before Phase 2:
  - `cmake --build build` → 0 errors, 0 warnings
  - `ctest --output-on-failure` → 27/27 existing tests pass
  - `resf2_app.exe` launches and plays normally

### Phase 2 — Test Harness (est. 0.5–1 day)

**Step 2.1 — `TestPlatform`** (30 min)
- Files: `tests/test_platform.hpp/cpp`
- `TestPlatform : NullPlatform` with `set_fixed_time_ms`/`advance_time_ms`; overrides `now_ms()`

**Step 2.2 — `HeadlessTestRunner`** (2 hours)
- Files: `tests/headless_test_runner.hpp/cpp`
- `Config` struct (asset_root, width, height, fixed_dt_ms, screenshot options)
- `init()` → builds platform + adapter + Game; calls `game->set_renderer`; calls `game->on_init`
- `run_frames(count)`, `run_until(cond, max_frames)`
- Input injection (delegates to TestPlatform)
- State accessors delegating to Game's `host_*` methods
- Need to add `Game::is_menu_overlay_visible()` accessor (field already exists)

**Step 2.3 — CMake wiring** (30 min)
- File: `tests/CMakeLists.txt`
- `add_integration_test()` helper function
- 4 test targets, each links against `resf2_game`, `resf2_renderer`, `resf2_platform`, etc.
- Timeout 120s per test

### Phase 3 — Integration Tests (est. 1–2 days, parallelizable)

**Step 3.1 — Battle test** (1 hour)
- Configure battle info (enemy = "Kenji", 1 round, 99s timer)
- Run 500 frames; assert HP decreased for both sides; assert outcome ∈ {victory, defeat, ""}

**Step 3.2 — Shop test** (1.5 hours)
- Start in Shop scene; `run_until` catalog loaded
- Buy first item via `host_buy_item`; assert currency decreased and `has_item` returns true

**Step 3.3 — Menu test** (1.5 hours)
- Iterate scenes: MainMenu, Map, Shop, Results
- Each scene: press M → menu visible; press M again → menu hidden

**Step 3.4 — Crash stability test** (30 min)
- Run 1000 frames on MainMenu; assert no crash, HP stays in [0, 1]

### Phase 4 — Validation (est. 0.5 day)

**Step 4.1** — Baseline: `ctest` → 31/31 pass
**Step 4.2** — Inject bug (comment out player-damage decrement); verify battle test catches it
**Step 4.3** — Revert bug; verify test passes again
**Step 4.4** — Write `.planning/headless-integration-test-results.md`

## Success Criteria

- [ ] `IRenderer` and `ITexture` interfaces exist and are used by `Game`
- [ ] `SoftwareRendererAdapter` implements `IRenderer` by wrapping `soft::Renderer`
- [ ] `ren::Renderer` implements `IRenderer`; GL path unchanged in behavior
- [ ] `TestPlatform` provides fixed-time control
- [ ] `HeadlessTestRunner` drives the real Game class
- [ ] 4 new integration tests pass under ctest
- [ ] Total: 27 existing + 4 new = 31 tests passing
- [ ] Injected-bug validation: at least one test fails when a damage bug is introduced
- [ ] GL build: 0 errors; `resf2_app.exe` launches and plays normally
- [ ] Results documented in `.planning/headless-integration-test-results.md`

## Rollback Plan

- **If Phase 1 breaks GL build in > 4 hours:** revert Phase 1 commits; keep `IRenderer`/`ITexture` headers but leave Game on `ren::Renderer` directly; reconsider approach (compile-time switch via templates).
- **If Phase 2 can't instantiate Game headlessly:** audit AssetManager / scene code for hard GL dependencies; add `if (!renderer_) return;` early-exits as minimal guards.
- **If integration tests are flaky:** first suspect is time-dependent logic not actually using fixed dt; second is uninitialized Game state. Make tests tolerant or fix determinism bug.

## Effort Summary

| Phase | Steps | Estimate | Risk |
|-------|-------|----------|------|
| 1 — Renderer abstraction | 1.1–1.10 | 2–3 days | **High** — touches ~60 call sites in Game |
| 2 — Test harness | 2.1–2.3 | 0.5–1 day | Medium |
| 3 — Integration tests | 3.1–3.4 | 1–2 days | Low |
| 4 — Validation | 4.1–4.4 | 0.5 day | Low |
| **Total** | | **4–7 days** | |
