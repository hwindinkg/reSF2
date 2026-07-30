# Capture Workflow — Recording Traces from the Original Game

This document describes the end-to-end process for recording behaviour
traces from the original Shadow Fight 2 Android APK and converting them
into JSON scenarios that our engine must pass.

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| Android phone / emulator | API 21+ | Runs the original game |
| `frida-server` | matching client | Instrumentation host |
| `frida` CLI | 16+ | Host-side driver |
| Shadow Fight 2 APK | 2.46.0 armeabi-v7a | Subject under test |
| Python 3.10+ | — | Trace conversion script |
| This repo | HEAD | Frida script + conversion script |

## Step-by-step

### 1. Prepare the device

```bash
# Push frida-server onto the phone (assumes adb is already connected)
adb push frida-server-<ver>-android-arm /data/local/tmp/frida-server
adb shell "chmod 755 /data/local/tmp/frida-server"

# Start frida-server as root (required for ptrace on modern Android)
adb shell "su -c '/data/local/tmp/frida-server &'"
```

### 2. Launch the game

```bash
adb shell am start -n com.nekki.shadowfight/com.unity3d.player.UnityPlayerActivity
# Wait for the main menu to appear
```

### 3. Attach the capture script

```bash
cd E:\reSF2\reverse\frida_hooks
frida -U -n com.nekki.shadowfight -l frida_trace_capture.js
```

You should see log lines like:

```
[trace] frida_trace_capture.js loaded
[trace] waiting for libgame.so...
[trace] libgame.so @ 0xabcdef00 size=4194304
[trace] libs3e_android.so @ 0x12340000
[trace] hooked pointer callback @ 0xabcdef10
[trace] hooked key callback @ 0xabcdef20
[trace] hooked Model::tick @ 0xabcdef30
[trace] hooked BlockBrain::decide @ 0xabcdef40
```

If any "hook failed" message appears, the offsets in `frida_trace_capture.js`
are wrong for this APK build. Update the `OFF` table and retry.

### 4. Perform the scenario

Navigate in-game to the state you want to capture. Good scenarios are:

- **Idle → block** — wait for an AI attack, hold block, observe transition
- **Walk forward → punch** — press D, release, press Space
- **AI retreat** — let the AI back off when HP is low
- **Combo** — specific sequence of attacks from the moves.xml

Keep scenarios **short** — under 5 seconds. Long scenarios produce large
traces and amplify clock drift.

### 5. Export the trace

In the Frida REPL:

```javascript
// 1. Check how many records were captured
rpc.exports.tracecount()

// 2. Pull the full JSON
var raw = rpc.exports.getTraces();

// 3. Save to disk (Frida's File API writes on the device)
var f = new File("/sdcard/trace_raw.json", "wb");
f.write(raw);
f.close();
```

On the host:

```bash
adb pull /sdcard/trace_raw.json reverse/traces/raw/block_when_idle.raw.json
```

### 6. Convert to the canonical format

The raw trace is a flat list of hook records. Convert it to the canonical
`reverse/traces/<scenario>.json` format with the Python helper:

```bash
python reverse/traces/convert_trace.py \
    --scenario block_when_idle \
    reverse/traces/raw/block_when_idle.raw.json
```

This produces `reverse/traces/block_when_idle.json` in the schema described
in `TRACE_FORMAT.md`.

### 7. Hand-review the trace

Open the JSON in an editor and:

1. Verify `inputs` contains exactly the keys you pressed, with plausible
   timestamps.
2. Verify `expected_states` starts with both fighters in `stance_idle`.
3. Verify the first `key_down` happens after the initial idle has settled
   (t > 200 ms).
4. Remove any spurious records from navigating menus.

### 8. Run the replay test

```bash
cd build
ctest -R test_trace_replay -V
```

The test loads `reverse/traces/block_when_idle.json`, feeds the inputs into
`HeadlessTestRunner`, and asserts on `expected_states`. A failing assertion
means our engine diverges from the original.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "libgame.so not loaded" | Game started too early, script attached before SO map | Wait for main menu, then re-attach |
| No `state` records | Wrong `Model::tick` offset | Re-run stalker scan; update `0x0002f0e0` |
| NaN in `hp_cur` | Fighter pointer was stale | Verify `self + 0x08` is a valid heap pointer |
| Replay fails on first frame | Initial state mismatch | Check `expected_states[0]` — does it match a fresh battle? |
| Timestamps drift by ~10 ms per second | Device ran at 30 Hz | Set `dt_ms: 33` in the JSON header |

## Recording tips

- **Anchor on input events**, not animation frames. Inputs are the only
  thing we control; everything else is observed.
- **Record multiple repetitions.** AI has randomness; capture 3 runs and
  pick the median, or record the RNG seed and use it in replay.
- **Short scenarios beat long ones.** A 200 ms "block when idle" trace is
  more valuable than a 30 s full fight — it isolates one behaviour.
- **Name files after the behaviour, not the input.** `block_when_idle.json`
  is self-documenting; `press_a_d_space.json` is not.
