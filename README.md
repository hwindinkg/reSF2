# reSF2 — Clean-room reimplementation of Shadow Fight 2

A reverse-engineered recreation of the Shadow Fight 2 game engine,
built from analysis of the original Android (ARM) and Windows (x86) binaries.

## Current State

### Working ✅
- **Scene/state manager**: 9 scenes (Boot→Loading→MainMenu→Map→Shop→Settings→Dialogue→Battle→Results)
- **Character rendering**: 82 capsules + 29 triangles from body.xml
- **Skeleton animation**: Loads all 556 .bin animation files dynamically
- **Root motion X**: Absolute positioning via NPivot displacement
- **Root motion Y**: NPivot-based Y positioning (standing, crouching, jumping)
- **Mirror/facing**: Animation-locked facing, dynamic facing updates
- **Verlet physics**: Punching bag with distance constraints
- **Hit detection**: Uses moves.xml Attack intervals, checks AttackingParts edges
- **Combat system**: Dynamic move selection from moves.xml templates
- **Move filtering**: Titan moves filtered, weapon subtype locks respected
- **Uninterrupt interval**: New moves blocked only during Uninterrupt (not entire animation)
- **Key handling**: GetAsyncKeyState polling on Windows, GLFW callbacks on Linux
- **DZ archive reader**: Container parser (DTRZ format), gzip type=8 decompression
- **Fallback directories**: Pre-extracted files loaded transparently when DZ decompression fails

### Partially Working ⚠️
- **DZ type=4 decoder**: Range coder + bit-tree structure implemented but probability
  model doesn't match original exactly. Falls back to pre-extracted files.
- **3key combos**: DoublePunch, DoubleSweep work when triggered during basic attack
- **Air attacks**: W+O/W+P during jump transitions to air_punch/air_axe_kick
- **Jump animation**: Y positioning correct but slight visual issues at transition frames

### Not Yet Implemented ❌
- **DZ type=4 full decompression**: Streaming range coder with 5-byte context window
- **Real enemy AI**: Only punching bag (no AI opponent)
- **Dialogue/map/shop content**: Scene shells only, no real content
- **Audio**: No sound playback
- **Magic/ranged weapons**: Only Fists (unarmed) combat

## Architecture

### Engine Structure
```
main.cpp                    — Game logic + SceneHost (~3600 lines)
engine/scene/               — Scene/state manager (9 scenes)
engine/platform/            — GLFW platform (Win32 GetAsyncKeyState support)
engine/renderer/            — OpenGL renderer
engine/reverse/             — Format parsers:
  - s3e_container.cpp       — Marmalade S3E container
  - plist_atlas.cpp         — Cocos2d TexturePacker v2
  - atf_tactics.cpp         — zlib-compressed tactics blob
  - bitmap_font.cpp         — AngelCode BMFont
  - dz_reader.cpp           — DZ archive reader (DTRZ container)
  - dz_decoder.cpp          — DZ type=4 range coder decoder (WIP)
assets/models/              — 72 model XML files
assets/animations/          — moves.xml + binary/ (556 .bin files)
assets/locations/           — 56 location directories
reverse/binaries/           — Original game binaries for RE:
  - ShadowFight2.s86        — Windows PE32 (6.95MB, i386)
  - ShadowFight2_android.bin — Android S3E (XE3U, x86_64)
  - libs3e_android.so       — Marmalade runtime (ARMv7)
scripts/                    — Analysis & decompilation tools
docs/                       — S3E reverse engineering documentation
```

### Original Binary Analysis

**ShadowFight2.s86** (Windows PE32, i386):
- Contains full game code, readable via objdump/radare2
- Key function addresses:
  - `0x10164fa0` — playInfo (animation update)
  - `0x10161ad0` — Model.step (Uninterrupt check)
  - `0x10161350` — Model.update
  - `0x100b9ff0` — Fight.update (main battle loop)
  - `0x100875a0` — ConditionKeys.virtual_8 (key condition check)
  - `0x10121e10` — Key array comparison
  - `0x10103d50` — Interval check (Uninterrupt/SemiUninterrupt)
  - `0x102c9778` — DZ archive reader
  - `0x102c9fbf` — DZ file read
  - `0x102ca66b` — DZ file table reader

**libs3e_android.so** (ARMv7):
- Contains DZ decoder at `0x389f8`
- Range coder at `0x37adc`
- Bit-tree decoder at `0x3751c`

## How to Build

### Windows
```
build.bat
```
Requires: CMake, MSVC, Windows SDK

### Linux (compile check only)
```
bash scripts/verify_main_compile.sh
```

## How to Run

```
resf2_app.exe --assets <path_to_assets>
```

Use `--assets E:\reSF2` to use the repository's pre-extracted assets.
Use `--assets E:\reSF2\sf2\assets` to use original game assets (requires DZ decompression).

## Controls (Original SF2 Layout)

```
W/A/S/D     — Up / Left / Down / Right (movement + attack direction)
O           — Punch (W=upper, S=low, D=heavy, A=spinning, S+A=elbow)
P           — Kick (S=sweep, D=front, A=back, S+D=dodge reverse)
W           — Jump (W+D=front flip, W+A=back flip)
S+D / S+A   — Forward roll / Back roll
S (hold)    — Duck (crouch)
Block       — Automatic (when idle, not attacking)
M           — Toggle menu
T           — Toggle dialog
1/2/3       — Zoom presets
Esc         — Quit
```

## Tools

### Decompilation
- `scripts/decompile_original.sh` — Decompile functions from ShadowFight2.s86 using radare2
- `scripts/verify_engine_code.py` — Verify engine code against original patterns
- `scripts/dz_*.py` — DZ archive analysis and decoder test scripts
- `scripts/dz_*_decompiled.c` — Decompiled original functions (30+ files)

### Verification
- `scripts/verify_main_compile.sh` — Compile check (Linux, no linking)
- `scripts/verify_engine_code.py` — Code pattern verification

## Next Steps

1. **DZ type=4 decoder**: Fix probability model to match original ARM code
2. **Combat timing**: Verify Uninterrupt intervals match original exactly
3. **Move transitions**: Implement proper transition frames from moves.xml
4. **Enemy AI**: Port AI logic from original Fight.update
5. **Audio**: Add sound playback from .wav files
6. **More weapons**: Support weapon-specific moves beyond Fists

## Reverse Engineering Documentation

See `docs/` for detailed RE notes:
- S3E binary format, JNI registration map
- Engine architecture, main loop analysis
- Animation format (.bin), tactics format (.atf)
- Resource formats (.dz, .plist, .fnt)
- DZ format analysis and decoder implementation notes
