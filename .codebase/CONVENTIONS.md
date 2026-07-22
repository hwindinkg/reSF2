# reSF2 Coding Conventions

_Last updated: 2026-07-22. Derived from source files in `engine/`, `tests/`, and `main.cpp`._

---

## Table of Contents

1. [Naming Conventions](#1-naming-conventions)
2. [File Organization](#2-file-organization)
3. [Header Layout](#3-header-layout)
4. [Namespace Hierarchy](#4-namespace-hierarchy)
5. [Scene System](#5-scene-system)
6. [Error Handling & Diagnostics](#6-error-handling--diagnostics)
7. [Comment Style & Markers](#7-comment-style--markers)
8. [Modern C++ Usage](#8-modern-c-usage)
9. [Member Variable Style](#9-member-variable-style)
10. [Build Configuration](#10-build-configuration)
11. [Testing Conventions](#11-testing-conventions)

---

## 1. Naming Conventions

| Category | Convention | Example | Source |
|---|---|---|---|
| Variables (local) | `camelCase` | `dt_ms`, `asset_root`, `max_frames` | `main.cpp:4-8` |
| Member variables | `trailing_underscore_` | `elapsed_ms_`, `current_move_`, `move_state_` | `scenes.hpp:34`, `scenes.hpp:50` |
| Global variables | `g_` prefix | `g_debug_log`, `g_debug_log_enabled` | `helpers.cpp:14-15` |
| Functions | `camelCase` | `read_file()`, `on_enter()`, `request_scene_transition()` | `game.hpp:50` |
| Types / Classes / Structs | `PascalCase` | `BootScene`, `MoveDef`, `SceneContext`, `Texture2D` | `scene_system.hpp:38` |
| Enums | `PascalCase` (scoped) | `SceneId::Boot`, `Key::N`, `Overlay::Menu` | `scene_system.hpp:38-48` |
| Constants | `kPascalCase` | `kBootDurationMs`, `kMinDisplayMs`, `kMaxVertices` | `scenes.hpp:35`, `renderer.hpp:175` |
| Namespaces | `snake_case` | `resf2::scene`, `resf2::format` | `scenes.cpp:16` |
| Header / Source files | `snake_case` | `scene_system.hpp`, `stage_parser.cpp` | project-wide |
| Test files | `test_` prefix | `test_stage_parser.cpp`, `test_moves_parser.cpp` | `tests/` |
| Namespace aliases | short lowercase | `namespace plat = resf2::platform;` | `game.hpp:36-43` |
| Parameters | `camelCase` | `ctx`, `location`, `dt_ms`, `asset_root` | `scene_system.hpp:60-65` |

### 1.1 Example: Member Variable Naming (file:scenes.hpp)

```cpp
class BootScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Boot; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    uint32_t elapsed_ms_ = 0;                    // trailing underscore
    static constexpr uint32_t kBootDurationMs = 500;  // k-prefix constant
};
```

### 1.2 Example: Constant Naming (file:engine/renderer/renderer.hpp:175)

```cpp
static constexpr std::size_t kMaxVertices = 65536;
```

### 1.3 Example: Global Variable Naming (file:engine/game/helpers.cpp:14)

```cpp
FILE* g_debug_log = nullptr;
bool g_debug_log_enabled = true;
```

---

## 2. File Organization

### 2.1 Directory Layout (engine/)

```
engine/
  animation/     Animation data types and loading
  audio/         WAV/MP3 playback (SDL-based or custom)
  core/          Core data types (asset_manager, state, math, node)
  fight/         Combat system (moves, animation, AI, fighter)
  format/        File format parsers (XML, JSON, stage, location)
  game/          Main game class (game.hpp — 4867 lines, monolithic)
  network/       (unused)
  physics/       Physics / collision detection
  platform/      Platform abstraction (GLFW, input, window)
  renderer/      2D/3D rendering (OpenGL, software fallback, textures)
  reverse/       Reverse-engineering tools (DZ archive, plist, S3E, bitmap font)
  runtime/       Application loop and asset pipeline
  scene/         Scene system (scene_system.hpp, scenes.hpp/cpp)
  tools/         Offline asset processing tools
  ui/            UI primitives (text, button)
```

### 2.2 Single-Header Implementation Style

The most notable pattern is `engine/game/game.hpp` — a **4867-line** header that contains the entire `Game` class with inline method implementations. This is a deliberate "header-heavy" style:

- **`game.hpp`** (4867 lines) — class declaration + all inline implementations
- **`helpers.cpp`** (105 lines) — file-scope helper functions called by inline Game methods
- **`game.cpp`** — does NOT exist (all in the header)

The header comment at `game.hpp:45-48` explains:

```cpp
// ---------- Forward declarations for helper functions ----------
// These are defined in helpers.cpp and used by inline Game methods.
// They live at file scope (not in any namespace) for backward compatibility
// with the monolithic main.cpp they were extracted from.
```

Other modules use a **split style** (separate `.hpp` + `.cpp`):
- `scene_system.hpp` + `scene_system.cpp`
- `stage_parser.hpp` + `stage_parser.cpp`
- `renderer.hpp` + `renderer.cpp`

### 2.3 File Size Norms

| Module | Header | Source |
|---|---|---|
| Game | `game.hpp` — **4867 lines** (monolithic) | N/A (inline) |
| Scene system | `scene_system.hpp` — 248 lines | `scene_system.cpp` — ~120 lines |
| Scenes | `scenes.hpp` — 170 lines | `scenes.cpp` — 560 lines |
| Renderer | `renderer.hpp` — ~190 lines | `renderer.cpp` — 443 lines |
| Stage parser | `stage_parser.hpp` — 73 lines | `stage_parser.cpp` (separate) |
| Helpers | `helpers.hpp` (forward) | `helpers.cpp` — 105 lines |

> **Note:** The monolithic `game.hpp` is an outlier. All other modules follow a pattern of focused headers (70–250 lines) with separate `.cpp` implementations.

---

## 3. Header Layout

Every header follows a consistent structure:

```
1. #pragma once                          (line 1)
2. Platform guards (if needed)           #ifdef _WIN32 / #define NOMINMAX
3. Standard library includes             <algorithm>, <string>, <vector>, etc.
4. Project includes                      "engine/renderer/renderer.hpp"
5. Namespace aliases                     namespace plat = resf2::platform;
6. Forward declarations (if needed)      class SceneHost;
7. `namespace resf2::xxx {`             (C++17 nested namespace)
8. Public types / structs
9. Class declarations
10. } // namespace resf2::xxx
```

Example from `engine/format/stage_parser.hpp`:

```cpp
#pragma once                            // line 1

#include <string>
#include <vector>

namespace resf2::format {               // nested namespace

struct StageReward { ... };
struct StageWarrior { ... };
struct StageFight { ... };
struct StageBattle { ... };
struct StageZone { ... };
struct StageData { ... };

class StageParser {
public:
    bool parse(const std::string& xml, StageData& out);
    bool load_file(const std::string& path, StageData& out);
    const std::string& error() const { return error_; }
private:
    std::string error_;
};

} // namespace resf2::format
```

### 3.1 Include Style

- **Project includes** use full relative paths from project root: `"engine/runtime/loop.hpp"` — never bare filenames.
- **Standard includes** use angle brackets: `<cstdio>`, `<string>`, `<vector>`.
- **Windows includes** are guarded:
  ```cpp
  #ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
  #endif
  ```
- **Forward declarations** in headers use the `namespace resf2::xxx { class Foo; }` form (scene_system.hpp:30-32):
  ```cpp
  namespace resf2::platform { class Platform; }
  namespace resf2::renderer { class Renderer; }
  namespace resf2::format { struct StageData; }
  ```

---

## 4. Namespace Hierarchy

All code lives under the `resf2` top-level namespace. Nested namespaces use C++17 syntax:

```cpp
namespace resf2::scene { ... }       // engine/scene/ files
namespace resf2::format { ... }      // engine/format/ files
namespace resf2::renderer { ... }    // engine/renderer/ files
namespace resf2::platform { ... }    // engine/platform/ files
namespace resf2::runtime { ... }     // engine/runtime/ files
namespace resf2::audio { ... }       // engine/audio/ files
namespace resf2::core { ... }        // engine/core/ files
namespace resf2::fight { ... }       // engine/fight/ files
namespace resf2::ui { ... }          // engine/ui/ files
namespace resf2::dz { ... }          // engine/reverse/ (DZ decoder)
namespace resf2::reverse::plist { ... }  // engine/reverse/ (plist atlas)
namespace resf2::reverse::font { ... }   // engine/reverse/ (bitmap font)
namespace resf2::reverse::s3e { ... }    // engine/reverse/ (S3E container)
namespace resf2::reverse::atf { ... }    // engine/reverse/ (ATF tactics)
namespace resf2::runtime::assets { ... } // engine/runtime/ (asset manager)
```

**Source files** open the namespace at the top (e.g., `scenes.cpp:16`):
```cpp
namespace resf2::scene {
```
**Headers** close with a comment (`stage_parser.hpp:73`):
```cpp
} // namespace resf2::format
```

**Namespace aliases** are declared in `game.hpp:36-43` to shorten common namespaces:
```cpp
namespace plat = resf2::platform;
namespace rt = resf2::runtime;
namespace ren = resf2::renderer;
namespace fmt = resf2::format;
namespace aud = resf2::audio;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;
namespace scene = resf2::scene;
```

---

## 5. Scene System

The scene system follows a **Scene → SceneHost → SceneManager** pattern (`engine/scene/scene_system.hpp`).

### 5.1 Class Hierarchy

```
Scene (abstract base)
  ├── BootScene        (splash)
  ├── LoadingScene     (progress bar)
  ├── MainMenuScene    (dojo hub)
  ├── MapScene         (level/zone selection)
  ├── ShopScene        (stub)
  ├── SettingsScene    (stub)
  ├── DialogueScene    (pre-battle)
  ├── BattleScene      (combat)
  └── ResultsScene     (post-battle)

SceneHost (abstract — implemented by Game class)
  - request_scene_transition(SceneId)
  - host_load_location(), host_update_gameplay(), host_render_scene(), etc.

SceneManager (owns current scene, handles deferred transitions)
  - register_scene(SceneId, SceneFactory)
  - start(), transition_to(), update(), render()

SceneContext (passed to every scene hook)
  - SceneHost& host
  - Platform& platform
  - Renderer& renderer
  - uint32_t dt_ms
```

### 5.2 Scene Lifecycle

```
start(initial)
  → Factory creates scene → on_enter(ctx)
  → Loop:
       update(ctx)  → on_update(ctx)
                        [if transition requested, deferred to end of frame]
       render(ctx)  → on_render(ctx)
       [end of frame → on_exit(ctx) → factory(new) → on_enter(ctx)]
```

### 5.3 Scene Flow

```
Boot → Loading → MainMenu → Map → Dialogue → Battle → Results → MainMenu
                    ↓
                Shop / Settings (stubs)
```

### 5.4 Deferred Transitions

Transitions are deferred to avoid destruction during a scene's own hooks (`scene_system.cpp`):
```cpp
void SceneManager::transition_to(SceneId to) {
    std::printf("[scene] transition requested: %s -> %s\n", ...);
    pending_ = to;  // applied at end of update()
}
```

### 5.5 Scene Context (file:scene_system.hpp:60)

```cpp
struct SceneContext {
    SceneHost& host;
    platform::Platform& platform;
    renderer::Renderer& renderer;
    std::uint32_t dt_ms = 0;
};
```

---

## 6. Error Handling & Diagnostics

### 6.1 Diagnostic Logging

The project uses `std::printf` for runtime diagnostics (not a formal logging library). The pattern is `[scopename] message`:

```cpp
// scenes.cpp:51
std::printf("[boot] splash\n");

// scenes.cpp:71
std::printf("[loading] start\n");

// scenes.cpp:100
std::printf("[mainmenu] enter\n");

// scene_system.cpp:54,95,114
std::printf("[scene] enter %s\n", scene_name(current_id_));
std::printf("[scene] exit %s\n", scene_name(current_id_));
std::printf("[scene] enter %s\n", scene_name(current_id_));
```

### 6.2 Error / Stderr Output

Errors use `std::fprintf(stderr, ...)`:

```cpp
// scene_system.cpp:43
std::fprintf(stderr, "SceneManager: start() called but a scene is already active\n");

// scene_system.cpp:48
std::fprintf(stderr, "SceneManager: no factory registered for scene %s\n", ...);

// scene_system.cpp:102,106
std::fprintf(stderr, "[scene] no factory for %s, falling back to MainMenu\n", ...);
std::fprintf(stderr, "[scene] FATAL: no MainMenu factory\n");
```

### 6.3 Debug Log File

`debug_log()` writes to a file-based log (`helpers.cpp:23-30`):

```cpp
void debug_log(const char* fmt, ...) {
    if (!g_debug_log) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_debug_log, fmt, args);
    va_end(args);
    std::fflush(g_debug_log);
}
```

### 6.4 Function Return Values for Error Signaling

Functions signal errors via `bool` return values:

```cpp
// stage_parser.hpp
bool parse(const std::string& xml, StageData& out);
bool load_file(const std::string& path, StageData& out);
const std::string& error() const { return error_; }  // error string accessor
```

### 6.5 No Exceptions in Hot Paths

The codebase does NOT use C++ exceptions in game logic paths. Error handling is:
- `bool` returns for success/failure
- `std::fprintf(stderr, ...)` for errors
- `std::printf` for debug diagnostics
- `[[nodiscard]]` to prevent ignoring return values

---

## 7. Comment Style & Markers

### 7.1 Comment Style

All comments use `//` (never `/* */`). Comments explain **WHY** not **WHAT**:

```cpp
// Good — explains WHY (scenes.cpp:78-79):
// Start asset loading on the first update (not in on_enter, to allow
// the loading screen to render at least one frame first).

// Good — explains WHY (scenes.cpp:108-109):
// Delegate dojo gameplay (movement, combat, animation, physics, overlays)
// to the host. The host handles A/D, Space, K, M, T, Esc, etc.

// Good — explains WHY not WHAT (scene_system.hpp:101-103):
// Request a scene transition. The manager will call on_exit on the
// current scene and on_enter on the new scene at the end of the
// current frame (deferred transition — safe to call from within
// on_update / on_render).
```

### 7.2 File Header Comments

Most files have a header comment block explaining the module's purpose:

```cpp
// engine/scene/scene_system.hpp
//
// Scene / State Manager for reSF2.
//
// Provides a clean finite-state machine for the application-level game flow:
//   Boot -> Loading -> MainMenu -> Map -> Dialogue -> Battle -> Results -> MainMenu
//
// Each Scene has on_enter / on_update / on_render / on_exit hooks. ...
```

```cpp
// engine/scene/scenes.cpp
//
// Concrete Scene implementations.
```

```cpp
// engine/scene/scenes.hpp
//
// Concrete Scene implementations for reSF2's game flow.
// ...
// Scene flow:
//   Boot -> Loading -> MainMenu -> Map -> Dialogue -> Battle -> Results -> MainMenu
```

### 7.3 Section Headers

Section separators use `// ----------` for sub-sections and `// ===` for major sections:

```cpp
// ---------- Forward declarations for helper functions ---------- (game.hpp:45)
// ---------- Asset types ----------                              (game.hpp:64)
// ---------- Animation system ----------                         (game.hpp:108)
// ============================================================   (scenes.cpp:46)
// BootScene                                                      (scenes.cpp:48)
// ============================================================   (scenes.cpp:66)
```

### 7.4 Special Markers

| Marker | Meaning | Example (file:line) |
|---|---|---|
| `[ORIGINAL]` | Confirmed original-game behavior, often with PC JS source reference | `game.hpp:195` `// [ORIGINAL] MoveInside pivot alignment (from <Align><Pivot .../></Align>` |
| `[HEURISTIC-TODO]` | Guess / uncertainty about original behavior | `game.hpp:1299` `// [HEURISTIC-TODO] step_min_played: invented 400ms threshold...` |
| `TODO:` | Standard implementation gap | `renderer.cpp:80` `// TODO: Add proper KTX transcoding using ktx library or basisu` |

**`[ORIGINAL]` examples** (72 occurrences across headers, 3 in .cpp files):
```cpp
// game.hpp:195 — identifies original-game behavior
// [ORIGINAL] MoveInside pivot alignment (from <Align><Pivot .../></Align>

// game.hpp:212 — source reference
// [ORIGINAL] CurrentAnimation condition from moves.xml <Conditions>.

// game.hpp:959 — links to PC JS source
// [ORIGINAL] PC source: sf2.js tKa() — player input is NOT gated by

// game.hpp:2842 — verification claim
// [ORIGINAL] MoveInside Y alignment — VERIFIED from PC version sf2.js.

// scene_system.hpp:14 — stage annotation
// This is a Stage 9 addition. The existing Game class in main.cpp is the
// scene host...
```

---

## 8. Modern C++ Usage

### 8.1 Language Standard

C++23 (`CMakeLists.txt:11-13`):
```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

### 8.2 Features in Use

| Feature | Usage | Example |
|---|---|---|
| `[[nodiscard]]` | All "query" methods (getters, bool checks) | `scene_system.hpp:51,89,113,236,237` |
| `noexcept` | Simple accessors and pure-virtual getters | `scene_system.hpp:51` |
| `override` | Every virtual override | `scenes.hpp:29-32` |
| `final` | Concrete scene classes | `scenes.hpp:27` `class BootScene final : public Scene` |
| `constexpr` | Constants and simple functions | `scenes.hpp:35` `static constexpr uint32_t kBootDurationMs = 500;` |
| `std::optional` | Pending scene transition | `scene_system.hpp:245` `std::optional<SceneId> pending_;` |
| `std::expected` | Error-returning parsers (newer code) | `reverse/plist_atlas.hpp:75` `auto parse(...) -> std::expected<ParsedAtlas, ParseError>;` |
| `std::span` | Buffer views in parsers | `reverse/s3e_container.hpp:108` `parse(std::span<const std::byte> data)` |
| `std::byte` | Raw binary data | `game.hpp:50` `std::vector<std::byte> read_file(...)` |
| `std::unique_ptr` | Ownership (renderer, scene instances) | `scene_system.hpp:243` |
| `std::shared_ptr` | Shared atlas references | `game.hpp:68` `std::shared_ptr<plist::ParsedAtlas> atlas;` |
| C++17 nested namespaces | `resf2::scene`, `resf2::reverse::plist` | `scene_system.hpp:34` |
| Structured bindings | Iteration | `scenes.cpp:37` `for (const auto& p : input.pointers)` |
| `auto` return types | Parser return types | `plist_atlas.hpp:75` `auto parse(...)` |

### 8.3 Type Aliases

```cpp
using SceneFactory = std::function<std::unique_ptr<Scene>()>;  // scene_system.hpp:201
```

### 8.4 Integer Types

- `unsigned` for unsigned values (not `unsigned int`)
- `std::uint32_t`, `std::uint8_t`, `std::int32_t` for sized integers
- `std::size_t` for sizes

---

## 9. Member Variable Style

### 9.1 trailing_underscore_ Pattern

All member variables use a trailing underscore (never `m_` prefix):

```cpp
// scenes.hpp — BootScene
uint32_t elapsed_ms_ = 0;

// scenes.hpp — LoadingScene
uint32_t elapsed_ms_ = 0;
bool loading_started_ = false;

// scenes.hpp — MapScene
std::vector<ZoneEntry> zone_battles_;
int selected_ = 0;
float scroll_x_ = 0;
float scroll_target_x_ = 0;

// game.hpp — inline Game class
decltype(std::unique_ptr<ren::Renderer>) renderer_;
bool location_loaded_ = false;
float player_pos_x_ = 0;
std::string current_anim_;
```

### 9.2 Default Member Initializers

Structs use inline member initializers (C++11+):

```cpp
// stage_parser.hpp
struct StageWarrior {
    std::string template_name;
    float weapon_damage = 1.0f;
    bool not_ai = false;
};

// game.hpp
struct GameLocation {
    std::string color;
    float width = 0, height = 0;
    float wall = 0;
    float floor = 0;
    float player_x = 0, player_y = 0;
    float enemy_x = 0, enemy_y = 0;
};
```

---

## 10. Build Configuration

### 10.1 CMake Configuration (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.24)
project(reSF2 VERSION 0.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

### 10.2 Compiler Warning Flags

- **MSVC**: `/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /wd4127`
- **Clang/GCC**: `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wconversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`

### 10.3 Options

- `RESF2_BUILD_TESTS` (ON) — unit/integration tests
- `RESF2_BUILD_TOOLS` (ON) — offline asset tools
- `RESF2_USE_GLFW` (ON) — GLFW platform backend
- `RESF2_BUILD_HEADLESS` (OFF) — software renderer (no GPU)
- `RESF2_WERROR` (OFF) — warnings as errors
- `RESF2_ENABLE_SAN` (OFF) — sanitizers

---

## 11. Testing Conventions

### 11.1 Test File Layout

Tests live in `tests/`. Each test file is a standalone `.cpp` with a `main()`:

```
tests/
  test_stage_parser.cpp
  test_moves_parser.cpp
  test_asset_manager.cpp
  test_asset_loaders.cpp
  test_dz_decode.cpp
  test_s3e_container.cpp
  test_xml_parsers.cpp
  test_list_parser.cpp
  test_platform_loop.cpp
  test_dz_standalone.cpp
  ...
```

### 11.2 Test File Naming

- Pattern: `test_<module_or_feature>.cpp`
- Each test is a standalone executable with its own `main()`

### 11.3 Test Patterns

Tests use `using namespace` for the module under test (in source files only, never headers):

```cpp
// tests/test_s3e_container.cpp:23
using namespace resf2::reverse::s3e;

// tests/test_platform_loop.cpp:13-14
using namespace resf2::platform;
using namespace resf2::runtime;
```

### 11.4 Test CMake

See `tests/CMakeLists.txt` (not inspected separately, but referenced from top-level CMake).

---

## Summary Table

| Convention | Rule | Source Evidence |
|---|---|---|
| `#pragma once` | Every header | 39 headers in `engine/` |
| Member vars | `trailing_underscore_` | `scenes.hpp:34,50`, `game.hpp` inline (100+ examples) |
| Globals | `g_` prefix | `helpers.cpp:14-15` |
| Constants | `k` prefix + PascalCase | `scenes.hpp:35,52`, `renderer.hpp:175` |
| Functions | `camelCase` | `read_file()`, `on_enter()`, `request_scene_transition()` |
| Types | `PascalCase` | `BootScene`, `SceneContext`, `MoveDef` |
| Enums (scoped) | `enum class` with PascalCase | `SceneId::Boot`, `Key::N` |
| Namespaces | `resf2::xxx` (C++17 nested) | `scenes.cpp:16`, `renderer.cpp:14` |
| Diagnostics | `std::printf` with `[scope]` tags | `scenes.cpp:51,71,100` |
| Errors | `std::fprintf(stderr, ...)` | `scene_system.cpp:43,48,102` |
| Comments | `//` only, explain WHY | Throughout |
| `[ORIGINAL]` | Confirmed game behavior | 72 occurrences in `.hpp`, 3 in `.cpp` |
| `[HEURISTIC-TODO]` | Guess / uncertain | `game.hpp:1299` |
| `override` | On every virtual override | `scenes.hpp` (40+ occurrences) |
| `[[nodiscard]]` | On query methods | 92 occurrences across `engine/` |
| C++ Standard | C++23 | `CMakeLists.txt:11` |
