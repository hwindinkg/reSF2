# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-38%20%C2%B7%2038%20%C2%B70-brightgreen) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 99%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

68 of 69 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **38** (38 at 0 divergences, 0 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟨 | 89% | 8/9 | dojo dummy cloth: with the seat fixed (f=0 exact) the ragdoll chain still hangs 1.234 units lower than the oracle's (fixed `Node12` -337.231 vs -335.997 at the same pivot -108.114); a seat sweep proves the two are independent (1.052 -> f0 exact, ragdoll -1.23; 2.286 -> ragdoll exact, f0 +1.234). `Edge15 Node12-Node7` has rest=25 against bind=37, so the equilibrium is a heavily-stretched constraint residual; the state comparator skips `y` on ragdoll frames, so no gate covers it yet |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟩 | 100% | 8/8 | - |
| **UI & screens** | 🟩 | 100% | 22/22 | - |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 99% | 68/69 | - |

The `overall` row is every checklist item at once (68 of 69); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

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
| `core/app` | 48158 | 18 |
| `core/scene` | 40012 | 35 |
| `app/game` | 12196 | 1 |
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
| **total** | **110309** | |
<!-- STATUS:END -->
