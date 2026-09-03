# ADR-005: AI Tactic Model (GAP-4) — Decision Pipeline, Table Families, FSM Replacement

**Status**: Implemented (2026-08-01 — gates G1/G2 passed; FSM/adapter removed in `f7a7c72`)
**Date**: 2026-07-31  
**Supersedes**: The heuristic enemy FSM (`enemy_ai_state_` distance-threshold model in `combat.cpp` / `game.cpp`)
**Residual (open, non-blocking)**: `ConditionalDesigionFactor` — extension point only (C1); v=7 `{name → ids}` map writer — claim (c), NEEDS_HUMAN (see risk table)

## Context

GAP-4 (`reverse/analysis/PORT_GAPS.md:127-206`, `PORT_PLAN.md:137-182`) replaces
reSF2's invented enemy AI with the original's weight/roulette tactic model.

### What the binary verifies

1. **Decision order — the verification contract** (tracer strings
   `0x8F798090`..`0x8F79834C`):
   ```
   UseDefense -> UseSafeAttack -> TableAttack -> DodgeMissiles ->
   QuickAttack[i] -> Evade[i] -> UseCautiousMovements
   then DistanceError / FrameError / Intervals / EnemyIntervals /
   DecisionType / Decision {Wait=%d}
   ```
2. **Weight-key schema confirmed, engine names correct**
   (`0x8F797574`..`0x8F797C58`): `AnimationWeights ExpectedWait Exponential
   AnimationFactors CurrentAnimation CounterFactor DamageFactor HealthFactor
   EnemyHealthFactor AnimationFramesFactor ChildFramesFactor MagicBulletFactor
   MissileBulletFactor HitFactor DistanceFactor AntiLimit FactorType`.
3. **Decision-level keys (20) the engine does not model**:
   `QuickAttackChance EvadeChance Memory Strikes RoundFactor UseDefense
   CounterAttackChance DodgeChance BlockChance UseSafeAttackChance
   TableAttackChance QuickAttacks Evades CautiousMovementsChance
   DodgeMissilesChance DodgeMagicChance DistanceError FrameError
   ResponseDelay EnemyResponseDelay`.
4. **Table families (7)**: `attack/*.tbs`, `shift/*.stb`, `shiftTables/*.sts`,
   `*.atf`, `dodge/`, `movements/`, `outcometablesforattack/`.
   **Table types (10)**: `RandomAnimation NoneTable AttackTable MovementsTable
   DodgeTable AttackTableOld SummaryResultTable QuickAttack ShiftTable
   ThrowTactics`. **Decision types (2)**: `Tabular`, `ExpectedWait`
   (`Strange tactic type: %s` rejects all others).

### Current engine state

- `engine/game/tactic_settings.hpp/.cpp` — `TacticWeight` (the `cc` score +
  Linear/Exponential curves, `Gb()` dot product), `TacticDef` (name, template,
  type, ordered `animation_weights`), `TacticSettings::choose()/choose_debug()`
  (the `jL` roulette). Correct but covers only the *weight* layer — none of the
  20 decision-level keys.
- `tactic_settings.cpp:43` — `[HEURISTIC-TODO]` admits the per-target
  **AnimationFactors probe (`a.a6.S5a`)** is omitted.
- The enemy decision model is an **invented FSM**: `enemy_ai_state_` ∈
  {idle, approach, attack, retreat, block} driven by distance thresholds and
  `rand()%100`, duplicated at `engine/game/combat.cpp:51-199`
  (`Combat::update_enemy_ai`) and `engine/game/game.cpp:1756-1822`.
  `game_clean.hpp:4184` reaches it via `combat_.mutable_enemy_ai_state()`.
  The existing roulette is wired in only as a *candidate picker over four
  hardcoded labels* (`ForwardStep/ShortAttack/BackStep/Duck`) — not the
  original staged pipeline.
- `engine/reverse/atf_tactics.hpp/.cpp` — working `.atf` parser (zlib blob →
  858-byte record + string pool). Record *semantics* unreversed; secondary
  records exposed as an unparsed `binary_records` span.

### Asset survey (this dump, 2026-07-31)

| family | status |
|---|---|
| `assets/tactics/*.atf` | **1578 files present**; zlib-compressed binary (`78 DA` magic), parsed by `atf_tactics` |
| `attack/*.tbs`, `shift/*.stb`, `shiftTables/*.sts` | **0 files, no subdirectories exist** |
| `dodge/`, `movements/`, `outcometablesforattack/` | **absent** |

Only the `.atf` family is loadable today. The loader architecture must treat a
missing family as a normal condition, and the possibility that some families
live inside `.atf` secondary records is an open `@reverser` question (Risks).

### Hard constraints (binding)

- **C1 — `ConditionalDesigionFactor`: 0 matches in the ARM string table.**
  Status: **BLOCKED pending binary evidence.** It must NOT be implemented from
  the JS-port name. This ADR leaves a documented extension point only (§D6).
- **C2** — The per-target `AnimationFactors` probe (`a.a6.S5a`) must be in the
  design (§D5).
- **C3** — Per-family loader architecture; no monolithic loader (§D3).
- **C4** — Verification keyed to the tracer decision order must land and pass
  **before** any FSM-removal commit (§D7, §Verification).
- **C5** — Regression guard: `test_full_battle`, `test_battle_integration`,
  `HeadlessTestRunner` users, and `test_trace_replay` stay green throughout.
- **C6** — Conventions: `[ORIGINAL]` binary-ref comments; format parsers in
  `engine/format/` (text) / `engine/reverse/` (binary RE containers, the
  existing `atf_tactics` precedent); game logic in `engine/game/`.

## Decision

### Architecture Overview

```
┌──────────────────────────── data layer (load once) ───────────────────────────┐
│ tacticSettings.xml      assets/tactics/*.atf   attack/ shift/ shiftTables/ …  │
│        │                       │                    (absent families → empty) │
│        ▼                       ▼                                               │
│ TacticSettings (extended)   per-family parsers:                                │
│  · TacticDef +20 keys       reverse::atf (exists)  reverse::tbs/stb/sts (@rev) │
│  · chance curves            ─────────────┬─────────────────────────────       │
│        │                                 ▼                                     │
│        │                        TacticTableSet (registry, engine/game)         │
│        │                        typed tables, family-indexed, absence-tolerant │
└────────┼─────────────────────────────────┬───────────────────────────────────┘
         ▼                                 ▼
┌──────────────────────── per decision (enemy AI tick) ────────────────────────┐
│ TacticDecisionPipeline  (engine/game, resf2::game)                            │
│   for each stage in TRACER ORDER:                                             │
│     1 UseDefense → 2 UseSafeAttack → 3 TableAttack → 4 DodgeMissiles →        │
│     5 QuickAttack[i] (i < QuickAttacks) → 6 Evade[i] (i < Evades) →           │
│     7 UseCautiousMovements                                                    │
│   stage fires? → pick table → jL roulette over candidates → TacticDecision    │
│   epilogue: DistanceError / FrameError jitter · Intervals/EnemyIntervals ·    │
│             DecisionType (Tabular | ExpectedWait) · Decision {Wait=%d}        │
│        │                                                                      │
│        ▼  adapter (transitional, §D7)                                         │
│ TacticDecisionAdapter → legacy enemy_ai_state_ int  ──► existing execution    │
│ TacticMemory (ring of last Memory actions, strikes, intervals, delays)        │
└───────────────────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

#### D1. Decision pipeline in tracer order

**Problem**: The original model is not an FSM and not a single roulette — it is
an ordered sequence of independent chance stages, each with its own curve and
table. Order is the one thing the binary *proves*.

**Solution**: One class, `TacticDecisionPipeline` (`engine/game/tactic_pipeline.hpp`),
whose `decide()` walks a fixed stage table. The stage order is a compile-time
constant mirroring the tracer strings, with `[ORIGINAL]` comments citing
`0x8F798090`..`0x8F79834C` per stage.

```cpp
// engine/game/tactic_pipeline.hpp
namespace resf2::game {

// One stage of the tracer decision order.
// [ORIGINAL] order fixed by tracer strings 0x8F798090..0x8F79834C.
enum class DecisionStage {
    kUseDefense,          // UseDefense / CounterAttackChance / DodgeChance / BlockChance
    kUseSafeAttack,       // UseSafeAttackChance
    kTableAttack,         // TableAttackChance -> attack table (.atf/.tbs)
    kDodgeMissiles,       // DodgeMissilesChance / DodgeMagicChance
    kQuickAttack,         // QuickAttackChance, repeated QuickAttacks times  (QuickAttack[i])
    kEvade,               // EvadeChance, repeated Evades times             (Evade[i])
    kUseCautiousMovements // CautiousMovementsChance -> movements table
};

struct TacticDecision {
    DecisionStage stage;            // which stage fired (kUseDefense if none?)
    std::string animation;          // chosen animation ("" = wait/idle)
    int wait_frames = 0;            // Decision {Wait=%d}; 0 for Tabular picks
    DecisionType type;              // Tabular | ExpectedWait (see D4)
    // Post-pick targeting jitter applied by the epilogue:
    float distance_error = 0;       // DistanceError
    int   frame_error   = 0;        // FrameError
};

class TacticDecisionPipeline {
public:
    TacticDecisionPipeline(const TacticSettings& settings,
                           const TacticTableSet& tables,
                           RngSource rng);          // D4 — injectable RNG

    // Runs the stages in tracer order. Writes one trace line group per stage
    // into `trace` (the golden-test hook, §Verification).
    [[nodiscard]] TacticDecision decide(const TacticDef& tactic,
                                        const TacticContext& ctx,
                                        TacticMemory& memory,
                                        DecisionTrace* trace) const;
};

} // namespace resf2::game
```

Each stage is a small free function (`stage_use_defense(...)`, …) taking
`(tactic, ctx, memory, tables, rng)` and returning `std::optional<TacticDecision>`;
`decide()` invokes them in the enum order and takes the first hit. Stage
internals are JS-port-informed defaults and each carries its own
`[HEURISTIC-TODO]` where the binary has not yet confirmed semantics — but the
**order is not negotiable**: changing it breaks the golden trace test.

**Alternatives**: a single generic "list of {chance, table}" stage vector
rejected — the stages are *not* homogeneous (Defense has three sub-chances,
QuickAttack/Evade loop, DodgeMissiles consults bullet state); the typed stage
functions keep each stage's quirks explicit.

#### D2. Data model — `TacticDef` extension for the 20 decision-level keys

**Problem**: `TacticDef` today holds only `animation_weights`.

**Solution**: extend `TacticDef` in place (same file, same template-resolution
pass). Chance keys are **full `TacticWeight` curves**, not scalars — the header
already documents this (`<Animation>` / `<...Chance>` elements share class `cc`).

```cpp
// engine/game/tactic_settings.hpp — additions to TacticDef
struct TacticDef {
    // ... existing fields (name, template_name, type, animation_weights) ...

    // --- chance curves (parsed via the existing parse_weight()) ---
    TacticWeight quick_attack_chance;
    TacticWeight evade_chance;
    TacticWeight counter_attack_chance;
    TacticWeight dodge_chance;
    TacticWeight block_chance;
    TacticWeight use_safe_attack_chance;
    TacticWeight table_attack_chance;
    TacticWeight cautious_movements_chance;
    TacticWeight dodge_missiles_chance;
    TacticWeight dodge_magic_chance;
    TacticWeight expected_wait;        // ExpectedWait — D4 (ExpectedWait type)

    // --- scalar decision keys ---
    int   use_defense = 0;             // UseDefense      (stage-1 gate)
    int   quick_attacks = 0;           // QuickAttacks    (stage-5 repeat count)
    int   evades = 0;                  // Evades          (stage-6 repeat count)
    int   memory = 0;                  // Memory          (ring depth, D8)
    int   strikes = 0;                 // Strikes         (D8)
    float round_factor = 0;            // RoundFactor     (score context, D8)
    float distance_error = 0;          // DistanceError   (epilogue jitter)
    float frame_error = 0;             // FrameError      (epilogue jitter)
    int   response_delay = 0;          // ResponseDelay   (frames between decisions)
    int   enemy_response_delay = 0;    // EnemyResponseDelay (reaction delay, D8)
};
```

`TacticContext` gains the fields the new terms need (populated by the caller
from `TacticMemory` + fight state — see D5, D8):

```cpp
struct TacticContext {
    // ... existing 11 fields ...
    float animation_factor = 0;   // per-target probe result (a.a6.S5a) — D5
    float strikes = 0;            // from TacticMemory — D8
    float round_factor = 0;       // tactic's RoundFactor, mirrored for scoring
    float self_interval = 0;      // frames since own last action (Intervals)
    float enemy_interval = 0;     // frames since enemy's last action (EnemyIntervals)
};
```

Template inheritance (`resolve_templates()`) extends to the new keys with the
existing rule: local wins, else inherit.

#### D3. Table loader family design — per-family parsers behind one registry

**Problem**: Seven families, different formats, most absent from this asset
dump. A monolithic loader would couple the working `.atf` path to unreversed
formats and force stubs everywhere.

**Solution**:

- **One parser per family**, placed by format kind (C6):
  - `engine/reverse/atf_tactics.*` — **exists**; extended during execution to
    interpret `binary_records` (secondary records) — `@reverser` gate.
  - `engine/reverse/tbs_tables.*`, `stb_tables.*`, `sts_tables.*` — new, but
    **format unreversed and assets absent**: they land as interface + stub
    returning "family unavailable", fleshed out only after `@reverser`
    determines whether `.tbs/.stb/.sts` are zlib blobs or XML (Risks, R1).
    If any prove to be XML, that parser lives in `engine/format/` instead.
  - `dodge/`, `movements/`, `outcometablesforattack/` — directory families;
    loader scans the dir if present, empty set otherwise.
- **Shared zlib inflate helper** extracted from `atf_tactics.cpp` into
  `engine/reverse/zlib_blob.hpp` (4 known zlib users: atf/tbs/stb/sts —
  satisfies the 3+ rule for extraction).
- **One typed table model + one registry** in game logic:

```cpp
// engine/game/tactic_tables.hpp
namespace resf2::game {

// [ORIGINAL] the 10 table types from the binary's type strings
// (PORT_GAPS.md:166-167). "Strange tactic type: %s" rejects others.
enum class TacticTableType {
    kRandomAnimation, kNoneTable, kAttackTable, kMovementsTable, kDodgeTable,
    kAttackTableOld, kSummaryResultTable, kQuickAttack, kShiftTable,
    kThrowTactics
};

struct TacticTable {
    TacticTableType type;
    std::string name;
    // Ordered candidate animations + per-candidate data rows.
    // Payload interpretation is family-specific (stride-858 record for .atf);
    // kept as bytes+names until the family's semantics are reversed.
    std::vector<std::string> candidates;
    std::vector<std::uint8_t> record;   // raw row data (may be empty)
};

class TacticTableSet {
public:
    // Loads every family that exists under <root>/tactics/. A missing dir or
    // an unimplemented family parser is NOT an error — the set stays partial.
    bool load(const std::string& asset_root);

    [[nodiscard]] const TacticTable* attack_table(
        std::string_view weapon_a, std::string_view weapon_b) const;  // .atf: "Axes_Fists"
    [[nodiscard]] const TacticTable* find(TacticTableType type,
                                          std::string_view name) const;
    [[nodiscard]] bool has_family(TacticFamily f) const;

private:
    std::vector<TacticTable> tables_;   // family-tagged, name-indexed
    std::bitset<7> families_loaded_;
};

} // namespace resf2::game
```

`TacticTableSet::load` iterates a **family descriptor table** (path, parser
function, family tag) — adding a family = one row + one parser file, never
editing a shared loader body (C3).

#### D4. Roulette/weight evaluation; `Tabular` vs `ExpectedWait`

**Chance evaluation**: every stage evaluates its `TacticWeight` chance curve
against the extended `TacticContext` and draws once against the RNG. The
JS-port comparison semantics (normalisation of the weight to a percentage) are
the default; the exact comparator is a named open point (Risks, R4) with the
JS port as `[HEURISTIC-TODO]` default until the binary's stage is reversed.

**Candidate selection** stays on the existing, verified `TacticSettings::choose()`
(`jL` roulette) — unchanged, reused per stage over the stage's candidate list
(from the stage's `TacticTable` or from `animation_weights`).

**Decision types** (`TacticDef::type`, extended):

| type | behaviour |
|---|---|
| `Tabular` (default) | stages produce candidate sets; pick = `jL` roulette; `wait_frames = 0` |
| `ExpectedWait` | the pick may resolve to *wait*: `wait_frames` derived from the `expected_wait` weight curve — mirrors tracer `Decision {Wait=%d}`. Exact curve→frames mapping: `[HEURISTIC-TODO]` default from JS port, `@reverser` verify (R5) |
| anything else | reject with `Strange tactic type: %s` on load ([ORIGINAL] string), tactic skipped |

**RNG**: `RngSource = std::function<unsigned()>`; production binds `std::rand`
(preserves `jL` parity with the existing code), tests bind a seeded LCG — this
is what makes the golden trace test deterministic without touching global RNG
state used by other tests (C5).

#### D5. `AnimationFactors` per-target probe (`a.a6.S5a`) — required by C2

`TacticWeight` gains the attribute `animation_factors` (parses `AnimationFactors`,
present in the binary key list). `TacticWeight::score()` gains the term:

```cpp
a += ctx.animation_factor * animation_factors;   // a.a6.S5a probe * AnimationFactors
```

The **probe**: when the pipeline scores a candidate animation, it queries the
loaded tables for the per-target factor of that (animation, target) pair —
`TacticTableSet::animation_factor(anim, target)` (backed by the `.atf` record
rows; exact row semantics `@reverser`-gated, R2). Absent table → probe returns
`0.0f`, which is neutral (mirrors PORT_PLAN's "a miss yields 0.0f and is
neutral, never an error" convention for lookups). The existing
`[HEURISTIC-TODO]` at `tactic_settings.cpp:43` is resolved by this term.

#### D6. `ConditionalDesigionFactor` — BLOCKED, extension point only (C1)

Zero ARM string-table matches ⇒ treated as **absent in this build**. Nothing is
parsed, scored, or named after the JS identifier. The single documented
extension point: the term list in `TacticWeight::score()` ends with

```cpp
// [EXTENSION POINT] ConditionalDesigionFactor — BLOCKED pending binary
// evidence (0 matches in ARM string table, PORT_GAPS.md:145-148). If a
// future @reverser pass finds the real key name, add its term HERE, after
// `shift`, and document the string-table address. Do NOT add it from the
// JS-port name alone.
```

If binary evidence ever appears, adding the term is a one-line change in
`score()` plus one attribute in `parse_weight()` — no structural change.

#### D7. FSM replacement — strangler via adapter, verify-before-remove (C4)

**Chosen: two-phase strangler.** A direct swap is rejected because C4 requires
the verification contract to pass *before* removal, and C5 requires the
battle tests green at every commit.

**Phase A — adapter (FSM stays as execution detail):**
`TacticDecisionAdapter` maps a `TacticDecision` onto the legacy
`enemy_ai_state_` int, so the existing movement/attack execution code in
`combat.cpp` / `game.cpp` runs unchanged:

| decision | legacy state |
|---|---|
| stage fired, animation is an attack | 2 (attack) + real animation name |
| stage fired, animation is a step/movement | 1/3 (approach/retreat by sign) |
| UseDefense fired | 4 (block) |
| `wait_frames > 0` or no stage fired | 0 (idle) |

Both integration points (`Combat::update_enemy_ai`, `game.cpp:1756-1822`) call
the pipeline through the adapter; the `rand()%100` distance-threshold branches
are deleted in Phase A only from the path where a tactic + tables are loaded
(the `[HEURISTIC-TODO]` no-settings fallback remains until Phase B).
The distance-threshold heuristic is *inside* the deleted branch — decisions now
come from curves, and the adapter only translates for the execution layer.

**Phase B — removal (only after verification gate G2):** `enemy_ai_state_` and
the adapter are deleted; execution consumes `TacticDecision` (animation name +
wait) directly. The planner must confirm whether `game.cpp`'s copy is live or
legacy before Phase B (`game_clean.hpp:4184` routes through `Combat`, so
`game.cpp:1756-1822` may be dead code — flagged, not assumed).

#### D8. `Memory` / `Strikes` / `ResponseDelay` state model

```cpp
// engine/game/tactic_memory.hpp
struct TacticMemory {
    // Ring of the last `Memory` self actions, for interval computation and
    // (future) pattern terms. Depth comes from TacticDef::memory.
    std::deque<std::string> self_actions;
    std::deque<std::string> enemy_actions;
    int strikes = 0;                 // Strikes counter [semantics: R6]
    int frames_since_self = 0;       // -> Intervals
    int frames_since_enemy = 0;      // -> EnemyIntervals
    int frames_until_next_decision = 0; // ResponseDelay countdown
    int enemy_reaction_frames = 0;      // EnemyResponseDelay countdown
};
```

- **ResponseDelay** replaces the invented `enemy_ai_decision_interval_` float:
  after each decision, `frames_until_next_decision = tactic.response_delay`;
  the pipeline is not re-entered until it elapses. Frame-based, matching the
  original's integer frame domain (GAP-1).
- **EnemyResponseDelay** gates *reactions* to fresh player actions (stage-1/4
  rolls triggered mid-decision-window).
- **Intervals / EnemyIntervals** are printed in the tracer epilogue and feed
  `TacticContext::self_interval / enemy_interval`.
- Exact update/reset points of `Strikes` and `Memory` consumption in scoring
  are `[HEURISTIC-TODO]` from the JS port, `@reverser`-verified (R6).

### Implementation Plan (phases with gates)

| phase | content | gate |
|---|---|---|
| P0 | `TacticDef`/`TacticContext`/`TacticWeight` extension (D2, D5, D6 comment); `RngSource` injection into `choose_debug` | existing unit tests green |
| P1 | `TacticTableSet` + family descriptor table + zlib helper extraction; `.atf` family wired via existing parser | per-family loader test on real `.atf` files green |
| P2 | `TacticDecisionPipeline` stages + `TacticMemory` + `DecisionTrace` recorder | **G1: `test_tactic_decision_trace` golden order test green** (§Verification) |
| P3 | `TacticDecisionAdapter`; wire `Combat::update_enemy_ai` + `game.cpp` through it; delete heuristic branches on the loaded path | **G2: C4 met — trace test green AND C5 regression suite green — FSM removal now permitted** |
| P4 | Phase B removal of `enemy_ai_state_`/adapter; execution consumes `TacticDecision` directly | full suite green; manual battle soak |

`@reverser` tasks (R1, R2, R5, R6) run in parallel with P1–P3; any of them can
promote a `[HEURISTIC-TODO]` to `[ORIGINAL]` without structural change.

## Trade-offs

| Benefit | Cost |
|---|---|
| Stage order is binary-verified and locked by a golden test | Stage *internals* remain partly JS-port defaults until `@reverser` lands |
| Existing verified pieces (`TacticWeight`, `jL` roulette, `atf` parser) are reused unchanged | Pipeline + adapter + registry is ~4 new files of new code |
| Missing families are a normal state (0 of 6 table dirs exist in this dump) | Table model carries raw `record` bytes — typed access waits on reversal |
| Strangler keeps C5 tests green at every commit | Legacy `enemy_ai_state_` survives until Phase B; two models coexist during A |
| Injectable RNG gives deterministic goldens without perturbing other tests | Small indirection in the draw path |

## Alternatives Considered

- **Option A — Direct swap of FSM for pipeline**: rejected — violates C4 (no
  verification can precede removal) and endangers C5.
- **Option B — Keep FSM, tune thresholds**: rejected — PORT_GAPS is explicit:
  "reSF2's AIEngine FSM … is not this model at all"; tuning cannot reach 1:1.
- **Option C — Monolithic `TacticLoader` for all families**: rejected (C3) —
  couples the working `.atf` path to unreversed formats; family addition by
  surgery instead of by extension.
- **Option D — Model stages as data (generic {chance, table} list from XML)**:
  rejected — stages are heterogeneous (sub-chances, loops, bullet-state
  queries); data-driven generality would hide the tracer order that C4 verifies.
- **Option E — Global `std::rand` everywhere (status quo)**: rejected for tests —
  golden trace needs a seeded stream; binding `std::rand` in production keeps
  parity, so injection costs nothing behaviourally.

## Consequences

### What becomes easier
- Verifying AI against the original: the trace recorder prints in tracer order
  by construction; a future live capture (GOLDEN_TESTS.md §2) drops in as data.
- Adding a table family: one parser file + one registry row.
- Removing the FSM: gated, mechanical, and reversible (adapter boundary).
- The F1 debug overlay: `DecisionTrace` is exactly the data it needs.

### What becomes harder
- Two AI models coexist during Phase A (adapter boundary must be kept clean).
- `.atf` row semantics, `.tbs/.stb/.sts` formats, stage comparators,
  ExpectedWait mapping, and Memory/Strikes semantics all remain to be reversed;
  until then parts of the model are calibrated JS-port defaults, flagged
  `[HEURISTIC-TODO]`.

### Risks — what is unknowable without more binary work (`@reverser` queue)

| # | unknown | impact if wrong | mitigation in design | status (2026-08-01) |
|---|---|---|---|---|
| R1 | `.tbs/.stb/.sts` format (zlib blob vs XML) and whether missing families are packed in `.atf` secondary records | those families stay unavailable | family interface + stub; no structural cost either way | **done** — families absent from this dump; stubs shipped as designed |
| R2 | `.atf` stride-858 row semantics (the AnimationFactors probe source) | probe returns 0 → term neutral | neutral-by-zero convention; pipeline correct regardless | **GREEN** — ATF_RECORD_858.md |
| R3 | exact stage chance comparator/normalisation | stage fire rates off | isolated in one comparator function; golden order test unaffected | **GREEN** — binary-verified (VERIFY_R34.md, `d3050c1`) |
| R4 | ExpectedWait curve→frames mapping | Wait counts off | isolated in stage epilogue; tracer `Wait=%d` is the future golden | **GREEN** — gate/wait mapping binary-verified (VERIFY_R34.md, `d3050c1`) |
| R5 | Memory/Strikes/Intervals update & reset points | scoring context drift | all mutation confined to `TacticMemory` | **GREEN** — no ring in binary; `<Memory>` = Strikes/RoundFactor only (VERIFY_R56.md, MEMORY_INDEXING_R56.md) |
| R6 | QuickAttack[i]/Evade[i] table indexing | wrong sub-table chosen | stage functions isolated; index mapping is one function | **GREEN** — index = XML document order → animation name (VERIFY_R56.md) |
| R7 | whether `game.cpp:1756-1822` is live or legacy duplicate | Phase B touches dead code or misses a live path | planner must resolve before P4 | **resolved** — zero callers; dead copy deleted (`d75dcad`) |

**Open residual — v=7 claim (c)**: the `{name → ids}` map @ `0x8F86F258` is
**populated at runtime in this dump (983 entries)** with no statically
discoverable writer — **NEEDS_HUMAN** (VERIFY_R56.md:204-226). Non-blocking:
the engine's D(+0x08)/H(+0x14) probe channels stay neutral (probe returns 0),
so the AnimationFactors term is inert-by-evidence, matching the port. Suggested
close: live-debugger trace watching `0x8F86F258` for writes.

## Verification Strategy (mapped to the tracer decision order)

The contract is the **order and presence of the tracer lines**, replicated
verbatim as a print-format golden:

| tracer line (binary) | verified by |
|---|---|
| `UseDefense` … `UseCautiousMovements` (stage order) | **`tests/test_tactic_decision_trace.cpp`** — scripted fight scenarios (fixed distance/health/bullet state, seeded RNG) run through the pipeline; the `DecisionTrace` output is asserted line-group-by-line-group against a golden file whose order is the tracer's. This test **fails on any stage reorder, merge, or skip**. Lands in P2 = gate G1, **before** Phase-B FSM removal (C4). |
| `QuickAttack[i]` / `Evade[i]` indexing | same test: scenario with `QuickAttacks=2` asserts `QuickAttack[0]`, `QuickAttack[1]` groups appear |
| `DistanceError` / `FrameError` | same test: epilogue lines present; jitter applied within configured bounds |
| `Intervals` / `EnemyIntervals` | same test + `TacticMemory` unit test (frame deltas) |
| `DecisionType` | load test: `Tabular`/`ExpectedWait` accepted, `Strange tactic type: %s` rejects others |
| `Decision {Wait=%d}` | ExpectedWait scenario asserts a Wait line with frames ≥ 0 (exact values pending R4) |
| stage chances & curves | unit tests per `TacticWeight` (existing pattern) with seeded RNG |
| family loaders | per-family tests: real `.atf` files round-trip; stub families report unavailable without error |
| live capture (future) | `reverse/frida_hooks/` capture of the real tracer per GOLDEN_TESTS.md §2 → upgrades the order-golden to a value-golden without test-structure changes |

**Regression guard (C5)**: `test_full_battle`, `test_battle_integration`,
`test_trace_replay`, and all `HeadlessTestRunner`-based tests must be green at
every phase gate (G1, G2) and at merge. The adapter exists precisely so these
tests never see a behavioural cliff.

## Migration Plan

1. P0/P1 land behind the existing `tactic_settings_->loaded()` path — no
   behavioural change when tables are absent (current dump ⇒ `.atf` only).
2. P2/P3 activate the pipeline through the adapter; heuristic FSM branch is
   deleted only where tactic data is loaded.
3. **G2 is the point of no return**: trace test + full regression green ⇒
   Phase-B removal commit may be made. No FSM-removal commit may precede G2 (C4).
4. P4 removes `enemy_ai_state_`, the adapter, and the legacy branches.
5. Rollback at any point = revert to the phase boundary; the adapter keeps
   phases independently shippable.

## References

- Spec: `reverse/analysis/PORT_GAPS.md:127-206`; `reverse/analysis/PORT_PLAN.md:137-182`
- Tracer order strings: `0x8F798090`..`0x8F79834C`; key schema: `0x8F797574`..`0x8F797C58`
- Golden-test methodology: `reverse/analysis/GOLDEN_TESTS.md` (§2 AI decisions)
- Current weight model: `engine/game/tactic_settings.hpp/.cpp` (`cc`/`Gb`/`jL`, JS refs :19910/:19930/:20044-20122)
- Current FSM: `engine/game/combat.cpp:51-199`; `engine/game/game.cpp:1756-1822`; `engine/game/game_clean.hpp:4184`
- `.atf` parser: `engine/reverse/atf_tactics.hpp/.cpp` (stride 858)
- Regression tests: `tests/integration/test_full_battle.cpp`, `test_battle_integration.cpp`, `test_trace_replay.cpp`, `tests/headless_test_runner.hpp`
- Block-model precursor: `reverse/analysis/BLOCK_LOGIC.md`
