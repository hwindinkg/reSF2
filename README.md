# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 8 — Character rendering works, gameplay in progress

### What works ✅
- Window 1280×720, GLFW, OpenGL ES 2.0 renderer
- Dojo location rendering (parallax background from params.xml)
- Character body mesh from body.xml (82 capsules + 29 triangles as dark silhouette)
- **Circle-based capsule rendering** (draw_filled_circle_world for capsule caps)
- Punching bag model from skeleton_punching_bag.xml + punching_bag.xml
- Skeleton from skeleton.xml (67 nodes: 54 Node + 1 COM + 12 MacroNode)
- Y-UP coordinate system (cocos2d-x convention)
- HUD with real game textures (Top_Panel, gold, energy, Level_bar)
- Scroll/parchment menu with real MenuRoll/Paper textures
- Sensei dialog overlay
- **.bin animation format fully decoded** (from Gymnast-Tool-Suite plugin)
- **DZ archive extractor** (from Marmalade-Modding, in scripts/dzextract.py)
- moves.xml parser (880+ moves with attack intervals, damage, edges)
- 555 .bin animation files in assets/animations/binary/

### What's broken ❌
- **Animations not loading** — search paths may still be wrong
- **Movement not working** — character mirrors but doesn't move (needs step animations)
- **Combat not working** — needs animations to play punch/kick
- **Bag doesn't react to hits** — needs working combat
- **Bag color** — changed to dark, needs verification

### .bin Animation Format (VERIFIED from Gymnast-Tool-Suite)
```
u32 frame_count (LE)
Per frame:
  byte 0: skip byte (type flag: 1=keyframe, 5=interframe)
  bytes 1-4: u32 node_count (LE)
  bytes 5+: node_count × 3 floats (X, Y, -Z) LE

Coordinate mapping: bin stores (game.X, game.Y, -game.Z)
Node order: ALL skeleton.xml nodes in XML order (67 total)
Positions: ABSOLUTE world space. Local = abs - NPivot_world_position.
```

### Controls
| Key | Action |
|-----|--------|
| A/D | Step left/right (step_forward/step_back animations) |
| Space | HighPunch (or direction+Space for other punches) |
| K | HighKick (or direction+K for other kicks) |
| M | Toggle menu |
| T | Toggle dialog |
| 1/2/3 | Zoom presets |
| Esc | Quit |

### Build
```cmd
git fetch origin
git reset --hard origin/main
build.bat
build\bin\Release\resf2_app.exe --assets E:\reSF2\sf2\assets
```

### Architecture
```
reSF2/
├── engine/
│   ├── platform/     — GLFW window, input, filesystem
│   ├── renderer/     — GLSL shaders, textures, sprite batch, camera
│   │   ├── renderer.cpp/hpp — draw_filled_circle_world (triangle fan)
│   │   └── software_renderer.cpp/hpp — headless renderer
│   ├── runtime/      — main loop, AssetManager
│   └── reverse/      — format parsers (.s3e, .plist, .atf, .fnt, .dz)
├── assets/
│   ├── models/       — skeleton.xml, body.xml, punching_bag.xml, etc.
│   └── animations/
│       ├── binary/   — 555 .bin animation files
│       └── moves.xml — 880+ move definitions
├── scripts/
│   └── dzextract.py  — DZ archive extractor (from Marmalade-Modding)
├── tests/            — 4 unit tests
├── main.cpp          — runtime entry point
├── build.bat         — Windows build script
└── CMakeLists.txt    — CMake build
```

### Key Technical Details

**Search Paths** (exe = `build/bin/Release/`):
- Models: `../../../assets/models/` (3 levels up to repo root)
- Animations: `../../../assets/animations/binary/`
- moves.xml: `../../../assets/animations/`

**Character Rendering**:
- 82 capsules rendered as rectangles + circle caps (draw_filled_circle_world)
- 29 triangles for small parts (feet, hands)
- All same dark color (20,20,25) → unified silhouette
- No skeleton lines overlay

**Animation System**:
- AnimationData: variable-length frames (1 byte skip + u32 count + floats)
- update_animation(): reads all 67 nodes, converts absolute→local via NPivot
- Root motion: step_forward/step_back move character (delta from prev frame)
- Attack animations: no root motion (character stays in place)
