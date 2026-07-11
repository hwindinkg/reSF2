# reSF2 — Clean-room reimplementation of Shadow Fight 2

## Current Status: Stage 8 — Character rendering + Verlet bag physics + Combat system

### What works ✅
- Window 1280×720, GLFW, OpenGL ES 2.0 renderer
- **Character body mesh** from body.xml (82 capsules + 29 triangles as dark silhouette)
- **Circle-based capsule rendering** (draw_filled_circle_world for capsule caps)
- **Skeletal animation** — 23 animations loaded from .bin files
- **Animation interpolation** — smooth blending between frames with alpha interpolation
- **Y normalization** — feet stay on floor across all animations (smoothed)
- **Punching bag Verlet physics** — real physics with gravity, distance constraints, damping
- **Bag impulse on hit** — directional impulse based on hit height and attacker position
- **Hit detection** — uses moves.xml Attack intervals (Start/End frames), 70px threshold
- Dojo location rendering (parallax background, Y-inverted, pre-cropped rotated frames)
- **Parallax tiling** — background layers tile horizontally
- HUD with real game textures (Top_Panel, gold, energy, Level_bar)
- Scroll/parchment menu with expand/collapse animation (300ms smoothstep)
- **Menu icon uniform scaling** — all icons same size
- **.bin animation format fully decoded** (67 nodes per frame, absolute world positions)
- **moves.xml parser** (858 moves with attack intervals, damage, edges)
- **S3E binary analyzed** — x86_64 PIE Marmalade + Cocos2d-x
- **DZ archive format documented** (custom compression, not yet decoded)

### What's broken ❌
- **Movement jitter** — GLFW sends spurious RELEASE events for held keys on Windows 10 (build 19044). `keys_down` flickers true→false→true every frame, causing step_forward↔fists_idle switching every frame. Tried: glfwGetKey() polling, debounce, GLFW_STICKY_KEYS, state machine, keys_just_released — ALL failed. **Suggested fix: use Win32 GetAsyncKeyState() directly.**
- **Root motion** — depends on movement fix (animation reset prevents NPivot delta accumulation)
- **Some background textures rotated** — pre-cropped rotated frames work for location but may need formula adjustment
- **Profile menu icon** — has parts of other buttons (rotation formula needs adjustment)
- **DZ archive decompression** — custom compression (flag=4, first byte 0x1D), not zlib/LZ4/LZMA/LZF

### S3E Binary Analysis
- **Architecture**: x86_64 PIE (NOT ARM)
- **Base address**: 0x4A000000
- **Engine**: Marmalade SDK + Cocos2d-x
- **Physics**: ModelPhysics (Verlet integration)
- **Animation**: ModelAnimation + MoveInside (pivot alignment)
- **Combat**: IntervalAttack (StartFrame/EndFrame validation)
- **Rendering**: Cocos2d-x (CCSprite, textureRotated, ShaderPositionTexture)
- See `docs/s3e_reverse_engineering.md` for full analysis

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

### moves.xml Attack Intervals
| Move | Attack Start | Attack End |
|------|-------------|-----------|
| HighPunch | 4 | 5 |
| HeavyPunch | 8 | 10 |
| LowPunch | 6 | 8 |
| DoublePunch | 6 | 8 |
| SpinningPunch | 5 | 6 |
| UpperCut | 9 | 12 |
| HighKick | 6 | 8 |
| FrontKick | 8 | 9 |
| BackKick | 7 | 9 |
| Sweep | 9 | 11 |
| LowKick | 4 | 6 |

### Controls
| Key | Action |
|-----|--------|
| A/D | Step left/right |
| Space | Punch (W=upper, S=low, D=double, A=spinning) |
| K | Kick (S=sweep, D=front, A=back) |
| M | Toggle menu |
| T | Toggle dialog |
| 1/2/3 | Zoom presets |
| Esc | Quit |

### Build
```bash
build.bat  # Windows, cmake + MSVC
resf2_app.exe --assets <path_to_sf2_assets>
```

### File Structure
```
main.cpp              — Game logic (~2750 lines)
engine/platform/      — GLFW platform (input, window) — BROKEN on Win10
engine/renderer/      — OpenGL renderer
engine/reverse/       — Plist atlas parser, bitmap font, s3e container
engine/runtime/       — Main loop
assets/models/        — skeleton.xml, body.xml, punching_bag.xml
assets/animations/    — moves.xml + binary/ (555 .bin files)
docs/                 — S3E reverse engineering docs
scripts/              — Python diagnostic scripts
HANDOFF.md            — Detailed handoff for next session
```

### Resources
- APK: `https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk`
- Game data: `https://chat.chobat.ru/sf2.7z`
- Marmalade-Modding: `https://github.com/knot126/Marmalade-Modding`
- S3ELoader (Ghidra): `https://github.com/knot126/S3ELoader`
