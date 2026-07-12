# HANDOFF — reSF2 (Shadow Fight 2 clean-room engine)

> Single source of truth for the current state of the repo. This file was
> previously self-contradictory (top said FIXED, bottom repeated an older
> "not working" block for the same items). It has been reconciled to reflect
> the real HEAD `249b1a8`. Anything not proven against the original binary or
> a real run is tagged **[HEURISTIC-TODO]**.
>
> **Updated (regression recovery session)** to HEAD `cd17d38`. Previous
> session's MoveInside Y heuristic (`9450c4f`) caused jump/flip/roll
> regressions; this session: bisected, disabled the unverified formula,
> fixed root-motion whitelist, added deterministic input replay +
> [INPUT_DECISION] logging, traced MoveInside pipeline to 3-step chain.
> See "Session changelog (regression recovery)" at the bottom.

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
| Input | 75% | Win32 `GetAsyncKeyState` polling on Windows, GLFW callbacks on Linux. **Never mixed.** [DIAGNOSTIC] `--input-script` + `--max-frames` CLI for deterministic replay added (tick from `host_update_gameplay`, not `poll_events`, so script frames align with gameplay frames). [DIAGNOSTIC] `[INPUT_DECISION]` structured log with reject reasons (none/uninterrupt/no_candidate) at every O/P edge. |
| State/Move Manager | 50% | Hand-rolled FSM. `in_basic_attack` honest (gated on `hit_anim_>0` + anim denylist). O/P from idle and step both work (verified with 10 deterministic scenarios). **[HEURISTIC-TODO]** replace denylist with `CurrentAnimationName=="1key"|"2key"` once Model::step is decoded. |
| Jump / Y | 30% | **REGRESSION RECOVERED**: MoveInside Y heuristic (9450c4f) disabled (b31681b) — it caused jump/flip/roll to SINK. Back to constant `FEET_FLOOR_OFFSET=4` (flat render_y during jump). MoveInside pipeline traced to 3-step chain (capture→resolve+axis→consume) but consumption formula NOT byte-confirmed. **[HEURISTIC-TODO]** trace fcn.10164c20 + fcn.101661d0 to close the chain. |
| Root motion | 50% | **FIXED**: `!anim_loop_` removed from whitelist (12778f8) — stance_2 no longer drifts player_x (was -293→-460, now stays -293). Grounded attacks (high_punch etc.) no longer slide the player. Whitelist is now explicit: step/roll/jump/flip/air-attack only. **[HEURISTIC-TODO]** make data-driven from MoveDef metadata. |
| Skeletal animation | 70% | `.bin` decoded. transition/MidFrames/FirstFrame **[HEURISTIC-TODO]**. |
| Verlet bag | 60% | 15 nodes / 23 constraints / Node12 fixed. Full `ModelPhysics` not implemented. |
| Combat / hit detection | 40% | moves.xml intervals read; `[INPUT_DECISION]` logging added. No enemy capsules / block / damage pipeline. |
| Rendering (build) | 65% | Linux/GCC build unblocked (5af53b3). Runtime rendering unchanged (rotated pre-cropped frames wrong; batch flushes per quad). |
| DZ archives | 40% | Container (type 1/2/8) parsed 1:1. Type-4 decoder function @ 0x389f8 verified (ARM mode) but algorithm NOT traced. Falls back to pre-extracted files. |
| Scene/State manager | 30% | Boot/Loading/MainMenu implemented; Map/Shop/Settings/Results are stubs. |
| Story/content | 10% | Placeholder levels and dialogue lines. |
| Save system | 10% | Temp JSON stub. |

## Known open bugs (verified against HEAD cd17d38)

1. **~~Stale `current_move_`~~ → ADDRESSED (96bfce1).** `in_basic_attack`
   honest. **[HEURISTIC-TODO]** replace denylist with original template check.
2. **Jump/roll Y — REGRESSION RECOVERED, not fixed.** MoveInside Y formula
   disabled (b31681b). render_y is flat at -89 during jump (character doesn't
   visually jump). The 3-step MoveInside pipeline is traced (capture→resolve
   →consume) but the consumption formula is NOT byte-confirmed. **[HEURISTIC-TODO]**
   trace fcn.10164c20 (axis retrieval, `push 2`) + fcn.101661d0 (consumer) to
   close the chain, then implement verified per-axis alignment.
3. **DZ type 4 decoder — NOT working.** Function verified, algorithm NOT
   traced. **[HEURISTIC-TODO]**.
4. **Asset path/list hardcoding.** **[HEURISTIC-TODO]** (deferred).

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
