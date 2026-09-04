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

## Input record / replay (prep wave, 2026-09-04)

Game inputs are discrete post-device-mapping events: `Df{control, index}`
(L453; subclasses fix the type via `Gfa()`: 0 touch / 1 keyboard / 2 gamepad).
Controls: 1-8 stick sectors (`ze.GBa`, L463-471), 9/10 keyboard, 11 punch,
12 kick, 14 special; `yJa`/`Gmb` consume them (L501). Coordinates do NOT
survive mapping — recording `(control, index, type)` is sufficient for exact
replay. Full input-path reference: `reference/AI_STATIC.md` §6.

- Recording: the `ca.N0a` (press) / `ca.O0a` (release) wrappers emit
  `[INPUT-REC] {"f","i","m":"N0a"|"O0a","c","x","t"}` per effective input
  (post-`Za` gating). `reference/tools/record_inputs.py` converts a
  console.log to a replay script (`atframe F press|release C X T`).
- Replay: the shell forwards `atframe press/release` to the page, which
  re-injects them pre-tick via direct `fight.N0a`/`fight.O0a` calls with
  equivalent `{control, index, Gfa}` objects — bypassing DOM + `Za.DEa`
  lesson gates deterministically (the recorded stream was already post-gate).
  Replayed inputs flow through the same wrappers, so the `[INPUT-REC]` echo
  is the mechanical fidelity check.
- Verified 2026-09-04 (smoke): `input_smoke.txt` (press/release 11/12 at
  f=150/160/200/210) → all four `[INPUT-REC]` at exact frames, 0 tracer
  ERRs → `record_inputs.py` round-trips the 4 lines byte-identically.

## READY FOR USER PLAYTHROUGH (manual tutorial run)

1. Run: `& ./shell/bin/Release/net9.0-windows/OracleShell.exe --input-script reference/tools/input_phase1.txt`
   (any script works; recording is automatic). A window opens with the game.
2. Play through the tutorial with mouse/touch/keyboard. Every effective
   input is recorded with its fight.frame — play as far as you like; the
   shell auto-closes at fight.frame 600 (or 150s timeout).
3. Recording lands in `reference/traces/console.log` as `[INPUT-REC]` lines.
4. Convert: `python reference/tools/record_inputs.py reference/traces/console.log reference/traces/recorded_inputs.txt`
5. Replay twice with `--input-script reference/traces/recorded_inputs.txt`,
   extract both via `extract_oracle.py`, compare sha256 → gate re-run.
