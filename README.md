# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 9 — Scene Manager + Combat + Movement + Special Moves

### What works ✅
- Window 1280×720, GLFW, OpenGL ES 2.0 renderer
- **Win32 input fix** — `GetAsyncKeyState()` bypasses GLFW's spurious RELEASE events on Windows 10 (build 19044)
- **Scene/State Manager** — 9 scenes (Boot, Loading, MainMenu, Map, Shop, Settings, Dialogue, Battle, Results) with deferred transitions
- **Character body mesh** from body.xml (82 capsules + 29 triangles as dark silhouette)
- **Skeletal animation** — 38 animations loaded from .bin files
- **Animation interpolation** — smooth blending between frames with alpha interpolation
- **Root motion (X)** — delta accumulation with wrap-around filter (+66 per step_forward loop, +404 per forward_roll)
- **Root motion (Y)** — delta accumulation for jumps/flips, smoothly decays to 0 on landing
- **Mirror/facing** — character auto-faces enemy; root motion inverted when facing left
- **Key latching** — 100ms window compensates for transient GetAsyncKeyState false readings
- **Verlet physics** for punching bag — gravity, distance constraints, damping
- **Hit detection** — uses moves.xml Attack intervals (Start/End frames), 70px threshold
- **Combat system** — Punch (O), Kick (P) with direction modifiers (W/A/S/D)
- **Special moves** — Jump (W), FrontFlip (W+D), BackFlip (W+A), ForwardRoll (S+D), BackRoll (S+A)
- **Duck** — crouch on S tap
- **Block** — automatic (when idle, not attacking) — as in original game
- Dojo location rendering (parallax background, Y-inverted, pre-cropped rotated frames)
- HUD with real game textures (gold, energy, level bar)
- Scroll/parchment menu with expand/collapse animation (300ms smoothstep)
- Menu item clicks trigger scene transitions
- Save system (JSON stub) — writes to temp_directory_path()/resf2_save.json

### What's broken ❌
- **DZ archive decompression** — container format decoded, streaming compression (arithmetic coding) not yet implemented. Use `dzip.exe` workaround to extract assets on Windows.
- **Some background textures rotated** — pre-cropped rotated frames work for location but may need formula adjustment
- **Profile menu icon** — has parts of other buttons (rotation formula needs adjustment)
- **No real enemy** — punching bag stands in for opponent (Battle scene uses same dojo)
- **No real dialogue/map/shop content** — stubs only, awaiting DZ extraction

### Controls (original SF2 layout)
| Key | Action |
|-----|--------|
| W | Up — Jump (W+D=front flip, W+A=back flip) |
| A | Left — Back step (relative to facing) |
| S | Down — Duck (tap), or Sweep/LowPunch/Elbow/DodgeKick when attacking |
| D | Right — Forward step (relative to facing) |
| O | Punch (D=heavy, A=spinning, W=upper, S=low, S+A=elbow strike) |
| P | Kick (D=front, A=back, S=sweep, S+D=dodge reverse kick, S+P+P=double sweep) |
| S+D | Forward roll (dodge) |
| S+A | Back roll (dodge) |
| S (tap) | Duck (crouch) |
| Block | AUTOMATIC (when idle, not attacking) |
| M | Toggle scroll menu |
| T | Toggle dialog overlay |
| N | New Game — go to Map |
| Y/L | Declare victory/defeat (Battle, debug) |
| 1/2/3 | Zoom presets |
| Esc | Quit / close overlay / back |

### S3E Binary Analysis
Two binary versions analyzed:

**Android (ARM)**:
- Architecture: ARMv7 (32-bit)
- Format: XE3U (LZMA-compressed S3E container)
- Loader: libs3e_android.so
- Base address: 0x4A000000
- See `docs/s3e_reverse_engineering.md` and `docs/09_s3e_binary_format.md`

**Windows (x86)** — easier for reverse engineering:
- Architecture: Intel i386 (32-bit)
- Format: PE32 DLL (standard Windows executable)
- Loader: s3e_native.dll (Marmalade SDK runtime)
- No decompression needed — Ghidra loads directly as PE32
- See `docs/s3e_windows_binary_re.md` for full analysis

Key engine classes found in Windows binary:
- `ModelAnimation::mirrorNodes` — skeleton mirroring for facing direction
- `ModelAnimation::getPlayerAnimation` — animation selection
- `Model::setNearestEnemy` — auto-facing logic
- `IntervalAttack::getFactors` — attack damage calculation
- `MoveInside` — root motion via NPivot alignment
- `s3eKeyboardGetState` — Marmalade keyboard API (equivalent to GetAsyncKeyState)

### .bin Animation Format
```
u32 frame_count (LE)
Per frame:
  byte 0: skip byte (1=keyframe, 5=interframe)
  bytes 1-4: u32 node_count (LE)
  bytes 5+: node_count × 3 floats (X, Y, -Z) LE
Node order: ALL skeleton.xml nodes in XML order (67 total)
Positions: ABSOLUTE world space. Local = abs - NPivot_world.
```

### Root Motion Implementation
- **X axis**: delta accumulation using interpolated NPivot X. Wrap-around (loop boundary) filtered by threshold 30. Delta inverted when facing left.
- **Y axis**: delta accumulation for jump/flip animations. Smoothly decays to 0 when switching to non-jump animation.
- **Key animations**: step_forward (+66/loop), step_back (-66/loop), forward_roll (+404), back_roll (-350), jump (Y: 0→+138→0), front_flip (X: +366, Y: 0→+145→0), back_flip (X: -334, Y: asymmetric).

### DZ Archive Format (container decoded, compression pending)
- Magic: `DTRZ`, container with file table
- All files type=4 (DZ) in files.dz, type=8 (DZ variant) in animations.dz
- DZ is a **STREAMING compressor** — file offsets overlap, entire data section is one continuous compressed stream
- Algorithm: arithmetic/range coding + 5-byte context window + CRC32 hash + LZ77 matches
- Entropy: 7.5-7.9 bits/byte (real compression, not XOR)
- Blocked on: ARM emulation needs full Marmalade runtime
- Workaround: use `dzip.exe` on Windows to extract assets
- See `engine/reverse/dz/README.md` for full details

### Build
```bash
# Windows (user's machine)
build.bat  # cmake + MSVC, produces resf2_app.exe
resf2_app.exe --assets <path_to_sf2_assets>

# Linux compile check (no linking — just verifies code compiles)
bash scripts/verify_main_compile.sh
```

### File Structure
```
main.cpp              — Game logic + SceneHost (~3100 lines)
engine/scene/         — Scene/State Manager (9 scenes)
engine/platform/      — GLFW platform (Win32 GetAsyncKeyState fix)
engine/renderer/      — OpenGL renderer
engine/reverse/       — Plist atlas, bitmap font, s3e container, DZ notes
engine/runtime/       — Main loop
assets/models/        — skeleton.xml, body.xml, punching_bag.xml
assets/animations/    — moves.xml + binary/ (555 .bin files)
docs/                 — S3E reverse engineering docs (Android + Windows)
scripts/              — Python diagnostic + analysis scripts
HANDOFF.md            — Detailed handoff for next session
worklog.md            — Full work history
```

### Resources
- APK: `https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk`
- Game data: `https://chat.chobat.ru/sf2.7z`
- Marmalade-Modding: `https://github.com/knot126/Marmalade-Modding`
- S3ELoader (Ghidra): `https://github.com/knot126/S3ELoader`
- Original Windows files: `work/original_windows/` (s86 PE32 + s3e_native.dll)

### Next Steps
1. **Load s86 into Ghidra** — decompile `ModelAnimation::mirrorNodes`, `Model::setNearestEnemy`, `MoveInside` to understand exact facing/root-motion logic
2. **Implement DZ decompression** — either port from Ghidra decompilation of `libs3e_android.so:0x389f8` or use `dzip.exe` workaround
3. **Add real enemy** — load enemy character model + AI (currently punching bag only)
4. **Real dialogue/map content** — extract from DZ archives once decompression works
5. **Fix rotated textures** — profile menu icon + some location backgrounds (Task 2)
