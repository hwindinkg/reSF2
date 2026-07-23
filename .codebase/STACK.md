# reSF2 — Tech Stack

> Clean-room reimplementation of the Shadow Fight 2 engine.
> Version: 0.0.3

---

## 1. Programming Languages

| Language | Usage | Evidence |
|----------|-------|----------|
| **C++23** | Core engine, renderer, platform, format parsers, tests | `CMakeLists.txt` L11: `set(CMAKE_CXX_STANDARD 23)` |
| **Python 3** | Asset RE scripts, DZ analysis/download scripts, test helpers | 117 `.py` files in `scripts/`; `check_xml.py` at root |
| **C (decompiled)** | Reference decompilations of original DZ engine code | `scripts/dz_*.c` files (decompiled ARM/thumb) |

---

## 2. Build System

| Tool | Version | Detail |
|------|---------|--------|
| **CMake** | ≥ 3.24 | `CMakeLists.txt` L3: `cmake_minimum_required(VERSION 3.24)` |
| **MSVC** | Visual Studio 2022 | `BUILD.md` — "Visual Studio 17 2022" generator, x64 |
| **GCC/Clang** | Linux/macOS | `-Wall -Wextra -Wpedantic` etc. in `CMakeLists.txt` L38–43 |
| **Ninja/Make** | Linux/macOS | `cmake --build --parallel` |

### CMake Options (`CMakeLists.txt` L20–26)

| Option | Default | Description |
|--------|---------|-------------|
| `RESF2_BUILD_TESTS` | ON | Unit and integration tests |
| `RESF2_BUILD_TOOLS` | ON | Offline asset tools |
| `RESF2_WERROR` | OFF | Warnings as errors |
| `RESF2_ENABLE_SAN` | OFF | Address + UB sanitizers (Clang/GCC) |
| `RESF2_BUILD_RUNTIME` | ON | `resf2_app` executable |
| `RESF2_USE_GLFW` | ON | GLFW platform backend |
| `RESF2_BUILD_HEADLESS` | OFF | Software-renderer-only, no GPU |

### Library Targets (`engine/CMakeLists.txt` L3–34)

| Target | Description | Source Dir |
|--------|-------------|------------|
| `resf2_reverse` | Binary format parsers (`.s3e`, `.plist`, `.atf`, `.fnt`, `.dz`) | `engine/reverse/` |
| `resf2_platform` | Platform abstraction, window/GL/input/filesystem | `engine/platform/` |
| `resf2_core` | Math, node graph, state, input system, asset manager | `engine/core/` |
| `resf2_format` | XML DOM, PLIST, location/stage/list parsers, JSON atlas | `engine/format/` |
| `resf2_fight` | Animation state machine, moves system | `engine/fight/` |
| `resf2_runtime` | Asset manager + main game loop | `engine/runtime/` |
| `resf2_scene` | Scene system and scene implementations | `engine/scene/` |
| `resf2_renderer` | GLES2/GL2.1 renderer + software fallback | `engine/renderer/` |
| `resf2_audio` | Audio mixer (NullAudioBackend currently) | `engine/audio/` |
| `resf2_game` | Game logic, helpers | `engine/game/` |
| `resf2_warnings` | Interface target: shared warning flags | `CMakeLists.txt` L29 |

### Executables

| Target | Links | Purpose |
|--------|-------|---------|
| `resf2_app` (`main.cpp`) | `resf2_game` | Main runtime executable |
| `resf2_port` (`main_port.cpp`) | `resf2_fight`, `resf2_format`, `resf2_platform`, `resf2_renderer`, `resf2_core`, `resf2_reverse` | Modular engine integration demo |
| `resf2_headless` (`headless_main.cpp`) | `resf2_renderer`, `resf2_reverse` | Software-renderer-only build (no GPU) |
| 12+ test executables | Various `resf2_*` libs | CTest-based unit tests |

---

## 3. Key Dependencies

### Auto-Fetched via CMake FetchContent

| Library | Version | Used By | Purpose |
|---------|---------|---------|---------|
| **zlib** | v1.3.1 (`GIT_TAG v1.3.1`) | `resf2_reverse`, `resf2_runtime`, `resf2_game` | `.atf` decompression, `.dz` decoding |
| **GLFW** | 3.4 (`GIT_TAG 3.4`) | `resf2_platform` | Window, GL context, input (desktop) |

### Vendored / Bundled

| Library | Location | Purpose |
|---------|----------|---------|
| **stb_image** | `engine/renderer/stb_image.h` | PNG loading for textures |
| **stb_image_write** | `engine/renderer/stb_image_write.h` | Screenshot/software-renderer output |
| **libwebp** | `engine/renderer/webp/` (full source) | WebP texture decoding in `.dz` archives |
| **dzip.exe** | `download/dzip.exe` | Windows-only DZ archive extraction tool |

### Platform-System Libraries

| Platform | Libraries | Reason |
|----------|-----------|--------|
| **Windows** | `opengl32`, `user32` | OpenGL, Win32 input (`GetAsyncKeyState`) |
| **Linux** | `OpenGL`, `X11` (via GLFW) | GLFW auto-disables Wayland; uses X11 |
| **macOS** | `OpenGL.framework` | GLFW + OpenGL framework |
| **Android** *(planned)* | `EGL`, `GLESv2` | NativeActivity + EGL (in Platform interface) |
| **All** | `Threads::Threads` | Threading (Win32 threads on MSVC, pthread elsewhere) |

---

## 4. Asset Pipeline

### Original Game Assets

| Format | Description | Parser Location |
|--------|-------------|-----------------|
| **.dz** | Nekki proprietary DZ archive (custom compression, derbh-based) | `engine/reverse/dz_reader.cpp`, `dz_decoder.cpp` |
| **.s3e** | Marmalade S3E container (17 shaders extracted) | `engine/reverse/s3e_container.cpp` |
| **.plist** | Cocos2d-x TexturePacker v2 atlas format | `engine/reverse/plist_atlas.cpp` |
| **.atf** | Zlib-compressed tactics/combat AI data | `engine/reverse/atf_tactics.cpp` |
| **.fnt** | AngelCode BMFont bitmap font | `engine/reverse/bitmap_font.cpp` |
| **.xml** | Configuration: moves, stages, zones, settings, equipment | `engine/format/xml_doc.cpp` |
| **.json** | Cocos2d-x JSON atlas (`cocoGUI/TestLayer.json`) | `engine/format/json_atlas.cpp` |
| **.png** | Texture images (loaded via stb_image) | `engine/renderer/renderer.hpp` L67 |
| **.jpg** | Photo backgrounds | Asset directory |
| **.wav** | Sound effects (loaded, not played — NullAudioBackend) | `engine/audio/` |
| **.mp3** | Music tracks (loaded, not played) | `assets/music/` |

### Retrieval (APK extraction)

Asset extraction workflow (`BUILD.md`):
1. Rename `Shadow_Fight_2_1.9.21.apk` → `.zip`
2. Extract `assets/` → `sf2/assets/`
3. `files.dz` and `animations.dz` live in `sf2/assets/assets/`

### DZ Extraction (Build-Time)

```
download/dzip.exe --decompress assets/files.dz     → assets/files/
download/dzip.exe --decompress assets/animations.dz → assets/animations/
```

Triggered by CMake custom target `dz_extract` (`CMakeLists.txt` L180–184). Only runs when `download/dzip.exe` exists. On non-Windows, pre-extracted files must be committed to the repo.

### Runtime Fallback Strategy

| Asset Source | Status |
|-------------|--------|
| `animations.dz` (type-8 gzip) | Read directly by engine |
| `files.dz` (type-4 custom) | **HEURISTIC-TODO** — engine falls back to pre-extracted `assets/` folder |
| `sf2/assets/` | Original mobile assets (sounds, music) |

---

## 5. Renderer

### Architecture (`engine/renderer/renderer.hpp`)

- **ES 2.0 / OpenGL 2.1** — targets GLES2 semantics; desktop uses GL 2.1 + extensions
- **Software fallback** — `software_renderer.cpp` when `RESF2_NO_GL` is defined
- **GLSL shaders** — 17 shaders extracted from original `.s3e` container
- **Vertex format**: `SpriteVertex` — position (x,y), UV (u,v), color (RGBA packed) — L123–127

### Key Components

| Component | Purpose |
|-----------|---------|
| `Texture2D` | OpenGL texture wrapper. Loads PNG (stb_image), WebP, KTX |
| `ShaderProgram` | GLSL program cache + uniform setters |
| `SpriteBatch` | Batched sprite rendering (VBO, up to 65536 vertices) |
| `Camera2D` | Orthographic 2D camera with follow + shake + zoom |
| `Renderer` | Main entry point: `begin_frame` / `draw_sprite*` / `end_frame` |

### Texture Loading

- **PNG**: stb_image (vendored `engine/renderer/stb_image.h`)
- **WebP**: libwebp (vendored `engine/renderer/webp/` full source)
- **KTX**: GPU compressed texture format
- **Auto-detect**: `init_from_memory()` handles magic-byte recognition

### Draw Primitives

Textured quad, filled rect, filled triangle, filled circle, lines — each in both screen-space (Y-down, top-left origin) and world-space (Y-up, camera projection).

---

## 6. Platform Targets

| Target | Backend | Source | Status |
|--------|---------|--------|--------|
| **Windows** | GLFW + `opengl32` | `engine/platform/glfw_platform.cpp` | ✅ Primary |
| **Linux** | GLFW + X11 + OpenGL | `engine/platform/glfw_platform.cpp` | ✅ |
| **macOS** | GLFW + OpenGL.framework | `engine/platform/glfw_platform.cpp` | ✅ |
| **Headless** | NullPlatform (no GPU) | `platform.cpp` + `software_renderer.cpp` | ✅ |
| **Android** | NativeActivity + EGL *(planned)* | Platform interface declares `AndroidPlatform` | ⏳ Stage 7.1.x |
| **Nintendo Switch** | libnx *(planned)* | Platform interface declars `SwitchPlatform` | ⏳ Optional Stage 8 |

### Platform Interface (`engine/platform/platform.hpp`)

Abstract `Platform` class providing:
- Window lifecycle (init, shutdown, poll_events, pause/resume)
- GL context management (make_current, swap_buffers)
- Input: keyboard (USB HID codes), mouse, touch (up to 16 pointers), gamepad
- Filesystem: read-only asset access, read-write save directory
- Time: monotonic `now_ms()`
- Deterministic input replay (`load_input_script`, diagnostic mode)

### Input Replay (Diagnostic)

Text-scripted key events merged into real platform input. Script format:
```
frame <N> keydown <KEY>
frame <N> keyup <KEY>
```

---

## 7. Testing

| Aspect | Detail |
|--------|--------|
| **Framework** | CTest (CMake) + standalone test executables |
| **Test dir** | `tests/` (`CMakeLists.txt` L116–119, `tests/CMakeLists.txt`) |
| **Test names** | `test_s3e_container`, `test_asset_loaders`, `test_asset_manager`, `test_platform_loop`, `test_moves_parser`, `test_asset_pipeline`, `test_stage_parser`, `test_list_parser`, `test_xml_parsers`, 5 DZ decoder tests, `test_json_atlas` |
| **Run** | `ctest --output-on-failure -C Release` |

---

## 8. Tools (RE & Development)

| Tool | Location | Purpose |
|------|----------|---------|
| **dzip.exe** | `download/` | Windows DZ archive extractor (Nekki proprietary) |
| **Ghidra** | `tools/ghidra/` | Reverse engineering of original game binaries |
| **radare2** | `tools/radare2/` | RE / disassembly |
| **apktool** / **jadx** | `tools/` | Android APK decompilation |
| **Python scripts** | `scripts/` (117 files) | DZ algorithm RE (ARM emulation with Unicorn, disassembly, decompression), binary format analysis, asset download/verification |
| **Test/fix scripts** | `scripts/` | `apply_all_fixes.py`, `verify_engine_code.py`, etc. |

---

## 9. Summary Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        resf2_app (EXE)                          │
│                     main.cpp → resf2_game                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌──────────────────┐ │
│  │ resf2    │  │ resf2    │  │ resf2   │  │ resf2            │ │
│  │ _game    │  │ _scene   │  │ _fight  │  │ _runtime         │ │
│  │          │  │          │  │         │  │ (AssetManager,   │ │
│  │ game.cpp │  │ scenes   │  │ moves,  │  │  Loop)           │ │
│  │ helpers  │  │ .cpp     │  │ anim    │  │                  │ │
│  └────┬─────┘  └────┬─────┘  └────┬────┘  └────────┬─────────┘ │
│       │              │             │                 │           │
│  ┌────┴─────┐  ┌─────┴──────┐  ┌──┴──────┐  ┌──────┴────────┐ │
│  │ resf2    │  │ resf2      │  │ resf2   │  │ resf2         │ │
│  │ _renderer│  │ _format    │  │ _core   │  │ _audio        │ │
│  │          │  │            │  │         │  │ (NullBackend) │ │
│  │ GLES2    │  │ XML, PLIST,│  │ math,   │  │               │ │
│  │ software │  │ JSON, BIN  │  │ nodes,  │  │               │ │
│  │ stb_image│  │ parsers    │  │ state   │  │               │ │
│  │ libwebp  │  │            │  │         │  │               │ │
│  └────┬─────┘  └────────────┘  └─────────┘  └───────────────┘ │
│       │                                                        │
│  ┌────┴──────────────┐  ┌─────────────────────────────────────┐│
│  │ resf2_platform     │  │ resf2_reverse                       ││
│  │                    │  │                                     ││
│  │ GlfwPlatform       │  │ .s3e (Marmalade) parser             ││
│  │  (Windows/Linux/   │  │ .plist (Cocos2D) parser             ││
│  │   macOS)           │  │ .atf (zlib tactics) parser          ││
│  │                    │  │ .fnt (BMFont) parser                ││
│  │ NullPlatform       │  │ .dz (Nekki derbh) decoder           ││
│  │  (headless tests)  │  │                                     ││
│  │                    │  │ Dependencies: zlib v1.3.1           ││
│  │ AndroidPlatform    │  └─────────────────────────────────────┘│
│  │  (planned)         │                                         │
│  └────────────────────┘                                         │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│                    External Dependencies                         │
│  ┌──────────┐  ┌──────────────┐  ┌───────────┐                 │
│  │ GLFW 3.4 │  │ zlib v1.3.1  │  │ OpenGL    │                 │
│  │ (window, │  │ (decompress  │  │ (ES 2.0 / │                 │
│  │  input)  │  │  .atf, .dz)  │  │  GL 2.1)  │                 │
│  └──────────┘  └──────────────┘  └───────────┘                 │
└─────────────────────────────────────────────────────────────────┘
```
