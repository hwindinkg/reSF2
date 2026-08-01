# Golden tests — testing reSF2 against the original

The idea: never assert what we *think* the game does. Record what it *actually*
does from the live ARM binary, store that as a golden file, and make the C++
engine reproduce it. A failing golden test is a measured 1:1 gap.

## Why this is possible at all

Two things unlocked it this session (see `RUNTIME_MAP.md`):

1. The game code is reachable at runtime (shared `rwxs /dev/zero` mapping), so
   any function can be hooked or read.
2. The original ships a **built-in tracer**. `internalSettings.xml` has
   `<Log Value="1"><Hits><Damage Value="1"/><Style Value="1"/></Hits>
   <Tactics Value="1"/><Animations Value="1"/></Log>`, and the format strings
   in the binary spell out the exact evaluation order of the damage and AI
   models. We do not have to guess intermediate values — the game prints them.

## Capture rules (hard-won, do not skip)

| rule | reason |
|---|---|
| Parse `/proc/self/maps`, never `Process.enumerateRanges()` | the game region is a *shared* mapping and is omitted from enumeration |
| Never `Interceptor.attach` a `game+0x6xxx` thunk | PC-relative `ADD IP, PC`; the trampoline corrupts the GOT math, hook silently no-ops |
| Hook **at most one** 16-byte PLT stub | stubs are adjacent and hold their literal pool at `+8`; two trampolines nearby clobber each other. A six-stub version recorded zero calls while a single-stub hook worked |
| Prefer calling an API yourself (`NativeFunction`) over hooking it | keeps the hook count at one |
| Don't rely on `setInterval` while a hot hook is installed | it is not scheduled reliably; report from inside the hook |
| Write results to a file under `/data/data/<pkg>/` | `/data/local/tmp` is not writable by the app, and buffered stdout is lost when the frida CLI is killed |
| Re-resolve the region base every run | ASLR moves it |
| Remember the loop is on **Thread-2**, not the main thread | hooks watching the main thread see nothing |

## Layout

```
tests/golden/<area>.golden.json     reference data + provenance
tests/test_<area>_golden.cpp        the C++ test that must reproduce it
reverse/frida_hooks/record_*.js     the capture script that produced it
```

Every golden file carries a `source` block (device, function address, date) and
`_how` keys explaining how each number was obtained. If a value cannot be
traced to a capture or to bytes, it does not belong in a golden file.

## Status

| golden | area | captured | engine result |
|---|---|---|---|
| `frame_timing.golden.json` | main-loop timestep | yes — `this+0x08` read live = 16 ms; divisor `1000.0` decoded from `0x8F0BB2B0` | **FAILS** (GAP-1/GAP-2), marked `WILL_FAIL` in CMake |

## Next captures, in priority order

### 1. Damage (GAP-3) — highest value

The tracer at `game+0x438530` prints the full chain:
`BaseDamage -> Critical -> BlockDamageFactor -> Block -> DamageFactor ->
DamageAttribute(+Shift) -> DefenseAttribute -> TargetAttributeDifference ->
Delta.Factor(+Shift) -> AttributeDifference -> HitDamage -> StyleValueAdd ->
StyleValue -> Style`.

`reverse/frida_hooks/trace_damage_func.js` hooks it and dumps arguments/floats.
**Status: hook installs, but the function did not fire while hitting the dojo
bag.** Either the bag uses a different path, or `game+0x438530` is one of
several damage entry points. Next step: hook the *sink* that formats those
strings (find it via `find_string_xrefs.py 0x8F799D1C`, the `HitDamage` string)
and capture the formatted lines verbatim — that sidesteps needing to know the
object layout, and gives a directly comparable text golden.

A real fight (not the dojo bag) is likely required.

### 2. AI decisions (GAP-4)  [GAP-4 PORTED 2026-08-01 — order golden landed]

The tactics tracer prints, per decision:
`UseDefense / UseSafeAttack / TableAttack / DodgeMissiles / QuickAttack[i] /
Evade[i] / UseCautiousMovements`, then
`DistanceError / FrameError / Intervals / EnemyIntervals / DecisionType /
Decision {Wait=%d}`, plus the `MyAnim/MyHeel_1` distance matrix.

Capturing a few hundred of those lines with the RNG state gives a golden that
pins the roulette model exactly, including the `Wait` frame counts. This is far
stronger than unit-testing the weight curve in isolation.

**Status (2026-08-01):** GAP-4 is ported (ADR-005 implemented; FSM/adapter
removed, commit `f7a7c72`). The tracer-**order** contract is now pinned by
`tests/golden/tactic_decision_trace.golden.txt` (gate G1, commit `b142fd7`):
a byte-exact golden of the pipeline's `DecisionTrace` output in tracer order,
built from the tracer strings at `0x8F798090`..`0x8F79834C` (see the file's
`[ORIGINAL]` provenance header). The live-capture **value** golden described
above remains the follow-up — it is the suggested way to close the open v=7
claim (c) (see the GAP-4 close-out inventory in PORT_GAPS.md).

### 3. Animation root motion (GAP-5)

`heel %s not found in shift table for %s` and `big frame %d` show shift tables
are keyed by heel node per frame. Capturing per-frame node positions
(`NPivot`, `Heel_1`, `Heel_2`) for a known animation yields a golden the
engine's root-motion code can be checked against frame by frame — the open
`[HEURISTIC-TODO]`s in `game_clean.hpp` (:708, :1715) are exactly here.

## Running

```
cmake --build build
ctest --test-dir build -R golden --output-on-failure
```

`test_frame_timing_golden` is marked `WILL_FAIL` because it documents a real,
unported divergence. When the integer timestep lands, remove that property —
the test passing is the definition of GAP-1 being closed.
