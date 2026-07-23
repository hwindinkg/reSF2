# Plan: Fix 4 HIGH Review Findings — Input Duplication, Dead Members, Orphan Files

## Overview
After T-10 refactoring extracted the `Game` class into `game_clean.hpp` and moved implementations to `game.cpp`, the `@reviewer` found 4 HIGH-priority issues: (1) 12 raw input fields in Game that duplicate `InputHandler`'s private fields, (2) a dead `SceneRenderer` member never initialized/used, (3) a completely dead `HudRenderer` class compiled but never instantiated, and (4) an orphan `game_new.hpp` from an earlier refactoring pass. These 4 fixes remove ~300 lines of dead/duplicate code and eliminate the data-duplication relationship between Game and InputHandler.

## Success Criteria
- [ ] `npx tsc` or equivalent build: 0 errors
- [ ] All existing tests pass (21/22 same pre-existing failure)
- [ ] Input state reads/writes in `game.cpp` route through `input_handler_` accessors, not raw fields
- [ ] `scene_renderer_` member removed and `scene_renderer.hpp/cpp` deleted (or left as files with zero link errors)
- [ ] `hud_renderer.cpp` removed from CMakeLists (and .hpp/.cpp deleted)
- [ ] `game_new.hpp` deleted
- [ ] `git diff --stat` shows fewer lines after applying all changes

## Implementation Steps

### Step 1 — Add missing getters/setters to InputHandler (input_handler.hpp, line 30 → ~line 49)

**File**: `E:\reSF2\engine\game\input_handler.hpp`

**Task**: Insert 14 missing public accessors (8 const getters, 4 setters, 2 set_double_step methods) after the existing getter block (after line 30).

**Exact insertion** (after line 30 `void set_duck_play_time(uint32_t t) { duck_play_time_ = t; }`):
```cpp
    int fwd_held_ms() const { return fwd_held_ms_; }
    void set_fwd_held_ms(int v) { fwd_held_ms_ = v; }
    int back_held_ms() const { return back_held_ms_; }
    void set_back_held_ms(int v) { back_held_ms_ = v; }
    uint32_t last_fwd_tap_ms() const { return last_fwd_tap_ms_; }
    void set_last_fwd_tap_ms(uint32_t v) { last_fwd_tap_ms_ = v; }
    uint32_t last_back_tap_ms() const { return last_back_tap_ms_; }
    void set_last_back_tap_ms(uint32_t v) { last_back_tap_ms_ = v; }
    uint32_t last_kick_press_ms() const { return last_kick_press_ms_; }
    uint32_t last_punch_press_ms() const { return last_punch_press_ms_; }
    uint32_t last_punch_seen_ms() const { return last_punch_seen_ms_; }
    uint32_t last_kick_seen_ms() const { return last_kick_seen_ms_; }
    void set_double_step_fwd(bool v) { double_step_fwd_requested_ = v; }
    void set_double_step_back(bool v) { double_step_back_requested_ = v; }
```

**Rationale**: `last_kick_press_ms_` / `last_punch_press_ms_` / `last_punch_seen_ms_` / `last_kick_seen_ms_` only need const getters (no setter) because they are internal to InputHandler and never written from game.cpp. `fwd_held_ms_` / `back_held_ms_` / `last_fwd_tap_ms_` / `last_back_tap_ms_` need both getter+setter because game.cpp writes to them. `double_step_fwd_` / `double_step_back_` need `set_*` in addition to existing `clear_*` because game.cpp writes `true` to them.

**Verify**: `g++ -std=c++23 -fsyntax-only input_handler.hpp` or equivalent compiles. The class now has complete public accessors for all 13 private fields.

---

### Step 2 — Remove 12 raw duplicate input fields from game_clean.hpp (lines 2615-2627)

**File**: `E:\reSF2\engine\game\game_clean.hpp`

**Task**: Delete 13 lines (the comment + 12 field declarations) that duplicate InputHandler's private state.

**Exact deletion** (lines 2615 through 2627 inclusive):
```
    // Input state (owned by input_handler_ eventually)
    uint32_t step_play_time_ = 0;
    uint32_t duck_play_time_ = 0;
    int fwd_held_ms_ = 0;
    int back_held_ms_ = 0;
    uint32_t last_fwd_tap_ms_ = 0;
    uint32_t last_back_tap_ms_ = 0;
    bool double_step_fwd_requested_ = false;
    bool double_step_back_requested_ = false;
    uint32_t last_kick_press_ms_ = 0;
    uint32_t last_punch_press_ms_ = 0;
    uint32_t last_punch_seen_ms_ = 0;
    uint32_t last_kick_seen_ms_ = 0;
```

After deletion, the comment on line 2614 `// Combat state aliases (owned by combat_ member)` becomes the new line 2615, and `std::string& current_move_` on line 2629 becomes the new line 2616.

**Verify**: `g++ -std=c++23 -fsyntax-only game_clean.hpp` compiles (may show errors for game.cpp field references — that's expected; Step 3 fixes those).

---

### Step 3 — Replace all game.cpp direct field references with input_handler_ accessors

**File**: `E:\reSF2\engine\game\game.cpp`

**Task**: Replace every direct read/write of the 12 removed fields through `input_handler_` accessor calls. 7 groups of changes:

#### Group 3a — Double-tap detection (lines ~1014-1025)
Replace:
```cpp
        if (now_ms - last_fwd_tap_ms_ < 300 && move_state_ == 2) {
            double_step_fwd_requested_ = true;
        }
        last_fwd_tap_ms_ = now_ms;
    }
    if (back_just_pressed) {
        if (now_ms - last_back_tap_ms_ < 300 && move_state_ == 1) {
            double_step_back_requested_ = true;
        }
        last_back_tap_ms_ = now_ms;
```
With:
```cpp
        if (now_ms - input_handler_.last_fwd_tap_ms() < 300 && move_state_ == 2) {
            input_handler_.set_double_step_fwd(true);
        }
        input_handler_.set_last_fwd_tap_ms(now_ms);
    }
    if (back_just_pressed) {
        if (now_ms - input_handler_.last_back_tap_ms() < 300 && move_state_ == 1) {
            input_handler_.set_double_step_back(true);
        }
        input_handler_.set_last_back_tap_ms(now_ms);
```

#### Group 3b — step_play_time tracking (lines ~1054-1059)
Replace:
```cpp
    if (move_state_ == 1 || move_state_ == 2) {
        step_play_time_ += dt;
    } else {
        step_play_time_ = 0;
    }
    bool step_min_played = step_play_time_ >= 400;
```
With:
```cpp
    if (move_state_ == 1 || move_state_ == 2) {
        input_handler_.set_step_play_time(input_handler_.step_play_time() + dt);
    } else {
        input_handler_.set_step_play_time(0);
    }
    bool step_min_played = input_handler_.step_play_time() >= 400;
```

#### Group 3c — fwd_held_ms / back_held_ms decay (lines ~1067-1070)
Replace:
```cpp
    if (key_forward) fwd_held_ms_ = 200;
    else if (fwd_held_ms_ > 0) fwd_held_ms_ -= (int)dt;
    if (key_back) back_held_ms_ = 200;
    else if (back_held_ms_ > 0) back_held_ms_ -= (int)dt;
```
With:
```cpp
    if (key_forward) input_handler_.set_fwd_held_ms(200);
    else if (input_handler_.fwd_held_ms() > 0) input_handler_.set_fwd_held_ms(input_handler_.fwd_held_ms() - (int)dt);
    if (key_back) input_handler_.set_back_held_ms(200);
    else if (input_handler_.back_held_ms() > 0) input_handler_.set_back_held_ms(input_handler_.back_held_ms() - (int)dt);
```

#### Group 3d — duck_play_time reset in duck move selection (line ~1465)
Replace:
```cpp
                    duck_play_time_ = 0;
```
With:
```cpp
                    input_handler_.set_duck_play_time(0);
```

#### Group 3e — fwd/back latch reads in step movement (lines ~1498-1499)
Replace:
```cpp
        bool fwd_latched = fwd_held_ms_ > 0;
        bool back_latched = back_held_ms_ > 0;
```
With:
```cpp
        bool fwd_latched = input_handler_.fwd_held_ms() > 0;
        bool back_latched = input_handler_.back_held_ms() > 0;
```

#### Group 3f — double_step flag reads and clears (lines ~1553-1576)

a) Line ~1553: `if (double_step_back_requested_ && ...)` → `if (input_handler_.double_step_back_requested() && ...)`
b) Line ~1559: `double_step_back_requested_ = false;` → `input_handler_.clear_double_step_back();`
c) Line ~1570: `if (double_step_fwd_requested_ && ...)` → `if (input_handler_.double_step_fwd_requested() && ...)`
d) Line ~1576: `double_step_fwd_requested_ = false;` → `input_handler_.clear_double_step_fwd();`

#### Group 3g — duck_play_time increment and read (lines ~1610-1611)
Replace:
```cpp
        duck_play_time_ += dt;
        if (!key_down && duck_play_time_ >= 100) {
```
With:
```cpp
        input_handler_.set_duck_play_time(input_handler_.duck_play_time() + dt);
        if (!key_down && input_handler_.duck_play_time() >= 100) {
```

**Verify**: Recompile the project. All 23 field references now go through `input_handler_`. `git grep -n "last_fwd_tap_ms_|last_back_tap_ms_|double_step_fwd_requested_|double_step_back_requested_|step_play_time_|fwd_held_ms_|back_held_ms_|duck_play_time_|last_kick_press_ms_|last_punch_press_ms_|last_punch_seen_ms_|last_kick_seen_ms_" engine/game/game.cpp` returns 0 matches (except possibly in comments/strings).

---

### Step 4 — Remove dead SceneRenderer member and include (game_clean.hpp)

**File**: `E:\reSF2\engine\game\game_clean.hpp`

**Task 4a**: Delete line 44 `#include "scene_renderer.hpp"`.
**Task 4b**: Delete line 2637 `std::unique_ptr<SceneRenderer> scene_renderer_;` (the line after the `// Module instances` comment).

After Step 2, the line numbers shift. The target lines are the `#include "scene_renderer.hpp"` line (currently line 44) and the `scene_renderer_` member (currently line 2637, which after deleting 13 lines in Step 2 becomes line ~2624). Use `#include "scene_renderer.hpp"` and `scene_renderer_;` as anchor strings.

**Exact deletion 4a**: Remove the line: `#include "scene_renderer.hpp"`
**Exact deletion 4b**: Remove the line: `    std::unique_ptr<SceneRenderer> scene_renderer_;`

**Verify**: `g++ -std=c++23 -fsyntax-only game_clean.hpp` compiles. No reference to `SceneRenderer` remains in the file.

---

### Step 5 — Remove SceneRenderer from CMakeLists.txt

**File**: `E:\reSF2\engine\game\CMakeLists.txt`

**Task**: Delete line 14 `    scene_renderer.cpp`.

**After deletion**, the CMakeLists.txt lines 13-15 will collapse:
```
    hud_renderer.cpp       ← line 13 (removed in Step 7)
                         ← was line 14, now removed
```
Wait — Step 7 also removes hud_renderer.cpp. The order matters. This step deletes `scene_renderer.cpp` from line 14. Step 7 will delete `hud_renderer.cpp` from line 13.

**Verify**: `cmake --build` succeeds without `scene_renderer.cpp`.

---

### Step 6 — Delete SceneRenderer source files

**Task**: Delete `E:\reSF2\engine\game\scene_renderer.hpp` and `E:\reSF2\engine\game\scene_renderer.cpp`.

**Verify**: Files no longer exist on disk. Build succeeds (no unresolved symbols).

---

### Step 7 — Remove HudRenderer from CMakeLists.txt

**File**: `E:\reSF2\engine\game\CMakeLists.txt`

**Task**: Delete line 13 `    hud_renderer.cpp`.

**Verify**: `cmake --build` succeeds without `hud_renderer.cpp`.

---

### Step 8 — Delete HudRenderer source files

**Task**: Delete `E:\reSF2\engine\game\hud_renderer.hpp` and `E:\reSF2\engine\game\hud_renderer.cpp`.

**Verify**: Files no longer exist on disk. Build succeeds (no unresolved symbols).

---

### Step 9 — Delete orphan game_new.hpp

**File**: `E:\reSF2\engine\game\game_new.hpp`

**Task**: Delete the entire file. It is not referenced by any `#include` anywhere in the engine.

**Verify**: File no longer exists. Build succeeds. `grep -r "game_new" engine/` yields no hits (except in .planning/STATE.md, which is documentation, not code).

---

### Step 10 — Final build and test verification

**Task**: Rebuild the entire project and run tests.

**Commands**:
```bash
cd build
cmake --build . --parallel
ctest --output-on-failure
```

**Verify**: 
- Build: 0 errors, 0 warnings (per-project baseline)
- Tests: same results as before (21/22 pass, same pre-existing failure)
- No new link errors from removed .cpp files

## Test Plan

| Step | What to test | How |
|------|-------------|-----|
| 1 | InputHandler has complete accessors | Compile check |
| 2 | game_clean.hpp has no duplicated fields | `git diff` inspection |
| 3 | game.cpp compiles without raw field refs | Build succeeds |
| 4-6 | SceneRenderer removed cleanly | No link errors |
| 7-8 | HudRenderer removed cleanly | No link errors |
| 9 | game_new.hpp deleted | File gone |
| 10 | Full regression | All 21/22 passing tests still pass |

## Rollback

All changes are additive or file-deletion only — no schema migrations. Rollback: `git checkout -- engine/game/input_handler.hpp engine/game/game_clean.hpp engine/game/game.cpp engine/game/CMakeLists.txt && git restore engine/game/scene_renderer.hpp engine/game/scene_renderer.cpp engine/game/hud_renderer.hpp engine/game/hud_renderer.cpp engine/game/game_new.hpp` reverts every change.
