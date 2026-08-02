# SOAK_TRIAGE — 2026-08-01 manual battle soak

| | |
|---|---|
| **Date** | 2026-08-01 |
| **Gate** | GE — **FAILED** |
| **Soak scope** | Dojo tutorial + moon battle vs Kenji (manual play, debug build) |
| **Linked failure record** | `soak-2026-08-01-ai` in `.codebase/FAILURES.json` (type `bug_fix`, tags `ai/movement/dialogue/ui/audio/soak`) |
| **Lesson ref** | Golden/determinism tests assert traces, not behavior — intro gating, approach movement, input order symmetry, dialogue advancement and asset-load integrity are only reachable via real play. Add behavioral tests per defect below. |
| **Defect count** | 30 rows / 7 clusters (AI 6, Movement 5, Dojo 3, Dialogue 6, UI 6, Audio 3, Log 1) |

Evidence source markers:
- `[log]` — verified verbatim in `assets/resf2_debug.log` (2026-08-01 16:46, 1.36 MB; trace contains `INPUT/MOVEMENT/MOVE/KEY/HIT` only, player-side movement trace, frames up to ~7218).
- `[report]` — quoted from the soak report (session lines `[scene]`/`[COMBAT]`/`[DIALOG]`/`[audio]`/`[MAP]` were captured at a higher verbosity than the on-disk trace; not present in this log file).

---

## 1. AI / combat (GAP-4 integration — OUR regression scope, highest priority)

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| A1 | Enemy attacks BEFORE battle start — no intro gating | `[report]` `[scene] enter Battle` → immediately `[COMBAT] Enemy hit player: base=0.110` ×2 before `[STATE] move_state 10->0 ... StartStance` ends | Battle intro gating (GAP-4) | Assert zero enemy damage events before StartStance completes |
| A2 | Enemy has NO start stance animation (only player plays stance_2) | `[log]` `anim='stance_2'` appears only in player `[MOVEMENT]` trace (log head); `[report]` `[STANCE] Playing start stance (stance_2, 52 frames)` once per location load — no enemy stance line | Enemy intro state machine | Assert enemy plays its start-stance anim during battle intro |
| A3 | Enemy stands still with tactic Standard — never approaches | `[log]` zero enemy `[MOVEMENT]` lines in entire trace; enemy appears only as HIT target (`[HIT] ... hit enemy capsule=EHead`); `[report]` no enemy step_forward/step_back anims in battle log | Tactic pipeline approach logic (tactic_pipeline.cpp) | Assert enemy closes distance to player within N seconds of round start |
| A4 | AI decisions change VERY fast in debug overlay (decision every frame) | `[report]` shipped ResponseDelay 0/0 opens the gate unconditionally | Tactic decision cadence (tactic_pipeline.cpp) | Assert AI holds a decision for ≥ response-delay ms |
| A5 | Player takes heavy damage when approaching the enemy | `[report]` `[COMBAT] Enemy hit player: base=0.110 final=0.110` every few lines during approach | Combat hit registration | Assert hit rate on approach ≤ original-game frequency |
| A6 | After player start stance → immediately idle; original holds stance until interaction/hit | `[log]` `[MOVEMENT] pos_x=-290.0 ... state=10 anim='stance_2'` → `state=0 anim='stance_2'` → `[MOVEMENT] pos_x=-276.2 ... state=0 anim='stance_idle'` right after | Start-stance lifecycle (game.cpp state machine) | Assert stance_idle starts only after stance_2 duration or a hit/interaction |

## 2. Movement / input

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| M1 | a+s cannot do back roll, s+a can — input ORDER asymmetry | `[report]`; `[log]` back+jump combos present (`[INPUT] fwd=0 back=1 up=1 ...`) but no roll fires on a+s ordering | Input buffering / move matching (input pipeline) | Assert back roll fires for both key orderings (a+s and s+a) |
| M2 | All back rolls (upper+lower) move the character poorly (short distance) | `[log]` `[MOVEMENT] ... state=1 anim='step_back' root_dx=-9.01 step_frames=0` then `root_dx=-0.88 step_frames=1..4` (≈10 px total, then crawl); `[MOVE] f=2571 BackHandflip (handstand retreat)` | Back-move root motion data | Assert back roll displacement equals authored root-motion distance |
| M3 | Jump slightly moves character LEFT (drift) | `[log]` `[MOVEMENT] pos_x=-500.1 ... anim='jump' root_dx=0.10` (nonzero root drift during jump; also `root_dx=-0.00` in later jump) | Jump root-motion / physics | Assert jump drift ≤ threshold (e.g. 0.01 px/frame) |
| M4 | Rapid a/d pressing moves in small steps without waiting for walk animation end — breaks a+a / d+d dashes | `[log]` `[KEY] f=802 ... anim='step_forward'` + `[MOVE] f=802 Kick dir=Forward -> FrontKick` (buffered attack fires mid-step); `[MOVE] f=1101/1931/2859 DoubleStepForward (dash)`; rapid-press block f=5476–5522: `P pressed ... ms=10` → `NO CANDIDATE (cand=0 ms=10 ...)` repeatedly | Step buffering (input* / game.cpp) | Assert a+a / d+d within window yields dash, not two micro-steps |
| M5 | Character turn is instant/sharp; must be smooth; turn after a move ends behind opponent only after a movement input | `[report]` | Turn smoothing / facing logic | Assert turn animation duration > 0 and turn deferred until movement input |

## 3. Dojo / quest

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| Q1 | Quest counts movement on a single d press; original requires a few steps (forward/back) | `[report]`; `[log]` single-step sequences visible (`[KEY] f=802 ... anim='step_forward'` → `[MOVEMENT] ... state=2 anim='step_forward'`) | Quest step counting (quest/*) | Assert quest advances only after N steps, not 1 keypress |
| Q2 | Punching bag: hits don't register visually (bag doesn't move), yet progress counted | `[log]` HIT lines exist with capsule edges (`[HIT] f=4733 move='BackKick' hit enemy capsule=EHead sq_dist=348.5 thresh=441.0 atk_edge=EThigh_2`; `f=5232 ... capsule=EArm_1 sq_dist=40.1 thresh=289.0`) — only 2 HIT lines in whole trace | Bag hit reaction / visual feedback | Assert bag position changes on each registered hit |
| Q3 | After bag phase a Kenji fight should follow; no story progression (returns to map, nothing advances) | `[report]` | Quest/story progression (quest/*, story state) | Assert Kenji fight starts after bag phase completes |

## 4. Dialogue

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| D1 | Dialogue appears at BOTTOM, darkens entire background; must be centered | `[report]` | Dialogue layout | Assert dialogue box centered with no full-screen dim |
| D2 | After dialogue, dojo fully RELOADS — must not reload | `[report]` `[scene] enter Dialogue` ... `-> Battle`, and on return full location reload | Scene transitions | Assert location state preserved across dialogue, no reload |
| D3 | Text must appear IMMEDIATELY, not letter-by-letter (typewriter) | `[report]` | Dialogue text renderer | Assert full line visible on open (typewriter off) |
| D4 | **Stuck**: dialogue never advances past line 1 | `[report]` `[DIALOG] typewriter=100% line=1/2` repeats ~50 times, never advances to line 2 | Dialogue line advancement | Assert next-line advances on confirm when typewriter=100% |
| D5 | Sensei dialogue shows RUSSIAN while game is English — eng.xml not applied | `[report]` | i18n / locale loading | Assert dialogue strings come from eng.xml in English session |
| D6 | Top-right expanding dialogue's bottom texture (подложка) stretched too far | `[report]` | Dialogue box texture scaling | Assert background texture aspect preserved |

## 5. UI

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| U1 | Weapon not displayed — only a yellow placeholder | `[report]` `Enemy weapon 'weapon_knuckles.xml': 0 nodes, 0 edges, 0 capsules` — weapon model load FAILS (0 nodes) | Weapon model loader (weapon*) | Assert weapon model loads with >0 nodes/edges |
| U2 | Shop doesn't work and looks wrong | `[report]` | Shop screen | Assert shop opens, renders and purchases |
| U3 | Settings look wrong | `[report]` | Settings screen | Assert settings layout matches design |
| U4 | Profile not clickable | `[report]` | Menu hit-testing | Assert profile entry navigates on click |
| U5 | Menu buttons don't disappear after navigating to submenu | `[report]` | Menu state management | Assert submenu hides parent buttons |
| U6 | Menu unfolds without animation (scroll top-down) | `[report]` | Menu animation | Assert menu unfold animation duration > 0 |

## 6. Audio

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| S1 | Player has FEMALE attack sounds; must be male per users.xml/userDefault.xml | `[report]` | Voice/sound selection | Assert player attack voices come from male set |
| S2 | Enemy sounds wrong too (stages.xml) | `[report]` | Sound bank selection per stage | Assert enemy sounds match stages.xml bank |
| S3 | Hit sounds MISSING | `[report]` `[audio] Sound not found or invalid: f_pl_hit1` / `f_pl_hit2` / `f_pl_hit3` (multiple occurrences) | Sound assets / lookup | Assert f_pl_hit* resolve and play on hit |

## 7. Log spam

| ID | Symptom (user words) | Log/evidence | Suspected subsystem | Proposed behavioral test |
|---|---|---|---|---|
| L1 | `round_progress` logged EVERY frame (~150 lines) | `[report]` `[MAP] round_progress: zone='ZONE_1' battle='BOSS_LYNX' done=0 total=6` every frame; `[log]` per-frame logging pattern visible in trace (`[INPUT]`/`[MOVEMENT]` pairs every frame) | Logging rate (MAP logger) | Assert round_progress logged once per state change, not per frame |

---

# SOAK_TRIAGE — Wave 7: parser-level defects (re-soak 2, 2026-08-02)

| | |
|---|---|
| **Date** | 2026-08-02 |
| **Soak scope** | Re-soak 2: parser-level defects — equipment model resolution, battle collision target, armor/helm attach, HUD localization/layout, post-defeat quest flow, input auto-repeat, dialogue textures, shop preview (manual play, debug build) |
| **NEW RESOURCE** | **Frida available** — socket device (remote frida-server, **no USB**) for live-game evidence; the game may be reachable via the socket device |
| **Evidence source markers** | `[log]` — verbatim in `resf2_debug.log` (re-soak 2 trace); `[report]` — quoted from the re-soak-2 report (user words); `[directive]` — explicit user directive, verify (not a symptom report) |
| **Defect count** | 12 rows (P1–P12) |

## 8. Wave 7 — parser-level defects (re-soak 2, 2026-08-02)

| ID | Symptom | Evidence | Suspected subsystem | Proposed test |
|---|---|---|---|---|
| P1 | Weapon model NOT FOUND on equip — equipped weapon invisible, only animations play | `[log]` `Player weapon 'Knives' model NOT FOUND (tried: weapon_knive.xml)!` — item 'Knives' maps to wrong model filename (`weapon_knive.xml` vs expected `weapon_knives.xml`) | Weapon model loader / item→model filename mapping (weapon*) | Assert item 'Knives' resolves to `weapon_knives.xml` and the weapon model loads with >0 nodes/edges |
| P2 | Battle hit detection uses the PUNCHING BAG — battle locations load the bag and HIT! lines log `bag_edge=` in fights vs the enemy; player damage only connects at point-blank | `[log]` battle location loads `Punching bag: 15 nodes...`; HIT! lines log `bag_edge=Edge17` in FIGHTS vs the enemy (moon location); player damage to enemy only connects at `sq_dist≈0-5` | Battle location setup / collision-target selection (hit detection) | Assert the enemy fighter is the collision target in battle locations; the bag exists only in the dojo |
| P3 | Armor and helms don't render — only the standard body/head models show | `[report]` equipped armor/helm items from `users.xml` never attach to the fighter model | Equipment attach / model composition (users.xml equipment parsing) | Assert equipped armor/helm attach to the fighter model |
| P4 | Fighter names in the battle HUD don't come from the language pack | `[report]` localized fighter names required — eng/rus xml not applied to HUD | HUD i18n / name lookup | Assert HUD fighter names come from the active locale pack (eng/rus xml) |
| P5 | HUD badly laid out | `[report]` "плохо настроен худ" | HUD layout | Assert HUD element positions match design |
| P6 | Story breaks after losing the first battle — no progression | `[log]` `[DOJO] Switched to punching bag / enemy fighter` toggling repeatedly with no progression; quest parser/state after defeat broken (expected: retry or advance, not a bag/fighter toggle loop) | Quest parser / post-defeat story state | Assert defeat leads to retry or advance — never a bag/fighter toggle loop |
| P7 | `[MOVE] Duck` fires ×16 in a row with NO key events | `[log]` `[MOVE] Duck (anim 'duck', prio=10)` ×16 consecutive with no intervening KEY lines — held-key auto-repeat bug (move re-triggered every frame) | Input auto-repeat / move trigger cadence (input pipeline) | Assert a move fires once per key-down, not every frame while the key is held |
| P8 | Dialogue textures and placement still wrong (deeper than D1–D6) | `[report]` check the dialogue texture parsers / atlas frames | Dialogue texture parser / atlas frame mapping | Assert dialogue box texture region maps to the correct atlas frame and placement matches design |
| P9 | Shop shows no character preview on the left; equipped weapon invisible there too | `[report]` links P1 (weapon model resolution) | Shop preview rendering / weapon model resolution (weapon*) | Assert the shop renders the character preview with the equipped weapon |
| P10 | Moves parse but attributes/effects misapplied — only animations apply, and incorrectly | `[directive]` user directive: verify the moves.xml parser — "применяются только анимации и то неправильно" | moves.xml parser (attribute/effect application) | Assert each move applies its authored attributes/effects, not just an animation |
| P11 | Story flow after defeat (quest parser) | `[directive]` user directive: verify the quest parser (story flow after defeat) | Quest parser / story state machine | Assert the quest state machine advances per authored flow after defeat |
| P12 | Dialogue texture parsers/positions | `[directive]` user directive: verify dialogue texture parsers/positions | Dialogue texture parser / positioning | Assert dialogue texture frames/positions match authored layout |
