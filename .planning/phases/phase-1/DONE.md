# Phase 1 — Done

**Completed:** 2026-07-22T21:19:50Z  
**Completed by:** fd-done  
**Prior status:** complete  
**Steps complete:** Waves 1–4 (T-01 through T-10) + 4 HIGH-fix cleanup steps  

## Overall Achievement

Phase 1 transformed the monolithic `game.hpp` (~4609 lines, ~255 KB) into a modular architecture:
- Extracted `LocationManager`, `AssetManager`, `CombatSystem`, `AnimationPlayer`, `InputHandler` as dedicated classes
- Extracted `save.hpp/cpp`, `player.hpp/cpp`, `inventory.hpp/cpp`, `shop.hpp/cpp`, `helpers.hpp/cpp`
- Split `game.hpp` → `game_clean.hpp` (refactored modular Game) + `game_old.hpp` (original reference)
- Achieved final line count: **`game_clean.hpp` ~2638 lines** (down from 4609)
- Implemented full feature set: combat AI, all 56 locations, save/load system, inventory/equipment, weapon content, OpenAL audio, moves.xml parser, skeletal animation, asset pipeline

## Waves Completed (Original Phase 1 Scope)

### Wave 1 — Foundation (T-01, T-02)
| Task | What | Files |
|------|------|-------|
| T-01 | moves.xml parser: `mid_frames`, `SoundEvent` fields, test coverage | `engine/fight/moves.cpp`, `tests/test_moves_parser.cpp` |
| T-02 | NPivot Y data-driven, MidFrames/FirstFrame animation support | `engine/fight/animation.cpp` |
| | Research gate: SF2 decompilation flow mapped | `.planning/phases/phase-1/DISCUSS.md` |

### Wave 2 — Content (T-03, T-04, T-05)
| Task | What | Files |
|------|------|-------|
| T-03 | Combat AI logic implementation | `engine/fight/ai.cpp` |
| T-04 | All 56 location loading | `tests/test_location_parser.cpp` |
| T-05 | OpenAL audio backend with MP3 decoding | `engine/audio/` |

### Wave 3 — Progression (T-07, T-08, T-09)
| Task | What | Files |
|------|------|-------|
| T-07 | Save/Load system with PlayerProfile | `engine/game/save.cpp`, `tests/test_save_system.cpp` |
| T-08 | Weapon content loading from XML | `tests/test_weapon_loading.cpp` |
| T-09 | Inventory & Equipment system | `engine/game/inventory.cpp`, `tests/test_inventory.cpp` |

### Wave 4 — Monolith Split (T-10)
| Task | What | Files |
|------|------|-------|
| T-10a | Extract LocationManager, AssetManager, CombatSystem, AnimationPlayer | `engine/game/location_manager.cpp`, `asset_manager.cpp`, `combat.cpp`, `animation_player.cpp` |
| T-10b | Create InputHandler with input state | `engine/game/input_handler.cpp`, `input_handler.hpp` |
| T-10c | Create game_clean.hpp (refactored) + game_old.hpp (reference) | `engine/game/game_clean.hpp`, `game_old.hpp` |
| T-10d | Original game.hpp → 7-line thin wrapper | `engine/game/game.hpp` |

### T-10 Cleanup (4 HIGH Review Findings)
| Step | Action | Result |
|------|--------|--------|
| 1 | Add 14 accessors to `input_handler.hpp` | Complete getter/setter API for all input state |
| 2 | Delete 12 duplicate input fields from `game_clean.hpp` | -12 lines from `Game` class |
| 3 | Replace 30 field refs in `game.cpp` with `input_handler_.xxx()` | All input state now routes through `InputHandler` |
| 4 | Remove `#include "scene_renderer.hpp"` from `game_clean.hpp` | Cleaned dead include |
| 5 | Remove `scene_renderer_` member from `game_clean.hpp` | Cleaned dead member |
| 6 | Delete 4 renderer/HUD files | `scene_renderer.hpp/.cpp`, `hud_renderer.hpp/.cpp` |
| 7 | Remove from working-tree CMakeLists.txt | (files were never in any committed CMakeLists.txt) |
| 8 | Delete `game_new.hpp` | Orphan removed (598 lines) |
| 9 | Document deferral | `.codebase/CONCERNS.md` §13 |
| 10 | Full clean rebuild + ctest | **22/22 exes built, 21/22 tests pass (1 pre-existing)** |

## Verification

✅ Built tests: **21/22 pass** (pre-existing `test_dz_first_byte` — missing `assets/files.dz`)
✅ Full parallel build: **0 errors, 164s**
✅ Code review: PASS (1 MEDIUM: dead `process_input()` — pre-existing)
✅ Security audit: PASS (0 findings)
✅ State: `verified`

## Codebase Mapping

ℹ️ Codegraph is installed (v1.5.0) with 83MB index (10,388 nodes, 29,434 edges). The MCP bridge returns a false negative for `installed`.

## Key Metrics

| Metric | Before Phase 1 | After Phase 1 |
|--------|---------------|---------------|
| game.hpp size | 4609 lines | 7 lines (thin wrapper) |
| game_clean.hpp | — | ~2638 lines |
| Module files | 0 | 8 extracted modules |
| Test executables | 9 built of 22 reg'd | **22/22 built**, 21/22 pass |
| Build time (parallel) | — | 164s |
| Line count | ~4609 in one file | Distributed across ~15 files |

## Remaining Risks
- 4 `[HEURISTIC-TODO]` marks in `game_clean.hpp` (combat-logic refactors — out of scope)
- Pre-existing `test_dz_first_byte` failure (missing test data)
- `process_input()` in `input_handler.cpp` is dead code — should be wired or removed
- Cross-platform gaps (fullscreen, KTX, GLFW)

## Next Steps
- Run `/fd-status` for full project state
- Run `/fd-new-feature` to start Phase 2
