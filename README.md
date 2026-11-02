# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-35%20%C2%B7%2033%20%C2%B70-green) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 97%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

64 of 66 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **35** (33 at 0 divergences, 2 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟩 | 100% | 6/6 | - |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟨 | 88% | 7/8 | live game: boss ranged/magic and abilities (shurikens, invisibility cast, summons) need a per-frame LIVE trace - the shipped JS ability path is a `debugger` stub, so the data cannot settle it |
| **UI & screens** | 🟩 | 95% | 21/22 | Titan bodyguards: the roster scroll and the per-fight location are both measured working (all 6 `C3_BOSS_TITAN` entries centre at x=640, delta 0.00; `<Fight Location>` applied). The reported wrong BACKGROUND still needs the operator's screenshot (QA-29.10 #7) |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 97% | 64/66 | - |

The `overall` row is every checklist item at once (64 of 66); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

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
| `core/app` | 48104 | 18 |
| `core/scene` | 39645 | 35 |
| `app/game` | 12126 | 1 |
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
| **total** | **109818** | |
<!-- STATUS:END -->
