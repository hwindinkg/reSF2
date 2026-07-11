# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 9 — Scene Manager + Combat + Movement + RE

### What works ✅
- Window 1280×720, GLFW, OpenGL ES 2.0 renderer
- **Win32 input fix** — `GetAsyncKeyState()` bypasses GLFW's spurious RELEASE events
- **Scene/State Manager** — 9 scenes (Boot, Loading, MainMenu, Map, Shop, Settings, Dialogue, Battle, Results)
- **Character rendering** — 82 capsules + 29 triangles as dark silhouette
- **Skeletal animation** — 42 animations from .bin files, 30fps with interpolation
- **Root motion (X)** — absolute positioning: `player_pos = step_start + displacement`
- **Root motion (Y)** — absolute offset from frame 0 NPivot Y for jumps/flips
- **Mirror/facing** — locked at animation start via `anim_facing_right_`
- **Verlet physics** for punching bag (gravity, constraints, impulse on hit)
- **Hit detection** — moves.xml Attack intervals, 70px threshold
- **Combat** — O=punch, P=kick with W/A/S/D direction modifiers
- **Special moves** — Jump, FrontFlip, BackFlip, ForwardRoll, BackRoll, Duck
- **Wall boundaries** — parsed from params.xml `Wall="305"`, `Width="1960"`
- **Camera** — Y=-50 to show floor properly, follows player X
- Dojo location rendering (parallax, Y-inverted, pre-cropped rotated frames)
- HUD (gold, energy, level bar), scroll/parchment menu, save system (JSON)

### What's broken ❌
- **Occasional teleport-back** — character sometimes snaps to animation start (root motion edge case)
- **DZ archive decompression** — streaming compression (arithmetic coding) not yet implemented
- **No real enemy** — punching bag only (no AI opponent)
- **No real dialogue/map/shop** — stubs only, awaiting DZ extraction
- **Rotated textures** — profile menu icon + some location backgrounds
- **Bag Y position** — may still be slightly off

### Controls (original SF2 layout)
| Key | Action |
|-----|--------|
| W | Jump (W+D=front flip, W+A=back flip) |
| A | Left — Back step (relative to facing) |
| S | Duck (tap), or Sweep/LowPunch/Elbow/DodgeKick when attacking |
| D | Right — Forward step (relative to facing) |
| O | Punch (D=heavy, A=spinning, W=upper, S=low, S+A=elbow) |
| P | Kick (D=front, A=back, S=sweep, S+D=dodge reverse, S+P+P=double sweep) |
| S+D | Forward roll (dodge) |
| S+A | Back roll (dodge) |
| M | Toggle scroll menu |
| N | New game (go to Map) |
| Y/L | Declare victory/defeat (Battle, debug) |
| 1/2/3 | Zoom presets |
| Esc | Quit / close overlay / back |

### S3E Binary Analysis
Two binary versions analyzed:
- **Android (ARM)**: XE3U LZMA container, ARMv7, needs S3ELoader plugin
- **Windows (x86)**: PE32 DLL, i386, loads directly in Ghidra/objdump — **easier for RE**

Key engine classes found in Windows binary (s86):
- `ModelAnimation::mirrorNodes` (0x10164093) — skeleton mirroring via XOR 0x80000000
- `ModelAnimation::getPlayerAnimation` (0x1016622A) — position update: `pos = mirrored * speed + old_pos`
- `ModelAnimation::playInfo` (0x101650FC) — animation update chain (6 steps)
- `Model::setNearestEnemy` (0x101586F0) — stores enemy at model+0x190
- `Model::getModelAlign` (0x10159780) — facing direction via switch(type)
- `IntervalAttack::getFactors` (0x10115921) — attack damage calc

**Animation timing**: game uses 1/120 physics timestep (0x3C088889 in binary), animations at 30fps. Original mirrored flag is signed byte: 0xFF=-1 (left), 0x01=+1 (right).

See `docs/s3e_function_analysis.md`, `docs/s3e_windows_binary_re.md`, `docs/ghidra_decompilation_guide.md`.

### Build
```bash
# Windows
build.bat  # cmake + MSVC
resf2_app.exe --assets <path_to_sf2_assets>

# Linux compile check
bash scripts/verify_main_compile.sh
```

### Next Steps
1. **Fix teleport-back** — analyze root motion edge cases via s86 disassembly
2. **Implement DZ decompression** — port from Ghidra decompilation or use dzip.exe
3. **Add enemy** — load second character model, basic AI, hit detection
4. **Real game content** — dialogue, map, shop from DZ archives
5. **Fix rotated textures** — profile icon + location backgrounds
6. **Audio** — s3eAudio API, background music, SFX

### Resources
- APK: `https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk`
- Game data: `https://chat.chobat.ru/sf2.7z`
- Marmalade-Modding: `https://github.com/knot126/Marmalade-Modding`
- S3ELoader (Ghidra): `https://github.com/knot126/S3ELoader`
