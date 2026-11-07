# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-39%20%C2%B7%2038%20%C2%B70-green) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 96%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

74 of 77 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **39** (38 at 0 divergences, 1 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟨 | 82% | 14/17 | cloth/chain divergence: FIXED the measured defect and bounded the rest. The solver cadence is 1:1 with JS `Al.ia` (one sk+jE per sim frame), all 277 merged edges resolve, Gravitation 0.4 and IterativeProcess 2 match internal_settings.xml, the ankle edge is satisfied (15.0 in 3D - the 5.6 reading was a 2D projection of a chain hanging along z), and the dummy is EXACT (0/568, max 0.426) after excluding the free-falling Mass=0 `COM` node. Both traces now carry [x,y,z] and the pose gate measures 3D (417 -> 425 on the same artifacts). The real bug found: `Fighter::advance_step` returned at the clip-finished branch WITHOUT stepping the solver, so the cloth froze for exactly one frame at every clip boundary - 11 frames measured with step 0.000 against the oracle's 0.4-0.9 - losing its velocity while JS `Nd.ia` runs unconditionally. Stepping it through the clip-less path removed all 11 and lowered the cloth 3D deviation mean 4.687 -> 3.901 and the gate 425 -> 421. What is left is the PLAYER's chain riding with a different lag under translation (site mean ~2, spread ~10) at StepForward/StepBack; BLOCKED ON USER - Titan bodyguards: the location question (the guards should load `skyport`, Titan `neural_network`) and the roster scroll are only partially measured (BUGS.md QA-29.10 п.7); the remaining half needs a live-game screenshot of the Titan bodyguard selection; BLOCKED ON USER - ranged/magic/ability enemies: MEASURED, not a port-vs-JS divergence - the reference (web build) never selects ranged/magic/ability moves in a whole traced fight (0 picks), the `.dat` animation pool has no `*Player` cast rows and the JS `EnableBossAbility -> Pi.dS` path is a `debugger;` stub. The live game does throw shurikens, so that path is outside the reference and needs a live trace to compare against |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟩 | 100% | 8/8 | - |
| **UI & screens** | 🟩 | 100% | 22/22 | - |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 96% | 74/77 | - |

The `overall` row is every checklist item at once (74 of 77); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

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
| `core/scene` | 40149 | 35 |
| `app/game` | 12224 | 1 |
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
| **total** | **110474** | |
<!-- STATUS:END -->
