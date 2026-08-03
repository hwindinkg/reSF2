# INPUT PATH AUDIT — Win32 GetAsyncKeyState vs TestPlatform injection

**Directive:** the user plays through run.bat (GLFW + Win32 GetAsyncKeyState
path); all input fixes (M1 roll key order, M4 step pacing, P7 held-duck,
D4 dialogue any-key advance, J/U weapon cycle) were validated ONLY through
the TestPlatform injection path. Audit whether the real input path delivers
the same InputState contract the tests assert.
**Date:** 2026-08-03. **HEAD:** 1909a61 (61/61 ctest green before this audit).
**Scope:** `engine/platform/` + `tests/` + this doc. **Mode:** audit + path-scoped fixes.

## TL;DR

The two paths are **producers of the same `InputState` struct** and the game
has **one entry point** — `platform.input()` read by `Game::on_update` — with
**no platform-type branching** anywhere in engine/game or engine/scene
(no `dynamic_cast`/`typeid` on `Platform`). For human-scale input (any press
that spans at least one frame) the Win32 poll path and TestPlatform injection
produce **byte-identical InputState timelines** for all four audited
behaviors. The divergences found are:

| # | Divergence | Severity | Status |
|---|-----------|----------|--------|
| DIV-1 | Sub-frame taps (press+release between two polls) are invisible to the Win32 poll; TestPlatform can express them | Physical limit, documented | **Pinned by test**, no code change |
| DIV-2 | Stale comment claimed "GLFW callback catches fast taps" — impossible: the callback is disabled on Win32 (it is the spurious-event source); `glfw_key_consumed_` was write-only dead state | Code truth | **FIXED** (commit 1) |
| DIV-3 | `cursor_pos_callback` tracked only button-0 (`p.id == 0`) — right/middle-button drags froze at press position; TestPlatform `inject_pointer_move` tracks any id | Minor, real | **FIXED** (commit 2) |
| DIV-4 | Win32 poll loop covers only Key enum 0..AltRight (231 keys) of `kMaxKeys` 256; HID 0xE7..0xFF never polled | Inert (no game key above 0xE6) | Documented |

The edge-decision logic — the ONLY logic in the Win32 path — had **no test
coverage**: the `--input-script` diagnostic bypasses the poll loop entirely
(returns early), and physical `GetAsyncKeyState` reads cannot be driven
headlessly. **FIXED** (commit 3): the edge decision now lives in the shared
`poll_key_frame()` (engine/platform/input_edges.hpp) called by the real
Win32 loop, and `tests/test_input_contract.cpp` drives it against
TestPlatform injection for the M1/P7/D4/J-U scenarios.

---

## Q1. Same entry point, or different handlers?

**Same.** Both paths write the same `InputState` (`keys_down`,
`keys_just_pressed`, `keys_just_released`, `pointers`), and the game reads it
via `platform.input()`:

- `engine/platform/platform.hpp:85-93` — the `InputState` struct (arrays
  indexed by `Key` HID code; `kMaxKeys = 256` covers HID 0..255).
- `engine/game/game.cpp:2441-2448` — `const auto& input = platform.input()`…
  `keys_down` for held state; `:2555-2589` `keys_just_pressed` for edges;
  `:2379-2390` J/U cycle; `:3001-3094` M1 move selector; `:3099-3158` duck;
  `:3194-3224` hardcoded roll block.
- `engine/game/input_handler.cpp:33-85` — same `platform.input()` reads
  (double-tap detection, punch/kick).
- `engine/scene/scenes.cpp:33-35, 836-877` — `key_pressed()` =
  `keys_just_pressed[(size_t)k]`; DialogueScene any-key advance scans the
  whole `keys_just_pressed` array.
- Producers: `engine/platform/platform.cpp:190-232` (NullPlatform inject —
  the TestPlatform base), `engine/platform/glfw_platform.cpp:438-488`
  (Win32 poll loop). No `dynamic_cast`/`typeid`/platform-name branching in
  game or scene code (verified by grep — 29 hits are all platform-internal).

**Frame timing is also identical**: `main.cpp:88-107` (real run) and
`tests/headless_test_runner.cpp:67-75` (tests) both run
`poll_events() → on_update → on_render`. Edges are cleared at the top of
`poll_events()` (`platform.cpp:68-80`, `glfw_platform.cpp:412-423`) and
survive exactly one `on_update` on both paths.

## Q2. How just_pressed / held / release are computed per path

| Aspect | TestPlatform (platform.cpp:190-208) | Win32 (glfw_platform.cpp) |
|---|---|---|
| just_pressed | `inject_key_down`: if not already down → set `keys_just_pressed`, set `keys_down` | `poll_key_frame` (input_edges.hpp): `down && !prev_down`; GLFW key callback **ignores all events** on Win32 (glfw_platform.cpp:257-262) |
| just_released | `inject_key_up`: if down → set `keys_just_released`, clear `keys_down` | `!down && prev_down` against `prev_keys_down_` |
| held | `keys_down` persists until `inject_key_up` | `GetAsyncKeyState(vk) & 0x8000` re-sampled each frame — persists while physically held |
| auto-repeat | none — held keys never re-edge | none — same (this is what P7 depends on) |
| spurious release | impossible (explicit events) | impossible (GLFW event stream never read on Win32) |
| pointer | `inject_pointer_down/up/move` — move matches **any id** (platform.cpp:234-242) | GLFW mouse callbacks; **was** `p.id == 0`-filtered on move → **FIXED** (commit 2) |

Non-Windows GLFW callback path (glfw_platform.cpp:263-277 + 489-494) exists
but is **not** the user's path — noted for completeness only.

## Q3. Same Key enum + frame timing? Platform-type branches?

- **Key enum**: both paths index the same arrays by `(size_t)Key::X` — HID
  codes (platform.hpp:63-82); the Win32 poll maps Key→GLFW→VK
  (glfw_platform.cpp:46-129, 138-213) and covers **every** enum key
  (0..AltRight = 0xE6) — verified by inspection of both mapping tables; no
  gap where a game key lacks a VK.
- **Frame timing**: identical order (Q1). Edge lifetime identical (1 frame).
- **Platform-type branches in game logic**: none.

## Q4. The four user-visible behaviors — Win32-path verification

| Behavior | TestPlatform result (existing tests) | Win32-path result | Divergence points |
|---|---|---|---|
| (a) Back roll, both key orders (M1) | GREEN — A-then-S, S-then-A, same-frame (tests/integration/test_soak_movement_defects.cpp:131-187) | **Match by contract**: roll = just_pressed edge on the 2nd key + held state on the 1st (game.cpp:3011-3094 selector, 3199-3224 roll block). Both inputs arrive identically on Win32 (contract test M1 scenario). | none beyond DIV-1 |
| (b) Held duck, no auto-repeat (P7) | GREEN — one `[MOVE] Duck` over 150 held frames (tests/integration/test_soak_parser_defects.cpp:309-354) | **Match by contract**: Win32 never re-edges a held key — `keys_down[S]` stays true, `just_pressed` fires once (contract test P7 scenario). The P7 state-11 hold guard (game.cpp:3173-3192) reads held state only. | none beyond DIV-1 |
| (c) Dialogue advance on any key (D4) | GREEN — P drives line 1→2→exit (tests/integration/test_soak_dialogue_defects.cpp:103-161) | **Match by contract**: DialogueScene scans `keys_just_pressed` for ANY key (scenes.cpp:868-877); Win32 sets edges exactly once per press (contract test D4 scenario). | none beyond DIV-1 |
| (d) J/U weapon cycle (R4b) | Cycle CONTENT tested via host_get_weapon_cycle (tests/integration/test_soak_re4_defects.cpp:193-225); J/U **key driving itself has no direct test** | **Match by contract**: cycle_weapon fires on `just_pressed` J/U (game.cpp:2379-2390); edges identical on both paths (contract test J/U scenario). | none beyond DIV-1 |

**Composition argument**: existing integration tests prove the game-level
behaviors from an InputState timeline; `test_input_contract` proves the
Win32 producer emits the same timeline from the same physical presses;
therefore the game-level behaviors hold on the Win32 path — with DIV-1 as
the only caveat.

## The seam (commit 3)

`engine/platform/input_edges.hpp` — `poll_key_frame(input, prev_down,
os_down)` implements the per-frame edge contract. The real Win32 loop
(glfw_platform.cpp) builds `os_down` from `GetAsyncKeyState` and calls it;
`tests/test_input_contract.cpp` drives the same function with scripted OS
state and asserts frame-by-frame equality with `NullPlatform` injection for
the M1/P7/D4/J-U timelines, and pins DIV-1 explicitly. Fast, pure platform
layer, no assets.

## Fixes

| Commit | Change |
|---|---|
| 1 | `fix(platform): Win32 poll — drop dead glfw_key_consumed_ guard, correct the fast-tap comment` — removed write-only array + dead `!keys_just_pressed` guard; documented why no GLFW-event fallback can ever exist (spurious PRESS/RELEASE pairs are the reason for the bypass; re-enabling PRESS would re-edge held keys → P7/D4/J-U regressions) |
| 2 | `fix(platform): track pointer position for any pressed button` — cursor_pos_callback `p.id == 0` filter dropped, matches TestPlatform's any-id move contract |
| 3 | `test(platform): input-contract pin — poll_key_frame seam + test_input_contract` — the Win32 path's edge logic is now the same tested function the tests exercise; adds ctest #62 |
| 4 | `docs(reverse): INPUT_PATH_AUDIT.md` — this document |

## Residual gaps

1. **DIV-1 (physical, unfixable at this layer)**: taps shorter than one poll
   interval are lost on the Win32 path. A human cannot reliably produce
   sub-16ms taps; the alternative (event-fed edges) re-introduces the
   19044 spurious-event flicker the bypass exists to remove. Pinned in
   test_input_contract.
2. **DIV-3 residual**: pointer MOVE parity is fixed for the first pressed
   pointer of any button; multi-pointer (multi-touch) positioning still has
   no headless test (GLFW callbacks are not unit-testable here) — the
   desktop has one cursor, so the first-pressed rule is exact.
3. **DIV-4**: HID codes 0xE7..0xFF (231..255) are never polled/cleared on
   Win32; no game key uses them (enum max = AltRight 0xE6). Inert; noted.
4. **J/U key→cycle wiring** has no direct game-level test (R4b tests cycle
   content only). Covered by composition: contract test (edge) + game.cpp
   reading the edge. A game-level J-tap test can be added in a later wave.
5. **Unfocused-window input**: GetAsyncKeyState reads the OS state globally,
   but the pause path (main.cpp:92-95) skips on_update while unfocused, and
   state re-syncs on resume — no stuck-input path found.
