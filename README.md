# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-104%20%C2%B7%2066%20%C2%B70-green) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 97%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

86 of 89 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **104** (66 at 0 divergences, 38 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟨 | 90% | 26/29 | cloth/chain divergence: FIXED the measured defect and bounded the rest.; BLOCKED ON USER - Titan bodyguards: the location question (the guards should load `skyport`, Titan `neural_network`) and the roster scroll are only p…; BLOCKED ON USER - ranged/magic/ability enemies: MEASURED, not a port-vs-JS divergence - the reference (web build) never selects ranged/magic/ability… (details: BUGS.md) |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟩 | 100% | 8/8 | - |
| **UI & screens** | 🟩 | 100% | 22/22 | - |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 97% | 86/89 | - |

The `overall` row is every checklist item at once (86 of 89); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

### Oracle divergences by scenario

| Scenario | Divergences | Status |
|---|---:|---|
| `challenge` | 0 | 🟩 exact |
| `boss-lynx` | 0 | 🟩 exact |
| `tournament` | 0 | 🟩 exact |
| `survival` | 0 | 🟩 exact |
| `dojo-demo` | 0 | 🟩 exact |
| `dojo-move` | 0 | 🟩 exact |

`0` = the whole fight state matches the original JS game frame by frame.

![history](https://hwindinkg.github.io/reSF2/history.svg)

### Code

| Module | Lines | Files |
|---|---:|---:|
| `core/app` | 49799 | 18 |
| `core/scene` | 43332 | 35 |
| `app/game` | 13345 | 1 |
| `core/data` | 2162 | 20 |
| `core/audio` | 1308 | 4 |
| `app/ai_golden` | 961 | 1 |
| `core/render` | 773 | 7 |
| `app/fight_controller_demo` | 706 | 1 |
| `app/ai_demo` | 689 | 1 |
| `app/fight_demo` | 679 | 1 |
| `app/move_demo` | 583 | 1 |
| `app/fighter_probe` | 498 | 1 |
| `app/scene_probe` | 435 | 1 |
| `app/texture_probe` | 398 | 1 |
| `app/anim_model_probe` | 335 | 1 |
| `app/move_probe` | 296 | 1 |
| `app/asset_explorer` | 263 | 1 |
| **total** | **116562** | |
<!-- STATUS:END -->
