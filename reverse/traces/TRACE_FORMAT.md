# Trace Format — Shadow Fight 2 Behavior Capture

A **trace** is a JSON document recording one self-contained gameplay scenario
from the original Shadow Fight 2 game. It is the ground truth our
reverse-engineered engine must reproduce bit-for-bit (within documented
tolerances).

The format is deliberately flat and human-readable so traces can be inspected,
diffed, and hand-edited.

## File layout

```
reverse/traces/<scenario_name>.json
```

Each file is one scenario. The name is a short snake_case description of the
behaviour under test (`block_when_idle`, `walk_and_punch`, `ai_retreat_low_hp`,
...).

## Top-level schema

```jsonc
{
  "$schema":   "trace/1",
  "scenario":  "block_when_idle",
  "version":   "1.0.0",              // trace format version
  "source":    "sf2_android_2.46",   // which APK / build produced it
  "fps":       60,                   // playback rate — fixed, deterministic
  "dt_ms":     16,                   // milliseconds per tick (1000/fps)
  "duration_ms": 1200,               // total scenario length
  "seed":      0,                    // RNG seed (0 = use captured stream)

  "inputs":          [ ... ],
  "expected_states": [ ... ],
  "function_calls":  [ ... ],
  "tolerances":      { ... }
}
```

## `inputs` — replay commands

Each entry is an event to inject into the engine at the given timestamp.
The replay harness must deliver the event **no later than** the frame whose
start time is `ceil(t / dt_ms) * dt_ms`.

```jsonc
{
  "t":    0,        // ms since scenario start
  "type": "key_down" | "key_up" | "pointer_down" | "pointer_up",
  "key":  "D",      // USB HID name (see engine/platform/platform.hpp Key enum)
                     // omitted for pointer events
  "x":    640.0,    // pointer events only — window-space X
  "y":    360.0,    // pointer events only — window-space Y
  "id":   0         // pointer id (multi-touch)
}
```

### Supported key names

The replay harness maps these to `platform::Key`:

```
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
Space Enter Escape Tab ArrowUp ArrowDown ArrowLeft ArrowRight
ShiftLeft ShiftRight CtrlLeft CtrlRight
Num0 Num1 Num2 Num3 Num4 Num5 Num6 Num7 Num8 Num9
F1 .. F12
```

## `expected_states` — assertions

Each entry is a snapshot the engine must match at frame `floor(t / dt_ms)`.

```jsonc
{
  "t":      16,
  "side":   "player" | "enemy",        // default "player"
  "move_state":   0,                   // 0 idle, 1 walk_back, 2 walk_fwd, ...
  "is_blocking":  true,
  "pos_x":        690.0,
  "pos_y":        0.0,
  "facing_right": true,
  "hp_frac":      0.95,                // hp_cur / hp_max
  "anim":         "high_block"         // human-readable anim name
}
```

### Allowed tolerances

The replay harness uses the `tolerances` block to decide how close is "close
enough". Defaults apply when the field is absent.

```jsonc
"tolerances": {
  "pos_x_eps":     1.0,    // world units
  "pos_y_eps":     1.0,
  "hp_frac_eps":   0.01,   // 1 %
  "move_state_eps": 0      // must match exactly
}
```

## `function_calls` — observed AI / logic decisions

These are informational. The replay harness logs them but does NOT assert on
them, because our engine may implement the same decision differently. They
exist so reviewers can compare side-by-side.

```jsonc
{
  "t":        16,
  "function": "block_decision",
  "side":     "player",
  "result":   "high_block",
  "score":    0.85,
  "context":  { "dist": 120.0, "enemy_anim": 7, "my_hp_frac": 0.4 }
}
```

## Rules for capture

1. **Fixed timestep.** The original game runs at 60 Hz on most devices. The
   capture script samples every `Model::tick` exit; if the device ran slower,
   the `dt_ms` field is set to the observed value (e.g. 33 ms for 30 Hz).
2. **Start from a known state.** Every scenario must begin with both fighters
   in `stance_idle` at full HP, `move_state == 0`. The capture script emits a
   `state` record at t=0 so the replay harness can verify the initial
   conditions.
3. **No cross-scenario bleed.** If the scenario ends (KO, timer, exit), the
   capture stops. The `duration_ms` field records exactly how long the
   scenario lasted.
4. **Deterministic RNG.** If the scenario uses randomness (AI decisions, crit
   chance), the capture records the `seed` so the replay can replay the same
   stream.

## Converting a captured trace

The Frida script outputs raw records. To produce a `.json` trace file:

```python
# pseudocode — run in the Frida REPL or a small Python wrapper
import json
raw = json.loads(rpc.exports.getTraces())
out = {"$schema": "trace/1", "scenario": "block_when_idle",
       "inputs": [], "expected_states": [], "function_calls": []}
for r in raw:
    if r["type"] == "key":
        out["inputs"].append({"t": r["t"],
            "type": "key_down" if r["data"]["pressed"] else "key_up",
            "key":  keyCodeToName(r["data"]["key"])})
    elif r["type"] == "state":
        p = r["data"]["player"]
        out["expected_states"].append({"t": r["t"], "side": "player",
            "move_state": p["move_state"],
            "is_blocking": p["is_blocking"],
            "pos_x": p["pos_x"],
            "facing_right": p["facing_right"],
            "hp_frac": p["hp_cur"] / p["hp_max"]})
    elif r["type"] == "ai_decision":
        out["function_calls"].append({"t": r["t"], "function": "block_decision",
            "result": choiceName(r["data"]["choice"]),
            "score": r["data"]["score"]})
open("reverse/traces/block_when_idle.json", "w").write(json.dumps(out, indent=2))
```

The `reverse/traces/CAPTURE_WORKFLOW.md` file documents this end-to-end.
