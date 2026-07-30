# Shadow Fight 2 — Block, Movement & AI Logic

## Source: Binary Analysis of ShadowFight2.s86 (PE32, x86) + XML assets

---

## 1. Block Mechanics

### 1.1 Block is an Interval Type

Block is **not** a simple state flag — it is an **Interval** in the game engine's
interval system. This is evidenced by:

```xml
<RemoveInterval Type="Block" ApplyTo="Player"/>
```

The same `<Interval>` mechanism handles: `Attack`, `Uninterrupt`, `SemiUninterrupt`,
`SelfUninterrupt`, `Invulnerable`, `Block`, and `Throwable`.

### 1.2 Block Chance Calculation

Block is governed by the `BlockChance` element inside `<UseDefense>`:

```xml
<BlockChance Base="0" CounterFactor="0.05" HitFactor="0.15"
             DamageFactor="0.5" AnimationFramesFactor="0.005" Limit="1" />
```

**Formula** (from `TacticWeight::score()` in `tactic_settings.cpp`):

```
score = counter * CounterFactor
      + damage * DamageFactor
      + (1 - health) * HealthFactor
      + (1 - enemy_health) * EnemyHealthFactor
      + anim_frames * AnimationFramesFactor
      + magic_bullets * MagicBulletFactor
      + missile_bullets * MissileBulletFactor
      + hits * HitFactor
      + child_frames * ChildFramesFactor
      + distance * DistanceFactor
      + Shift
```

Then `apply_curve(score)` applies either Linear or Exponential curve to get the
final probability in [anti_limit .. limit] range.

**Linear curve** (default):
- If score >= 0: `result = base + (limit - base) * min(1.0, score)`
- If score < 0:  `result = base + (anti_limit - base) * min(1.0, -score)`

**Exponential curve**:
- If score >= 0: `result = limit + (base - limit) * 2^(-score)`
- If score < 0:  `result = anti_limit + (base - anti_limit) * 2^(score)`

### 1.3 When Does Block Activate?

Block is **NOT automatic when idle**. The AI uses a **weighted roulette wheel**
to pick an action every 0.6-1.0 seconds. "Duck" in the animation weights
maps to the block action.

**Per-tactic block parameters:**

| Tactic | BlockChance Base | CounterFactor | DamageFactor | AnimFramesFactor | Limit |
|--------|-----------------|---------------|-------------|-----------------|-------|
| NoTables | 0 | 0.2 | 4 | 0.03 | 1 |
| UseTables | 0 | 0.05 | 0.5 | 0.005 | 1 |
| Sensei | 1 (always) | — | — | — | — |

**Key insight**: Block chance starts at 0 and increases dynamically based on:
- **CounterFactor**: Increases when the enemy is in counter-attack state
- **DamageFactor**: Increases based on damage recently taken
- **AnimationFramesFactor**: Increases the longer the current animation runs
- **HitFactor**: Increases when the bot has been hit

### 1.4 Block Attributes

```xml
<BlockDamage Attribute="BodyDefense" />
<BlockDamageFactor Base="0.0001" Attribute="BlockDamageFactor" />
```

- `BlockDamage` — damage dealt when blocking (counter-damage)
- `BlockDamageFactor` — multiplier for block damage (base 0.0001)
- `BlockDefense` — defense bonus while blocking

### 1.5 Block-Related Perks

| Perk | Effect |
|------|--------|
| `PERK_BLOCK_BREAKER` | Can break through enemy blocks |
| `PERK_WEAPON_BLOCK_BREAKER` | Weapon-specific block breaking |
| `PERK_SPIKE_BLOCK` | Deals damage when blocking (15%) |
| `PERK_ANTI_SHOCK` | Prevents stagger on block |

### 1.6 Block Removal

Block can be completely disabled per-fight via rules:

```xml
<RemoveInterval Type="Block" ApplyTo="Player"/>
```

This is used in NO_BLOCKS challenge modifiers.

---

## 2. Movement System

### 2.1 Input Processing

Input is processed in `InputHandler::process_input()`:

```
Keyboard → Directional Keys → Relative Forward/Back → Action (punch/kick/block)
```

**Key mappings** (from input_handler.cpp):
- W/Up = up, S/Down = down
- A/Left = left, D/Right = right
- O/Space = punch, P/K = kick
- Directions are relative to facing: `key_forward = facing_right ? key_right : key_left`

### 2.2 Double-Tap Detection

Window = 300ms (`kDoubleTapWindowMs`), from `Model::step @ 0x10161ad0`.

- Double-tap forward → `DoubleStepForward`
- Double-tap back → `BackHandflip` (retreat flip)

Detection works from both idle (`move_state_ == 0`) and walking states.

### 2.3 Available Movements

From `ComputerSettings.xml > MovementsTables`:

**Main iterations** (per-frame movement options):
- StepForward, StepBack, JumpUp, Duck
- FrontFlip, BackFlip, ForwardRoll, BackRoll
- BackHandflip, DoubleStepForward

**Last iteration** (final pose after movement):
- StanceIdle, StepForward, StepBack

**Iteration count**: 3 frames of movement decisions.

### 2.4 Movement Gating (Interruptibility)

The AI can only change movement during specific animation states:

```xml
<ShiftTables><Animations>
  <Animation Name="Controlled" />
  <Animation Name="Hit" />
  <Animation Name="GetUp" />
  <Animation Name="IdleStance" />
  <Animation Name="StartIdleStance" />
</Animations></ShiftTables>
```

During attack animations (`RangedPlayer`, `MagicPlayer`, `Throw`, etc.),
movement changes are **blocked** — the character is "locked in".

### 2.5 Uninterruptible Intervals

From `UninterruptibleIntervals`:
- **Strict**: Only `Uninterrupt` interval
- **Extended**: `Uninterrupt` + `SemiUninterrupt`

During uninterruptible intervals, the fighter cannot be staggered or interrupted
by hits. This is the window where combos are safe to throw.

---

## 3. AI Decision System

### 3.1 Architecture Overview

```
tacticSettings.xml → TacticSettings::load() → TacticDef (per tactic name)
                                                    │
Fight state → TacticContext ────────────────────────┤
                                                    │
                                            TacticSettings::choose()
                                                    │
                                            jL roulette-wheel pick
                                                    │
                                            Map label → Fighter action
```

### 3.2 Decision Timing

- Decision cooldown: **0.6–1.0 seconds** (random)
- Enemy response delay: **30–60 frames** (~0.5–1.0s at 60fps)
- Matched in original binary at `FUN_10171d80`

### 3.3 Tactic Types

| Tactic | Template | Type | Description |
|--------|----------|------|-------------|
| Standard | UseTables | Roulette | Balanced AI |
| NoTables | — | Tabular | All defense at 0, random |
| UseTables | — | Tabular | Full table-driven |
| Sensei | — | Tabular | Always blocks/dodges/attacks |
| Aggressive | Standard | Roulette | No retreat, always forward |
| Careful | Standard | Roulette | More backflips, higher evade |
| Beginner | NoTables | Random | Very passive |

### 3.4 Animation Weights (Standard Tactic)

| Animation | Base Weight | Notes |
|-----------|------------|-------|
| ForwardStep | 800 (cap 1600) | Distance factor +0.002, Shift -0.25 |
| BackStep | 250 (cap 1) | Distance factor +0.0025, Shift -0.25 |
| BackHandflip | 500 (cap 1) | Distance factor +0.0025, Shift -0.25 |
| ShortAttack | 1000 | High base weight |
| Weapon | 200 | Weapon-specific attacks |
| RangedPlayer | 400 (cap 1000) | Distance factor +0.0025, Shift -0.75 |
| MagicPlayer | 1000 | Magic attacks |
| BossAbility | 3000 | Boss special moves |
| Retreat | 100 (cap 1) | Distance factor +0.0025, Shift -0.25 |
| Duck | 1 | Block action |
| JumpUp | 1 | Neutral jump |
| *(unnamed)* | 100 | Catch-all / idle |

### 3.5 Defensive Decisions

```xml
<UseDefense>
  <CounterAttackChance Base="0" CounterFactor="0.05" HitFactor="0.15"
                       DamageFactor="0.5" AnimationFramesFactor="0.0015" Limit="1"/>
  <DodgeChance         Base="0" CounterFactor="0.05" HitFactor="0.15"
                       DamageFactor="0.5" AnimationFramesFactor="0.0015" Limit="1"/>
  <BlockChance         Base="0" CounterFactor="0.05" HitFactor="0.15"
                       DamageFactor="0.5" AnimationFramesFactor="0.005" Limit="1"/>
</UseDefense>
```

All three defensive actions use the same factor model:
- **CounterFactor** (0.05): Weight when enemy is attacking
- **HitFactor** (0.15): Weight when recently hit
- **DamageFactor** (0.5): Weight based on damage taken
- **AnimationFramesFactor**: Weight based on animation duration
- **Block** has higher AnimFramesFactor (0.005 vs 0.0015) — blocks more as animation progresses

### 3.6 Movement Tables (AI Decision Pipeline)

From `ComputerSettings.xml`:

1. **ShiftTables** — When can the bot shift position?
   - Only during: Controlled, Hit, GetUp, IdleStance, StartIdleStance
   - Nodes: NPivot, NHeel_1, NHeel_2

2. **OutcomeTables** — Maps weapon+animation to outcomes
   - Alpha: idle/control states
   - Beta: non-wall, non-boss states
   - Throws: Throwable intervals

3. **MovementsTables** — Movement decisions (3 iterations)
   - 3 frames of movement computation before final pose

4. **AttackTables** — Attack timing discretization
   - Frames: 1|6|12|1000 (discrete attack timing buckets)

5. **TablesReduction** — Discretization step
   - Movement step: 5 units
   - Interval rounding: step=10, lower_limit=-100

### 3.7 Condition System

The condition system checks when moves can chain:

| Condition Class | Address | Purpose |
|----------------|---------|---------|
| `ConditionInterval::virtual_8` | `0x10086b90` | Check if animation is in named interval |
| `ConditionAnimation` | `0x10083190` | Check animation name match |
| `ConditionCurrentAnimation` | `0x10083ac0` | Check current playing animation |
| `ConditionModelMirrored` | (string ref) | Check facing direction |
| `ConditionPerk` | (string ref) | Check if perk is active |
| `ConditionWeapon` | (string ref) | Check equipped weapon |

---

## 4. Key Function Addresses (ShadowFight2.s86)

| Function | Address | Purpose |
|----------|---------|---------|
| `FUN_10171d80` | `0x10171d80` | Main AI decision loop |
| `IntervalAttack::getFactors` | `0x10115921` | Attack damage calculation |
| `Model::startAction` | `0x1015C540` | Start model action |
| `Model::setCurrentNode` | `0x1015B530` | Set current animation node |
| `Model::step` | `0x10161ad0` | Movement/step processing |
| `Model::setNearestEnemy` | `0x101586F0` | Store enemy ptr at model+0x190 |
| `Model::getModelAlign` | `0x10159780` | Get facing reference |
| `ModelAnimation::getPlayerAnimation` | `0x1016622A` | Position update |
| `ModelAnimation::playInfo` | `0x101650FC` | Animation update chain |
| `ModelAnimation::mirrorNodes` | `0x10164093` | Skeleton mirroring |
| `ConditionInterval::virtual_8` | `0x10086b90` | Interval condition check |
| `ConditionAnimation ctor` | `0x10083190` | Create animation condition |
| `ConditionCurrentAnimation ctor` | `0x10083ac0` | Create current-anim condition |
| `interpolateNodes` | `0x10163F60` | Lerp skeleton positions |
| `updateAnimationFrame` | `0x10164F20` | Advance animation frame |
| `updateNodes` | `0x10165C10` | Update skeleton nodes |
| `applyInterpolation` | `0x10164C20` | Apply frame interpolation |
| `finalizePosition` | `0x101661D0` | Root motion finalization |

---

## 5. Data Files

| File | Purpose |
|------|---------|
| `assets/tacticSettings.xml` | AI behavior weights and decision parameters |
| `assets/ComputerSettings.xml` | Movement tables, outcome tables, interrupt rules |
| `assets/moves.xml` (per weapon) | Move definitions with intervals and conditions |
| Battle XML (inline in binary) | Fight rules including block removal |

---

## 6. Summary: How Block Works End-to-End

1. Every 0.6–1.0s, the AI runs `FUN_10171d80` (decision loop)
2. It builds a `TacticContext` from fight state (distance, health, damage, hits, frames)
3. For each candidate animation, it evaluates the `TacticWeight` using `score()` + `apply_curve()`
4. For defense, `BlockChance` factors determine block probability:
   - Higher when taking damage (DamageFactor)
   - Higher when being attacked (CounterFactor)
   - Higher when hit recently (HitFactor)
   - Higher in longer animations (AnimationFramesFactor)
5. The weighted roulette picks an action — if "Duck" wins, the fighter blocks
6. Block activates as an `Interval` — it persists for a duration, not a single frame
7. While blocking, `BlockDefense` reduces incoming damage, `BlockDamage` can counter-damage
8. `PERK_BLOCK_BREAKER` can override the block and deal full damage
9. `RemoveInterval Type="Block"` completely disables blocking for specific fights

**Block is NOT automatic.** The bot must "decide" to block via the weighted
roulette, and the decision probability depends on current fight state.
