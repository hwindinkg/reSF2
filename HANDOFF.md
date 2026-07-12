# HANDOFF — reSF2 (Shadow Fight 2 clean-room engine)

> Single source of truth for the current state of the repo. This file was
> previously self-contradictory (top said FIXED, bottom repeated an older
> "not working" block for the same items). It has been reconciled to reflect
> the real HEAD `249b1a8`. Anything not proven against the original binary or
> a real run is tagged **[HEURISTIC-TODO]**.
>
> **Updated this session** to HEAD `f329ae5` (4 new commits: `5af53b3`
> Linux/GCC build unblock, `96bfce1` Task B main.cpp patches, `9450c4f`
> MoveInside pivot-node Y alignment, `f329ae5` DZ type-4 honest status).
> See "Session changelog" at the bottom.

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

## Subsystem status (audited this session, HEAD f329ae5)

| Subsystem | Done | State |
|---|---:|---|
| Input | 75% | Win32 `GetAsyncKeyState` polling on Windows, GLFW callbacks on Linux. **The two are never mixed** — mixing them reintroduces the repeated-keypress bug. Do not revert this. |
| State/Move Manager | 50% | Hand-rolled FSM with magic states 0/1/2/10/11. `in_basic_attack` now honest (Task B): gated on `hit_anim_>0` AND `current_anim_` ∉ {stance_idle,fists_idle,step_forward,step_back}. Clear of `current_move_` on `hit_anim_==0` already existed. **[HEURISTIC-TODO]** replace the anim-name denylist with the original `CurrentAnimationName=="1key"|"2key"` template check once Model::step is byte-decoded. |
| Jump / Y | 45% | MoveInside pivot-node Y alignment implemented (Task C): parses `<Pivot Part="NHeel_X"/>` from moves.xml (720/858 moves node-aligned, 57 animation-only), computes per-frame `y_adjust` to ground the pivot node to floor_y. **[ORIGINAL]** mechanism byte-verified (fcn.10165c10); **[HEURISTIC-TODO]** consumption formula NOT byte-confirmed (see docs/s3e_reverse_engineering.md "MoveInside"). Falls back to constant 4 for `Object="Animation"` moves. |
| Root motion | 60% | X = absolute NPivot displacement, committed on loop wrap. Y-alignment now per-move (Task C). `[ROOT]` log expanded (Task B): per-frame `f/anim/fi/px/py/npx/npy/ry/face`. |
| Skeletal animation | 70% | `.bin` decoded (u32 count + per-frame nodes, absolute coords). transition/MidFrames/FirstFrame semantics **[HEURISTIC-TODO]**. |
| Verlet bag | 60% | 15 nodes / 23 constraints / Node12 fixed match data. Full `ModelPhysics` (collisions/passive/weak/cloth) not implemented. |
| Combat / hit detection | 40% | moves.xml intervals read; collision reduced to distance endpoint→bag node. No enemy capsules / block / reaction / damage pipeline. |
| Rendering (build) | 65% | Linux/GCC build unblocked (Task A): `<algorithm>` + `GL_GLEXT_PROTOTYPES` before `<GL/gl.h>` + GLFW X11-only + full-path GL linking. 0 errors on GCC 14.2.0. Runtime rendering unchanged (rotated pre-cropped frames still wrong; batch flushes per quad). |
| DZ archives | 42% | Container (type 1/2/8) parsed 1:1. **type 4 decoder function verified to exist at 0x389f8 (ARM mode, capstone disasm)** but algorithm NOT byte-traced (Task D). Implementation remains speculative; falls back to pre-extracted files on disk. See docs/s3e_reverse_engineering.md "DZ type 4 decoder". |
| Scene/State manager | 30% | Boot/Loading/MainMenu implemented; Map/Shop/Settings/Results are stubs. |
| Story/content | 10% | Placeholder levels and dialogue lines. |
| Save system | 10% | Temp JSON stub; real `localSettings.bin`/AES/`UserDefault.xml` not implemented. |

## Known open bugs (verified against HEAD f329ae5)

1. **~~Stale `current_move_`~~ → ADDRESSED (Task B, commit 96bfce1).**
   `in_basic_attack` now requires `hit_anim_>0` AND the current animation
   not being a neutral/idle anim. The clear of `current_move_` on
   `hit_anim_==0` already existed (lines ~1037/1225/1236). **Remaining
   [HEURISTIC-TODO]**: replace the animation-name denylist with the exact
   original `CurrentAnimationName=="1key"|"2key"` template predicate once
   Model::step/MoveInside is byte-decoded.
2. **Jump/roll Y — PARTIALLY ADDRESSED (Task C, commit 9450c4f).**
   MoveInside pivot-node grounding implemented (binary mechanism
   byte-verified at fcn.10165c10; consumption formula NOT byte-confirmed).
   Runtime Y-effect during combat NOT verified (idle run doesn't enter
   combat; no xdotool for input injection). floor_y hardcoded to dojo
   (-193). **[HEURISTIC-TODO]** trace the align_y (Model[0x5c]) consumption
   in Model::step/render to confirm the formula.
3. **DZ type 4 decoder unproven — PARTIALLY VERIFIED (Task D, commit f329ae5).**
   Decoder function at 0x389f8 verified to exist (ARM mode), context struct
   fields +0x14/+0x24/+0x28/+0x48 identified. Full algorithm NOT traced.
   **[HEURISTIC-TODO]** disassemble 0x389f8..0x38d00 + range coder 0x37adc
   + bit-tree 0x3751c, prove byte-identical output on a real .dz block.
4. **Asset path/list hardcoding.** Several loaders enumerate fixed paths/lists
   instead of reading what the archive/manifest declares. Audit and make
   dynamic where the original enumerates content. **[HEURISTIC-TODO]**
   (Task E, deferred — depends on DZ type-4 completion.)

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

1. ~~Stale `current_move_` guard~~ — DONE (Task B, commit 96bfce1). Remaining:
   replace the animation-name denylist with the original
   `CurrentAnimationName=="1key"|"2key"` template predicate once
   Model::step/MoveInside is byte-decoded.
2. ~~Replace `y_adjust` constant with per-move contact alignment~~ — PARTIALLY
   DONE (Task C, commit 9450c4f). MoveInside mechanism byte-verified at
   fcn.10165c10; pivot-node grounding implemented. Remaining: trace the
   align_y (Model[0x5c]) consumption formula in Model::step/render; verify
   runtime Y-effect during a triggered combat move (needs input injection —
   no xdotool in this sandbox); generalize floor_y beyond the dojo.
3. ~~Byte-validate DZ type 4 against real archives~~ — PARTIALLY DONE (Task D,
   commit f329ae5). Decoder function at 0x389f8 verified (ARM mode, capstone),
   context struct fields identified. Remaining: trace the full decode loop
   (0x389f8..0x38d00) + range coder (0x37adc) + bit-tree (0x3751c); prove
   byte-identical output on a real `.dz` block (e.g. `settings.xml`).
4. De-hardcode asset enumeration (Task E, deferred — depends on DZ type-4).
5. ~~Extend `[ROOT]` logging~~ — DONE (Task B, commit 96bfce1). Per-frame
   `f/anim/fi/px/py/npx/npy/ry/face` (291 [ROOT] lines in a 15s idle run vs
   1 line before). See Testing/after_log_AFTER_TaskB.txt.

## Session changelog (this session, HEAD 249b1a8 → f329ae5)

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
