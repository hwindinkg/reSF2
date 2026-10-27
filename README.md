# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-30%20%C2%B7%2027%20%C2%B70-green) ![readiness](https://img.shields.io/badge/readiness-72%25-blue) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 72%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

34 of 47 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **30** (27 at 0 divergences, 3 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟨 | 75% | 3/4 | moon disc: 20-40 px lower and darker than the oracle |
| **Animation & physics** | 🟨 | 75% | 3/4 | gravity / impulses point UP instead of down (reported) |
| **Combat** | 🟥 | 67% | 4/6 | player throw: gate correct, probe cannot hold the range; mid-fight disarm: moves/sounds stay on the lost weapon instead of fists |
| **AI** | 🟥 | 67% | 4/6 | `fk=9` (dodge) and `fk=10` (block) never fire in a live fight; in Survival one clean hit kills the enemy (Eclipse x4000) - unverified after the align fix |
| **UI & screens** | 🟥 | 38% | 3/8 | results screen: RETRY / STORE buttons missing; results: `<Reward><Item>` drops are not granted; map: a completed battle still offers FIGHT; shop: solver state on the preview start; energy dialog: raw variable names instead of labels, no crystal grant, HUD energy tap and the green plus are dead |
| **Quests & progress** | 🟨 | 75% | 3/4 | quests stall after buying the knives until a restart (reported) |
| **Audio** | 🟥 | 67% | 2/3 | SFX while the scene is hidden during a round change |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 3/3 | - |

### Oracle divergences by scenario

| Scenario | Divergences | Status |
|---|---:|---|
| `challenge` | 0 | 🟩 exact |
| `boss-lynx` | 0 | 🟩 exact |
| `tournament` | 0 | 🟩 exact |
| `survival` | 863 | 🟥 open |
| `dojo-demo` | 2 | 🟨 known |
| `dojo-move` | 2 | 🟨 known |

`0` = the whole fight state matches the original JS game frame by frame.

![history](https://hwindinkg.github.io/reSF2/history.svg)

### Code

| Module | Lines | Files |
|---|---:|---:|
| `core/app` | 43200 | 17 |
| `core/scene` | 38475 | 35 |
| `app/game` | 10080 | 1 |
| `core/data` | 2162 | 20 |
| `core/audio` | 1251 | 4 |
| `app/ai_golden` | 961 | 1 |
| `app/fight_controller_demo` | 706 | 1 |
| `app/ai_demo` | 689 | 1 |
| `core/render` | 682 | 7 |
| `app/fight_demo` | 679 | 1 |
| `app/move_demo` | 583 | 1 |
| `app/fighter_probe` | 498 | 1 |
| `app/scene_probe` | 435 | 1 |
| `app/texture_probe` | 398 | 1 |
| `app/anim_model_probe` | 335 | 1 |
| `app/move_probe` | 296 | 1 |
| `app/asset_explorer` | 263 | 1 |
| **total** | **101693** | |
<!-- STATUS:END -->
