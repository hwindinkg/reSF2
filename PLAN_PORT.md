# reSF2 PC Port Plan: sf2_pc (JS) → C++ PC → Symbian

## Architecture Comparison

| Layer | sf2_pc (JS) | Current C++ | Target C++ |
|-------|-------------|-------------|------------|
| **Display Tree** | `Db→O→subclasses` | Manual render calls | `Node→SpriteNode` tree |
| **Renderer** | `Pf→Lk(Canvas2D)/Id(WebGL)` | OpenGL 2.1 hardcoded | OpenGL 3.3+ abstraction |
| **State Machine** | `PD[]` state stack (Hk,Ik,Jk,rf) | SceneManager FSM (9 scenes) | StateStack + Pushdown |
| **Asset Manager** | `G` class (ID-based) | Manual FILE* + DZ fallback | ID-based AssetManager |
| **Animation** | Frame-based `.dat` + `.bin` | Basic `.bin` interpolation | Full `.dat`/`.bin` system |
| **Fight Engine** | Stateful (idle→attack→hit→...) | Monolithic in main.cpp | Class-based state machine |
| **Input** | `Za` (keyboard+touch+gamepad) | GLFW+GetAsyncKeyState | GLFW+gamepad abstraction |
| **Audio** | Web Audio API (OGG/M4A) | STUB (empty) | OpenAL (OGG/WAV) |

## Strategy

Port in 6 phases. Each phase produces a **compilable, runnable** program.

### Phase 1: Foundation (`engine/core/`)
- `engine/core/node.hpp` — Display tree: `Node` base (name, children, active, time, update/render traversal)
- `engine/core/renderer.hpp` — Renderer abstraction: `Renderer` base (clear, viewport, transform stack, scissor)
- `engine/core/renderer_gl.cpp` — OpenGL 3.3 implementation (VAO+VBO, shader, batch rendering)
- `engine/core/state.hpp` — State stack: `State` base (update, render), `StateStack` (push, pop, layered)
- `engine/core/game_loop.hpp` — Fixed-timestep loop (60 Hz update, variable render)
- `engine/core/asset_manager.hpp` — Asset manager: load by path/ID, cache, decode dispatch
- `engine/core/input.hpp` — Input state: keyboard bitset, mouse pos, gamepad axes, virtual buttons
- `engine/core/math.hpp` — vec2, vec3, mat4, rect, color

**Output**: Window with clear color, FPS counter, state stack cycling test.

### Phase 2: Asset Pipeline (`engine/reverse/` reuse + `engine/format/`)
- Reuse existing parsers: `plist_atlas`, `bitmap_font`, `dz_reader`, `atf_tactics`
- Fix DZ type-4 decompression
- New parsers for JS format equivalents:
  - `engine/format/moves_parser.hpp` — Proper XML moves.xml parser (pugixml or rapidxml)
  - `engine/format/stages_parser.hpp` — stages.xml parser
  - `engine/format/list_parser.hpp` — list.xml items parser
  - `engine/format/location_parser.hpp` — params.xml location parser (refactor from main.cpp)
  - `engine/format/binary_stream.hpp` — Binary reader for `.dat` tactic files
- `engine/core/texture.hpp` — Texture2D class (load from PNG/DDS/KTX via stb_image)

**Output**: Loads all major asset types, prints info, renders sprites from atlases.

### Phase 3: UI Framework (`engine/ui/`)
- `engine/ui/text.hpp` — Bitmap text rendering (AngelCode .fnt)
- `engine/ui/button.hpp` — Button with click/hover states
- `engine/ui/label.hpp`, `engine/ui/panel.hpp`, `engine/ui/list.hpp`
- `engine/ui/layout.hpp` — Simple layout system (anchor, center, grid)
- `engine/ui/hud.hpp` — Health bar, energy bar, gold display
- Scenes from JS:
  - `BootScene` — Loading screen with progress bar
  - `MainMenuScene` — Play, Shop, Settings buttons
  - `LoadingScene` — Transition with fade

**Output**: Main menu displayed with real UI textures, can navigate to placeholder scenes.

### Phase 4: Fight Engine (`engine/fight/`)
- `engine/fight/fighter.hpp` — Fighter class (state machine: idle, walk, attack, hit, block, dead)
- `engine/fight/animation.hpp` — Animation player (bin keyframe interpolation, root motion)
- `engine/fight/move_selector.hpp` — Move selection from moves.xml (direction+button combos)
- `engine/fight/hit_detection.hpp` — Capsule/segment collision detection
- `engine/fight/damage.hpp` — Damage calculation (weapon + armor + perks)
- `engine/fight/ai.hpp` — AI opponent (tactic tables from .atf)
- `engine/fight/verlet.hpp` — Verlet physics (reuse from main.cpp, refactor)
- `BattleScene` — Full fight scene with two fighters, HUD, input

**Output**: Two fighters on a location background, can control player with keyboard, AI fights back.

### Phase 5: Game Systems (`engine/game/`)
- `engine/game/player.hpp` — Player profile (XP, level, gold, gems, inventory)
- `engine/game/inventory.hpp` — Equipment inventory (weapons, armor, helmets, ranged, magic)
- `engine/game/shop.hpp` — Shop (items from list.xml, IAP stubs)
- `engine/game/quests.hpp` — Quest/scripting system from quests.xml
- `engine/game/perks.hpp` — Perk system from perks.xml
- `engine/game/save.hpp` — Save/load (JSON to file)
- `engine/game/map.hpp` — World map scene (zone selection)
- `engine/game/progression.hpp` — Zone → fights → rewards loop
- Scenes: `ShopScene`, `MapScene`, `DialogueScene`, `ResultsScene`, `EquipmentScene`

**Output**: Complete game flow: Boot → Menu → Map → Fight → Results → Map → ...

### Phase 6: Audio + Polish (`engine/audio/` + `engine/fx/`)
- `engine/audio/audio.hpp` — Audio system (OpenAL for PC, WAV+OGG playback)
- `engine/audio/sound.hpp` — Sound effect (WAV from assets)
- `engine/audio/music.hpp` — Music track (MP3/OGG)
- `engine/fx/particles.hpp` — Particle system from JSON configs
- `engine/fx/effects.hpp` — Hit effects, block sparks, screen shake
- Polish: smooth transitions, screen shake, proper menu animations

**Output**: Complete game with audio, particles, and polish.

---

## Test/Verification Strategy

1. **Each phase**: `cmake --build build` must succeed
2. **Each phase**: `build/bin/Release/resf2_app.exe` must launch and show expected behavior
3. **Regression**: Existing input-script tests must still pass
4. **Format parsers**: Existing unit tests (`test_s3e_container`, `test_asset_loaders`) must pass
5. **Manual test**: Fight between two AI players, verify no crashes for 5 minutes

## Build System

- Top-level CMake: add `engine/` subdirectories as library targets
- `resf2_app` links all engine libraries
- C++23 for PC, CMake ≥ 3.24, GLFW 3.4, OpenGL 3.3+, ZLIB, OpenAL (phase 6)
- Option: `RESF2_USE_OPENGL_ES` for GLES2 mode (prep for Symbian)

## Current State Reference

### sf2_pc JS Class Hierarchy (relevant subset)
```
ue (lifecycle base)
  └── Db (display tree node)
       ├── O (has transform node)
       │    ├── Tk (layout container)
       │    ├── lk / jk / ik (screen transitions)
       │    ├── gk / hk / fk (UI widgets)
       │    └── wk / qk (game objects)
       ├── Gk (copyright overlay)
       ├── Mk (scene wrapper)
       └── Re (debug overlay)

Pg (game base)
  └── L (SF2 game)
       └── K (global singleton)

Pf (renderer base)
  ├── Lk (Canvas2D)
  └── Id (WebGL)

nc (input/state base)
  ├── Hk (mouse/key state)
  ├── Jk (touch state)
  ├── Ik (transition overlay)
  └── rf (gamepad state)

G (asset manager, static)
Qg (game loop)
Ss (audio system, static)
```

### Current C++ Engine Limits
- Monolithic Game class (main.cpp, 4190 lines)
- OpenGL 2.1 (no VAO, VBO-only)
- Primitive string-based XML parsing
- No audio, no shop, no AI, no proper UI
- DZ type-4 broken
- Animation, physics, network modules are empty stubs
- Y-positioning incorrect
- moves.xml parser loses ComplexInterval, Distance, Locks, MoveInside Pivot
