# 1:1 port gaps — verified against the live ARM binary (2026-07-30)

Every item here is backed by bytes in `game_region_runtime.bin` or by a live
Frida capture, not by inference from the JS port. Addresses are `game+off`
(add the runtime region base; it moves with ASLR).

---

## GAP-1 — Frame timing is integer milliseconds, not float 1/60  [PORTED 2026-07-30]

**Fixed in `engine/runtime/loop.cpp`.** The real runtime loop was worse than
first described: it was *variable-step* (`dt = now - last_ms`, clamped to
200 ms) with no frame limiter at all, so gameplay speed tracked the host frame
rate. It now steps by a fixed integer 16 ms and sleeps the remainder in the
inner spin loop. Verified by `tests/test_frame_timing_golden.cpp` (23 checks)
and by Ghidra's decompilation of `FUN_8f0bb400`.

The legacy helper below is still unported; it is a separate, less-used class.

**reSF2 legacy helper:** `engine/core/game_loop.hpp`
```cpp
GameLoop(float fixed_dt = 1.0f / 60.0f)   // 0.016666667
accumulator_ += real_dt;                  // float accumulator
```

**Original:** `game+0x64400`, frame interval read from `this+0x08` as a
**64-bit integer number of milliseconds**, captured live = **16**.

Consequences the current code gets wrong:

1. `1000 / 60 = 16.67` truncated to `16` ms, so the original's cap is
   **62.5 fps**, not 60. Over a 90-frame combo timer (`<Combo Time="90"/>`)
   that is a drift of ~1.5 frames.
2. The wait is an **inner spin loop** (`+0x644C0`), re-reading both the clock
   and `this+0x08` each pass — it does not carry a fractional remainder into
   the next frame the way an accumulator does.
3. The step function `game+0x64230` receives **integer milliseconds** and
   divides by a literal `1000.0` (double, verified at `0x8F0BB2B0`), i.e.
   `dt` is quantised to 1 ms steps: only `0.016`, `0.017`, … ever occur.
   A float accumulator produces values the original can never produce.

`internalSettings.xml` says `<FrameRate Value="60"/>`; that is the *nominal*
rate. The loop's real period is the truncated 16 ms. Both facts must be kept.

**Fix:** integer-ms timestep, `dt = ms / 1000.0` as **double**, and the
two-stage wait. Do not carry a float accumulator.

---

## GAP-2 — Step delta is double, and three more values ride with it  [CONFIRMED]

`game+0x64230` (called once per frame from the loop):

```
vmov s13, r1 ; s14, r2 ; s15, r3      ; three ints
vcvt.f64.f32 d18, s13                 ; -> double
vldr d8, [pc, #0x50]                  ; 1000.0
vdiv.f64 d18, d18, d8                 ; /1000
vstr d18, [r4, #0x08]                 ; this+0x08
vdiv.f64 d16, d16, d8   -> [r4, #0x18]
vdiv.f64 d17, d17, d8   -> [r4, #0x10]
bl  game+0x6DD264                      ; another 64-bit time source
vdiv.f64 d8, d16, d8    -> [r4, #0x20]
then vtable[0]() on the object in r5
```

So the per-frame state object holds **four doubles** at `+0x08`, `+0x10`,
`+0x18`, `+0x20`, all seconds. reSF2 passes a single `float dt`. The extra
three (accelerometer X/Y/Z are fetched right before the call in the loop) are
dropped entirely.

---

## GAP-3 — Damage formula: attribute system missing  [SCHEMA CONFIRMED]

**reSF2 now:** `engine/game/game.cpp` ~1840 and ~3641
```cpp
float attribute_multiplier = 1.0f + dmg_settings.damage_factor_base * 0.0f;  // HEURISTIC
float attack_factor        = 1.0f;   // HEURISTIC
float factor_set_multiplier = 1.0f;  // HEURISTIC
```
Three of six terms are stubbed at 1.0, so damage cannot match.

**Original evaluation order**, read off the built-in tracer's format strings
(`0x8F7999FC`..`0x8F799DCC`, function `game+0x438530`):

```
Hit / Attack / Attack Interval ID
BaseDamage:                %.4f
Critical:                  %s / %.4f
BlockDamageFactor:         %d
Block:                     %s / %.4f
DamageFactor:              %d / %.4f
DamageAttribute:           %s / %d    Shift: %.4f
DefenseAttribute:          %s / %d
TargetAttributeDifference: %.4f
Delta.Factor:              %.4f    Shift: %.4f
AttributeDifference:       %.4f / %.4f
HitDamage:                 %.3f (%.3f)
StyleValueAdd:             %.3f
StyleValue:                %.3f
Style:                     %s
```

Two whole stages are absent from reSF2: the **attribute difference** stage
(attacker's DamageAttribute vs defender's DefenseAttribute, with per-attribute
`Shift`, a `Delta.Factor`+`Shift`, and a `TargetAttributeDifference`) and the
**style** stage (`StyleValueAdd` -> `StyleValue` -> named `Style`).

Real values from `assets/internalSettings.xml`:

| key | value | note |
|---|---|---|
| `AverageBaseDamage` | `0.1` | top-level; the one inside `<RatingEvaluation>` is for *rating*, not combat — do not confuse them |
| `AlignTargetAttributes` | WeaponDamage 12, UnarmedDamage 0, BodyDefense 12, HeadDefense 5, RangedDamage 12, MagicDamage 12, EnchantmentResistance 12 | the attribute baseline reSF2 lacks |
| `ModifiedAlignFormula` | RangedDamage: DamageMultiplier 1.4, NetDamage 0.3, MinAttributeDifference 0 | |
| `StyleLevels` | StylePerHit 0.5, DecreaseSpeed 0.08, Penalty 2 | styles: Turtle/Hard/Brutal/Agressive/Crazy/Fantastic |
| `CounterPunches` | 50 | |
| `Combo` | MinHits 3, Time 90 | |
| `Great` | MaxHealth 0.1 | |
| `HitEffects` | CriticalHit: pause 60, effect 90, ampX 5, freqX 1.1, ampY 15, freqY 1; HeadHit: 0/60/3/1.1/9/1; Shock: 30/60/4/1.1/9/1 | reSF2 has no hit-effect timing |
| `Shock` | Treshold 999, FrameReduction 0.001, LooseningDelay 12 frames, Impulse Y -0.5, CriticalHitChance Base 0.0001, HeadHitChance Base 0.0001 | |
| `DifficultyEvaluation` | diff0 0, diff1 0.82, diff2 1.3, diff3 2.6, diff4 5.2 | rating-ratio thresholds |

---

## GAP-4 — AI tactic model: two terms omitted  [SCHEMA CONFIRMED]

`engine/game/tactic_settings.cpp` says:
```
// [HEURISTIC-TODO] The per-target AnimationFactors probe (a.a6.S5a) and
// the ConditionalDesigionFactor term ... are omitted here
```

The binary's tactic key list (`0x8F797574`..`0x8F797C58`) confirms the schema
and shows the engine's names are right, with these present:

```
AnimationWeights  ExpectedWait  Exponential  AnimationFactors  CurrentAnimation
CounterFactor  DamageFactor  HealthFactor  EnemyHealthFactor
AnimationFramesFactor  ChildFramesFactor  MagicBulletFactor
MissileBulletFactor  HitFactor  DistanceFactor  AntiLimit  FactorType
```

Note: **`ConditionalDesigionFactor` does not appear in the ARM string table**
(0 matches) although the engine's header lists it from the JS port. Either it
is absent in this build or the name differs — do not implement it from the JS
name alone.

Decision-level keys reSF2 does not model at all:
```
QuickAttackChance  EvadeChance  Memory  Strikes  RoundFactor  UseDefense
CounterAttackChance  DodgeChance  BlockChance  UseSafeAttackChance
TableAttackChance  QuickAttacks  Evades  CautiousMovementsChance
DodgeMissilesChance  DodgeMagicChance  DistanceError  FrameError
ResponseDelay  EnemyResponseDelay
```

And the tactic-table families, all loaded from `assets/tactics/*`:
```
assets/tactics/attack/*.tbs        assets/tactics/movements/
assets/tactics/shift/*.stb         assets/tactics/outcometablesforattack/
assets/tactics/dodge/              assets/tactics/shiftTables/*.sts
assets/tactics/*.atf
```
Table types: `RandomAnimation NoneTable AttackTable MovementsTable DodgeTable
AttackTableOld SummaryResultTable QuickAttack ShiftTable ThrowTactics`.
Decision types: `Tabular`, `ExpectedWait` (`Strange tactic type: %s` rejects
others).

The AI debug format also fixes the **decision order**
(`0x8F798090`..`0x8F79834C`):
```
UseDefense -> UseSafeAttack -> TableAttack -> DodgeMissiles ->
QuickAttack[i] -> Evade[i] -> UseCautiousMovements
then DistanceError / FrameError / Intervals / EnemyIntervals /
DecisionType / Decision {Wait=%d}
```
reSF2's `AIEngine` FSM (Idle->Approach->Attack->Retreat->Block) is not this
model at all.

---

## GAP-5 — Skeleton/geometry nodes  [CONFIRMED]

Node and figure keys present in the binary that the engine's collision code
does not use: `Edge`, `Radius1`, `Radius2`, `Margin1`, `Margin2`, `Figures`,
`Capsule`, `ModelEdge`, `NPivot`, plus `ModelMacroNode` / `MacroNodeTmpData`.
`heel %s not found in shift table for %s` and `big frame %d` show the shift
tables are keyed by heel node per animation frame.

`engine/game/game_clean.hpp` has open `[HEURISTIC-TODO]`s on exactly this
(root-motion consumption at :1715, framing at :708). The capsule/edge radii
and margins are the missing piece.

---

## Priority for a 1:1 port

1. ~~**GAP-1**~~ — **DONE**: `engine/runtime/loop.cpp` now uses the original's
   fixed integer timestep. `engine/core/game_loop.hpp` (legacy) still differs.
2. **GAP-2** — pass the four per-frame doubles instead of one float.
3. **GAP-3** — **formula now fully recovered**, see the section at the end of
   this file. Implementing it needs the attribute system.
3. **GAP-4** — replace the FSM with the weight/roulette model and load the
   `.tbs`/`.stb`/`.sts`/`.atf` tables.
4. **GAP-5** — capsule/edge collision geometry.

---

# GAP-3 RESOLVED — the real damage formula (Ghidra, 2026-07-30)

`Model::getTotalDamage` = **`game+0x4527B4`**, found via the single xref to
`"Model::getTotalDamage - wtf so strong"` (`0x8F79A2A0`) and decompiled with
Ghidra against the relocated dump loaded at base `0x8F057000`.

## Verified formula

```
getTotalDamage(self, hit, is_ranged, weapon, ctx) -> float
    enemy = self[0x1E4]

    base = powf(2.0, baseAttr * baseWeight)      # weight from game+0x60674C
    f1   = powf(2.0, attr1 * w1)                 # game+0x4A94F0(self, is_ranged)
    f2   = powf(2.0, attr2 * w2)                 # game+0x4A95A8(enemy, weapon)
    f3   = game+0x60E794(...)                    # defense / attribute difference
    add  = hit[0x48] + enemy[0x774]

    dmg  = base * f2 * f1 * f3 * add
    dmg  = max(dmg, 0.0)

    crit = game+0x42A8A8(hit)[1]
    dmg  = dmg * crit * enemy[0x678] * enemy[0x6AC]

    assert 0.0 <= dmg <= 100000.0                # DAT clamps, verified
    return dmg
```

## What reSF2 gets wrong

`engine/game/game.cpp` (~1840, ~3641) computes:
```cpp
attribute_multiplier = 1.0f + damage_factor_base * attr;   // LINEAR
raw = base * attribute_multiplier * block * attack * crit * 2.0f;
```

Three concrete errors:

1. **The attribute curve is exponential, not linear.** Both factor helpers
   (`game+0x4A94F0`, `game+0x4A95A8`) return
   `powf(2.0, weight * attribute)`, and `1.0f` (`0x3F800000`) when their
   selector argument is null. The engine's `1 + factor*attr` is a different
   function; they agree only at `attr == 0`, which is exactly the degenerate
   case the engine currently hardcodes — so the bug is invisible today and will
   appear the moment attributes are implemented.
2. **The `× 2.0` is not a separate term.** `0x40000000` is the *base* of the
   `powf`, i.e. `2.0` is the curve base, not a trailing multiplier. Keeping
   both double-counts it.
3. **Two multipliers and an additive pair are missing entirely:**
   `hit[0x48] + enemy[0x774]` (additive, applied before the clamp) and
   `enemy[0x678] * enemy[0x6AC]` (applied after). The clamp itself
   (`0.0 .. 100000.0`) is also absent.

Note `f1` takes `self`/`is_ranged` while `f2` takes `enemy`/`weapon` — the
attacker and defender contribute through *different* helpers, which is the
"DamageAttribute vs DefenseAttribute" split the tracer prints.

## Provenance

Ghidra MCP at `127.0.0.1:8089`, program `game_region_runtime.bin` imported as
`ARM:LE:32:v7` with image base `0x8F057000` (~9800 functions auto-analysed).
With that base, every `game+off` address in these notes is directly usable.

Cross-check: Ghidra's decompilation of the main loop (`FUN_8f0bb400`)
independently reproduces the hand-reconstructed control flow in section 5 of
RUNTIME_MAP.md, including the inverted `interval <= elapsed` comparison and the
inner spin loop — so the reconstruction method itself is validated.

## The "wtf so strong" assert, and what it revealed

`"Model::getTotalDamage - wtf so strong"` at `0x8F79A2A0` is a developer sanity
warning left in the shipped binary by Nekki. It sits among a cluster of similar
informal asserts (`"Both is player! Wat!?"`, `"attacker is null"`,
`"Wrong magic count %d"`, `"Model::setNearestEnemy - enemy is weapon, fix code
bug"`), so this is their normal debug-logging style, not dead code.

It fires at the very end of `getTotalDamage`:

```
vcmpe.f32 s16, #0            ; dmg < 0 ?
bmi   -> warn                ; negative goes to the warning
vldr  s15, [pc, #0x68]       ; 100000.0
vcmpe.f32 s16, s15
ble   -> normal return       ; 0 <= dmg <= 100000 : fine
                             ; otherwise fall through to the warning
ldr   r0, [pc, #0x5c]        ; "wtf so strong"
add   r0, pc, r0
bl    game+0x1CFA58          ; the log sink
vmov  s15, s16               ; RETURNS THE VALUE ANYWAY
```

Three things follow, all of which matter for a 1:1 port:

1. **It only warns — it does not clamp.** The out-of-range value is returned
   unchanged (`vmov s15, s16` after the log call). An implementation that
   clamps to 100000 would silently diverge on very strong builds. The log sink
   `game+0x1CFA58` itself checks a global flag first and returns early when
   logging is disabled, which is why nothing appeared in logcat on this
   release build.

2. **It is direct evidence the curve is exponential.** A linear
   `1 + 0.0001 * attr` model cannot reach 100000 from any sane attribute value
   (it stays near 1.0), so a developer would never have needed this guard.
   With `2 ^ (delta / 10)` a delta of 200 already yields `2^20 = 1048576`.
   The assert only makes sense for an unbounded curve.

3. **It named the function.** With a single xref, the string identified
   `getTotalDamage` immediately — far faster than following the call graph
   from the damage tracer, which turned out to be a separate 650-line logging
   routine at `game+0x438530`.

### The doubling-range mechanic

The `f3` term (`game+0x60E794`) is:

```
powf(2.0, difference / divisor)
```

`divisor` is read from a global settings struct at `+0x18`, verified live as
**10.0f**, which is `<DamageDoublingRange Value="10"/>` from
`internalSettings.xml`. So the design is literally: **every 10 points of
attribute advantage doubles your damage**; 10 points behind halves it. The
sibling `<ResistanceDoublingRange Value="500"/>` applies the same curve shape
to enchantment resistance.

Note on reading these globals: the pointer is formed as `LDR Rn,[PC,#x]` then
`ADD Rn,PC,Rn`, and the PC used is that of the **ADD**, not the LDR. Using the
LDR's address gives a plausible-looking but wrong pointer (`0x8F8780A4` instead
of `0x8F8780A8`), which yields garbage (`-1.8e-35`) instead of `10.0`. This is
the same PC+8 pitfall that produced the earlier return-address-vs-call-site
mistake.

### Ported

`engine/game/damage_formula.hpp` implements the recovered formula, preserving
the emitted multiplication order (`base * f2 * f1 * f3 * add`, note f2 before
f1) because float multiplication is not associative. Covered by
`tests/test_damage_formula_golden.cpp` (24 checks, passing) against
`tests/golden/damage_formula.golden.json`.

Still required before it can replace the model in `game.cpp`: the attribute
system itself, whose baseline values are in `<AlignTargetAttributes>`
(WeaponDamage 12, BodyDefense 12, HeadDefense 5, RangedDamage 12,
MagicDamage 12, EnchantmentResistance 12, UnarmedDamage 0).
