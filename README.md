# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 8 — Character rendering + Verlet bag physics + Root motion movement

### What works ✅
- Window 1280×720, GLFW, OpenGL ES 2.0 renderer
- **Character body mesh** from body.xml (82 capsules + 29 triangles as dark silhouette)
- **Circle-based capsule rendering** (draw_filled_circle_world for capsule caps)
- **Skeletal animation** — 23 animations loaded from .bin files (fists_idle, step_forward, step_back, high_punch, high_kick, etc.)
- **Animation interpolation** — smooth blending between frames with alpha interpolation
- **Root motion movement** — step_forward/step_back animations drive player position via NPivot offset
- **Step cooldown** — 533ms (16 frames at 30fps) between steps, matching original game
- **Non-looping animation fix** — animations stay at last frame instead of wrapping to frame 0
- **Punching bag Verlet physics** — real physics simulation with gravity, distance constraints, and damping (matches original game's ModelPhysics system)
- **Bag impulse on hit** — applies impulse to specific bag node based on hit height
- **Hit detection** — uses actual moves.xml Attack intervals (Start/End frames) with 60px threshold
- **Hit detection timing** — update_animation runs BEFORE hit detection for frame-accurate limb positions
- Dojo location rendering (parallax background from params.xml, Y-inverted for Y-UP world)
- **Parallax tiling** — background layers tile horizontally to prevent flying off-screen
- Skeleton from skeleton.xml (67 nodes: 54 Node + 1 COM + 12 MacroNode)
- Y-UP coordinate system (cocos2d-x convention)
- HUD with real game textures (Top_Panel, gold, energy, Level_bar)
- Scroll/parchment menu with real MenuRoll/Paper textures
- **Menu expand/collapse animation** (300ms smoothstep easing)
- **Menu icon uniform scaling** — all icons same size via max_texture_dimension scaling
- Sensei dialog overlay
- **.bin animation format fully decoded** (from Gymnast-Tool-Suite plugin)
- **DZ archive extractor** (from Marmalade-Modding, in scripts/dzextract.py)
- **moves.xml parser** (858 moves with attack intervals, damage, edges, keys, blocks)
- 555 .bin animation files in assets/animations/binary/
- **Rotated atlas frame handling** — Cocos2d plist rotated=true frames un-rotated correctly

### What's in progress 🔧
- **Dojo background rotation** — some atlas frames may still appear rotated
- **Floor gaps** — floor segments may have gaps due to coordinate alignment
- **Player Y position** — offset adjustment to align feet with floor
- **Bag centering** — bag may not be centered on ceiling holder
- **Profile icon** — rotation formula still being tested

### Reverse Engineering Progress

#### Original Engine Architecture (from disassembly)
- **Cocos2d-x** game engine (confirmed: CCNode, CCSprite, CCLayer, CCScrollView classes)
- **ModelPhysics** — Verlet integration physics system (confirmed from string table)
- **MoveInside** — animation alignment system using pivot nodes
- **IntervalAttack** — attack interval system with StartFrame/EndFrame validation
- **AnimationTablesForAnimation** — animation management system
- **InfoAnimation** — animation info with MoveInside alignment

#### .bin Animation Format (VERIFIED)
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

#### moves.xml Attack Intervals (VERIFIED)
| Move | Attack Start | Attack End | FirstFrame |
|------|-------------|-----------|------------|
| HighPunch | 4 | 5 | 1 |
| HeavyPunch | 8 | 10 | 2 |
| LowPunch | 6 | 8 | 3 |
| DoublePunch | 6 | 8 | 1 |
| SpinningPunch | 5 | 6 | 2 |
| UpperCut | 9 | 12 | 2 |
| HighKick | 6 | 8 | 3 |
| FrontKick | 8 | 9 | 2 |
| BackKick | 7 | 9 | 1 |
| Sweep | 9 | 11 | 2 |
| LowKick | 4 | 6 | 2 |

Note: moves.xml uses 1-indexed frames; converted to 0-indexed in code.

#### Coordinate Systems
- **params.xml**: Y-DOWN (Y=0 at top, positive Y = down)
  - Floor at y=225, ceiling at y=-202, player at y=-93
- **Our world**: Y-UP (Y=0 at center, positive Y = up)
  - Location images: world_y = -img.y (inverted)
  - Player/bag: use params Y directly with -45 offset to align feet with floor
- **Skeleton model space**: Y-UP (Y=0 at feet, positive Y = up)
  - NPivot at Y=169.48, NToe at Y≈72 (feet)

#### Verlet Physics (matches original ModelPhysics)
```
Node state: position (x,y), prev_position (px,py), mass, inv_mass, fixed, attenuation
Integration: pos_new = pos + (pos - prev) * (1 - attenuation) + gravity * dt²
Constraints: distance constraints from edges, 8 iterations per frame
Fixed nodes: Node12 (ceiling attachment) has inv_mass = 0
Impulse: prev_pos -= impulse (adds velocity in Verlet)
```

### Controls
| Key | Action |
|-----|--------|
| A/D | Step left/right (root motion from step_forward/step_back .bin) |
| Space | HighPunch (or direction+Space for other punches) |
| K | HighKick (or direction+K for other kicks) |
| M | Toggle menu |
| T | Toggle dialog |
| 1/2/3 | Zoom presets |
| Esc | Quit |

### Combat System
- **Space + nothing**: HighPunch
- **Space + D**: DoublePunch
- **Space + A**: SpinningPunch
- **Space + W**: UpperCut
- **Space + S**: LowPunch
- **K + nothing**: HighKick
- **K + D**: FrontKick
- **K + A**: BackKick
- **K + S**: Sweep

### File Structure
```
main.cpp              — Game logic (rendering, animation, combat, movement, physics)
engine/renderer/      — OpenGL renderer (draw_quad, draw_triangle, draw_circle, camera)
engine/platform/      — GLFW platform (input, window, events)
engine/reverse/       — Reverse engineering (plist atlas parser, bitmap font, s3e container)
engine/runtime/       — Main loop
assets/models/        — skeleton.xml, body.xml, punching_bag.xml, skeleton_punching_bag.xml
assets/animations/    — moves.xml + binary/ (555 .bin animation files)
scripts/              — Python diagnostic scripts (verify_bin_order, check_attack_intervals, etc.)
```

### Build
```bash
# Windows (build.bat)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRESF2_BUILD_TESTS=ON -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON -DRESF2_BUILD_TOOLS=OFF
cmake --build build --config Release

# Run
resf2_app.exe --assets <path_to_sf2_assets>
```

### Next Steps
1. Fix dojo background rotation (atlas frame UV mapping)
2. Fix floor gaps between segments
3. Fix bag centering on ceiling holder
4. Verify Profile icon rotation formula
5. Continue disassembly of original APK for deeper engine logic
6. Implement animation blending (crossfade between animations)
7. Add more combat moves from moves.xml

## S3E Binary Analysis (from ShadowFight2.s3e in APK)

### Original Engine: Marmalade SDK + Cocos2d-x

The game binary `ShadowFight2.s3e` (LZMA-compressed, 8.3MB uncompressed) is a
Marmalade S3E executable containing Thumb ARM code.

### Key Engine Components (from string table analysis)

#### Physics System: ModelPhysics
- `12ModelPhysics` — Main physics class
- `PhysicsFrameNumber` — Per-frame physics simulation
- `16ConditionPhysics` — Physics condition checker
- Node attributes: `Mass`, `Fixed`, `Attenuation`, `Cloth`, `Weak`, `Collisible`, `Passive`
- Uses Verlet integration (confirmed by node attributes matching Verlet physics)

#### Animation System: ModelAnimation + InfoAnimation
- `14ModelAnimation` — Animation playback controller
- `13InfoAnimation` — Animation metadata
- `10MoveInside` — Animation alignment system using pivot nodes
  - `align.pivotID == -1` — Pivot alignment validation
  - `moveInside is null` — Null check for MoveInside
- `17IntervalAnimation` — Animation interval system
  - `IntervalAnimation: startFrame (%i) greater then endFrame(%i)` — Frame validation
- `14IntervalAttack` — Attack interval system
  - `IntervalAttack::getFactors random return` — Attack factor calculation
  - `StartFrame (%i) is outside of attack interval (%i-%i)` — Frame bounds check
  - `EndFrame (%i) is outside of attack interval (%i-%i)` — Frame bounds check
- `currentAttackInterval not found` — Attack interval lookup
- Intervals: `Intervals`, `Interval`, `IntervalStart`, `IntervalEnd`
- `EventIntervalStart`, `EventIntervalEnd` — Interval events
- `SelfUninterrupt`, `SemiUninterrupt`, `Uninterrupt` — Interruption states
- `MidFrames`, `FirstFrame` — Frame offsets from moves.xml

#### Rendering System: Cocos2d-x
- `N7cocos2d8CCSpriteE` — CCSprite class
- `N7cocos2d17CCSpriteBatchNodeE` — Sprite batch node
- `N7cocos2d13CCSpriteFrameE` — Sprite frame
- `N7cocos2d18CCSpriteFrameCacheE` — Sprite frame cache
- `textureRotated` — Atlas frame rotation flag (90° CW)
- `CCSpriteFrameCache file not found:` — Atlas loading error
- `N7cocos2d7CCLayerE` — CCLayer class
- `N7cocos2d11CCLayerRGBAE` — RGBA layer
- `N7cocos2d12CCLayerColorE` — Color layer
- `N7cocos2d16CCLayerMultiplexE` — Multiplex layer
- `N7cocos2d6CCNodeE` — Base node class
- `N7cocos2d10CCNodeRGBAE` — RGBA node
- Shaders: `ShaderPositionTexture_uColor`, `ShaderPosition_uColor`, `ShaderPositionTexture`

#### Location System
- `ImageLayer` — Layer image class
- `setupBackground - unknownType: %i` — Background setup with layer types
- `_backgroundPicture '%s' not load` — Background loading error
- Layer types: `type=1` (parallax), `type=2` (models viewer)
- `Factor` — Parallax factor (controls scroll speed)

#### Combat System
- `Attack Interval ID: %i` — Attack interval identification
- `BlockDamageFactor`, `DamageFactor` — Damage calculation
- `HitFactor`, `DistanceFactor` — Hit detection factors
- `AnimationFactors`, `CounterFactor`, `HealthFactor` — Combat factors

### S3E File Structure
```
0x000: "XE3U" magic (4 bytes)
0x004: code_size (4 bytes)
0x008: data_size (4 bytes)
0x00C: flags (4 bytes)
0x010-0x03C: section offsets
0x040: ICF config (Marmalade configuration)
0x1521: Code section (Thumb ARM)
0x730000+: String table / data section
0x849519: Relocation table
0x8496CD: End of file
```

### DZ Archive Format (animations.dz, files.dz)
```
0x00: "DTRZ" magic
0x04: version (16-bit: 120)
0x06: flags (16-bit: 105)
0x08: Filenames (null-terminated strings)
0x0EED: Block table (120 entries × 6 bytes: 0xFFFF + file_id + block_id)
0x11ED: Size table (120 entries × 16 bytes: offset + comp_size + uncomp_size + flag)
0x1935: Compressed data blocks (custom compression, not zlib/LZ4/LZMA)
```

### Key Findings
1. **Physics**: Verlet integration confirmed (node attributes match)
2. **Animation**: MoveInside system uses pivot alignment
3. **Rendering**: Cocos2d-x with texturePacker atlases (rotated frames)
4. **Location**: ImageLayer with parallax Factor
5. **Combat**: IntervalAttack with StartFrame/EndFrame validation
6. **Movement**: Root motion from .bin NPivot positions (MoveInside align)
