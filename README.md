# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 7.12 — Per-node animation from .bin files (partial mapping)

### What works
- ✅ Window 1280×720, ESC to exit, A/D movement, Space hit, M menu, T dialog
- ✅ Renderer: GLSL shaders, texture loading (PNG+JPEG), sprite batch, 2D camera
- ✅ .plist atlas parsing (TexturePacker v2)
- ✅ .atf tactics loading (zlib decompress)
- ✅ .fnt bitmap font parsing
- ✅ .dz files extracted (via dzip.exe on Windows)
- ✅ Loading screen → Dojo location transition
- ✅ Dojo location rendering from params.xml (parallax background layers)
- ✅ Real 3D punching bag model from skeleton_punching_bag.xml + punching_bag.xml
- ✅ Character body mesh from body.xml (capsules + filled triangles)
- ✅ Skeleton from skeleton.xml (46 nodes, 193 edges)
- ✅ Y-UP coordinate system (cocos2d-x convention)
- ✅ HUD with real Top_Panel/gold/energy/Level_bar textures
- ✅ Scroll/parchment menu with real MenuRoll/Paper textures
- ✅ Sensei dialog overlay
- ✅ **.bin animation format reverse-engineered from .s3e binary**
- ✅ **Per-node skeletal animation (21 of 67 nodes mapped)**
- ✅ **Root motion (lunge forward during air_punch)**
- ✅ All 4 unit tests passing

### Animation system

The .bin animation format was fully reverse-engineered by disassembling the
`ShadowFight2.s3e` binary (ARM code) using capstone. Key findings:

**Format** (verified by disassembly at offset 0x4634E9):
```
u32 frame_count (LE)
frame_count × 809 bytes (one record per frame)

Each 809-byte frame:
  byte 0: type flag (1=keyframe, 5=interframe)
  bytes 1..808: 202 LE floats = 1 padding + 67 nodes × 3 floats (Y, X, Z)
  
  Per-node: float[1 + i*3] = Y (local)
           float[1 + i*3 + 1] = X (absolute = root_x + local_x)  
           float[1 + i*3 + 2] = Z (local or rotation)
  
  node[0] = root (root_x, root_y)
```

**Formula from disassembly**: `count = (file_size - frame_count*5 - 4) / 12`
gives total node-instances; `count / frame_count = 67` nodes per frame.

**Node mapping**: 21 of 67 .bin nodes mapped to skeleton.xml names by matching
(local_x, Y) at frame 0 against skeleton rest pose. The remaining 46 .bin
nodes are unmapped (they animate but we don't know which skeleton node they
correspond to). Unmapped skeleton nodes stay in rest pose during animation.

**What this means in practice**:
- Pressing Space plays `air_punch.bin` — character lunges forward (root motion)
  and mapped nodes (head, knees, elbows, hips, etc.) animate
- Unmapped nodes (shoulders, wrists, ankles, toes) stay in rest pose
- The animation is PARTIAL — full animation requires mapping all 67 nodes

### What's NOT working yet
- ❌ **Full node mapping** — 46 of 67 .bin nodes unmapped (need more RE work)
- ❌ **Combat** — no hit detection, no damage, no AI
- ❌ **Multiple locations** — only Dojo is implemented
- ❌ **Audio** — no sound/music playback

---

## Animation Reverse-Engineering History

The .bin animation format was solved through the following process:

1. **String mining** of `ShadowFight2.bin` (8.7 MB extracted .s3e) found:
   - `Animation load error` at offset 0x7808ed
   - `count % nodeCount != 0` at 0x77f081
   - `ModelAnimation` class name
   - `%s/%08x.bin` filename format

2. **ARM disassembly** using capstone (Python) found the loader function at
   offset 0x4634E9. The code uses the magic number 0xAAAAAAAB for division
   by 12, revealing that each node occupies 3 floats (12 bytes).

3. **Formula verification**: `(file_size - frame_count*5 - 4) / 12 = 2546`
   for air_punch.bin, and `2546 / 38 frames = 67 nodes per frame`.

4. **Layout discovery**: 67 nodes × 3 floats (Y, X, Z) + 1 padding = 202 floats
   per frame. X is stored as ABSOLUTE (root_x + local_x), Y and Z are LOCAL.

5. **Node mapping** (partial): 21 of 67 .bin nodes mapped to skeleton.xml
   names by matching (local_x, Y) at frame 0 against skeleton rest pose.
   See `kBinNodeNames[]` in main.cpp for the full mapping table.

### Remaining work for full animation
- Map the remaining 46 .bin nodes to skeleton node names
- This requires either:
  - More sophisticated matching (use multiple frames, velocity analysis)
  - Finding the engine's node-name table in the .s3e binary (the order
    in which nodes are stored in the .bin differs from skeleton.xml)
- Once fully mapped, all 46 skeleton nodes will animate correctly

---

## Roadmap

### Phase 1: Core rendering ✅ DONE
- Location rendering, character mesh, HUD, menu, dialog

### Phase 2: Animation (BLOCKED)
- Requires solving the .bin per-node layout (see above)
- Alternative: implement a procedural animation system (not from .bin files)
  as a placeholder while the RE work continues

### Phase 3: Combat
- Parse .atf tactics files (hitbox/hurtbox/damage per weapon pair)
- Physics: rectangle intersection (hitbox vs hurtbox)
- Damage system: HP, knockback, combos
- Basic AI: approach, attack, retreat

### Phase 4: Game flow
- Multiple locations (50+ in original game)
- Stage progression from stages.xml
- Player progression (XP, gold, equipment)
- Save/load from users.xml

---

## Build

```cmd
build.bat rebuild
build\bin\Release\resf2_app.exe --assets E:\reSF2\sf2\assets
```

## Controls

| Key | Action |
|---|---|
| A / D or ← / → | Move player |
| W / S or ↑ / ↓ | Move camera (debug) |
| Space | Hit (visual feedback on punching bag) |
| M or click menu | Toggle scroll menu |
| T | Toggle dialog overlay |
| 1 / 2 / 3 | Zoom presets |
| Esc | Quit (or close menu) |

## Architecture

```
reSF2/
├── engine/
│   ├── platform/     — GLFW window, input, filesystem
│   ├── renderer/     — GLSL shaders, textures, sprite batch, camera
│   ├── runtime/      — main loop, AssetManager
│   ├── reverse/      — format parsers (.s3e, .plist, .atf, .fnt, .dz)
│   ├── physics/      — (future) hitbox/hurtbox
│   ├── animation/    — (future) skeletal animation
│   ├── audio/        — (future) sound mixer
│   └── network/      — (future) SmartFox2X client
├── tests/            — 4 unit tests
├── docs/             — 19 engineering docs (see docs/README.md)
├── main.cpp          — runtime entry point (GLFW interactive)
├── headless_main.cpp — headless driver (software renderer, saves PNGs)
├── build.bat         — Windows build script
└── CMakeLists.txt    — CMake build (auto-downloads GLFW + zlib)
```

## Documentation

Key engineering docs in `docs/`:
- `16_bin_animation_format.md` — .bin animation format analysis (with unsolved node mapping)
- `17_detailed_reverse_plan.md` — detailed RE plan for the engine
- `18_engine_flow_analysis.md` — engine flow from .s3e reverse engineering
- `19_complete_re_plan.md` — complete RE plan v2 (based on PvZ-Portable study)

## Coordinate system

- **Y-UP** (cocos2d-x 2.2.6 convention, origin at bottom-left)
- World center = (0, 0)
- Skeleton local coords: Y-UP (0 = feet, positive = up)
- Texture atlas coords: Y-DOWN (PNG top-left origin), renderer flips V internally
