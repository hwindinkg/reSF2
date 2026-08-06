# tests/legacy/ — retired test sources

Sources in this directory are **retired** from the ctest suite (no `add_test`
entry, no CMake target in `tests/CMakeLists.txt`) per the Wave 10B user
mandate: the new real-binary E2E layer (`tests/e2e/`, which boots the actual
`resf2_app` with `--input-script` scripts and asserts through the app's own
diagnostics) is the primary verification now. These legacy suites were
hermetic-injection tests (headless harness + injected `TestPlatform`) whose
scenarios the E2E layer now verifies on the real binary.

The code is intentionally **not deleted** — it stays here for reference and
for any probe that needs to be revived before an equivalent E2E test exists.

## Retired tests and their E2E replacements

| Retired test (hermetic) | Scenarios it probed | Replaced by (real-binary E2E) |
|---|---|---|
| `test_soak_ai_defects` | A1 intro gate (no enemy decisions before the battle intro), A2 enemy start-stance animation, A6 player stance persists until first input | `test_e2e_battle_knockback_bounds`, `test_e2e_battle_location_bounds`, `test_e2e_fight_timer`, `test_e2e_knives_kick`, `test_e2e_magic_button` — every E2E battle test boots a REAL battle and drives intro-gated scripted inputs against it |
| `test_soak_ai_pacing` | A3/A4/A5 per-decision wait-pacing (decisions hold, enemy closes ground, approach damage bounded) | the same battle E2E set — real fights pace decisions and play out full rounds (`test_e2e_fight_timer` walks both round timeouts end to end) |
| `test_soak_quest_defects` | Q1 movement-stage step count, Q2 bag visual reaction, Q3 Kenji fight after the bag phase (+ §7 L1 map-log spam gate) | `test_e2e_fight_timer` — boots the SAME `--tutorial-start` chain and walks FIRST_FIGHT → COMPLETE with the quest chain advanced. (The L1 map-log gate side-probe is not re-asserted by E2E.) |
| `test_soak_dialogue_defects` | D1 centered panel / no background dim, D2 no location reload, D3 instant reveal, D4 line advance + completion, D5 eng.xml strings, D6 panel height | `test_e2e_dialogue_background` — the D1 dim regression probe (location must stay lit behind the parchment, read from real rendered pixels); `test_e2e_fight_timer` — dialogue lines must advance for the tutorial to reach COMPLETE. (D2/D3/D5 sub-probes are not re-asserted by E2E.) |
| `test_soak_wave7b_defects` | P4 HUD names i18n, P5 fight HUD layout, P6 defeat-retry flow, P8/P12 dialogue textures, P9 shop fighter preview, P11 quest state chain MOVE→BAG→FIRST_FIGHT→win/loss | `test_e2e_fight_timer` — P11 chain incl. the loss→rematch→win path (both round timeouts) and P6 story continuation; `test_e2e_magic_button` — P5 fight HUD render; `test_e2e_shop_scroll_currency` — P9 shop surface. (P4/P8/P12 sub-probes are not re-asserted by E2E.) |
| `test_enemy_ai_pipeline` | GAP-4 D3: hermetic live enemy-AI decision branch → TacticDecisionPipeline + adapter wiring | the battle E2E set — the real binary's own battle AI now fights the real fights |
| `test_full_battle` | hermetic full battle flow from start to Results | `test_e2e_fight_timer` + battle E2E tests — real fights boot, resolve, and keep the story moving on the real binary |
| `test_battle_integration` | hermetic battle smoke (harness-level) | the battle E2E tests (`test_e2e_smoke` is the harness canary for the E2E path) |

## Kept soak suites (checked against the E2E list — NOT retired)

These were candidates, but their scenarios are **not** covered by the E2E
layer, so retiring them would leave a verification gap. The user's mandate
keeps tests the ORIGINAL engine would pass and the RE cannot fake (1:1
parsing/asset contracts), plus any domain E2E does not exercise:

| Test | Why it stays |
|---|---|
| `test_soak_movement_defects` | M1–M5 probe rolls (both key orders), roll/dash displacement vs authored NPivot, jump drift, step pacing, deferred turn. No E2E test plays these sub-moves — E2E covers only walk clamping (`test_e2e_location_bounds` / `test_e2e_battle_location_bounds`). |
| `test_soak_ui_defects` | U1 pins `weapon_knuckles.xml` loading 138 MacroNodes/276 triangles (asset-loader fidelity); U3 settings layout, U4 Profile click, U5 menu hide, U6 unfold have no E2E equivalent. E2E shop (`test_e2e_shop_scroll_currency`) covers only scroll + currency, not U2's purchase flow/layout. |
| `test_soak_audio_defects` | S1/S2 gender voice sets (male/female per `users.xml` / `stages.xml` Warrior Voice) and S3 hit-sound resolution — audio selection is not verifiable by the current E2E probes. |
| `test_soak_parser_defects` | P1 weapon-model filename resolution, P3 armor/helm model attach, P7 held-key duck auto-repeat — parser/model fidelity against real files (the explicitly keep-mandated category). |
| `test_soak_re3_defects` | R1 pins the dark-silhouette render law and weapon-at-hand placement (render contract, no E2E pixel probe of the fighter). Its R2 battle-hit half IS E2E-covered (`test_e2e_battle_knockback_bounds` requires punches to connect to the enemy fighter), but the render half keeps the suite. |
| `test_soak_re4_defects` | R4b weapon attack-edge resolution and R4c authored tactic-reach parsing (RED parser-fidelity probe), plus R4a armor double-draw — asset/parse contracts, not E2E-verifiable. |
| `test_soak_wave9a_defects` | F1 hit-feedback chain (reaction anim, `_pl_hit2` sound, hit_blade sparks, swish, KO fall sound), F2 block duty cycle, F3 root-motion tail — combat-feel details the E2E battle tests do not assert. Only its F1d knockback half is E2E-covered. |

Not a candidate, but noted: `test_soak_shop_story_defects` (Wave 9B) stays
registered — its S1 icon resolution, S2 category-list render, S3 BUY hit-test,
S4 preview placement go beyond E2E shop scroll+currency; its S5 story
continuation is E2E-covered by `test_e2e_fight_timer`.

`test_input_trace` / `test_step_cooldown` are real-binary tests — untouched
and still registered (in the root `CMakeLists.txt` and `tests/CMakeLists.txt`
respectively).
