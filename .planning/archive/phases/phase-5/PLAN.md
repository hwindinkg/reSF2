> ⚠️ УСТАРЕЛО. Этот документ описывает реверс нативного/Unity билда SF2,
> НЕ веб-версии. Не использовать как источник истины для порта.
> Валидный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.

# Plan: GAP-4 — AI Tactic Model (ADR-005): Decision Pipeline, Table Families, FSM Replacement

## Overview

Replace reSF2's invented enemy-AI FSM with the original's weight/roulette tactic model per
ADR-005 (approved 2026-07-31) and the RE spec `reverse/analysis/PORT_GAPS.md:127-206`.
A `TacticDecisionPipeline` walks 7 chance stages in binary-tracer order (UseDefense →
UseSafeAttack → TableAttack → DodgeMissiles → QuickAttack[i] → Evade[i] →
UseCautiousMovements + epilogue), fed by an in-place-extended `TacticDef` (all 20
decision-level keys, parsed from the real `assets/tacticSettings.xml`) and a
family-indexed `TacticTableSet` (only `.atf` exists in this dump — 1578 files; missing
families are a normal state). The FSM is strangled behind `TacticDecisionAdapter` (Phase A
of the ADR's D7) and deleted only after gate G2 (ADR Phase B).
`ConditionalDesigionFactor` is NOT implemented (0 ARM string matches — hard constraint C1,
documented extension point only).

## Requirements

- R1: `TacticDef`/`TacticContext`/`TacticWeight` extended in place with all 20 decision-level
  keys from PORT_GAPS:152-157; chance keys are full `TacticWeight` curves (ADR D2).
- R2: `TacticWeight::score()` gains the ADR D5 term `a += ctx.animation_factor * animation_factors;`
  after `shift`; the `tactic_settings.cpp:43` `[HEURISTIC-TODO]` is replaced by the term + the
  D6 extension-point comment for `ConditionalDesigionFactor` (no implementation — C1).
- R3: `<AnimationFactors Animation="..." .../>` child elements (real XML shape — confirmed in
  this dump inside `<Animation>`, `<QuickAttackChance>`, `<EvadeChance>`) are parsed into
  per-target entries; absent probe data is neutral-by-zero, never an error.
- R4: Decision-type validation on load: accept `""`/`Tabular` (default) and `ExpectedWait`;
  anything else prints the [ORIGINAL] `Strange tactic type: %s` and the tactic is skipped.
  Real-data consequence (grep-verified 2026-07-31): `Beginner` (`Type="Random"`) is skipped;
  13 `<Tactic>` elements → 12 unique names → 11 loaded. Pinned in a test with justification.
- R5: `RngSource = std::function<unsigned()>` injected into the roulette; production binds
  `std::rand` (parity with existing `jL`), tests bind a seeded LCG.
- R6: Per-family loader architecture (C3): one parser per family, family descriptor table,
  absence-tolerant `TacticTableSet`; shared zlib inflate helper extracted to
  `engine/reverse/zlib_blob.hpp`; `.atf` wired via the existing stride-858 parser;
  `.tbs/.stb/.sts` land as stubs (format unreversed, 0 files in dump).
- R7: Pipeline stages execute in the exact tracer order with `[ORIGINAL]` comments citing
  `0x8F798090`..`0x8F79834C`; epilogue prints DistanceError / FrameError / Intervals /
  EnemyIntervals / DecisionType / `Decision {Wait=%d}` in that order.
- R8: `TacticMemory` (action rings, Strikes, Intervals, ResponseDelay/EnemyResponseDelay
  frame countdowns) replaces the invented `enemy_ai_decision_interval_` on the loaded path.
- R9: Strangler migration: `TacticDecisionAdapter` maps `TacticDecision` → legacy
  `enemy_ai_state_`; the live enemy-AI block (`game.cpp:1740-1826`) routes through the
  pipeline; heuristic `rand()%100` branches deleted ONLY where tactic data is loaded.
- R10: Gate G1 (contract test `test_tactic_decision_trace` green) lands BEFORE any
  FSM-removal commit (ADR C4); gate G2 = G1 + full regression green (ADR C5) before Phase B.
- R11: Regression guard: `test_full_battle`, `test_battle_integration`, `test_trace_replay`,
  all `HeadlessTestRunner` tests, and `test_tactic_weights` green at every phase gate.
- R12: Conventions (ADR C6): `[ORIGINAL]` binary-ref comments; binary parsers in
  `engine/reverse/`; game logic in `engine/game/`; per-step build+ctest gates (NOT batched).

## Architecture Changes

- **Modified**: `engine/game/tactic_settings.hpp` (L64-156) — `TacticContext` +6 fields
  (`animation_factor`, `strikes`, `round_factor`, `self_interval`, `enemy_interval`,
  `current_animation`); `TacticWeight` + `animation_factors` coefficient +
  `animation_factor_entries` (per-target `<AnimationFactors>` children); `TacticDef` + the
  20-key decision block (concrete model in Step A2); `choose`/`choose_debug` + `RngSource`.
- **Modified**: `engine/game/tactic_settings.cpp` — score term (L42-48), `parse_weight`
  (L90-113) + AnimationFactors children, `load` (L159-174) + decision-key parse + type
  validation, `resolve_templates` (L185-213) extended to the new keys.
- **New**: `engine/reverse/zlib_blob.hpp` — inflate helper extracted from
  `atf_tactics.cpp:19-49` (4 known zlib users: atf/tbs/stb/sts — 3+ rule satisfied).
- **Modified**: `engine/reverse/atf_tactics.cpp` — uses the extracted helper (no behavior change).
- **New**: `engine/game/tactic_tables.hpp/.cpp` — `TacticTableType` (10, [ORIGINAL] type
  strings), `TacticFamily` (7), `TacticTable`, `TacticTableSet` (family descriptor table,
  `attack_table(a,b)`, `find(type,name)`, `has_family(f)`, `animation_factor()` neutral-by-zero).
- **New (stubs)**: `engine/reverse/tbs_tables.*`, `stb_tables.*`, `sts_tables.*` — interface
  + "family unavailable" until @reverser R1. If any prove XML → `engine/format/` instead (ADR D3).
- **New**: `engine/game/tactic_memory.hpp` — `TacticMemory` (ADR D8), header-only.
- **New**: `engine/game/tactic_pipeline.hpp/.cpp` — `DecisionStage`, `TacticDecision`,
  `DecisionTrace`, `TacticDecisionPipeline::decide()`, 7 stage functions + epilogue.
- **New**: `engine/game/tactic_decision_adapter.hpp` — ADR D7 mapping table, header-only.
- **Modified**: `engine/game/game.cpp` (L1740-1826 live enemy-AI block; L842/1990/2024
  resets; F1 overlay stash `ai_last_*`) — Phase D wiring, Phase E consumption.
- **Modified**: `engine/game/combat.hpp/.cpp` — Phase E only: delete dead
  `Combat::update_enemy_ai` (combat.cpp:51-202, decl combat.hpp:145), `enemy_ai_state_`
  (combat.hpp:191), `mutable_enemy_ai_state()` (combat.hpp:94).
- **Modified**: `engine/game/game_clean.hpp` (L4184 alias; L1576/1697 overlay readers) — Phase E.
- **New tests**: `tests/test_tactic_memory.cpp`, `tests/test_tactic_tables.cpp`,
  `tests/test_tactic_decision_trace.cpp` (+ `tests/golden/tactic_decision_trace.golden.txt`),
  `tests/test_tactic_decision_adapter.cpp`; **extended**: `tests/test_tactic_weights.cpp`;
  all registered in `tests/CMakeLists.txt` (mirror the `test_tactic_weights` block at
  L301-311: `add_executable` / link / `cxx_std_23` / `add_test` / `TIMEOUT 60`,
  `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` where real assets are read).
- **Docs**: `reverse/analysis/TABLE_FORMATS.md` (R1/R2 deliverable); `PORT_GAPS.md` GAP-4
  marked DONE at the end.
- **NOT modified**: the `jL` roulette algorithm itself, `apply_curve`, existing
  `animation_weights` behavior, the `.atf` parse logic (only the zlib helper moves).

## Key Decisions (surfaced for CONFIRM)

1. **R7 pre-resolved at planning time** (ADR risk R7, "confirm whether game.cpp:1756-1822 is
   live or legacy BEFORE Phase B"): grep-verified 2026-07-31 — `Combat::update_enemy_ai`
   (combat.cpp:51-202) has **zero call sites** (only decl combat.hpp:145 + definition); the
   **live** path is the inline block `game.cpp:1740-1826`, which drives `enemy_ai_state_`
   through the `game_clean.hpp:4184` reference alias into `combat_`. Consequence: ADR D7's
   "both integration points call the pipeline" is narrowed — Phase D wires ONLY the live
   game.cpp block; the dead `Combat::update_enemy_ai` is deleted in Phase E unwired.
   A cheap re-verification step (D1) re-runs the call-graph check before deletion.
2. **XML ground truth refines ADR D2 field shapes** (read from `assets/tacticSettings.xml`,
   element bodies confirmed — data fidelity, not redesign):
   `<UseDefense>` is a container of 3 chance curves (presence = stage-1 gate, not an int);
   `<QuickAttacks>`/`<Evades>` are containers of per-animation `<QuickAttackChance
   Animation="...">`/`<EvadeChance Animation="...">` entries (repeat count = entry count,
   indexing semantics = R6); `<DistanceError>`/`<FrameError>`/`<ResponseDelay>`/
   `<EnemyResponseDelay>` are `<Min Base/><Max Base/>` **ranges**, not scalars;
   `<ExpectedWait>` is an **animation-weight list** (same shape as `animation_weights`),
   not a single curve; `<Memory Strikes="3" RoundFactor="1"/>` carries Strikes/RoundFactor
   as attributes (ring depth source = R5). `TacticContext` also gains `current_animation`
   (binary key `CurrentAnimation` confirmed at `0x8F797574`..; needed by the D5 probe).
3. **ADR D5 score term implemented verbatim** (`a += ctx.animation_factor * animation_factors;`)
   — binding and neutral-by-zero; the parsed `<AnimationFactors>` entries are the weight-side
   probe source consumed by the pipeline in Phase C; `TacticTableSet::animation_factor()` is
   the table-side source, 0.0f until @reverser R2 lands (ADR: "a miss yields 0.0f and is
   neutral, never an error"). Exact contribution semantics of matched entries stay
   `[HEURISTIC-TODO]` under R2 — the term compiles and is exercised from day one.
4. **ADR risk-table numbering is authoritative** (R3=comparator, R4=ExpectedWait,
   R5=Memory/Strikes, R6=QuickAttack/Evade indexing): the ADR's inline refs in D4/D8 are
   off-by-one vs its own table (L492-500); this plan follows the table.
5. **`Type="Random"` rejected** per the binary's 2-type list → real tactic `Beginner` is
   skipped with the [ORIGINAL] print. If binary evidence later shows `Random` accepted, the
   accept-list change is one line + one test count bump.

## @reverser Queue (R1-R7 mapped to explicit steps)

| ADR risk | Step | Deliverable | Until GREEN |
|---|---|---|---|
| R1 `.tbs/.stb/.sts` format; missing families inside `.atf` secondaries? | B3 | `reverse/analysis/TABLE_FORMATS.md` + stub notes | stubs report unavailable; pipeline correct regardless |
| R2 `.atf` stride-858 row semantics (AnimationFactors probe source) | B4 | row-layout doc + candidate C++; **@re-verifier loop max 3** | `animation_factor()` returns 0.0f neutral |
| R3 stage chance comparator/normalisation | P4 | binary anchor + candidate; @re-verifier | JS-port default flagged `[HEURISTIC-TODO]`; golden pins ORDER only |
| R4 ExpectedWait list→`Wait=%d` frames mapping | P4 | binary anchor + candidate; @re-verifier | default mapping flagged; Wait line asserted present, value unpinned |
| R5 Memory/Strikes/Intervals update & reset points | P5 | binary anchors + candidate; @re-verifier | mutations confined to `TacticMemory`, flagged |
| R6 QuickAttack[i]/Evade[i] table indexing | P5 | binary anchor + candidate; @re-verifier | index mapping isolated in one function |
| R7 game.cpp enemy-AI block live vs legacy | D1 (re-verify) | commit-message verdict | PRE-RESOLVED at planning (Key Decision 1) |

Each @reverser step carries a ready-to-dispatch prompt (see the step body). Promotion path:
`[HEURISTIC-TODO]` → `[ORIGINAL]` without structural change (ADR L446). RE-verification
project rule applies to every reversed function (@re-verifier, max 3 rounds; not GREEN →
STOP, keep the heuristic default, escalate).

## Non-Goals

- NO `ConditionalDesigionFactor` implementation (C1 — extension point comment only).
- NO `.tbs/.stb/.sts` parser bodies beyond stubs (blocked on R1; 0 files in dump).
- NO warrior-template tactic selection (game.cpp L1685 "until warrior templates land (5.3)")
  — the wired path keeps the current `tactic("Standard")`/`("NoTables")` selection.
- NO GAP-2/GAP-3 follow-ups, NO perk system, NO GAP-5 (shift tables beyond the family stub).
- NO changes to the `jL` roulette math or curve shapes (verified, golden-pinned).
- NO behavior change when tactic settings are absent: the `[HEURISTIC-TODO]` no-settings
  fallback survives until Phase E.

## Implementation Steps

Verify convention (every step, per-step — supervisor binding, no batching):
`cmake --build build --config Release --target <t>` &&
`ctest --test-dir build -R "^<t>$" --output-on-failure`.
Regression line where noted: `ctest --test-dir build -R "^(test_tactic_weights|test_full_battle|test_battle_integration|test_trace_replay)$" --output-on-failure`.

---

### PHASE A — Foundation: data model + RNG (ADR P0) → GATE G0

**Entry**: ADR-005 approved; this plan CONFIRMED.
**Exit (GATE G0)**: new + existing unit tests green; regression line green; zero behavior
change (all additions default-neutral).

#### Step A1 — TacticWeight probe term + TacticContext extension (TDD)
**Files**: `tests/test_tactic_weights.cpp` (extend, RED first), `engine/game/tactic_settings.hpp` (L64-108), `engine/game/tactic_settings.cpp` (L30-48, L90-113)
**Task**:
1a. RED — extend test_tactic_weights with: (i) `score()` unchanged when `ctx.animation_factor == 0`
(neutral); (ii) with `ctx.animation_factor = 2` and `animation_factors = 3`, score increases by
exactly 6; (iii) `<AnimationFactors Animation="Throw" DamageFactor="4" CounterFactor="0.5"/>`
child of an `<Animation>` parses into one entry `{animation="Throw", factors{...}}`;
(iv) new TacticContext fields default to 0/empty.
1b. Add to `TacticContext`: `animation_factor`, `strikes`, `round_factor`, `self_interval`,
`enemy_interval` (float, =0) and `current_animation` (std::string, empty) — [ORIGINAL] key
refs `0x8F797574`...
1c. Add to `TacticWeight`: `float animation_factors = 0;` and
`struct AnimationFactorEntry { std::string animation; TacticWeight factors; };
std::vector<AnimationFactorEntry> animation_factor_entries;` — parse `<AnimationFactors>`
children in `parse_weight` (recursive reuse of `parse_weight` for the entry's factor attrs).
1d. In `score()` after `a += shift;` (L42) add the ADR D5 term verbatim, then replace the
L43-46 `[HEURISTIC-TODO]` with the D6 extension-point comment block (ConditionalDesigionFactor
BLOCKED, 0 ARM matches, PORT_GAPS.md:145-148 — do not implement from the JS name).
**Verify**: target `test_tactic_weights` builds; ctest `^test_tactic_weights$` green; regression line green.
**Commit**: `feat(ai): add AnimationFactors score term + extended TacticContext (ADR-005 D5/D6)`

#### Step A2 — TacticDef: 20 decision-level keys + type validation (TDD)
**Files**: `engine/game/tactic_settings.hpp` (TacticDef L112-124), `engine/game/tactic_settings.cpp` (load L117-180, resolve_templates L185-213), `tests/test_tactic_weights.cpp` (or split new `tests/test_tactic_def_keys.cpp` mirroring the same CMake block)
**Task**:
2a. RED — tests: synthetic XML written to a temp dir, loaded via `TacticSettings::load`,
asserting every key below; template inheritance of the new keys (local wins, else inherit);
`Type="Bogus"` → skip + `Strange tactic type` printed; `Type="Random"` → skipped;
absent Type → accepted as Tabular. Real-asset case: load `assets/tacticSettings.xml`
(WORKING_DIRECTORY root) → `Standard`, `NoTables`, `UseTables` present, `Beginner` absent,
loaded count == 11 (13 elements − 1 duplicate `Titan_Aggressive` − 1 skipped `Beginner`;
justification comment required).
2b. Add to `TacticDef` (XML-shaped, Key Decision 2):
`bool use_defense` (+ `counter_attack_chance`, `dodge_chance`, `block_chance` — TacticWeight);
`use_safe_attack_chance`, `table_attack_chance`, `cautious_movements_chance`,
`dodge_missiles_chance`, `dodge_magic_chance` (TacticWeight);
`std::vector<std::pair<std::string,TacticWeight>> quick_attack_chances`, `evade_chances`
(per-animation, order preserved);
`struct MinMax { float min = 0, max = 0; } distance_error, frame_error, response_delay,
enemy_response_delay`;
`std::vector<std::pair<std::string,TacticWeight>> expected_wait` (same entry shape as
animation_weights); `int strikes = 0; float round_factor = 0; int memory = 0;`
(`<Memory Strikes RoundFactor/>`; `memory` attr absent in this dump → 0, ring-depth source
flagged R5); `type` validation per R4.
2c. Parse in `load`: sibling elements of `<AnimationWeights>` per the confirmed schema;
`<Min>/<Max>` read via `tof(attr("Base"))` (full-curve possibility flagged `[HEURISTIC-TODO]`).
2d. Extend `resolve_templates` (L185-213): new keys inherit with the same rule (local
declared → keep; else copy from base). Presence-based keys (`use_defense`) inherit only when
not locally present.
**Verify**: target builds; new/extended tests green; regression line green.
**Commit**: `feat(ai): parse 20 decision-level tactic keys + Strange-type validation (ADR-005 D2)`

#### Step A3 — RngSource injection into the roulette (TDD)
**Files**: `engine/game/tactic_settings.hpp` (L126-154), `engine/game/tactic_settings.cpp` (L222-254), `tests/test_tactic_weights.cpp`
**Task**:
3a. RED — test: two `choose` runs with the same seeded LCG produce identical pick sequences
(≥20 draws); different seeds differ; all-zero weights still return -1.
3b. Add `using RngSource = std::function<unsigned()>;` and overloads
`choose(..., RngSource rng)` / `choose_debug(..., RngSource rng)`; existing signatures
delegate binding `std::rand` (production parity, ADR D4). The draw consumes
`rng()` in place of `std::rand()` (same `[0,sum)` math, `RAND_MAX` replaced by the
documented LCG range — keep the draw formula identical: scale by `sum`).
**Verify**: target builds; test green; regression line green.
**Commit**: `feat(ai): injectable RngSource for tactic roulette (ADR-005 D4)`

---

### PHASE B — Table layer (ADR P1) → GATE GT

**Entry**: G0 passed.
**Exit (GATE GT)**: `test_tactic_tables` green against real `.atf` assets; regression line green.

#### Step B1 — Extract shared zlib inflate helper (refactor)
**Files**: `engine/reverse/zlib_blob.hpp` (new), `engine/reverse/atf_tactics.cpp` (L17-49)
**Task**: move `zlib_decompress` into `resf2::reverse::zlib_inflate(std::span<const std::byte>)
-> std::vector<std::byte>` (header-only, same semantics: empty vector on failure);
atf_tactics.cpp calls it. No behavior change.
**Verify**: targets `test_asset_loaders`, `test_asset_pipeline` build; ctest
`^(test_asset_loaders|test_asset_pipeline)$` green.
**Commit**: `refactor(reverse): extract shared zlib inflate helper (ADR-005 D3)`

#### Step B2 — TacticTableSet + family registry + .atf wiring (TDD)
**Files**: `engine/game/tactic_tables.hpp/.cpp` (new), `engine/reverse/tbs_tables.hpp`,
`stb_tables.hpp`, `sts_tables.hpp` (new stubs), `tests/test_tactic_tables.cpp` (new),
`tests/CMakeLists.txt` (after the test_tactic_weights block)
**Task**:
2a. RED — test: `TacticTableSet::load("assets")` on the real dump → `has_family(kAtf)` true,
`has_family(kTbs/kStb/kSts/kDodge/kMovements/kOutcome)` false, no error returned; a known
weapon pair resolves via `attack_table(a,b)` (index key from parsed Header:
`weapon_a + "_" + weapon_b`, v=2 → `weapon_a` alone — NOT the filename); `find(kAttackTable, name)`
round-trips; stub family `find` → nullptr; `animation_factor(anything)` → 0.0f.
2b. Implement per ADR D3: `TacticTableType` (10, [ORIGINAL] type strings PORT_GAPS:166-167),
`TacticFamily` (7), `TacticTable{type,name,candidates,record}`, `TacticTableSet` with a
**family descriptor table** `{subdir, parser-fn, family-tag}` — adding a family = one row +
one parser file. `.atf` family: scan `<root>/tactics/*.atf` (+ `<root>/assets/tactics`),
`reverse::atf::parse_file` each, index by header names. Directory families
(`dodge/`, `movements/`, `outcometablesforattack/`): scan-if-present, else empty.
`tbs/stb/sts` stubs: `parse` returns "family unavailable" (format unreversed — R1).
2c. CMake registration mirroring the test_tactic_weights block (link resf2_game +
resf2_reverse + zlibstatic as needed; `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"`; TIMEOUT 60).
**Verify**: target builds; ctest `^test_tactic_tables$` green; regression line green.
**Commit**: `feat(ai): TacticTableSet registry + .atf family loader, stubs for tbs/stb/sts (ADR-005 D3)`

#### Step B3 — @reverser R1: table formats survey (parallel, no build gate)
**Files**: `reverse/analysis/TABLE_FORMATS.md` (new); stubs updated with notes
**Prompt (dispatch-ready)**: "Determine the `.tbs`/`.stb`/`.sts` container formats (zlib blob
`78 DA` vs XML vs other) from the relocated dump `reverse/binaries/game_region_runtime.bin`
(base 0x8F057000, ARM:LE:32:v7) and the string/xref evidence near the family path strings
(`attack/*.tbs`, `shift/*.stb`, `shiftTables/*.sts`); determine whether the missing families
are packed inside `.atf` `binary_records` secondary records (atf_tactics.hpp:88). Record
findings + anchors in TABLE_FORMATS.md. Ghidra method per PORT_PLAN notes (PC+8 trap;
get_xrefs_to over hand scanning). Any reversed function → @re-verifier, max 3 rounds."
**Verify**: TABLE_FORMATS.md committed; stub headers carry a one-line verdict + anchor each.
**Commit**: `docs(reverse): TABLE_FORMATS survey — tbs/stb/sts + atf secondary records (R1)`

#### Step B4 — @reverser R2: .atf stride-858 row semantics (parallel, no build gate)
**Files**: `reverse/analysis/TABLE_FORMATS.md` (append); `engine/game/tactic_tables.cpp`
(`animation_factor` body) only if verdict GREEN
**Prompt (dispatch-ready)**: "Reverse the 858-byte .atf record layout (engine/reverse/
atf_tactics.hpp L21-28, 47) — per-index semantics, what the u8 pool indices select, and the
secondary `binary_records` region. Goal: the per-target factor lookup backing
`TacticTableSet::animation_factor(anim, target)` (ADR-005 D5, a.a6.S5a probe). Produce
candidate C++; @re-verifier loop mandatory (max 3 rounds, GREEN required before wiring)."
**Verify**: if GREEN — `animation_factor()` wired + test B2 extended (RED→green); if not —
documented anchors only, probe stays 0.0f neutral (plan unaffected).
**Commit**: `feat(reverse): .atf record semantics + animation_factor probe (R2)` *(only if GREEN)*

---

### PHASE C — Decision pipeline + memory + trace (ADR P2) → GATE G1

**Entry**: GT passed. **Hard rule**: NO FSM-removal commit in this phase (ADR C4).
**Exit (GATE G1)**: `test_tactic_decision_trace` golden green AND regression line green.

#### Step P1 — TacticMemory state model (TDD)
**Files**: `engine/game/tactic_memory.hpp` (new, header-only), `tests/test_tactic_memory.cpp` (new), `tests/CMakeLists.txt`
**Task**:
1a. RED — test: ring respects depth (last N self/enemy actions); `tick()` advances
`frames_since_self/enemy`, decrements `frames_until_next_decision` and
`enemy_reaction_frames` to 0 (never negative); `record_decision` sets
`frames_until_next_decision = response_delay` roll (Min/Max inclusive); interval values feed
`TacticContext::self_interval/enemy_interval`.
1b. Implement `TacticMemory` exactly per ADR D8 (deques, strikes, 4 frame counters) +
`tick()` / `record_self(name)` / `record_enemy(name)` / `start_response_delay(min,max,rng)` /
`start_enemy_reaction(min,max,rng)`. Update/reset semantics `[HEURISTIC-TODO]` pending R5.
**Verify**: target builds; ctest `^test_tactic_memory$` green; regression line green.
**Commit**: `feat(ai): TacticMemory state model — rings, strikes, frame countdowns (ADR-005 D8)`

#### Step P2 — TacticDecisionPipeline: 7 stages in tracer order (TDD)
**Files**: `engine/game/tactic_pipeline.hpp/.cpp` (new), `tests/test_tactic_decision_trace.cpp` (skeleton, RED), `tests/CMakeLists.txt`
**Task**:
2a. `DecisionStage` enum with [ORIGINAL] per-stage tracer addresses `0x8F798090`..`0x8F79834C`;
`TacticDecision{stage, animation, wait_frames, type, distance_error, frame_error}`;
`DecisionTrace` recorder appending one line-group per stage + epilogue lines in tracer order
(DistanceError / FrameError / Intervals / EnemyIntervals / DecisionType / Decision {Wait=%d}).
2b. Seven stage free functions `stage_use_defense(...) -> std::optional<TacticDecision>` etc.,
signature `(const TacticDef&, const TacticContext&, TacticMemory&, const TacticTableSet&,
RngSource)`; `decide()` invokes in enum order, first hit wins, none → idle decision.
Stage internals = JS-port defaults, each carrying `[HEURISTIC-TODO]` with its R-number
(comparator R3 isolated in ONE function `chance_fires(curve, ctx, rng)`; QuickAttack/Evade
index mapping R6 isolated in ONE function each). Stage 1 gates on `tactic.use_defense` and
rolls the three sub-chances; stage 5/6 loop over `quick_attack_chances`/`evade_chances`
entries printing `QuickAttack[i]`/`Evade[i]`; stage 3 picks via `attack_table(...)` when
present else `animation_weights`; candidate picks reuse `TacticSettings::choose(..., rng)`.
2c. Epilogue: distance/frame jitter drawn from the Min/Max ranges; `ExpectedWait` type →
wait pick over `expected_wait` list, `wait_frames` mapping `[HEURISTIC-TODO]` (R4, default:
weight value as frames, clamped ≥ 0); other types never reach here (load rejects).
The D5 probe: before scoring a candidate, `ctx.animation_factor` =
tables.`animation_factor(candidate, ctx.current_animation)` (0.0f until R2) and weight-side
`animation_factor_entries` matching `ctx.current_animation` contribute per Key Decision 3.
**Verify**: target builds; skeleton test compiles and FAILS on order (RED); regression line green.
**Commit**: `feat(ai): TacticDecisionPipeline — 7 stages in tracer order (ADR-005 D1)`

#### Step P3 — Decision-trace golden contract test (GATE G1 artifact)
**Files**: `tests/test_tactic_decision_trace.cpp` (complete), `tests/golden/tactic_decision_trace.golden.txt` (new)
**Task**: scripted fight scenarios (fixed distance/health/bullet state; synthetic TacticDef
covering all stages; seeded LCG via RngSource): assert the `DecisionTrace` output equals the
golden **line-group-by-line-group in tracer order** — fails on any stage reorder/merge/skip;
scenario with 2 QuickAttack entries asserts `QuickAttack[0]` + `QuickAttack[1]` groups;
epilogue lines present with jitter within Min/Max bounds; Intervals/EnemyIntervals match
TacticMemory deltas; ExpectedWait scenario asserts a `Decision {Wait=…}` line (value unpinned
pending R4); two runs with the same seed produce byte-identical traces. Golden provenance
comment: tracer strings `0x8F798090`..`0x8F79834C`, methodology GOLDEN_TESTS.md §2.
**Verify**: ctest `^test_tactic_decision_trace$` green; regression line green. **G1 RECORDED.**
**Commit**: `test(ai): decision-trace golden — tracer-order contract, gate G1 (ADR C4)`

#### Step P4 — @reverser R3 + R4 (parallel, no build gate)
**Files**: `reverse/analysis/TABLE_FORMATS.md` (or new `DECISION_SEMANTICS.md`); pipeline
`[HEURISTIC-TODO]`→`[ORIGINAL]` promotions only on GREEN
**Prompt (dispatch-ready)**: "(R3) Reverse the stage chance comparator/normalisation in the
decision routine (tracer strings 0x8F798090.. anchor it): how a weight curve value becomes a
fire/no-fire draw. (R4) Reverse the ExpectedWait mapping from the picked `<Animation>` weight
to `Decision {Wait=%d}` frames. Candidates + @re-verifier, max 3 rounds each."
**Verify**: per GREEN verdict — one-function swap (`chance_fires` / wait mapping) + golden
re-pinned if values change; test stays green. Not GREEN → defaults remain flagged; G1 unaffected.
**Commit**: `feat(ai): binary-verified stage comparator + ExpectedWait mapping (R3/R4)` *(only on GREEN)*

#### Step P5 — @reverser R5 + R6 (parallel, no build gate)
**Files**: `reverse/analysis/DECISION_SEMANTICS.md` (append); `tactic_memory.hpp`,
`tactic_pipeline.cpp` promotions only on GREEN
**Prompt (dispatch-ready)**: "(R5) Find the Memory ring depth source (binary key `Memory`;
`<Memory Strikes RoundFactor/>` has no depth attr in this dump), the Strikes update/reset
points, and the Intervals/EnemyIntervals reset points. (R6) Reverse QuickAttack[i]/Evade[i]
indexing — how the i-th QuickAttack/Evade selects its table/animation. Candidates +
@re-verifier, max 3 rounds each."
**Verify**: on GREEN — confined edits (TacticMemory methods / the two index functions) +
tests re-pinned; suite green.
**Commit**: `feat(ai): binary-verified Memory/Strikes + QuickAttack/Evade indexing (R5/R6)` *(only on GREEN)*

---

### PHASE D — Adapter + wiring (ADR P3) → GATE G2

**Entry**: G1 passed (C4 satisfied — removal now *permitted*, not yet done).
**Exit (GATE G2)**: trace test green AND `test_full_battle`, `test_battle_integration`,
`test_trace_replay`, all HeadlessTestRunner tests green. Tag `gap4-g2`.

#### Step D1 — R7 re-verification (cheap, blocking)
**Files**: none (evidence into commit message)
**Task**: re-run the call-graph check (`update_enemy_ai` references across engine/ + tests/)
and confirm the planning-time finding still holds: zero callers of `Combat::update_enemy_ai`;
live enemy-AI = `game.cpp:1740-1826` via alias `game_clean.hpp:4184`. If the finding changed,
STOP and re-plan Phase D/E scope.
**Verify**: verdict recorded in commit message; full build green (no code change).
**Commit**: `chore(ai): re-verify enemy-AI call graph before wiring (R7: live=game.cpp, Combat copy dead)`

#### Step D2 — TacticDecisionAdapter (TDD)
**Files**: `engine/game/tactic_decision_adapter.hpp` (new, header-only), `tests/test_tactic_decision_adapter.cpp` (new), `tests/CMakeLists.txt`
**Task**:
2a. RED — test pins the ADR D7 mapping rows: attack animation → state 2 + animation name;
step/movement → 1/3 by sign; UseDefense fired → 4; `wait_frames > 0` or no stage fired → 0.
2b. Implement `TacticDecisionAdapter::to_legacy_state(const TacticDecision&) -> int` +
`animation_for(const TacticDecision&) -> std::string` ("" → idle anim fallback by caller).
Attack-vs-movement classification: candidate name lookup against the stage's table
(`attack_table` candidates = attacks; movements family = steps) with a `[HEURISTIC-TODO]`
name-list fallback (ForwardStep/BackStep/ShortAttack/Duck) until family data lands.
**Verify**: target builds; ctest `^test_tactic_decision_adapter$` green; regression line green.
**Commit**: `feat(ai): TacticDecisionAdapter — decision → legacy state mapping (ADR-005 D7)`

#### Step D3 — Wire the live enemy-AI block through the pipeline
**Files**: `engine/game/game.cpp` (L1740-1826; L1685 area for member init), `engine/game/game_clean.hpp` (member declarations near L4180), `engine/game/combat.hpp` (TacticMemory member)
**Task**:
3a. Add members: `TacticTableSet tactic_tables_` (loaded beside `tactics_.load(...)`),
`TacticMemory enemy_tactic_memory_` (in Combat beside enemy state), pipeline constructed
on demand (stateless; settings+tables+`std::rand` bound).
3b. In the L1744 decision branch: when `tactics_.loaded()` && tactic found && tables loadable
→ build the extended `TacticContext` from fight state (distance/health/hits as today; bullets/
anim_frames where available, else 0 = neutral), run `pipeline.decide(*td, ctx,
enemy_tactic_memory_, &trace)`, translate via the adapter, feed the F1 overlay stash
(`ai_last_candidates_`/`ai_last_weights_`/`ai_last_distance_`/`ai_last_pick_`) from the
`DecisionTrace` + adapter result (overlay keeps working — C5).
3c. DELETE the L1770-1792 hardcoded-candidate roulette and the L1793-1798 `dist>200→1:2`
fallback **on the loaded path only**; the `!loaded()` `[HEURISTIC-TODO]` fallback (L1793-1798
form) stays until Phase E. Keep `enemy_ai_decision_interval_` gating until D4.
3d. `Combat::update_enemy_ai` (dead): add a header comment "dead code — scheduled for
deletion in Phase E (R7-verified zero callers)". NOT wired (Key Decision 1).
**Verify**: full build; ctest `^test_tactic_decision_trace$` green; ctest
`^(test_full_battle|test_battle_integration|test_trace_replay)$` green.
**Commit**: `feat(ai): route live enemy AI through TacticDecisionPipeline via adapter (ADR-005 D7)`

#### Step D4 — ResponseDelay frame countdown replaces decision interval (loaded path)
**Files**: `engine/game/game.cpp` (L1740-1745), `engine/game/combat.hpp` (member), tests via D3 integration
**Task**: on the loaded path, pipeline re-entry is gated by
`enemy_tactic_memory_.frames_until_next_decision == 0` (tick per frame); after each decision,
`start_response_delay(tactic.response_delay.min, .max, rng)`. `enemy_ai_decision_interval_`
remains for the fallback path only (deleted in Phase E). EnemyResponseDelay countdown gates
stage-1/4 reaction rolls mid-window (`[HEURISTIC-TODO]` granularity pending R5).
**Verify**: full build; `^test_tactic_decision_trace$` + battle trio green.
**Commit**: `feat(ai): ResponseDelay frame countdown gates pipeline re-entry (ADR-005 D8)`

**GATE G2 CHECK (explicit, recorded in commit)**: ctest full suite green
(`ctest --test-dir build --output-on-failure`), incl. G1 test + battle trio + all
HeadlessTestRunner users → `git tag gap4-g2`. FSM removal now permitted (ADR C4+C5).
**Commit**: `test(ai): gate G2 — trace golden + full regression green, FSM removal permitted`

---

### PHASE E — FSM deletion (ADR P4 / "Phase B") → GATE GE

**Entry**: tag `gap4-g2` exists; D1 verdict re-confirmed.
**Exit (GATE GE)**: full ctest green + manual battle soak; GAP-4 marked DONE.

#### Step E1 — Delete dead Combat::update_enemy_ai
**Files**: `engine/game/combat.cpp` (L49-202), `engine/game/combat.hpp` (L145 decl)
**Task**: delete the function + decl (R7-verified zero callers). Nothing else.
**Verify**: full build; full ctest green.
**Commit**: `refactor(ai): delete dead Combat::update_enemy_ai (R7-verified zero callers)`

#### Step E2 — Execution consumes TacticDecision directly
**Files**: `engine/game/game.cpp` (L1800-1826 execute block; L1740-1799 decision block)
**Task**: the execute block switches off `enemy_ai_state_`: animation name from the decision
drives `enemy_anim_` (attack anims set attacking state + cooldown as today; wait → idle anim
for `wait_frames`); the adapter is bypassed (kept one more step for bisectability). F1
overlay readers (`game_clean.hpp:1576/1697`) rewired to the stored decision/trace.
**Verify**: full build; battle trio + trace test green.
**Commit**: `refactor(ai): execution consumes TacticDecision directly (ADR-005 Phase B)`

#### Step E3 — Remove enemy_ai_state_, adapter, interval, fallbacks
**Files**: `engine/game/combat.hpp` (L94, L191 members), `engine/game/game_clean.hpp` (L4184
alias; L4183 interval alias if dead), `engine/game/game.cpp` (L842/1990/2024 resets; fallback
branches), `engine/game/tactic_decision_adapter.hpp` (delete), `tests/test_tactic_decision_adapter.cpp` (delete + CMake block)
**Task**: delete the legacy state int + aliases + reset sites + the no-settings
`[HEURISTIC-TODO]` fallback (settings absent now ⇒ idle/wait decision, traced) + adapter +
`enemy_ai_decision_interval_` (if fully dead after E2). No settings loaded = neutral enemy,
no rand()%100 branches anywhere.
**Verify**: full build; full ctest green.
**Commit**: `refactor(ai): remove enemy_ai_state_ FSM, adapter, decision interval (gate G2 passed)`

#### Step E4 — Docs close-out + soak
**Files**: `reverse/analysis/PORT_GAPS.md` (GAP-4 → DONE note, L127/L205-206 priority list),
`reverse/analysis/GOLDEN_TESTS.md` (cross-ref the new golden), `.planning/adr/ADR-005-ai-tactic-model.md` (Status: Proposed → Accepted/Implemented)
**Task**: mark GAP-4 done with commit refs; note surviving `[HEURISTIC-TODO]`s (R1-R6 items
not yet GREEN) with their queue status. Manual battle soak: one human-played Dojo fight +
one map battle, enemy visibly approaches/attacks/blocks/waits; F1 overlay shows trace data.
**Verify**: full ctest green; soak notes in commit body.
**Commit**: `docs: GAP-4 done — AI tactic model ported (ADR-005 implemented)`

## Test Plan

| Step | Test type | File |
|---|---|---|
| A1-A3 | Unit (golden-style, seeded) | `tests/test_tactic_weights.cpp` (extended) |
| P1 | Unit | `tests/test_tactic_memory.cpp` |
| B2 | Unit + real-asset | `tests/test_tactic_tables.cpp` (1578-file dump) |
| P2+P3 | **Contract golden** (G1) | `tests/test_tactic_decision_trace.cpp` + `tests/golden/tactic_decision_trace.golden.txt` |
| D2 | Unit | `tests/test_tactic_decision_adapter.cpp` (deleted in E3) |
| D3-E3 | Regression (C5) | `test_full_battle`, `test_battle_integration`, `test_trace_replay`, HeadlessTestRunner users — green at every gate |
| E4 | Manual soak | Dojo + map battle, F1 overlay |

## Rollback Plan

- **Phase A**: pure additions, defaults neutral — `git revert` A1..A3; no behavior change existed.
- **Phase B**: new files + one extraction — revert B1..B2; `test_asset_loaders` proves `.atf` parity. B3/B4 are docs/gated edits — revert independently.
- **Phase C**: pipeline reachable only from tests — revert P1..P3; game behavior untouched. P4/P5 revert = restore `[HEURISTIC-TODO]` defaults.
- **Phase D**: adapter boundary — revert D4 then D3 (restores heuristic branch + interval gating), then D2. The no-settings fallback was never deleted in D, so revert restores prior behavior exactly.
- **Phase E** (point of no return was G2): rollback = `git revert` E1..E3 or reset to tag `gap4-g2`.
- @reverser steps never block rollback: every binary-verified change lands as an isolated one-function commit.

## Success Criteria

- [ ] G0: extended `test_tactic_weights` green (20 keys parsed; probe term; seeded RNG) + regression line green
- [ ] GT: `test_tactic_tables` green on the real 1578-file `.atf` dump; missing families absence-tolerant
- [ ] **G1: `test_tactic_decision_trace` golden green — fails on any stage reorder/merge/skip (lands before ANY FSM-removal commit)**
- [ ] G2: G1 + `test_full_battle` + `test_battle_integration` + `test_trace_replay` + HeadlessTestRunner tests green; tag `gap4-g2`
- [ ] GE: `enemy_ai_state_`, adapter, `enemy_ai_decision_interval_`, and all `rand()%100` AI branches deleted; full ctest green; battle soak done
- [ ] `ConditionalDesigionFactor` nowhere in code except the D6 extension-point comment
- [ ] Every `[HEURISTIC-TODO]` carries its R-number; R1/R2 (and any GREEN R3-R6) have TABLE_FORMATS.md / DECISION_SEMANTICS.md anchors
- [ ] Full `ctest --test-dir build --output-on-failure` exits 0 at every gate; each step built+tested individually (no batched gates)
