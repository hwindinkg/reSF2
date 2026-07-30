# reSF2 — 1:1 port plan and status

Living document. Every claim is backed by bytes in the binary, a live Frida
capture, or a build/test run. Where something is unverified it says so.

Addresses are `game+off` against the relocated runtime dump
(`reverse/binaries/game_region_runtime.bin`, image base `0x8F057000` — load it
in Ghidra at that base and every address here resolves directly).

Last updated: 2026-07-30. Tests: 37/37 pass.

---

## Status at a glance

| # | Area | State | Evidence |
|---|---|---|---|
| 1 | Frame timing / main loop | **PORTED** | `game+0x64400`, Ghidra-confirmed |
| 2 | Step state (4 doubles) | open | `game+0x64230` |
| 3 | Damage formula | **RECOVERED**, not wired | `game+0x4527B4` |
| 4 | AI tactic roulette | partial | `tacticSettings.xml` schema confirmed |
| 5 | Collision geometry | open | capsule/edge radii |
| 6 | Triangle rasteriser | **FIXED** | was a no-op stub |
| 7 | Headless input edges | **FIXED** | `poll_events` wiped press edges |
| 8 | Attribute system | schema recovered | `game+0x60DF98` |
| 9 | Location parser coverage | partial | 6 attributes unparsed |
| 10 | Scene visuals | partial | placeholders in `scenes.cpp` |

---

## 1. Frame timing — PORTED

`engine/runtime/loop.cpp`. The shipped loop was *variable-step*
(`dt = now - last_ms`, clamped to 200 ms) with **no frame limiter at all**, so
gameplay speed tracked the host frame rate and 1:1 was impossible in principle.

Now mirrors `game+0x64400`: a fixed integer **16 ms** interval read from the
loop object at `this+0x08` (1000/60 truncated, so the real cap is **62.5 fps**,
not 60 — `<FrameRate Value="60"/>` in internalSettings.xml is nominal only),
`dt = ms / 1000.0` in **double** (divisor verified as exactly `1000.0` at
`0x8F0BB2B0`), and a two-stage **inner spin loop** at `+0x644C0` that re-reads
both the clock and the interval each pass and carries no fractional remainder.

Ghidra's decompilation of `FUN_8f0bb400` independently reproduces the
hand-reconstruction, including the inverted comparison:
```c
bVar12 = uVar9 <= uVar11;
if (uVar11 == uVar9) bVar12 = uVar8 <= uVar10;   // interval <= elapsed
```

Covered by `tests/test_frame_timing_golden.cpp` (23 checks).

**Residual:** `engine/core/game_loop.hpp` (the legacy float-1/60 accumulator
helper) is still unported. It is not on the runtime path; the golden test is
marked `WILL_FAIL` solely to document that. Either port or retire it.

## 2. Step state — open

`game+0x64230` writes **four doubles** per frame (`+0x08`, `+0x10`, `+0x18`,
`+0x20`), all in seconds: the frame delta plus three more values, with
accelerometer X/Y/Z fetched immediately before the call. reSF2 passes a single
`float dt` and drops the rest.

Low priority for a desktop port (no accelerometer), but the 4th slot comes from
a *second* 64-bit time source (`game+0x6DD264`) and may be a wall-clock stamp
that gameplay reads. Worth identifying before assuming it is inert.

## 3. Damage formula — RECOVERED, not yet wired

`Model::getTotalDamage` = **`game+0x4527B4`**, found via the sole xref to its
assert string `"Model::getTotalDamage - wtf so strong"` (`0x8F79A2A0`).

```
getTotalDamage(self, hit, is_ranged, weapon, ctx) -> float
    enemy = self[0x1E4]
    base = powf(2.0, base_attr * base_weight)
    f1   = game+0x4A94F0(self,  is_ranged)   ; powf(2.0, w*attr), or 1.0
    f2   = game+0x4A95A8(enemy, weapon)      ; powf(2.0, w*attr), or 1.0
    f3   = game+0x60E794(...)                ; powf(2.0, delta / 10.0)
    add  = hit[0x48] + enemy[0x774]
    dmg  = base * f2 * f1 * f3 * add         ; note f2 BEFORE f1
    dmg  = max(dmg, 0.0)
    dmg  = dmg * crit * enemy[0x678] * enemy[0x6AC]
    if (!(0 <= dmg <= 100000)) warn("wtf so strong")   ; warns, does NOT clamp
```

The divisor `10.0` is `<DamageDoublingRange Value="10"/>`, read live from the
global settings struct at `0x8F8780A8+0x18`. So the mechanic is: **every 10
points of attribute advantage doubles damage**; 10 behind halves it. That is
where the base of 2.0 comes from, and why the assert exists at all — the curve
is unbounded.

Three errors in the engine's current model (`game.cpp` ~3641):
1. the attribute curve is `powf(2.0, w*attr)`, **not** linear `1 + factor*attr`
2. `0x40000000` is the powf **base**, not a trailing `* 2.0f` multiplier
3. missing entirely: `hit[0x48] + enemy[0x774]`, `enemy[0x678] * enemy[0x6AC]`

The linear and exponential forms agree **only at `attr == 0`** — exactly the
value the engine hardcodes, so the bug is invisible today and will surface the
moment attributes exist.

Ported to `engine/game/damage_formula.hpp`, preserving the emitted
multiplication order (float multiply is not associative). Covered by
`tests/test_damage_formula_golden.cpp` (24 checks, passing).

**Blocked on:** the attribute system (§8). Until characters have real
attributes every factor evaluates to 1.0 and swapping the formula in changes
nothing observable — which is why it is deliberately not wired yet.

## 8. Attribute system — schema recovered

`game+0x60DF98` resolves an attribute by name from a `std::vector` of 16-byte
records in the global settings struct:

```
[0x8F8780A8+0x534] begin = 0x85BF4C08
[0x8F8780A8+0x538] end   = 0x85BF4C78     ; 112 bytes = 7 x 16
```

7 records matches `<AlignTargetAttributes>` exactly (WeaponDamage 12,
UnarmedDamage 0, BodyDefense 12, HeadDefense 5, RangedDamage 12, MagicDamage
12, EnchantmentResistance 12). Record shape:

```
+0x00 const char* name_begin
+0x04 const char* name_end      ; length compared before memcmp (cheap reject)
+0x0C int value                 ; read as (float)record[3]
```

A miss yields `0.0f` and is neutral, never an error.

Still needed: per-character attribute *state* — equipment and perks
contributing to each attribute. The values above are alignment targets, not the
character's own stats. This is the single highest-value remaining task, because
it unblocks §3.

## 4. AI tactic model — partial

Already better than the notes suggested: `game.cpp` does call
`tactics_.choose()` / `choose_debug()` with a `TacticContext`, i.e. the
roulette-wheel pick from `tacticSettings.xml` is wired in for both move
selection and the block decision. The `enemy_ai_state_` FSM values in
`combat.cpp` are only a debug readout, not the decision model.

Confirmed present in the binary's tactic key table
(`0x8F797574`..`0x8F797C58`), and the engine's names match:
```
AnimationWeights ExpectedWait Exponential AnimationFactors CurrentAnimation
CounterFactor DamageFactor HealthFactor EnemyHealthFactor
AnimationFramesFactor ChildFramesFactor MagicBulletFactor MissileBulletFactor
HitFactor DistanceFactor AntiLimit FactorType
```

**Correction to earlier notes:** `ConditionalDesigionFactor` (from the JS port)
does **not** appear in the ARM string table at all. Do not implement it from
the JS name.

Decision-level keys the engine does not model yet:
```
QuickAttackChance EvadeChance Memory Strikes RoundFactor UseDefense
CounterAttackChance DodgeChance BlockChance UseSafeAttackChance
TableAttackChance QuickAttacks Evades CautiousMovementsChance
DodgeMissilesChance DodgeMagicChance DistanceError FrameError
ResponseDelay EnemyResponseDelay
```

And the tactic tables under `assets/tactics/` (`.tbs`, `.stb`, `.sts`, `.atf`)
are not loaded. Table types: `RandomAnimation NoneTable AttackTable
MovementsTable DodgeTable AttackTableOld SummaryResultTable QuickAttack
ShiftTable ThrowTactics`. Decision types: `Tabular`, `ExpectedWait`.

The debug format string fixes the decision ORDER, which is the cheapest thing
to verify a port against:
```
UseDefense -> UseSafeAttack -> TableAttack -> DodgeMissiles ->
QuickAttack[i] -> Evade[i] -> UseCautiousMovements
then DistanceError / FrameError / Intervals / EnemyIntervals /
DecisionType / Decision {Wait=%d}
```

Also open: `tactic_settings.cpp:43` admits the per-target `AnimationFactors`
probe is omitted.

## 5. Collision geometry — open

Keys present in the binary that the collision code ignores: `Edge`, `Radius1`,
`Radius2`, `Margin1`, `Margin2`, `Figures`, `Capsule`, `ModelEdge`, `NPivot`,
plus `ModelMacroNode` / `MacroNodeTmpData`.

`heel %s not found in shift table for %s` and `big frame %d` show the shift
tables are keyed by heel node per animation frame — relevant to the open
root-motion items at `game_clean.hpp:1715` and `:708`.

Note the models are genuinely 3D: `skeleton_punching_bag.xml` nodes are spread
over **Z** (±9.9) with X only ±11.8. Any collision work has to decide
explicitly what happens to Z rather than silently dropping it.

## 6. Triangle rasteriser — FIXED

`software_renderer` had **no triangle rasteriser**:
`draw_filled_triangle_screen` and `..._world` were no-op stubs with a comment
saying "mainly used for debug visualization". They are not — a capsule
silhouette is two triangles for the shaft plus a circle at each end, so with
triangles missing **every fighter and the punching bag rendered as a string of
disconnected circles**. This was the single largest visual defect.

Added a half-space (barycentric) rasteriser, clipped per-pixel so limbs do not
vanish at the frame edge, plus `draw_filled_circle_world` that scales the
world-space radius by the camera zoom.

Before/after: `screenshots/05_dojo_hud.png` (beads, bag as a red rectangle) vs
`screenshots_audit/audit_dojo.png` (solid fighter, solid bag).

## 7. Headless input edges — FIXED

`HeadlessTestRunner::run_frames()` calls `poll_events()` at the top of each
frame, and `poll_events()` clears `keys_just_pressed`. A key injected *between*
`run_frames()` calls therefore lost its press edge before any scene observed
it, and scenes test `key_pressed()` (an edge), so **all scripted key input
silently did nothing**.

Added `tap_key()`, which injects after `poll_events()` inside the same frame.

This masked a second problem: on a fresh save the tutorial pushes a `Dialogue`
scene over the dojo, and `Dialogue` does not call `host_update_gameplay`, so the
fighter never animated (`anim nodes 0 posed this frame` against 67 skeleton
nodes and 556 loaded animations). Tests could not dismiss it because of the
input bug. `tool_visual_audit` now opens the dojo directly via
`start_scene = "dojo"`.

## 9. Location parser — partial

`engine/format/location_parser.cpp` ignores 6 attributes that appear in the
shipped `params.xml` files:

| tag/attr | occurrences | meaning |
|---|---|---|
| `Layer/Scaling` | 62 | flag on foreground layers; sets `+0x154`/`+0x155` on the layer object (`game+0x3E40D0`) |
| `Layer/Path` | 26 | atlas borrowed from ANOTHER location, e.g. `Path="locations/spaceship/"` |
| `SimpleEffect/Speed`, `/Offset`, `/Pause` | 110/110/9 | scrolling and pausing effect layers |
| `OscillationX/Y`, `ReappearX/Y`, `Rotation`, `Speed` | 5/73/153/59/4/168 | animated-layer sub-elements |

`Layer/Path` is a correctness bug, not cosmetic: those layers currently look
for their atlas in the wrong directory and silently render nothing.

Root-parser schema, verified at `game+0x3E56F8` with field offsets:

| attribute | field |
|---|---|
| `Floor` | `+0x2C` |
| `PositionY` | `+0x30` |
| `Wall` | `+0x34` |
| `Width` | `+0x38` |
| `Height` | `+0x3C` |
| `GridSize` | `+0x28` |
| `FrictionForce` | global physics, not Location |
| `MinWidth`, `Music`, `Color` | present, offsets not pinned |

`MinWidth` and `PositionY` are read by the original and **not** by reSF2. The
comment `Width2="visible world width"` appears in the shipped XML but no
location actually sets it, so the framing must derive from `Height` — which is
what the engine does, and the measurement below supports that.

## 10. Framing — measured, close

`tool_visual_audit` on dojo at 1280x720:

```
zoom 1.2857 (vh / Height)   visible world 995.6 x 560.0
player screen x 36.4%  (original ~36%)
enemy  screen x 63.6%  (original ~64%)
floor screen y 85.7%   (original ~87.5%)
```

Horizontal framing is effectively correct. The ~1.8 pp floor discrepancy is the
remaining item and is small enough that it needs the original measured properly
(from a clean capture at a known resolution) rather than more guessing.

---

## Priority order

1. **Attribute system** (§8) — unblocks the recovered damage formula (§3).
   Schema and baseline values are already in hand; what is needed is per-
   character state from equipment and perks.
2. **`Layer/Path` in the location parser** (§9) — a real correctness bug with a
   small, self-contained fix; those layers render nothing today.
3. **Wire `damage_formula.hpp` into `game.cpp`** (§3) — do this immediately
   after §1, and delete the linear model plus the stray `* 2.0f`.
4. **Tactic tables** (§4) — load `.tbs`/`.stb`/`.sts`/`.atf` and add the
   decision-level keys. Verify against the tracer's decision order.
5. **Retire or port `engine/core/game_loop.hpp`** (§1 residual) so no float
   accumulator remains anywhere.
6. **Collision geometry** (§5) — capsule/edge radii and margins, and an
   explicit decision about Z.
7. **Scene placeholders** (§10) — `scenes.cpp` has ~13 markers (star ratings
   always 4, sword/gem icons drawn as text, silhouette backdrop standing in for
   a 3D model).
8. **Step state** (§2) — identify the 4th double before assuming it is inert.

## Method notes (do not relearn these)

- **Ghidra beats hand-rolled xref scanning.** `get_xrefs_to` gives exact
  results; the `find_string_xrefs.py` heuristic produced false positives that
  cost real time (it "found" `Wall` in a function that actually references
  `basic_string`). Use `analysis/resolve_dats.py` to turn Ghidra's
  `DAT_x + -0yyy` noise into readable string names.
- **Import the RELOCATED dump, not the static file.** Base `0x8F057000`,
  `ARM:LE:32:v7`. The static file's odd code offset (`0x45251`) leaves ARM
  instructions unaligned and Ghidra cannot analyse it.
- **The PC+8 trap.** Globals are addressed as `LDR Rn,[PC,#x]` +
  `ADD Rn,PC,Rn`, and the PC belongs to the **ADD**. Using the LDR's address
  gives a plausible but wrong pointer — it produced `0x8F8780A4` and garbage
  (`-1.8e-35`) instead of `0x8F8780A8` and `10.0`. The same trap earlier made
  return addresses look like call sites.
- **Frida hooking limits.** Never hook the `game+0x6xxx` import thunks (they
  are PC-relative; the trampoline corrupts the `ADD IP,PC` math and the hook
  silently no-ops). Hook at most ONE 16-byte PLT stub — adjacent stubs hold
  their literal pool at `+8` and two trampolines clobber each other. Parse
  `/proc/self/maps`, never `Process.enumerateRanges()`. The game loop runs on
  **Thread-2**, not the main thread.
- **Build:** `build.bat` works (MSVC is present, just not on PATH; CMake finds
  it). `cmake --build build --config Release --target <t>` for a single target.
- **Diagnose visuals with numbers.** `tool_visual_audit` found both fixed bugs
  in one run; eyeballing screenshots had not.
