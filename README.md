# Shadow Fight 2 — native port

<!-- STATUS:START -->

![gates](https://img.shields.io/badge/gates-90%20%C2%B7%2059%20%C2%B70-green) ![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey) ![lang](https://img.shields.io/badge/C%2B%2B-20-00599C) ![api](https://img.shields.io/badge/OpenGL-3.3-5586A4)

### Readiness: 97%

![progress](https://hwindinkg.github.io/reSF2/progress.svg)

84 of 87 checklist items; every gap below is documented in BUGS.md.

Gates with a baseline: **90** (59 at 0 divergences, 31 known non-zero) · reference: the shipped `sf2.readable.js`

| System | | Readiness | Checks | Gaps |
|---|---|---|---|---|
| **Assets & archives** | 🟩 | 100% | 5/5 | - |
| **Rendering** | 🟩 | 100% | 4/4 | - |
| **Animation & physics** | 🟨 | 89% | 24/27 | cloth/chain divergence: FIXED the measured defect and bounded the rest. The solver cadence is 1:1 with JS `Al.ia` (one sk+jE per sim frame), all 277 merged edges resolve, Gravitation 0.4 and IterativeProcess 2 match internal_settings.xml, the ankle edge is satisfied (15.0 in 3D - the 5.6 reading was a 2D projection of a chain hanging along z), and the dummy is EXACT (0/568, max 0.426) after excluding the free-falling Mass=0 `COM` node. Both traces now carry [x,y,z] and the pose gate measures 3D (417 -> 425 on the same artifacts). The real bug found: `Fighter::advance_step` returned at the clip-finished branch WITHOUT stepping the solver, so the cloth froze for exactly one frame at every clip boundary - 11 frames measured with step 0.000 against the oracle's 0.4-0.9 - losing its velocity while JS `Nd.ia` runs unconditionally. Stepping it through the clip-less path removed all 11 and lowered the cloth 3D deviation mean 4.687 -> 3.901 and the gate 425 -> 421. Speed is now dumped on BOTH sides as the true `ma - mf` (not derived from positions): the module matches (|v| 2.291 vs 2.296) while the velocity VECTOR differs by 0.623 mean / 9.4 max, and that difference is almost entirely TANGENTIAL to the anchor (0.76-1.21 vs radial 0.25-0.81) - the chain rotates at a slightly different rate, and without Attenuation the phase difference cannot decay. The float-vs-double explanation was MEASURED and REFUTED: perturbing gravity by 1e-7 or 1e-6 leaves every metric identical (3.901 mean / 414 frames), and even 1e-2 moves it only to 4.561, so the chain is insensitive at the precision scale - the residual needs a systematic difference of order 1 percent, which is still unidentified after enumerating ALL 277 merged edges name-matched in 3D (mean |port-oracle| length difference 0.013 units, no edge above 0.5) and after checking for a hidden velocity reset on either side (0 collapses below 30 percent of the neighbour average). A NEW gate covers velocity too: posevel:dojo-move = 69 of 568 frames with any bone true velocity over the 5-unit tolerance (player only; the dummy 0/568, max 2.44), so a velocity-class regression can no longer hide behind matching positions. The per-edge RELAXATION corrections are now measured on BOTH sides (oracle ?relaxTrace=1 reads Nd.oa.Nl.all read-only from afterFrame - no engine patching; port SF2_TRACE_RELAX=1 samples the same index band): over frames 100..400 on 60 edges the pre-relax lengths differ by 0.19 units on average and the applied correction by 0.116 (max 0.42), all of it proportional to the already-diverged chain - no edge has a wrong length, mass or rest value. Frame alignment between the two traces is PROVEN at lag 0 by the skeleton (mean distance 0.578 at lag 0 - the constant z offset - versus 2.23/4.22/6.23/8.21 at lags +-1/+-2/+-3/+-4): with same-numbered frames i213 already differs by 0.64 at f=100, i210/i224 cross 0.5 by f=105-106 and i223 only from ~f=139 (locomotion start). On the SAME 60 edges and frames 60..140 the chain differs by 0.283 BEFORE relaxation and 0.134 AFTER (mixed-stage variants 0.242/0.192), so relaxation absorbs part of the difference each frame; the earlier pre-0.37-versus-post-0.013 figures compared different sample sets and is corrected here. MORE IMPORTANTLY, the whole port-arrives-more-distorted idea is a STAGE ARTIFACT: comparing both sides PRE-jE (oracle hook on the fight.yb.Nd INSTANCE, try/catch guarded, trace intact) drops the per-edge excess from +0.41 to +0.034, and by class the port is even slightly LESS distorted on cloth<->skeleton edges (-0.041), so the solver INPUT state matches to 0.04 in the steady stance. Running that stage-matched comparison over the WHOLE fight (60 edges, f=0..600) locates the origin precisely: the input states differ most in the FIRST frames (f=0 4.60, f=1 11.65, f=2 16.64, f=3 10.23, f=4 6.07, f=5 2.53) and then settle to median 0.15 / mean 0.25 after f=150, so the residual is born in the STARTING chain state (port seeds bind + pre-fight settling, JS inherits the hub preview, whose history is not in the shipped data) and never decays because body cloth has no Attenuation. Every local remedy for that start state was measured and makes it WORSE: zeroing the velocity on the first live frame 417->423, and warming the chain with N solver steps at the first live frame 3.901 -> 4.487 (5 steps) -> 5.276 (20) -> 5.768 (60). The JS start state is not reachable from the port bind seed, so this is recorded as a dependency on the hub preview history, which was also PROVEN unreachable: the lead-in branch (fight.eu === 0) never executes in a traced run (0 lines for preview, previewdiag and leadin). Precision is ruled out directly too - a full double solver state (std::vector<double> sol_ma_/sol_mf_ with double sk/jE arithmetic) measures 3.883 mean versus float 3.901 with the same max 29.4 and the same 414 frames, so arithmetic width is not the cause. A THIRD start-state remedy was measured and also fails: stepping the solver through the pre-fight idle (the port freezes the chain for 332 idle rows while the JS keeps its hub preview running) gives 3.901 -> 5.489 (1 step per idle frame) and 5.199 (3 steps), with the max rising to 38.6/54.2. All three start-state attempts therefore move the chain AWAY from the JS state (zeroing 417->423, live-frame warm 3.901->4.487/5.276/5.768, idle prewarm 3.901->5.489/5.199). The earlier claim that the origin is the starting chain state with a 16.64 pre-length gap at f=2 is CORRECTED: the POSITION traces (the only signal with proven lag-0 alignment) peak at f=1 (3.06 mean / 15.68 max) and decay to about 1.0 by f=10, and a 1.5-unit position gap cannot produce a 16-unit edge-length gap, so that per-frame preje number was a probe artifact; the window aggregates (pre 0.283 vs post 0.134 on the same edges and frames) and all four start-state results stay valid. What is left is the PLAYER's chain riding with a different lag under translation (site mean ~2, spread ~10) at StepForward/StepBack; BLOCKED ON USER - Titan bodyguards: the location question (the guards should load `skyport`, Titan `neural_network`) and the roster scroll are only partially measured (BUGS.md QA-29.10 п.7); the remaining half needs a live-game screenshot of the Titan bodyguard selection; BLOCKED ON USER - ranged/magic/ability enemies: MEASURED, not a port-vs-JS divergence - the reference (web build) never selects ranged/magic/ability moves in a whole traced fight (0 picks), the `.dat` animation pool has no `*Player` cast rows and the JS `EnableBossAbility -> Pi.dS` path is a `debugger;` stub. The live game does throw shurikens, so that path is outside the reference and needs a live trace to compare against |
| **Combat** | 🟩 | 100% | 6/6 | - |
| **AI** | 🟩 | 100% | 8/8 | - |
| **UI & screens** | 🟩 | 100% | 22/22 | - |
| **Quests & progress** | 🟩 | 100% | 4/4 | - |
| **Audio** | 🟩 | 100% | 3/3 | - |
| **Save & input** | 🟩 | 100% | 3/3 | - |
| **Localization** | 🟩 | 100% | 1/1 | - |
| **Harness & verification** | 🟩 | 100% | 4/4 | - |
| **overall** | 🟩 | 97% | 84/87 | - |

The `overall` row is every checklist item at once (84 of 87); a system row counts only that system's own items, so the rows are NOT averaged into the overall.

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
| `core/scene` | 40237 | 35 |
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
| **total** | **110562** | |
<!-- STATUS:END -->
