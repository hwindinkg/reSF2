# reference/traces — Oracle trace format (Phase 1)

All oracle records are emitted by `reference/tools/trace_oracle.js` (injected
via WebView2 `AddScriptToExecuteOnDocumentCreated`, runs before game boot) as
single-line `console.log("[ORACLE] " + json)` calls. OracleShell appends them
to `console.log` with wall-clock timestamps; `reference/tools/extract_oracle.py`
strips the timestamps and writes canonical JSONL (sorted keys, `(",", ":")`
separators) plus a sha256 + gate-field validation verdict.

## Record types

### `oracle_header` (once per run, first fight tick)
```json
{"t":"oracle_header","js":"sf2.502f0946.js",
 "harness":{"dateFixed":1720000000000,"mathRandom":"mulberry32(0xC0FFEE)"},
 "ai_side":"Enemy",
 "hooked":{"iPa":[],"N0a":["fight.fight"],
           "Pqb":{"Enemy":true},"de.ia":{"Enemy":true}}}
```
- `harness` — entropy pins (game code untouched). `Date` frozen to `dateFixed`
  (`L.seed`, `Da.rL()`/`Da.IT()` reseeds, InstallID all derive from
  `ed.getDate().getTime()`); `Math.random` replaced with mulberry32
  (used by `oa.eT`: particle jitter + `Ie.Gb()` tactic-range evaluation).
  `Da.jf()` draws come from the `Xx` LCG
  (`mf = (mf*1103515245+12345) mod 2^31`, glibc constants — portable).
- `hooked` — which method wrappers actually attached (honest reporting;
  empty `iPa` below is the known tutorial-timer gap, see Status).
- `ai_side` — `parameters.Fj` ("NotAI" flag) guess, `Enemy` fallback.

### `oracle` (one per fight.frame)
```json
{"t":"oracle","f":366,"phase":2,"round":0,"cf":0,"cf_enemy":3,
 "ai_side":"Enemy","ai_branch":-1,"ai_zone":1,
 "chosen_move":null,"chosen_candidates":null,
 "chances":{"CZ":0,"bda":0,"tba":0,"tua":0,"dua":0,"Bpa":0,"rqa":0,
            "oqa":0,"qPa":0,"vO":0,"Awa":0,"pua":0,"oua":0,
            "eh":1,"fk":-1,"aqa":1,"oC":0},
 "input_buffer_state":{"me_empty":true,"me_control":null,
   "enemy_empty":true,"enemy_control":null,
   "recent":[{"f":352,"control":10}]},
 "round_timer_xU":null,"round_timer_NF":null,
 "block_state":{"me":0,"enemy":1},"block_info":{"me":null,"enemy":null},
 "camera":{"cx":836.09,"cy":-170.34,"zoom":1},
 "trace_stats":{"Pqb":0,"ia":0,"N0a":12,"iPa":0,"deCount":1,"decider":null}}
```
- `f`/`phase` — `fight.frame` / `fight.eu` (fight controller `ca`, top screen's `Ig`).
- `cf`/`cf_enemy` — move frame `da.Xh` for Me (`fight.pb`) / Enemy (`fight.yb`).
- `ai_branch`/`ai_zone` — deciding `de` instance's `fk` (0-11, -1 = never
  decided) / `aqa` (1-4, 1 = init). Source = `lastDecider` (de with latest
  Pqb/ia event) else the `ai_side` guess.
- `chosen_move` — last non-null `de.Pqb`/`de.ia` return (move name duck-typed
  from `name`/`Eza`/`jb`/`ID`/`FileName`); `chosen_candidates` = `ld.length`.
- `chances` — the evaluated numbers the decision consumes on that `de`
  (`dqb` trio `CZ`/`bda`/`tba`, `mQ` rolls, tactic thresholds
  `qPa`/`vO`/`Awa`/`pua`/`oua`, plus `eh`/`fk`/`aqa`/`oC`). Numbers only.
- `input_buffer_state` — `WC` buffer per fighter (`-1` = empty) + last-8
  `ca.N0a` calls (`{f, control}`).
- `round_timer_xU`/`round_timer_NF` — `xU` (`--xU` per `iPa`,
  init `round.gma*60+1`) and `NF = xU/60|0`. Read from the BFS-found timer
  host (fight screen `Sf`); `null` when unreachable (see Status).
- `block_state` — the game's own `da.yD(5)` query per fighter (1/0);
  `block_info` — interval name when active.
- `camera` — `fight.Ta.Go.ma` focus + `fight.Ta.ia.Bj` zoom (post-tick read).
- `trace_stats` — wrapper fire counters (`Pqb`/`ia`/`N0a`/`iPa`), `deCount`
  (de instances found by fight-graph BFS), `decider` (label or null).
- `window.__trace` (in-page ring, cap 20000, not in console) holds every
  method entry/exit: `{m:"Pqb",side,f,fk,aqa,ret,cand}`,
  `{m:"de.ia",side,f,fk0,fk1,aqa,ret}`, `{m:"N0a",f,control,eu}`,
  `{m:"iPa",f,xU0,xU1}`.

Pose records (`{"t":"frame",...}` with bones) come from the unchanged
`trace.js` v2 in the same `console.log` (first ~350 app ticks).

## Reproduce
```powershell
& ./shell/bin/Release/net9.0-windows/OracleShell.exe --input-script reference/tools/input_phase1.txt
python reference/tools/extract_oracle.py reference/traces/console.log reference/traces/oracle_trace.jsonl
```
Each run uses a fresh browser profile (clean saves) + the pinned harness, so
repeated runs start from identical state. The shell auto-closes when
`window.__oracleDone` (fight.frame ≥ 600) or after a 150s timeout.

## Status (2026-09-04, wave 2)
- Infra: PASS (injection, hooks, entropy pins, frame-exact page-side stimulus
  via `atframe`, fresh profile, auto-close, extractor all work; 0 tracer ERRs).
- Determinism: PARTIAL — two identical-script runs are bitwise identical for
  lines 0..366 (367 records incl. header: phases, cf, camera floats, chances,
  block states). First divergence at line 367 (`f≈366`):
  `input_buffer_state.recent` / `trace_stats.N0a` (12 vs 11 events) — an extra
  button-tap `N0a` (control 6) registered in one run only. Stick-drag inputs
  register 1:1; button taps are gated/flaky (tutorial lesson gating,
  wall-clock-dependent consumption — see MASTER_TODO BLOCKED).
- Gate: FAIL — `chosen_move` null in 601/601, `ai_branch`/`ai_zone` frozen at
  init (-1/1), `round_timer_xU` null in 601/601, `iPa` wrapper never attached.
  Evidence over ~1600 combined fight frames, `deCount: 1`, `ia: 0`, `Pqb: 0`:
  the auto-reached dojo tutorial fight contains ZERO AI decisions (scripted
  enemy, `PhysicalDummy` player, no round timer in the tutorial HUD).
  Hashes: run3 `75348487…52ec`, run4 `a7a11c1b…e356` (differ only from the
  input-flakiness above).
