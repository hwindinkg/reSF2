# reSF2 Architecture

## Overview

reSF2 is a C++ reimplementation of the Shadow Fight 2 game engine. It replaces the original Marmalade SDK runtime with a custom OpenGL ES 2.0 renderer, audio mixer, and scene management system, while retaining the original game's asset formats (DZ archives, BMFont, PLIST atlases, XML configs, and .bin animation files).

The architecture follows a **Scene-as-State-Machine** pattern: a `Game` class (the scene host) owns a `SceneManager` that drives scene transitions. The `Game` class contains all heavy game logic (~4867 lines in `game.hpp`) — rendering, physics, animation, combat, AI, audio, HUD, and persistence. Scenes are lightweight orchestrators that call back into `Game` via the `SceneHost` interface.

---

## High-Level System Architecture

```
┌─────────────────────────────────────────────────────┐
│  main.cpp                                           │
│  ┌──────────────────────────────────────────────┐   │
│  │  GlfwPlatform (window, GL context, input)    │   │
│  └──────────┬───────────────────────────────────┘   │
│             │                                        │
│  ┌──────────▼───────────────────────────────────┐   │
│  │  Game (SceneHost)                             │   │
│  │  ┌─────────────────────────────────────┐     │   │
│  │  │  SceneManager                        │     │   │
│  │  │  ┌─────────┐ ┌──────────┐ ┌──────┐ │     │   │
│  │  │  │ Scene A │ │ Scene B  │ │ ...  │ │     │   │
│  │  │  └─────────┘ └──────────┘ └──────┘ │     │   │
│  │  └─────────────────────────────────────┘     │   │
│  │  ┌─────────────────────────────────────┐     │   │
│  │  │  Renderer (OpenGL ES 2.0)           │     │   │
│  │  │  - Camera2D, SpriteBatch, Textures  │     │   │
│  │  └─────────────────────────────────────┘     │   │
│  │  ┌─────────────────────────────────────┐     │   │
│  │  │  AudioEngine (WAV mixer)            │     │   │
│  │  └─────────────────────────────────────┘     │   │
│  │  ┌─────────────────────────────────────┐     │   │
│  │  │  Asset System                       │     │   │
│  │  │  - DzRegistry (DZ archives)         │     │   │
│  │  │  - Filesystem fallback              │     │   │
│  │  │  - XML parser, PLIST atlas loader   │     │   │
│  │  └─────────────────────────────────────┘     │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

---

## Scene Flow Diagram

```
                    ┌──────────┐
                    │   Boot   │  (1s splash)
                    └────┬─────┘
                         │
                    ┌────▼─────┐
                    │ Loading  │  (asset load + progress bar)
                    └────┬─────┘
                         │
                    ┌────▼──────┐
              ┌─────│ MainMenu  │◄──────────────────────────────┐
              │     │ (dojo+bag)│                                │
              │     └────┬──────┘                                │
              │          │                                        │
              │    ┌─────▼──────┐    ┌───────────┐    ┌────────┐ │
              │    │    Map     │───►│ Dialogue  │───►│ Battle │ │
              │    │ (zone sel) │    │ (text box)│    │(combat) │ │
              │    └────────────┘    └───────────┘    └───┬────┘ │
              │                                           │      │
              │    ┌─────────┐   ┌───────────┐    ┌──────▼──┐  │
              │    │  Shop   │   │ Settings  │    │ Results │  │
              │    │ (stub)  │   │ (stub)    │    │(vic/def)│  │
              │    └─────────┘   └───────────┘    └────┬─────┘  │
              │                                        │        │
              └────────────────────────────────────────┘        │
                         │                                        │
                         └────────────────────────────────────────┘
```

**Transition mechanics:**
- All transitions are **deferred** — `transition_to()` stores the target in `pending_`; the actual switch (`on_exit` → `on_enter`) occurs at the end of the current frame's `update()` call (`apply_transition()`).
- After each transition, `clear_input_edges()` is called to prevent input carryover (e.g., a click that triggered a menu transition should not also fire an action in the next scene).
- `request_scene_transition()` is the `SceneHost` method scenes call; it delegates to `SceneManager::transition_to()`.
- When `--replay` mode is active, `request_scene_transition()` hijacks transitions to `MainMenu` and redirects to `Battle` instead.

---

## Key Design Decisions

### 1. Monolithic Game Class as Scene Host
**File:** `engine/game/game.hpp` (lines 384–4867)

The `Game` class implements `scene::SceneHost` and contains all game logic inline:
- Combat, physics, animation, rendering, HUD, menus, audio, asset loading, persistence
- Public methods called by scenes via the `SceneHost` interface (~30 virtual methods)

**Rationale** (from inline comments): The Game class was extracted from a monolithic `main.cpp`. The scene system was added later (Stage 9) as a lightweight FSM layer on top. Heavy logic stays in Game; scenes only orchestrate transitions.

### 2. Scene/SceneHost Split via Interface
**File:** `engine/scene/scene_system.hpp` (lines 60–195)

Scenes receive a `SceneContext` containing references to the `SceneHost`, `Platform`, and `Renderer`. This prevents scenes from depending on the full `Game` type — they interact only through the abstract `SceneHost` interface.

### 3. Deferred Scene Transitions
**File:** `engine/scene/scene_system.hpp` (lines 218–246)

`SceneManager::transition_to()` sets a `pending_` optional. On the next `update()` call, `apply_transition()` is invoked:
1. Calls `on_exit()` on the current scene
2. Calls `clear_input_edges()` on the platform (resets all `just_pressed` flags)
3. Constructs the new scene from the factory
4. Calls `on_enter()` on the new scene

This prevents re-entrancy issues when a scene requests a transition during its own `on_update()`.

### 4. Input Edge Clearing on Transition
**File:** `engine/scene/scene_system.hpp` (line 234)

After every transition, all `just_pressed` input flags are cleared. This prevents a keypress that triggered a menu button from also firing an action in the newly entered scene on its first frame.

### 5. Custom Renderer Over Cocos2d-x
**File:** `engine/renderer/renderer.hpp` (lines 222–312)

The renderer is a custom OpenGL ES 2.0 / OpenGL 2.1 implementation modeled after Cocos2d-x 2.x. It provides:
- `SpriteBatch`: batched sprite rendering with a single VBO
- `Camera2D`: orthographic projection with smooth follow, screen shake, zoom
- `Texture2D`: loads PNG/WebP/KTX via stb_image
- Dual coordinate systems: world space (Y-up) and screen space (Y-down, top-left origin)

### 6. Moves.xml-Driven Combat
**File:** `engine/game/game.hpp` (lines 130–243, 3293–3400)

All combat moves are defined in `moves.xml`, parsed at runtime into `MoveDef` structs:
- Template strings (e.g., `"1key|Forward|Unarmed|Punch"`) define key count, direction, weapon, and move type
- Intervals define Attack frames, Block windows, Uninterrupt frames, and Complex conditions
- Moves are selected by matching input (punch/kick + direction) against priority-sorted candidates
- Hit detection uses segment-segment closest-point collision between attacking edges (from `moves.xml`) and collisible edges (from `body.xml`/`head.xml`)

### 7. Verlet Physics for Punching Bag
**File:** `engine/game/game.hpp` (lines 321–337, 3088–3166)

The dojo punching bag uses Verlet integration:
- `pos_new = 2*pos - pos_prev + acc * dt^2`
- Nodes have mass, attenuation (damping), and can be fixed (node 12 = ceiling attachment)
- Constraints maintain edge lengths
- Hit impulses are distributed along edges by hit position ratio

### 8. DZ Archive + Filesystem Fallback
**File:** `engine/reverse/dz_reader.hpp` (lines 1–97)

Assets live in Marmalade DZ archives (`.dz` files). The `DzRegistry` singleton:
1. Opens `files.dz` and `animations.dz` from multiple search paths
2. Reads files directly (type=1 copy) or decompresses (type=8 gzip via zlib)
3. Falls back to filesystem directories for type=4 (DZ custom compression, not yet supported)
4. `model_paths()` utility searches multiple path combinations for assets

---

## Data Flow Between Components

### Per-Frame Loop (main.cpp lines 50–68)

```
platform.poll_events()
    │
    ▼
game.on_update(platform, dt)
    │
    ├── scene_manager_.update(ctx)
    │       │
    │       └── current_scene.on_update(ctx)
    │               │
    │               └── ctx.host.host_update_gameplay(dt)
    │                       │
    │                       ├── platform→tick_input_script()
    │                       ├── Input processing (movement, combat, AI)
    │                       ├── AudioEngine::instance().update(dt)
    │                       ├── Enemy AI state machine
    │                       ├── Animation update (update_animation)
    │                       ├── Hit detection (segment-segment collision)
    │                       ├── Verlet physics update (bag)
    │                       └── Camera follow + shake
    │
    ├── Check pending transition → apply_transition()
    │
    ▼
game.on_render(platform)
    │
    ├── renderer→begin_frame()
    ├── scene_manager_.render(ctx)
    │       │
    │       └── current_scene.on_render(ctx)
    │               │
    │               └── ctx.host.host_render_scene()
    │                       │
    │                       ├── render_location() (parallax layers)
    │                       ├── render_enemy_fighter() or render_punching_bag()
    │                       ├── render_character() (skeleton + body model)
    │                       ├── hit sparks overlay
    │                       ├── render_hud() (health, currency, energy)
    │                       ├── render_menu_expanded() (scroll menu)
    │                       └── render_dialog_overlay()
    │
    └── renderer→end_frame()
    │
    ▼
platform.swap_buffers()
```

### Scene Transition Data Flow

```
User clicks "Map" in MainMenu
    │
    ├── MainMenuScene::on_update()
    │       │
    │       ├── host_update_gameplay(dt)     (processes all gameplay input first)
    │       ├── Detects click on Map menu item
    │       └── ctx.host.request_scene_transition(SceneId::Map)
    │               │
    │               └── SceneManager::transition_to(SceneId::Map)
    │                       │
    │                       └── pending_ = SceneId::Map
    │
    └── (end of frame) SceneManager::update() continues
            │
            └── pending_ has value → apply_transition(ctx)
                    │
                    ├── current_scene.on_exit(ctx)     [MainMenuScene]
                    ├── platform.clear_input_edges()   [prevents carryover]
                    ├── current_ = factories_[Map]()   [construct MapScene]
                    ├── current_->on_enter(ctx)         [MapScene]
                    └── pending_.reset()
```

---

## Asset Loading Pipeline

```
Asset Root (CLI --assets or CWD)
    │
    ├── DZ Archives (DzRegistry::instance())
    │   ├── files.dz        → XML configs, textures, sounds
    │   │   (type=1 copy: direct read, type=4 DZ custom: fallback to disk)
    │   └── animations.dz   → .bin animation files
    │       (type=8 gzip: decompressed via zlib)
    │
    ├── Filesystem Fallback (add_fallback_dir)
    │   └── Extracted assets on disk (when DZ decompression fails)
    │
    ├── Location Loading (init_location, line 2116)
    │   ├── params.xml      → GameLocation (layers, wall, floor, player/enemy pos)
    │   ├── PLIST atlases   → AtlasRef (Texture2D + ParsedAtlas)
    │   ├── skeleton.xml    → SkelNode map + edge definitions
    │   ├── body.xml        → BodyModel (body capsules, edges, triangles)
    │   ├── head.xml        → merged into BodyModel (HEAD- prefix)
    │   ├── bag model       → Verlet node/constraint setup for punching bag
    │   ├── .bin files      → AnimationData (frame→node→position)
    │   ├── moves.xml       → MoveDef map (combat moves)
    │   ├── stages.xml      → StageData (zones, battles, fights, rewards)
    │   ├── HUD textures    → UI panel, icons, gold, energy, level bar
    │   ├── Menu textures   → Scroll menu caps, center, icons
    │   ├── BMFont .fnt+.png → Bitmap font for text rendering
    │   └── WAV sounds      → PcmData loaded into AudioEngine
    │
    └── Loading Screen (load_loading_screen, line 2060)
        └── Loading image textures from asset root
```

### Asset Path Resolution

The `model_paths()` helper (helpers.cpp) searches multiple path combinations for each asset file, supporting both the original game's directory structure and extracted asset layouts:

```
{asset_root}/assets/{path}
{asset_root}/{path}
{asset_root}/assets/768/{path}
{asset_root}/768/{path}
{asset_root}/assets/1536/{path}
{asset_root}/1536/{path}
```

---

## Component Directory Map

| Directory | Purpose | Key Files |
|-----------|---------|-----------|
| `engine/game/` | Game class, scene host, helpers | `game.hpp` (4867-line monolith) |
| `engine/scene/` | Scene system interface + implementations | `scene_system.hpp`, `scenes.cpp`, `scenes.hpp` |
| `engine/renderer/` | GLES2/OpenGL 2.1 renderer | `renderer.hpp` (Camera2D, SpriteBatch, Texture2D) |
| `engine/platform/` | Platform abstraction (window, input, GL) | `platform.hpp`, `glfw_platform.hpp` |
| `engine/audio/` | WAV loader + mixer | `audio.hpp` (WavSound, AudioEngine, AudioBackend) |
| `engine/reverse/` | Reverse-engineered format parsers | `dz_reader.hpp`, `plist_atlas.hpp`, `bitmap_font.hpp` |
| `engine/format/` | XML/document format parsers | `xml_doc.hpp`, `stage_parser.hpp` |
| `engine/runtime/` | Runtime loop interface | `loop.hpp` (IGame interface) |
| `engine/physics/` | Physics engine (future) | — |
| `engine/network/` | Network layer (future) | — |
| `engine/animation/` | Animation system (future) | — |
| `engine/ui/` | UI framework (future) | — |
| `engine/core/` | Core utilities | — |
| `engine/fight/` | Fight-specific logic | — |

---

## Combat System Architecture

```
User Input (O/P/WASD + direction)
    │
    ├── Move Selection (host_update_gameplay)
    │   ├── Filter by: key_count, direction, weapon, is_uninterrupt, current_animation
    │   ├── Priority-sort candidates
    │   └── Play winning animation + set current_move_
    │
    ├── Hit Detection (per-frame during Attack interval)
    │   ├── Get attacking edge endpoints from animation pose
    │   ├── Transform to world space (facing + position)
    │   ├── Segment-segment closest-point distance test
    │   │   └── Against ALL collisible edges (bag or enemy fighter)
    │   └── On hit: apply impulse, play sound, spawn sparks, flash
    │
    ├── Animation Update (update_animation)
    │   ├── Advance anim_time_ by dt
    │   ├── Interpolate between current/next frame
    │   ├── Apply root motion displacement
    │   └── Populate anim_node_pos_ map
    │
    ├── Enemy AI State Machine
    │   ├── States: idle=0, approach=1, attack=2, retreat=3, block=4
    │   ├── Timer-driven decisions (every ~0.8s)
    │   ├── Distance-based behavior (far→approach, mid→attack, close→mix)
    │   └── Health-sensitive (low health → more defensive)
    │
    └── Verlet Physics (punching bag)
        ├── Integration: pos = pos*2 - prev + acc*dt^2
        ├── Constraint satisfaction (edge length preservation)
        ├── Attenuation (damping per node)
        └── Hit impulses distributed by edge hit position
```

---

## Integration Points

| Interface | Provider | Consumer | Purpose |
|-----------|----------|----------|---------|
| `rt::IGame` | `Game` | `main.cpp` loop | Lifecycle: on_init, on_update, on_render, on_shutdown |
| `scene::SceneHost` | `Game` | `Scene` implementations | Asset loading, rendering, persistence, state queries |
| `platform::Platform` | `GlfwPlatform` | `Game`, `Scene` | Window, input, GL context, timing |
| `AudioBackend` | `NullAudioBackend` | `AudioEngine` | PCM output device abstraction |
| `DzRegistry` | Singleton | `Game::init_location` | Archive-based file reading |

---

## Persistence Layer

**File:** `engine/game/game.hpp` (lines 5–52 in SceneHost impl)

```
Save:   temp_directory_path() / "resf2_save.json"
Load:   Same path (simple string-based JSON parser)

Schema:
{
  "version": 1,
  "current_level": "ZONE_1/EGYPT_1",
  "battle_result": "victory",
  "completed_levels": ["ZONE_1/EGYPT_1", "ZONE_1/EGYPT_2"],
  "currency": 500
}
```

- `host_save_progress()`: Writes JSON manually via `std::ofstream`
- `host_load_progress()`: Reads JSON by string searching (no JSON library dependency)
- `host_add_completed_level()`: Stores level + auto-saves
- Save location: OS temp directory (not configurable)
