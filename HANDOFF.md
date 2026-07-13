# HANDOFF — reSF2 (Shadow Fight 2 clean-room engine)

> Single source of truth for the current state of the repo. This file was
> previously self-contradictory (top said FIXED, bottom repeated an older
> "not working" block for the same items). It has been reconciled to reflect
> the real HEAD `249b1a8`. Anything not proven against the original binary or
> a real run is tagged **[HEURISTIC-TODO]**.
>
> **Updated (jump-under-floor fix + roll wrapping analysis)** to HEAD `b86c453`.
> This session: found root cause of jump-under-floor regression (interim
> formula applied negative y_adjust during crouch phase), fixed with
> upward-only clamp. Verified numerically: jump ry[-89..-17] (was [-161..-17]).
> Roll wrapping root cause documented (NToe lifts 79px above floor during
> roll mid because y_adjust doesn't compensate for NPivot descent).
> [HEURISTIC-TODO] grounded contact fix not implemented (MoveInside not closed).
> See "Session changelog (jump fix + roll analysis)" at the bottom.

## Project

Clean-room reimplementation of the Shadow Fight 2 engine (APK v1.9.21) in
C++23 / OpenGL ES 2.0-style GL. Marmalade SDK-style architecture; the original
is Cocos2d-x on top of Marmalade. The original x86_64 PIE S3E binary and the
Windows PE32 (`ShadowFight2.s86`) are partially reversed (see
`docs/s3e_reverse_engineering.md` and `scripts/*_decompiled.c`).

## External references

- APK: https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk
- Game data: https://chat.chobat.ru/sf2.7z
- S3ELoader (Ghidra loader): https://github.com/knot126/S3ELoader
- Marmalade-Modding: https://github.com/knot126/Marmalade-Modding

## Build

See `BUILD.md`. Summary:

- Windows: `build.bat` (CMake + VS 2022), output `build\bin\Release\resf2_app.exe`.
- Linux: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
- GLFW 3.4 and zlib are auto-fetched via CMake FetchContent.
- Run: `resf2_app --assets <path>`.
- CI: `.github/workflows/linux-build.yml` builds the whole tree on
  ubuntu-latest (GCC) and runs ctest. NOTE: CI is a GCC/Linux smoke test;
  the shipping target is MSVC/Windows, so a green Linux build does not by
  itself guarantee the Windows build and vice versa.

## Subsystem status (audited this session, HEAD cd17d38)

Percentages reflect MEASURED behavior, not documentation/logging/discovery.

| Subsystem | Done | State |
|---|---:|---|
| Input | 75% | Win32 `GetAsyncKeyState` (Win) / GLFW (Linux). [DIAGNOSTIC] `--input-script`/`--max-frames` + `[INPUT_DECISION]` + `[HIT_CHECK]` logs. 15 jittered scenarios tested — "hit without animation" NOT reproduced on Linux. |
| State/Move Manager | 50% | `in_basic_attack` honest. O/P from idle/step verified (13 scenarios). **[HEURISTIC-TODO]** replace denylist with original template check. |
| Jump / Y | 42% | **INTERIM FIX**: NPivot Y displacement for airborne (jump rises: ry[-184..-17]). [ORIGINAL] MoveInside consumption FULLY TRACED: Model+0x80 = scaled pos, Model+0x98 = displacement Vec3, fcn.1028e890 = Vec3 subtract. [ORIGINAL] moveInside+0x68 is RUNTIME-COMPUTED (not from moves.xml — all jump/flip/roll have identical Align). **[HEURISTIC-TODO]** implement verified formula; trace where +0x68 is written. |
| Root motion | 50% | `!anim_loop_` removed (12778f8). stance_2 px stays -293. Whitelist: step/roll/jump/flip/air-attack. **[HEURISTIC-TODO]** make data-driven. |
| Hit detection | 42% | Y-sync fixed (y_adjust in update_animation). `[HIT_CHECK]` log verifies match=1. Jittered input (15 variants) — 0 bug frames. **Windows-specific bug** not reproduced. |
| Skeletal animation | 70% | `.bin` decoded. **[HEURISTIC-TODO]** transition/MidFrames/FirstFrame. |
| Verlet bag | 60% | 15 nodes / 23 constraints / Node12 fixed. |
| Rendering (build) | 65% | Linux/GCC 0 errors. Runtime: rotated pre-cropped frames wrong. |
| DZ archives | 40% | Container parsed. Type-4 @ 0x389f8 verified, NOT traced. |
| Scene/State manager | 30% | Boot/Loading/MainMenu; Map/Shop/Settings/Results stubs. |
| Story/content | 10% | Placeholder. |
| Save system | 10% | Temp JSON stub. |

## Known open bugs (verified against HEAD c83d626)

1. **~~Stale `current_move_`~~ → ADDRESSED (96bfce1).** **[HEURISTIC-TODO]** denylist.
2. **Jump/flip Y — INTERIM FIX (82ba8ad/921405b).** NPivot displacement for airborne. [ORIGINAL] consumption formula traced (Model+0x80/0x98). **[HEURISTIC-TODO]** implement verified formula instead of interim; trace moveInside+0x68 write site.
3. **moveInside+0x68 mode selector — NOT data-driven.** [ORIGINAL] all jump/flip/roll have identical Align in moves.xml. Runtime-computed. **[HEURISTIC-TODO]** trace write site (needs Ghidra type analysis).
4. **"Hit without animation" — Windows-specific.** NOT reproduced on Linux (15 jittered variants, 0 bug frames). `[HIT_CHECK]` log in place for Windows diagnosis.
5. **Grounded roll Y — still flat.** Roll uses FEET_FLOOR_OFFSET=4 (no floor-contact correction). **[HEURISTIC-TODO]** depends on Task 2 (moveInside+0x68) — don't patch with constant.
6. **DZ type-4 decoder — NOT working.** **[HEURISTIC-TODO]**.
7. **Asset path/list hardcoding.** **[HEURISTIC-TODO]** (deferred).

## Invariants (do not violate)

- Never mix Win32 `GetAsyncKeyState` with GLFW keyboard callbacks for game
  input. The combination re-triggers held keys every frame (confirmed in
  prior-session logs). Windows = GetAsyncKeyState only; Linux = GLFW callbacks
  only.
- Never leave a silent fallback without a TODO comment and a warning log in
  the code itself (not just in docs).
- Tag every substantive change `[ORIGINAL]` (with binary address/symbol you
  personally verified) or `[HEURISTIC-TODO]` (with what remains to be reversed).
- Commit in compiling steps; do not leave the repo broken between commits.

## Key files

- `main.cpp` — Game class + SceneHost (~3600 lines). Gameplay in
  `host_update_gameplay()`, dojo draw in `host_render_scene()`.
- `engine/platform/glfw_platform.cpp` — Win32 GetAsyncKeyState path (Windows),
  GLFW callback path (Linux). Do not merge the two.
- `engine/scene/` — scene FSM (9 scenes).
- `engine/renderer/` — GL renderer + software renderer.
- `engine/reverse/` — format parsers: `s3e_container`, `plist_atlas`,
  `bitmap_font`, `atf_tactics`, `dz_reader` (container + type 1/2/8),
  `dz_decoder` (type 4, speculative).
- `scripts/*_decompiled.c` — decompiled original functions used as reference
  (Model step, playInfo, key handling, DZ read path, etc.).
- `docs/s3e_reverse_engineering.md` — S3E binary notes.

## Controls (current, original SF2 layout)

| Key | Action |
|---|---|
| W/A/S/D | Up / Left / Down / Right (movement + attack direction) |
| O | Punch (W=upper, S=low, D=heavy, A=spinning, S+A=elbow) |
| P | Kick (S=sweep, D=front, A=back, S+D=dodge reverse) |
| W | Jump (W+D=front flip, W+A=back flip) |
| S+D / S+A | Forward roll / Back roll |
| S (hold) | Duck (crouch) |
| Block | Automatic (when idle, not attacking) |
| M | Toggle menu |
| T | Toggle dialog |
| N | New game (go to Map) |
| Y/L | Declare victory/defeat (debug, in Battle) |
| 1/2/3 | Zoom presets |
| Esc | Quit / close overlay / back |

## Next entry points (updated this session)

1. **MoveInside Y consumption** — trace the 3-step pipeline end-to-end:
   - Step 2: `fcn.10164c20` @ 0x10164c20 — what does `fcn.10103690(node_owner, axis=2, &out)`
     return? What does `fcn.10103e80(animInfo, result)` do with it?
   - Step 3: `fcn.101661d0` @ 0x101661d0 — reads Model+0x58/0x5c; how does it
     produce the final transform?
   - Then implement verified per-axis alignment (X|Z from moves.xml Axis attr).
2. **Root-motion data-driven** — replace the animation-name whitelist with
   MoveDef metadata lookup (is_step/is_jump/is_retreat). Requires a reverse
   map from animation filename to MoveDef.
3. **O/P during step on Windows** — the issue does NOT reproduce on Linux/Xvfb
   with deterministic input. If it reoccurs on Windows, use `[INPUT_DECISION]`
   log (cd17d38) to diagnose. Possible cause: `step_min_played` (400ms threshold)
   may eat taps that arrive too early in a human-speed press.
4. **DZ type 4 decoder** — trace 0x389f8..0x38d00 + range coder 0x37adc.
5. **De-hardcode asset enumeration** — deferred (depends on DZ type-4).

## Session changelog (jump fix + roll analysis, HEAD 2f2a4ca → b86c453)

- `0820bfc` — fix(y): clamp airborne displacement to upward-only (fix jump under floor).
  [HEURISTIC-TODO] Root cause: jump animation starts with NPivot Y=106 (below rest
  169.48). Interim formula y_adjust = 4 + (npy - 169.48) gave -59 at frame 0,
  dragging render_y to -152 (below floor -89). Fix: clamp displacement to >= 0
  (upward only). Verified numerically: jump ry[-89..-17] (was [-161..-17]).
  No regression: roll flat -89, combat flat -89, flip ry[-89..-10].
- `b86c453` — reverse: document roll 'wrapping' root cause (numerically verified).
  [HEURISTIC-TODO] Roll wrapping: NToe lifts 79px above floor during roll mid
  (npy=25, NToe sy=-113 vs floor -193). Root cause: grounded y_adjust=4 (flat)
  doesn't compensate for NPivot descent. Needed: y_adjust = 4 + (npy - 106) for
  grounded (106 = stance baseline, NOT rest 169.48). NOT implemented (MoveInside
  not closed, premature formulas risky per 9450c4f experience).

### Discrepancy with previous report (2f2a4ca)

Previous report claimed "no behavioral changes" — but user saw regressions.
**Explained**: the interim formula (restored in 82ba8ad after session reset)
ALWAYS had the jump-under-floor bug — it was present since 921405b but not
detected because previous testing only checked ry min/max (which showed
variation) without verifying that ry never goes below floor (-89). The
"no regression" verdict was based on incomplete numerical verification.
This session's Python analysis (frame-by-frame y_adjust computation) revealed
the bug. Fixed in 0820bfc.

Both commits pushed to origin/main. `git log --oneline -4`:
```
b86c453 reverse: document roll 'wrapping' root cause (numerically verified)
0820bfc fix(y): clamp airborne displacement to upward-only (fix jump under floor)
2f2a4ca docs: reconcile HANDOFF/worklog with +0x68 write site search
e69ce57 reverse: correct moveInside+0x68 finding — writes are to Model+0x68
```

## Session changelog (+0x68 write site search, HEAD ce7ba32 → e69ce57)

- `e69ce57` — reverse: correct moveInside+0x68 finding — writes are to Model+0x68.
  [ORIGINAL] CORRECTION: 12 writes to [reg+0x68] in Model::step are to
  Model+0x68 (edi=Model), NOT moveInside+0x68 (separate allocation via
  animInfo+0x94 dereference). moveInside+0x68 is first field of a sub-structure
  (pointers at +0x88/+0x94/+0xa0 freed in destructor). Constructor at
  0x10101b00 (found via RTTI vtable at 0x105ac8e8) does NOT init +0x68.
  Constructor has 0 direct call xrefs — inlined or placement new.
  [HEURISTIC-TODO] write site needs Ghidra typed struct analysis.

Task 2 (implement verified formula) BLOCKED — can't implement without
finding how moveInside+0x68 is set. Interim NPivot Y displacement still
active (no regression: jump ry[-183..-17], roll flat -89).

Task 3 ("hit without animation") — Windows logs requested from user.
Not reproduced on Linux (15 jittered variants, 0 bug frames).

## Session changelog (consumption trace, HEAD be3610f → c83d626)

- `82ba8ad` — restore: recover lost commits 921405b/2c27cc1 after session reset.
  Sandbox reset wiped local repo (3 unpushed commits lost). Restored:
  NPivot Y displacement for airborne + Y-sync fix + [HIT_CHECK] logging.
- `3d94e68` — reverse: trace MoveInside consumption formula + mode selector finding.
  [ORIGINAL] Step 3 FULLY TRACED: Model+0x80 = scaled pos, Model+0x98 =
  displacement, fcn.1028e890 = Vec3 subtract. Formula:
    Model+0x80.x = (float)Model+0x54 * moveInside+0xb4 + [ebp-0x28].x
    Model+0x80.y = moveInside+0xb8 + [ebp-0x28].y
  [ORIGINAL] moveInside+0x68 is RUNTIME-COMPUTED (all jump/flip/roll have
  identical Align in moves.xml — no attribute distinguishes grounded/airborne).
- `c83d626` — test: jittered input scenarios for 'hit without animation' diagnosis.
  15 jittered scenarios (±2 frame). Bug NOT reproduced on Linux (0 bug frames
  across 5 close+jitter variants with hits). Confirms Windows-specific.

All 3 commits pushed to origin/main. `git log --oneline -5`:
```
c83d626 test: jittered input scenarios for 'hit without animation' diagnosis
3d94e68 reverse: trace MoveInside consumption formula + mode selector finding
82ba8ad restore: recover lost commits 921405b/2c27cc1 after session reset
be3610f docs: reconcile HANDOFF/worklog with measured behavior (regression recovery)
cd17d38 fix(input): add [INPUT_DECISION] structured logging for O/P diagnosis
```

## Session changelog (regression recovery, HEAD a26567e → cd17d38)

- `551b0c3` — test: deterministic input replay CLI + 10 regression scenarios.
  `--input-script`/`--max-frames` args. `Platform::load_input_script` +
  `tick_input_script` (called from `host_update_gameplay`, NOT `poll_events`,
  so script frames align with gameplay frames). 10 scenarios in Testing/scenarios/.
- `b31681b` — revert(main): disable unverified MoveInside render-Y consumption.
  Recovery commit. Bisect confirmed 9450c4f is the first bad commit (jump
  render_y: -89→-161 during NPivot rise 106→243). Formula disabled, back to
  constant FEET_FLOOR_OFFSET=4. Parser + metadata kept. One-shot stderr warning.
- `12778f8` — fix(root): restrict world root motion to movement moves only.
  Removed `!anim_loop_` from is_root_motion_anim (stance_2 was drifting
  player_x -293→-460). Removed grounded attacks from whitelist (high_punch
  etc. no longer slide the player). Whitelist: step/roll/jump/flip/air-attack.
- `cd17d38` — fix(input): add [INPUT_DECISION] structured logging for O/P
  diagnosis. Machine-distinguishable reject reasons (none/uninterrupt/
  no_candidate). candidate_count tracking. Verified: O and P both work from
  step in Linux/Xvfb (HeavyPunch / FrontKick, reject=none).

All 4 commits pushed to origin/main. `git log --oneline -6`:
```
cd17d38 fix(input): add [INPUT_DECISION] structured logging for O/P diagnosis
12778f8 fix(root): restrict world root motion to movement moves only
b31681b revert(main): disable unverified MoveInside render-Y consumption
551b0c3 test: deterministic input replay CLI + 10 regression scenarios
a26567e docs: update HANDOFF.md + worklog.md to HEAD f329ae5 (Task F)
f329ae5 dz: honest status for type-4 decoder + verified 0x389f8 disasm (Task D)
```

## Session changelog (previous session, HEAD 249b1a8 → f329ae5)

- `5af53b3` — build(linux): fix GCC compile of renderer.cpp + GLFW
  FetchContent on Wayland-less hosts. `<algorithm>` + `GL_GLEXT_PROTOTYPES`
  before `<GL/gl.h>` + GLFW X11-only + full-path GL linking. 0 errors on
  GCC 14.2.0. App runs end-to-end on Xvfb. See Testing/base_log_BEFORE_TaskB.txt.
- `96bfce1` — main: honest in_basic_attack, expanded [ROOT] log, y_adjust
  HEURISTIC-TODO. Three prepared patches applied (bug #1 honest gate,
  per-frame [ROOT] with player_x/y/npivot_x/y/render_y/frame_idx, one-shot
  stderr warning next to `y_adjust_smoothed_ = FEET_FLOOR_OFFSET`).
- `9450c4f` — main: MoveInside pivot-node Y alignment (Task C). Byte-verified
  mechanism at fcn.10165c10 (Model+0x20→animInfo, +0x94→moveInside, +0x70→
  pivotID, +0x5c←node_array[pivotID].Y). Parses `<Pivot Part="NHeel_X"/>` from
  moves.xml (720/858 node-aligned). Heuristic per-frame y_adjust grounds the
  pivot node to floor_y; falls back to constant 4 for Object="Animation".
  See docs/s3e_reverse_engineering.md "MoveInside".
- `f329ae5` — dz: honest status for type-4 decoder + verified 0x389f8 disasm
  (Task D). Corrects the stale "Algorithm reverse-engineered" claim in
  dz_decoder.hpp. Capstone ARM disasm confirms the function exists and
  identifies context struct fields. Full algorithm NOT byte-traced.

All four commits pushed to `origin/main`. `git log --oneline -6`:
```
f329ae5 dz: honest status for type-4 decoder + verified 0x389f8 disasm (Task D)
9450c4f main: MoveInside pivot-node Y alignment (Task C, Y-jump first candidate)
96bfce1 main: honest in_basic_attack, expanded [ROOT] log, y_adjust HEURISTIC-TODO
5af53b3 build(linux): fix GCC compile of renderer.cpp + GLFW FetchContent ...
59a7608 docs: reconcile HANDOFF.md with real HEAD (249b1a8), remove self-contradiction
249b1a8 Fix: key repeat (disable GLFW on Win), jump Y (constant offset), combo trigger
```
