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
