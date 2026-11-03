# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-37%20%C2%B7%2035%20%C2%B70-green) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 99%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

66 of 67 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **37** (35 at 0 divergences, 2 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟨 | 86% | 6/7 | kusarigama + enemy AI: the spurious f=161 decision is measured - gating the Random path on a fired `KeyPressed` event (`SF2_RANDOM_REQUIRE_EVENT=1`, default off) drops the diff 1394 -> 1223 and moves the first divergence to f=249/379, where the port now MISSES the oracle's decision: `dsb` must inject the event instead of calling the Random path directly. Root cause localized - path picks a move directly (`br:"random/hJa"`, `fk=6`) on the idle-clip END, where JS `hJa` only injects a synthetic KeyPressed and `EZa` admits no candidate for type 10 (port's own `gc_frame`: `matched=0 kept=0`); the oracle's decision rows are now traced (`t:"ai"`, `diff:boss-lynx` stays 0) and the two sides match frame-for-frame at a +129 offset EXCEPT one spurious early decision in the port (f=161 `KnivesSpinningSlash`; the oracle decides first at its f=333) - a single-decision bug, not a systemic AI difference; `diff:kusarigama` reproduces it; not gated yet |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟩 | 100% | 8/8 | - |
| **UI & screens** | 🟩 | 100% | 22/22 | - |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 99% | 66/67 | - |

The `overall` row is every checklist item at once (66 of 67); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

### Oracle divergences by scenario

| Scenario | Divergences | Status |
|---|---:|---|
| `challenge` | 0 | 🟩 exact |
| `boss-lynx` | 0 | 🟩 exact |
| `tournament` | 0 | 🟩 exact |
| `survival` | 0 | 🟩 exact |
| `dojo-demo` | 2 | 🟨 known |
| `dojo-move` | 2 | 🟨 known |

`0` = the whole fight state matches the original JS game frame by frame.

![history](https://hwindinkg.github.io/reSF2/history.svg)

### Code

| Module | Lines | Files |
|---|---:|---:|
| `core/app` | 48158 | 18 |
| `core/scene` | 39766 | 35 |
| `app/game` | 12172 | 1 |
| `core/data` | 2162 | 20 |
| `core/audio` | 1251 | 4 |
| `app/ai_golden` | 961 | 1 |
| `app/fight_controller_demo` | 706 | 1 |
| `app/ai_demo` | 689 | 1 |
| `core/render` | 687 | 7 |
| `app/fight_demo` | 679 | 1 |
| `app/move_demo` | 583 | 1 |
| `app/fighter_probe` | 498 | 1 |
| `app/scene_probe` | 435 | 1 |
| `app/texture_probe` | 398 | 1 |
| `app/anim_model_probe` | 335 | 1 |
| `app/move_probe` | 296 | 1 |
| `app/asset_explorer` | 263 | 1 |
| **total** | **110039** | |
<!-- STATUS:END -->
