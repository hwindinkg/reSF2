# HANDOFF PROMPT FOR NEXT CHAT SESSION

## Project: reSF2 — Shadow Fight 2 Engine Reverse Engineering

### Git Repository
- Remote: `git@github.com:hwindinkg/reSF2.git`
- Git root: `/home/z/my-project/`
- SSH key: `/home/z/.ssh/id_ed25519_resf2`
- SSH wrapper: `/home/z/ssh_wrapper.py` (paramiko-based, for git push)
- Push command: `cd /home/z/my-project && git add -A && git commit -m "..." && GIT_SSH_COMMAND="/home/z/ssh_wrapper.py" git push origin main`

### Game Files
- APK: `https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk` (94MB, already downloaded to `/home/z/my-project/work/sf2.apk`)
- Game data: `https://chat.chobat.ru/sf2.7z`
- S3E binary: `/home/z/my-project/work/ShadowFight2.bin` (extracted from APK, LZMA-decompressed, 8.3MB)
- DZ archives: `/home/z/my-project/work/assets/assets/files.dz` and `animations.dz`

### Disassembly Tools & Repositories
- **Marmalade-Modding**: `https://github.com/knot126/Marmalade-Modding` (cloned to `/home/z/my-project/work/Marmalade-Modding/`)
  - `dzextract.py` — DZ archive extractor (needs modded gzip.py)
  - `dump_s3e.py` — S3E header parser
  - `docs/Dzip format.md` — DZ format documentation
  - `docs/misc/IwS3ERead.h` — S3E header C header file
- **S3ELoader**: `https://github.com/knot126/S3ELoader` (cloned to `/home/z/my-project/work/S3ELoader/`)
  - Ghidra plugin for loading S3E binaries
  - `src/main/java/s3eloader/S3ELoaderLoader.java` — S3E format parser
- **Capstone**: Python disassembler (installed via pip, use `/usr/bin/python3`)
- **S3E binary analysis docs**: `/home/z/my-project/docs/s3e_reverse_engineering.md`

### S3E Binary Key Facts
- Architecture: **x86_64 PIE** (NOT ARM!)
- Base address: `0x4A000000`
- Magic: `XE3U` (version 4.40.0)
- Code section: file offset `0x45251`, virtual `0x4A000000`, size `0x7B8000`
- Data section: file offset `0x7FD251`, virtual `0x4A7B8000`, size `0x4C2C8`
- Fixup table: file offset `0x1521`, size `0x43D30` (4 sections, 67461 relocations)
- Strings at file offsets `0x70xxxx-0x79xxxx`
- Engine: Marmalade SDK + Cocos2d-x

### Key Engine Strings Found
- `12ModelPhysics` — Verlet integration physics
- `14ModelAnimation` — Animation controller
- `13InfoAnimation` + `10MoveInside` — Animation alignment with pivotID
- `14IntervalAttack` — Attack interval system (StartFrame/EndFrame)
- `ImageLayer` — Location layer rendering
- `setupBackground` — Background setup function
- `textureRotated` — Atlas frame rotation flag (Cocos2d TexturePacker)
- `LZF` — Possible DZ compression method
- `derbh` — Marmalade archive system

### DZ Archive Format
- Magic: `DTRZ`, 120 files, 104 folders
- Block table: 120 entries × 6 bytes (0xFFFF + file_id + block_id)
- Size table: 120 entries × 16 bytes (offset + comp_size + uncomp_size + flag)
- All blocks have flag=4, first byte 0x1D
- Compression: UNKNOWN (not zlib/gzip/deflate/LZ4/LZMA/bzip2/LZF)
- s3e binary contains `decompress chunk` strings and embedded zlib 1.2.3

---

## CURRENT PROBLEMS (CRITICAL — MUST FIX)

### 1. MOVEMENT JITTER (TOP PRIORITY)
**Symptom**: When holding A or D, character switches between step_forward/step_back and fists_idle EVERY FRAME, causing visual jitter and preventing movement (character returns to start position).

**Root cause**: GLFW on user's Windows 10 (build 19044) sends spurious `GLFW_RELEASE` events for held keys. `keys_down` flickers true→false→true every frame. This has been confirmed via [MOVE] and [STATE] diagnostic logs:
- `[STATE] IDLE→RIGHT (L=0 R=1)` — D detected as pressed
- `[STATE] RIGHT→IDLE (no_key=11)` — no_key_frames_ reaches 11 because keys_down flickers to false for 10+ consecutive frames

**What was tried (ALL FAILED)**:
1. `glfwGetKey()` polling in `poll_events()` — same flicker
2. Debounce (3-frame release delay) — `glfwGetKey()` is stable in wrong state for 10+ frames
3. Key callback only (no glfwGetKey) — callback receives same spurious RELEASE events
4. `GLFW_STICKY_KEYS` mode — didn't help
5. State machine with `no_key_frames_` hysteresis — no_key_frames_ reaches 11 in one cycle
6. State machine with `keys_just_released` — `just_released` also fires every frame
7. Movement state machine (IDLE/MOVING_LEFT/MOVING_RIGHT) — same flicker causes transitions

**Suggested fix for next session**: Use **Win32 API directly** (`GetAsyncKeyState()` or `GetKeyState()`) instead of GLFW for key state on Windows. This bypasses GLFW's event system entirely and queries the OS keyboard state directly. Add a `#ifdef _WIN32` block in `poll_events()` that uses `GetAsyncKeyState(VK_A)` etc.

### 2. ROOT MOTION NOT WORKING
**Symptom**: Character returns to start position after step animation.

**Root cause**: Related to problem #1 — because animation switches every frame between step and idle, the root motion code in `update_animation()` never accumulates displacement. The root motion code uses delta accumulation:
```cpp
float delta = npivot_x - prev_npivot_x_;
if (std::abs(delta) < 40.0f) {
    player_pos_x_ += delta;
}
prev_npivot_x_ = npivot_x_;
```
But when animation resets to frame 0 every other frame, NPivot jumps back to start, creating negative delta that cancels the positive delta. Fix problem #1 first, then root motion should work.

### 3. BACKGROUND ROTATION (dojo)
**Symptom**: Some background textures appear rotated 90°.

**Status**: Pre-cropped textures for rotated atlas frames are implemented in `load_atlas()`. Formula used: `sx = atlas_x + (fh-1-y), sy = atlas_y + x` with swapped dimensions `fw=atlas_h, fh=atlas_w`. This works for location textures but may need verification.

### 4. MENU ICON PROFILE
**Symptom**: Profile icon has parts of other buttons.

**Status**: HUD/menu icon un-rotation uses formula A (`sx = atlas_x + (fh-1-y), sy = atlas_y + x`) without dimension swap. Different atlases may store dimensions differently.

### 5. BAG PHYSICS
**Status**: Verlet physics implemented and working. Bag swings on hit. Gravity=-800, 10 constraint iterations, impulse strength 18 (punch) / 25 (kick), hit threshold 70px. Hit detection uses moves.xml Attack intervals.

---

## WHAT'S WORKING
- ✅ Character body rendering (82 capsules + 29 triangles as silhouette)
- ✅ Skeletal animation (23 animations from .bin files)
- ✅ Animation interpolation (alpha blending between frames)
- ✅ Verlet physics for punching bag
- ✅ Hit detection (moves.xml Attack intervals, 70px threshold)
- ✅ Bag impulse on hit (directional, height-based target node)
- ✅ Dojo location rendering (parallax, Y-inverted)
- ✅ Pre-cropped rotated atlas frames for location textures
- ✅ HUD (gold, energy, level bar)
- ✅ Menu scroll animation (300ms smoothstep)
- ✅ Menu icons (uniform scaling)
- ✅ Y normalization (feet on floor across animations, smoothed)
- ✅ .bin animation format decoded
- ✅ moves.xml parser (858 moves with attack intervals)
- ✅ S3E binary analyzed (x86_64, Marmalade + Cocos2d-x)

## WHAT'S NOT WORKING
- ❌ Movement (GLFW input flicker causes animation jitter)
- ❌ Root motion (depends on movement fix)
- ❌ Some background textures still rotated
- ❌ Profile menu icon has parts of other buttons
- ❌ DZ archive decompression (custom compression unknown)

---

## KEY FILES
- `main.cpp` — All game logic (~2750 lines)
- `engine/platform/glfw_platform.cpp` — GLFW input + window (BROKEN on user's Windows)
- `engine/renderer/renderer.cpp` — OpenGL renderer
- `engine/reverse/plist_atlas.cpp` — Atlas plist parser
- `assets/models/` — skeleton.xml, body.xml, punching_bag.xml, skeleton_punching_bag.xml
- `assets/animations/binary/` — 555 .bin animation files
- `assets/animations/moves.xml` — 858 move definitions
- `docs/s3e_reverse_engineering.md` — S3E binary analysis
- `scripts/` — Python diagnostic scripts

## BUILD
```bash
# Windows
build.bat  # uses cmake + MSVC
# Run
resf2_app.exe --assets <path_to_sf2_assets>
```

## CONTROLS
| Key | Action |
|-----|--------|
| A/D | Step left/right (root motion from .bin) |
| Space | Punch (direction modifiers: W=upper, S=low, D=double, A=spinning) |
| K | Kick (direction modifiers: S=sweep, D=front, A=back) |
| M | Toggle menu |
| T | Toggle dialog |
| 1/2/3 | Zoom presets |
| Esc | Quit |
