# Concerns, Technical Debt & Risks — reSF2

> Generated 2026-07-22 from code and docs audit.

---

## 1. Monolithic Single-File Architecture (Critical)

**`engine/game/game.hpp` — 4609 lines** (file size ~255 KB).

- Contains `class Game` which implements both `rt::IGame` and `scene::SceneHost` — two distinct responsibilities in one class.
- Includes all rendering, combat, AI, animation, scene management, input, atlas loading, dialogue, progression, and HUD logic in one header.
- 32 `#include` directives at the top pull in most of the engine.
- File is a header (`game.hpp`) but contains method bodies that read like implementation — no clear header/source separation within the module.
- Many inner structs/handlers are defined at file scope rather than in dedicated files (e.g., `AtlasRef`, `DrawableItem`, `FighterState`).

**Risk:** Any change risks merge conflicts. Compilation time is high. New contributors face a steep learning curve. Cannot unit-test components in isolation without pulling in the entire game engine.

**Path to fix:** Split into `game/player.hpp`, `game/enemy.hpp`, `game/combat.hpp`, `game/render.hpp`, `game/ui.hpp`, `game/scene_handlers.cpp`, etc.

---

## 2. [HEURISTIC-TODO] — Remaining Unverified Heuristics

Across the codebase, `[HEURISTIC-TODO]` marks logic that is **not yet byte-confirmed** against the original binary. Most were resolved in T-01 through T-11; **4 remain** in C++ code (all in `game.hpp`), plus 1 in a Python script.

### Source: `HANDOFF.md` (reconciled at T-11)
| Area | Status | Detail |
|------|--------|--------|
| State/Move Manager | 55% | `in_basic_attack` honest. Denylist still heuristic — needs original template check. |
| Jump / Y | 55% | **[ORIGINAL] ShiftY=0** verified from PC sf2.js (Axis="X|Z"). Old heuristic removed. |
| Root motion | 80% | Data-driven via MoveDef metadata. Only fallback whitelist for non-MoveDef anims remains. |
| Skeletal animation | 85% | `.bin` decoded. MidFrames/FirstFrame data-driven. Transition frames missing. |

### Source: `engine/game/game.hpp` (4 inline marks — all with documented plans)
| Current Line | Tag | Issue | Status |
|-------------|-----|-------|--------|
| 1299 | HEURISTIC-TODO | `step_min_played`: invented 400ms threshold | **STILL OPEN** — needs combat-logic refactor to use MoveDef::intervals |
| 1316 | HEURISTIC-TODO | `fwd_held_ms_`/`back_held_ms_`: invented 200ms latch | **STILL OPEN** — needs combo-logic refactor to use MoveQuery |
| 3284 | HEURISTIC-TODO | Vec3→world consumption formula traced but not fully verified for non-X|Z axes | **STILL OPEN** — covered by ShiftY=0 for all current moves |
| 4525 | HEURISTIC-TODO | Fallback whitelist for anims not in moves.xml | **STILL OPEN** — main root-motion is data-driven, only stance_idle/fists_idle remain |

### Resolved `[HEURISTIC-TODO]` (this cleanup)
| Original Location | Resolution |
|------------------|-----------|
| `game.hpp:215` (align_y formula) | → [ORIGINAL] pipeline traced, ShiftY=0 verified |
| `game.hpp:4594` (interim Y formula) | → removed; code now uses [ORIGINAL] ShiftY=0 |
| `audio.hpp` / `audio.cpp` (MP3 not implemented) | → T-05 (11f102d): minimp3 integrated |
| `dz_decode_final.py:442` (DZ type-4) | Remains — Python script only, no C++ change |

**Risk:** The 4 remaining heuristics are well-documented and have specific replacement plans. They each require a combat/input state-machine refactor that was out of scope for Phase 1 Wave 4.

---

## 3. Missing Atlas Textures — ~44 Locations Render as Colored Rectangles

**Source: `engine/game/game.hpp` lines 2456–2494**

When the location parser references an atlas that doesn't exist on disk (`.plist` + `.png` not found), the renderer falls back to drawing **filled rectangular borders** instead of textures:

```cpp
auto it = atlases_.find(img.atlas_name);
if (it == atlases_.end()) {
    // Atlas not found → render solid rect from location Color as fallback.
    // Many locations lack atlas files; this prevents black screens.
    ...
    renderer_->draw_filled_rect_screen(sx, sy, sw, sh, c2);
    // Draw a border to show individual image boundaries
    ...
}
```

**Impact:** Most non-dojo locations render as structureless colored boxes. The player sees a "debug wireframe" look rather than the actual game backgrounds. This undermines the Map → Battle flow, since battle background textures aren't displayed.

---

## 4. Enemy Has No Equipment / Weapon / Armor System

**Source: `engine/game/game.hpp` line 4646, `engine/scene/scene_system.hpp` line 43, `engine/scene/scenes.hpp` line 159**

- `currency_ = 1000` is hardcoded — no actual economy.
- `Shop` scene (`engine/scene/scenes.cpp:330–344`) is a stub — just prints `[shop] enter (stub)` and listens for Escape/M to return.
- `Profile` is listed in the menu (`"Dojo", "Map", "Shop", "Profile", "Settings"`) but **there is no `Profile` scene** in `SceneId` enum or `scenes.cpp` — clicking it likely does nothing or crashes.
- No `results_rewards` / equipment tracking in `ResultsScene` (`scenes.cpp:523–558`).
- The original SF2 has extensive weapon/armor/magic/perk inventory systems across 7 weapon categories.

**Risk:** The entire progression loop (fight → earn gold → buy gear → equip → fight stronger enemies) is absent. The game has no retention mechanics.

---

## 5. Shop / Settings / Profile Are Stubs

| Scene | File | Behavior |
|-------|------|----------|
| **Shop** | `scenes.cpp:330-344` | Stub. Logs entry, returns to MainMenu on Esc/M. No equipment UI, no purchasing. |
| **Settings** | `scenes.cpp:350-364` | Stub. Same pattern as Shop. |
| **Profile** | `game.hpp:4509` | Listed in menu, but no `SceneId::Profile` — not hooked up. |

**Scenes.hpp confirms** (`scenes.hpp:5`):
> "Most scenes are lightweight stubs that render a placeholder overlay..."

---

## 6. All UI Is Immediate-Mode OpenGL — No Retained-Mode Framework

**Evidence across `engine/game/game.hpp`:**

- HUD rendering uses raw `draw_textured_quad_screen()` / `draw_filled_rect_screen()` calls inline in `Game::render_panel()`.
- Menu icons are looked up from `menu_textures_` unordered_map and drawn one by one.
- Scroll menu state tracked manually via `menu_open_`, `menu_timer_`, `menu_eased_` variables.
- Button hit-testing is manual bounding-box checks (`clicked_in()` helper in `scenes.cpp`).
- No widget tree, no event bubbling, no layout system, no focus management.

**Risk:** UI becomes exponentially harder to maintain as features are added. No accessibility support. No animation framework beyond manual `lerp`. No text input for profile names.

---

## 7. No Localization Support (All Strings Hardcoded in English)

**All user-facing strings found are hardcoded C++ literals:**

- `"Dojo"`, `"Map"`, `"Shop"`, `"Profile"`, `"Settings"` (menu items, `game.hpp:4509`)
- `"FIGHT"`, `"[Y] Win  [L] Lose  [Esc] Forfeit"` (`scenes.cpp:500-506`)
- `"Welcome back, fighter."` (dialogue, `scenes.cpp:136`)
- `"Click to continue..."` (`scenes.cpp:441`)
- `"MAP"`, `"POWER"`, `"< BACK"` (HUD strings, `scenes.cpp`)
- Hit point labels, combo counter display, AI dialogue

**No i18n infrastructure:** No string table, no locale detection, no UTF-8 font rendering beyond basic bitmap font (`engine/reverse/bitmap_font.hpp`). The bitmap font appears to be Latin-only (single-byte glyphs).

---

## 8. Dialogue Uses Hardcoded Strings, Not `quests.xml`

**Source: `engine/scene/scenes.cpp:135-140`**

```cpp
ctx.host.host_set_dialogue({
    {"Sly", "Welcome back, fighter."},
    {"Sly", "The tournament awaits. Are you ready?"},
    {"Narrator", "Round 1 - Fight!"},
});
```

The original game defines all quest/dialogue data in `assets/quests.xml` (confirmed in `assets/files/settings.xml` line 13 and `sf2.js` line 79781). reSF2 parses `quests.xml` via the asset system but **never reads dialogue from it**.

**Impact:** Dialogue scenes can't be authored or modified without recompilation. No branching dialogue. No quest progression.

---

## 9. Battle Always Shows Dojo Background

**Source: `engine/scene/scenes.cpp`:**

- `MainMenuScene::on_enter` (line 104): Always loads `"dojo"` location.
- `BattleScene::on_render` (line 496): Comment says `"Host renders the dojo + character + bag + HUD"`.
- While `BattleScene::on_enter` (line 457-460) attempts to load a non-dojo battle location, the comment at line 496 confirms the render still says "dojo".
- `game.hpp:render_location_layer()` has the missing-atlas fallback: if the target location's atlases aren't extracted, it renders colored rectangles instead of textures.

**Risk:** Non-dojo battles show either dojo or a mess of colored rectangles. The visual feedback from the Map → Fight flow is broken.

---

## 10. Pre-Existing Test Failure: `test_dz_first_byte`

**File: `tests/test_dz_first_byte.cpp`** (264 lines)

- Attempts to decode the first byte (0x3C = `<`) from DZ compressed block at offset 0x1937.
- Returns `1` (failure) when `byte_val != 0x3C`.
- The test has `// Reproduce the first byte decode with full tracing` — it is a **debug tracer**, not a passing unit test.
- It is registered in `tests/CMakeLists.txt` as a CTest and **will fail** on every test run.

**Status:** Known. The DZ decoder is incomplete — type-4 blocks are not fully understood. The docs confirm: *"DZ type-4: function verified, algorithm NOT traced"* (`worklog.md` line 1432).

---

## 11. Codegraph Not Installed / Indexed

`codegraph` is not available for this repository. All code analysis falls back to grep/ripgrep/file reads. There is no symbol index, call graph, or dependency graph for navigation.

---

## 12. Additional Risks

### 12a. DZ Type-4 Decoder Not Working
- `scripts/dz_decode_final.py` line 442: `[HEURISTIC-TODO] DZ type-4 — current blocker`
- `HANDOFF.md` bug #6: *"DZ type-4 decoder — NOT working."*
- The whole asset extraction pipeline for type-4 assets (many `.dz` files in the data archive) is blocked.

### 12b. MP3 / Music Not Implemented
- `audio.hpp:15` + `audio.cpp:127`: `[HEURISTIC-TODO] MP3 loading not implemented`
- Only WAV sound effects work. Background music is silent.
- The original SF2 has ~30 MP3 tracks.

### 12c. Player Profile System Missing
- `game.hpp:4646`: `currency_ = 1000;  // starting gold (stub)`
- No save/load for player level, XP, equipped items, inventory, or story progress beyond completed levels.
- `ResultsScene::on_enter` (line 533) calls `host_save_progress()` but the implementation is basic.

### 12d. Skeletal Animation Incomplete
- `HANDOFF.md`: 70% complete — `.bin` decoded but transition frames, MidFrames, FirstFrame are missing.
- Results in visible snapping between animations.

### 12e. Root Motion Not Data-Driven
- `HANDOFF.md` bug #5: Uses a whitelist of animation names for root motion. Should read from `MoveDef` metadata.
- `game.hpp:3819`: `[HEURISTIC-TODO] fallback whitelist for anims not in moves.xml`

### 12f. Asset Path/List Hardcoding
- `HANDOFF.md` bug #7: `[HEURISTIC-TODO] Asset path/list hardcoding (deferred)`
- Asset lookup paths are hardcoded strings rather than driven from `files.xml` / `settings.xml`.

### 12g. No Quest/Perk System
- `PLAN_1TO1.md` Phase F4: Quests/Perks from `quests.xml`/`perks.xml` are planned but not started.
- `assets/quests.xml` exists on disk and is referenced in `settings.xml` but never consumed by gameplay code.

### 12h. Cross-Platform Gaps
- `engine/platform/platform.cpp:33`: `// TODO Stage 7.1.x: GLFW backend for Windows/Linux/macOS`
- `engine/platform/glfw_platform.cpp:678`: `// TODO: implement fullscreen toggle`
- `engine/renderer/renderer.cpp:80`: `// TODO: Add proper KTX transcoding using ktx library or basisu`

### 12i. Symbian Port Is Separate and Minimal
- `sf2_symbian/src/main.cpp:82`: `// TODO: render dojo background, character, bag, HUD`
- The Symbian port is a separate code path with its own main loop, no asset pipeline, and almost no gameplay.

---

## Summary

| Severity | Count | Key Items |
|----------|-------|-----------|
| **Critical** | 4 | Monolithic game.hpp (~4609 lines), 3 remaining HEURISTIC-TODO heuristics in C++, missing atlas textures (44 locations), DZ type-4 decoder blocked |
| **High** | 6 | No equipment/weapon/armor system, stub scenes (Shop/Settings/Profile), dialogue not from quests.xml, battle always shows dojo, MP3/music not implemented, skeletal transitions missing |
| **Medium** | 5 | No localization, immediate-mode UI only, root motion not data-driven, asset paths hardcoded, pre-existing test failure |
| **Low** | 4 | Codegraph not indexed, profile system stub, no quest system, cross-platform gaps (fullscreen, KTX, GLFW) |

---

## 13. Renderer/HUD Extraction Deferred

**Added 2026-07-22 during T-10 HIGH-fix cleanup.**

The `SceneRenderer` and `HudRenderer` classes were designed as extracted rendering modules but were never wired into production code. They were removed as dead code in this session. Their API surface and internal logic were not preserved — if rendering extraction is revisited, the implementation will need to be rebuilt from scratch.

**Files deleted:**
- `engine/game/scene_renderer.hpp` / `.cpp`
- `engine/game/hud_renderer.hpp` / `.cpp`
- `engine/game/game_new.hpp` (orphan alternative Game header, zero references)

**Status:** Closed — dead code removed.
