# HANDOFF — reSF2 (Shadow Fight 2 clean-room engine)

> Single source of truth for the current state of the repo. This file was
> previously self-contradictory (top said FIXED, bottom repeated an older
> "not working" block for the same items). It has been reconciled to reflect
> the real HEAD `249b1a8`. Anything not proven against the original binary or
> a real run is tagged **[HEURISTIC-TODO]**.

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

## Subsystem status (last audit, HEAD 249b1a8)

| Subsystem | Done | State |
|---|---:|---|
| Input | 75% | Win32 `GetAsyncKeyState` polling on Windows, GLFW callbacks on Linux. **The two are never mixed** — mixing them reintroduces the repeated-keypress bug. Do not revert this. |
| State/Move Manager | 45% | Hand-rolled FSM with magic states 0/1/2/10/11. `current_move_` can outlive the attack that set it. **[HEURISTIC-TODO]** replace with the original `Model::step` state logic (`scripts/dz_model_step_decompiled.c`). |
| Jump / Y | 35% | No physical Y position/velocity. `y_adjust` is a global constant that grounds standing/crouch/jump but floats rolls. **[HEURISTIC-TODO]** per-move contact/pivot alignment (MoveInside). |
| Root motion | 55% | X = absolute NPivot displacement, committed on loop wrap. Y-alignment is heuristic. |
| Skeletal animation | 70% | `.bin` decoded (u32 count + per-frame nodes, absolute coords). transition/MidFrames/FirstFrame semantics **[HEURISTIC-TODO]**. |
| Verlet bag | 60% | 15 nodes / 23 constraints / Node12 fixed match data. Full `ModelPhysics` (collisions/passive/weak/cloth) not implemented. |
| Combat / hit detection | 40% | moves.xml intervals read; collision reduced to distance endpoint→bag node. No enemy capsules / block / reaction / damage pipeline. |
| Rendering | 55% | Capsules/triangles/atlas/parallax OK. Rotated pre-cropped frames wrong; sprite batch flushes per quad. |
| DZ archives | 40% | Container (type 1/2/8) parsed 1:1. **type 4 decoder is speculative** (LZMA-like range coder, NOT byte-proven). Falls back to pre-extracted files on disk. |
| Scene/State manager | 30% | Boot/Loading/MainMenu implemented; Map/Shop/Settings/Results are stubs. |
| Story/content | 10% | Placeholder levels and dialogue lines. |
| Save system | 10% | Temp JSON stub; real `localSettings.bin`/AES/`UserDefault.xml` not implemented. |

## Known open bugs (verified against HEAD, not yet fixed)

1. **Stale `current_move_`.** After a non-loop attack ends, the animation is
   already `step_forward`/`stance_idle` but `current_move_` still points at the
   finished attack, so the combat matcher treats the next press as a combo
   continuation (`in_basic_attack` stays true). Fix direction: gate
   `in_basic_attack` on the attack animation actually still playing, and clear
   `current_move_`/`is_uninterrupt_`/`bag_hit_` when `hit_anim_` reaches 0.
2. **Jump Y is a global constant.** `y_adjust=4` grounds jump but floats roll
   (~84px). Needs per-move contact point. **[HEURISTIC-TODO]**
3. **DZ type 4 decoder unproven.** Anchors: flag=4, first block byte `0x1D`.
   Decompiled candidates live in `scripts/dz_*_decompiled.c`
   (e.g. `dz_read_file_decompiled.c`, `dz_open_files_decompiled.c`). Needs
   byte-level validation against real `.dz` files. **[HEURISTIC-TODO]**
4. **Asset path/list hardcoding.** Several loaders enumerate fixed paths/lists
   instead of reading what the archive/manifest declares. Audit and make
   dynamic where the original enumerates content. **[HEURISTIC-TODO]**

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

## Next entry points

1. Stale `current_move_` guard + full state clear on attack end.
2. Replace `y_adjust` constant with per-move contact alignment.
3. Byte-validate DZ type 4 against real archives.
4. De-hardcode asset enumeration.
5. Extend `[ROOT]` logging to include player_x, player_y, npivot_x, npivot_y,
   render_y and frame index (needed to diagnose the Y bug from a real run).
