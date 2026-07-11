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

## CURRENT STATUS (updated this session)

### 1. MOVEMENT JITTER — FIXED ✅
**Fix applied**: `engine/platform/glfw_platform.cpp` now uses Win32 `GetAsyncKeyState()` directly under `#ifdef _WIN32`, bypassing GLFW's key event system entirely. The `key_callback` early-returns on Windows; `poll_events()` iterates the Key enum, maps each to a VK code via `glfw_key_to_vk()`, and polls the OS keyboard state. Edge transitions (`keys_just_pressed`/`keys_just_released`) are computed from `prev_keys_down_`. Non-Windows path unchanged (GLFW callback + sticky keys).

**Commit**: `0b3d55c` — pushed to GitHub.

**Root motion** should now work automatically (animation no longer resets every frame). Verify on Windows by holding A/D and checking for smooth step animation + accumulated displacement.

### 2. SCENE/STATE MANAGER — IMPLEMENTED ✅ (this session)
**What was done**: Created `engine/scene/` with a proper finite-state machine:
- `SceneId` enum: `{Boot, Loading, MainMenu, Map, Shop, Settings, Dialogue, Battle, Results}`
- `Scene` interface: `on_enter`/`on_update`/`on_render`/`on_exit`/`on_quit_request`
- `SceneHost` interface: implemented by `Game` class — scenes call back into Game for asset loading, gameplay update, rendering, save/load
- `SceneManager`: owns current scene, handles deferred transitions

**Game flow cycle** (minimal, on stubs):
```
Boot → Loading → MainMenu → (click "Story") → Map → (select level) → Dialogue → (Space) → Battle → (Y=victory / L=defeat) → Results → (Space) → MainMenu
```

**Key architectural changes in main.cpp**:
- `Game` class now inherits from both `rt::IGame` and `scene::SceneHost`
- Old `GameState { Loading, Location }` enum removed — replaced by `SceneManager`
- `on_update()` / `on_render()` delegate to `scene_manager_.update/render()`
- Dojo gameplay (movement, combat, animation, physics, overlays) extracted to `host_update_gameplay(dt)` — called by MainMenu/Battle scenes
- Dojo rendering (location, character, bag, HUD, overlays) extracted to `host_render_scene()` — called by MainMenu/Battle scenes
- Save system (JSON stub): `host_save_progress()` writes to `temp_directory_path()/resf2_save.json`

**What works**: Scene transitions, menu item clicks (Story/Shop/Settings/Test Dialog), keyboard shortcuts (N=New Game, Y/L=victory/defeat in Battle), save on entering Results.

**What's stubbed**: Map (list of 6 fake levels), Shop/Settings (empty screens with Esc-to-menu), Dialogue (3 hardcoded lines, Space to advance), Results (empty, Space to continue). Real content requires DZ archive extraction (Task 3).

### 3. DZ ARCHIVE FORMAT — PARTIALLY DECODED ⚠️ (this session)
**Container format**: Fully decoded. See `engine/reverse/dz/README.md` for the corrected file table layout (the old `parse_dz.py` had the field order wrong — `dz_parse_correct.py` is the fixed version).

**Key findings**:
- All files in `files.dz` are type=4 (DZ), all in `animations.dz` are type=8 (DZ variant)
- DZ is a **STREAMING compressor** — file offsets overlap, decompressor is stateful, entire archive must be decoded as one stream
- Entropy 7.5-7.9 bits/byte for larger files → real arithmetic/range coding (NOT XOR)
- Algorithm: arithmetic coding + 5-byte context window + CRC32 hash + LZ77 matches
- Located in `libs3e_android.so` at 0x389f8 (~250 ARM instructions)

**Blocked on**: ARM emulation needs full Marmalade runtime (init_array constructors fail). Manual port is incomplete.

**Recommended path**: Use `dzip.exe` on Windows to extract assets, OR port the DZ decoder from Ghidra decompilation of 0x389f8.

### 4. UI/ROTATED TEXTURES — NOT STARTED ❌
Profile menu icon has parts of other buttons; some location textures rotated incorrectly. Cocos2d-x `textureRotated` flag handling needs formula adjustment. Lowest priority — doesn't block movement or game flow.

---

## KEY FILES (updated)

- `main.cpp` — Game class (~2850 lines) with SceneHost integration. Gameplay in `host_update_gameplay()`, rendering in `host_render_scene()`.
- `engine/scene/scene_system.hpp` — SceneId, Scene, SceneHost, SceneManager
- `engine/scene/scenes.hpp` / `scenes.cpp` — 9 concrete scene implementations
- `engine/platform/glfw_platform.cpp` — Win32 GetAsyncKeyState fix (Windows input)
- `engine/reverse/dz/README.md` — DZ format documentation (corrected)
- `scripts/dz_parse_correct.py` — corrected DZ container parser + entropy analysis
- `scripts/s3e_analyze.py` — S3E binary analysis pipeline (from previous session)
- `work/s3e_analysis/` — S3E analysis output (header, imports, strings, config)
- `work/sf2_data/` — extracted game data (3138 files from sf2.7z)
- `work/Marmalade-Modding/` — cloned RE tools (dzextract.py, dump_s3e.py)
- `work/S3ELoader/` — Ghidra plugin for S3E binaries

## BUILD
```bash
# Windows (user's machine)
build.bat  # cmake + MSVC, produces resf2_app.exe
resf2_app.exe --assets <path_to_sf2_assets>

# Linux compile check (no linking — just verifies code compiles)
bash scripts/verify_main_compile.sh
```

## CONTROLS (updated)
| Key | Action |
|-----|--------|
| A/D | Step left/right (Win32 GetAsyncKeyState on Windows) |
| Space | Punch (W=upper, S=low, D=double, A=spinning) / advance dialogue |
| K | Kick (S=sweep, D=front, A=back) |
| M | Toggle scroll menu (MainMenu/Battle) |
| T | Toggle dialog overlay (MainMenu/Battle) |
| N | New Game — go to Map (MainMenu) |
| Y/L | Declare victory/defeat (Battle, debug) |
| 1/2/3 | Zoom presets |
| Esc | Quit / close overlay / back (scene-specific) |

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
