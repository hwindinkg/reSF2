# reSF2 Directory Structure

> Project root: `E:\reSF2`
> C++23 clean-room reimplementation of the Shadow Fight 2 engine (v0.0.3)
> Build system: CMake 3.24+, MSVC/Clang/GCC

---

## Top-Level Layout

```
reSF2/
├── .codebase/         # Agent-generated project documentation
├── .flowdeck/         # FlowDeck agent orchestration config
├── .opencode/         # OpenCode agent/plugin config
├── .planning/         # Feature planning artifacts
├── build/             # CMake build output (bin/, lib/, _deps/)
├── assets/            # Game assets (original + extracted)
├── docs/              # 27 reverse-engineering documentation files
├── engine/            # Core C++ engine source (15 subdirectories)
├── tests/             # 15 C++ test executables + test data
├── scripts/           # 107 Python/C/shell RE and download scripts
├── tools/             # RE tools (jadx, Ghidra, radare2)
├── reverse/           # Original game binaries for RE
├── upload/            # Working files, test animation .bin files
├── Testing/           # Integration test scenarios and results
├── sf2/               # Extracted APK contents (Java, libs, assets)
├── sf2_pc/            # Extracted Windows Store version
├── sf2_symbian/       # Symbian S60v3 port source and build files
├── OriginalWindowsFiles/ # Original Windows Store game files
├── download/          # Downloaded tools (dzip.exe)
---
├── main.cpp           # Main entry point (GLFW + full engine)
├── headless_main.cpp  # Headless test driver (software renderer)
├── main_port.cpp      # Port demo entry point (modular engine)
├── CMakeLists.txt     # Top-level build configuration
├── build.bat          # Convenience build script
├── BUILD.md           # Build instructions
├── README.md          # Project overview
├── HANDOFF.md         # Work handoff notes
├── check_xml.py       # XML validation utility
```

---

## `engine/` — Core C++ Engine Source

```
engine/
├── CMakeLists.txt         # Engine-level CMake (targets: resf2_core, resf2_fight, etc.)
├── .keep                  # Placeholder for stub directories
│
├── game/                  # High-level game logic
│   ├── game.hpp           # 255KB — main Game class (largest file in project)
│   ├── game.cpp           # 21 lines — delegates to game.hpp template impl
│   ├── helpers.cpp/hpp    # Asset path resolution, utility functions
│   └── types.hpp          # Core game types and enums
│
├── scene/                 # Scene management & state machine
│   ├── scene_system.hpp/cpp  # Scene lifecycle (init, update, render, shutdown)
│   ├── scenes.hpp/cpp     # 24KB — all scene implementations (loading, menu, battle, etc.)
│   └── CMakeLists.txt
│
├── fight/                 # Combat system
│   ├── ai.hpp             # AI opponent behavior
│   ├── fighter.hpp        # Fighter state, attributes, equipment
│   ├── animation.hpp/cpp  # Animation state machine, interpolation, blending
│   ├── moves.hpp/cpp      # Move definitions, startup/active/recovery frames
│   └── CMakeLists.txt
│
├── core/                  # Shared engine infrastructure
│   ├── asset_manager.hpp/cpp  # Asset lifecycle (load, cache, unload)
│   ├── game_loop.hpp      # Fixed-timestep game loop template
│   ├── input.hpp          # Input event types and bindings
│   ├── input_system.hpp/cpp   # Keyboard/gamepad input handling
│   ├── math.hpp/cpp       # Vectors, matrices, geometry utilities
│   ├── node.hpp/cpp       # Scene graph node hierarchy
│   ├── renderer2d.hpp     # 2D rendering API abstraction
│   └── state.hpp/cpp      # Finite state machine for game states
│
├── renderer/              # Rendering backends
│   ├── renderer.hpp/cpp       # Abstract renderer interface
│   ├── software_renderer.hpp/cpp # Software rasterizer (headless mode)
│   ├── gl_loader.hpp/cpp      # OpenGL function loading
│   ├── stb_image.h            # Image loading (stb)
│   ├── stb_image_write.h      # PNG output
│   ├── stb_image_impl.cpp     # stb implementation file
│   └── webp/                  # WebP decoder (submodule)
│
├── format/                # Asset format parsers
│   ├── xml_doc.hpp/cpp        # Generic XML document parser
│   ├── list_parser.hpp/cpp    # .list file parser (file index)
│   ├── location_parser.hpp/cpp # Location params.xml parser
│   ├── stage_parser.hpp/cpp   # Stage config parser
│   └── json_atlas.hpp/cpp     # JSON sprite atlas parser (cocos2d)
│
├── reverse/               # Reverse-engineered original formats
│   ├── dz_decoder.hpp/cpp      # DZ archive decoder (proprietary compression)
│   ├── dz_reader.hpp/cpp       # DZ file reader
│   ├── dz_external_decoder.hpp/cpp # External dzip.exe bridge
│   ├── s3e_container.hpp/cpp   # S3E container format reader
│   ├── plist_atlas.hpp/cpp     # Apple plist sprite atlas parser
│   ├── atf_tactics.hpp/cpp     # ATF tactics file parser
│   ├── bitmap_font.hpp/cpp     # Bitmap font renderer
│   ├── dz/                     # DZ format notes (README.md)
│   ├── s3e_classes.txt         # S3E class hierarchy dump
│   ├── s3e_imports.txt         # S3E imported function list
│   └── s3e_shaders.txt         # S3E shader source dumps
│
├── platform/              # Platform abstraction layer
│   ├── platform.hpp/cpp        # Abstract platform (window, input, timing)
│   ├── glfw_platform.hpp/cpp   # GLFW backend implementation
│   └── CMakeLists.txt
│
├── runtime/               # Minimal headless runtime bootstrap
│   ├── loop.hpp/cpp           # Minimal game loop (headless mode)
│   ├── asset_manager.hpp/cpp  # Runtime-specific asset manager
│   └── CMakeLists.txt
│
├── audio/                 # Audio subsystem (stub)
│   ├── audio.hpp/cpp          # Audio interface (WIP/stub)
│   └── CMakeLists.txt
│
├── ui/                    # UI components
│   ├── button.hpp/cpp         # Button widget
│   ├── text.hpp/cpp           # Text rendering
│   └── CMakeLists.txt
│
├── animation/             # Animation system (stub/directory only)
│   └── CMakeLists.txt
│
├── network/               # Network subsystem (stub)
│   └── CMakeLists.txt
│
├── physics/               # Physics subsystem (stub)
│   └── CMakeLists.txt
│
└── tools/                 # Engine build tools (stub)
    └── CMakeLists.txt
```

---

## `assets/` — Game Assets

```
assets/
├── .keep
│
# --- Game configuration XML files ---
├── settings.xml              # Main game settings
├── usersDefault.xml          # Default player profile
├── userSettings.xml          # User-specific settings
├── internalSettings.xml      # Engine internal settings
├── devices.xml               # Device capability database
├── config_cdn.xml            # CDN configuration
├── obbSettings.xml           # APK expansion file settings
├── versionController.xml     # Version management
├── googleActivity.ini        # Google Play activity config
├── Achievements.xml          # Achievement definitions
├── CharacterProgress.xml     # Character progression data
├── ComputerSettings.xml      # AI opponent settings
├── forge.xml                 # Weapon forging recipes
├── perks.xml                 # Perk/ability definitions
├── quests.xml                # Quest definitions
├── raid_stages_default.xml   # Raid mode stages
├── stages.xml                # Stage/level definitions
├── tacticSettings.xml        # AI tactic configuration
├── zone_*_list.xml           # Zone progression lists (zones 2-7, IM)
│
# --- Archived data (DZ format) ---
├── animations.dz            # Compressed animation data (all .bin files)
├── files.dz                 # Compressed game files archive
├── ZONE_*.dz                # Per-zone compressed archives (zones 2-7, IM)
│
# --- Extracted file indices ---
├── list.xml                 # Master file list
├── files/
│   ├── files_list.xml       # Index of extracted game files
│   └── settings.xml         # Per-file settings
│
# --- Extracted file data ---
├── files_extracted/         # (empty — extraction destination)
│
# --- Animations ---
├── animations/
│   └── binary/              # Per-weapon animation .bin files (~350+ files)
│       ├── fists_idle.bin, air_punch.bin, axe_kick.bin, ...
│       ├── back_flip.bin, front_flip.bin, ...
│       └── ... (weapon-specific: axe_, sword_, staff_, spear_, etc.)
│
# --- Locations (56 total) ---
├── locations/
│   ├── dojo/
│   │   └── params.xml        # Location parameters (colors, layers, spawn points)
│   ├── arena/
│   │   └── params.xml
│   ├── autumn/ ... bamboo_grove/ ... battlefield/ ... bridge/
│   ├── burning_town/ ... castle_and_bridge/ ... cave/ ... chess_yard/
│   ├── dark_room/ ... emerald_forest/ ... factory/ ... fatum_raid/
│   ├── flooded_village/ ... flowers_field/ ... flying_rocks/
│   ├── fuji/ ... fungus_raid/ ... graveyard_ships/ ... heaven/
│   ├── ice_cave/ ... lamps_on_water/ ... lava/ ... magic_rocks/
│   ├── megalith_raid/ ... moon/ ... mountain/ ... neural_network/
│   ├── new_year_dojo/ ... night_bridge/ ... pink_lake/ ... road/
│   ├── ruins_village/ ... sakura/ ... shadow_gate/ ... ships/
│   ├── skyport/ ... snowy_peak/ ... spaceship/ ... statue/
│   ├── stone_dragon/ ... stone_forest/ ... swamp/ ... village/
│   ├── volcano/ ... vortex_raid/ ... vulcan_raid/
│   └── waterfall/ ... (and small variants)
│
# --- Weapon tactics (ATF format) ---
├── tactics/
│   ├── fists_*.atf, sword_*.atf, axe_*.atf, ...
│   ├── staff_*.atf, spear_*.atf, claws_*.atf, ...
│   ├── nunchaku_*.atf, tonfa_*.atf, sai_*.atf, ...
│   ├── ... (all weapon-type × weapon-type combinations)
│   ├── priorityandcapabilitytable.xml  # Weapon priority tables
│   └── _.atf                          # Default/unarmed tactics
│
# --- 3D model definitions (XML) ---
├── models/
│   ├── body.xml                    # Player body model (nodes, edges, capsules, triangles)
│   ├── skeleton.xml                # Player skeleton (bone hierarchy)
│   ├── skeleton_punching_bag.xml   # Punching bag skeleton
│   ├── punching_bag.xml            # Punching bag collision capsules
│   └── armor_*.xml                 # Per-armor visual model definitions
│
# --- Music ---
├── music/
│   ├── menu.mp3                    # Menu theme (1.3MB)
│   ├── fight1_samurai_spirit.mp3 through fight37_Titan_Epic_Fight.mp3
│   └── act.mp3                     # Act transition track
│
# --- Sound effects ---
├── sounds/
│   ├── *.wav                       # 150+ sound effects
│   ├── armor.wav, hit*.wav, bodyfall*.wav
│   ├── weapon-specific: bow_fast.wav, harpoon_shoot.wav, ...
│   ├── character: f_pl_attack*.wav, f_pl_hit*.wav, f_pl_jump*.wav
│   └── environment: blizzard_*.wav, earthquake.wav, ...
│
# --- Video ---
├── video/
│   ├── intro.mp4                   # Intro cinematic
│   ├── Shadow_fight_ending.mp4     # Ending cinematic
│   ├── shadow_gate.mp4             # Shadow gate cutscene
│   ├── Shadow_fight_ending.mp3     # Ending audio track
│   └── shadow_gate.mp3             # Shadow gate audio track
│
# --- HD Texture atlases ---
├── 768/                            # Low-res textures (iPad)
├── 1536/                           # HD textures (iPad retina)
│
# --- Other asset directories ---
├── assets/           # Additional config files
├── cocoGUI/          # Cocos2d GUI definitions
├── credits/          # Credits text (eng.xml, rus.xml)
├── localizations/    # 12 locale files (eng, rus, chn, fra, ger, ita, jpn, kor, por, spa, tur)
├── quest_extensions/ # DLC quest configs (dynamic_discounts.xml, promotions.xml, raid_quests.xml)
└── cocoGUI/          # GUI layout definitions
```

### Location Pattern (`assets/locations/<name>/params.xml`)

Each location contains a single `params.xml` that defines:
- **Root element**: background color, world dimensions, floor Y position
- **Layer elements**: parallax background layers with atlas references
- **Image elements**: sprite references within each layer (ClassName, position, size)
- **ModelsViewer**: player/enemy spawn positions (PlayerPositionX/Y, EnemyPositionX/Y)
- **SimpleEffect**: particle/visual effect placements
- **Atlas references**: `.plist` + `.png` pairs for sprite sheets

56 locations total including variants (small arenas, raid versions, seasonal).

---

## `tests/` — 15 Test Executables

```
tests/
├── CMakeLists.txt        # Test build config (CTest integration)
├── .keep
│
# --- DZ decoder tests ---
├── test_dz_decode.cpp          # Core DZ decompression tests
├── test_dz_decoder_util.cpp    # DZ utility function tests
├── test_dz_first_byte.cpp      # First-byte heuristic tests
├── test_dz_prob_layout.cpp     # Probability layout tests
├── test_dz_range_settings.cpp  # Range coding settings tests
├── test_dz_standalone.cpp      # Standalone DZ decoder driver
│
# --- Asset pipeline tests ---
├── test_asset_loaders.cpp      # Individual asset loader tests
├── test_asset_manager.cpp      # Asset manager caching/lifecycle
├── test_asset_pipeline.cpp     # Full asset pipeline integration
├── test_s3e_container.cpp      # S3E container format tests
├── test_json_atlas.cpp         # JSON atlas parser tests
├── test_list_parser.cpp        # .list file parser tests
├── test_moves_parser.cpp       # Move definition parser tests
├── test_stage_parser.cpp       # Stage configuration parser
├── test_xml_parsers.cpp        # XML parser tests
│
# --- Integration test drivers ---
├── test_platform_loop.cpp      # Platform loop integration test
│
# --- Test utility scripts ---
├── dump_anim.cpp               # Animation binary dumper
├── dump_bin.py                 # .bin file introspection
└── dump_dz.py                  # DZ archive introspection
```

Built executables output to `build/bin/{Debug,Release}/`:
- `test_asset_loaders.exe`
- `test_asset_manager.exe`
- `test_asset_pipeline.exe`
- `test_dz_decode.exe`
- `test_dz_decoder_util.exe`
- `test_dz_first_byte.exe`
- `test_dz_prob_layout.exe`
- `test_dz_range_settings.exe`
- `test_json_atlas.exe`
- `test_list_parser.exe`
- `test_moves_parser.exe`
- `test_platform_loop.exe`
- `test_s3e_container.exe`
- `test_stage_parser.exe`
- `test_xml_parsers.exe`

---

## `Testing/` — Integration Test Scenarios

```
Testing/
├── scenarios/          # 30+ deterministic input scripts (.txt)
│   ├── 01_w_jump.txt   # Walk + jump test
│   ├── 02_wd_front_flip.txt
│   ├── 03_wa_back_flip.txt
│   ├── 04_sd_forward_roll.txt
│   ├── 05_sa_back_roll.txt
│   ├── 06_d_hold_o_punch.txt
│   ├── 07_d_hold_p_kick.txt
│   ├── 08_a_hold_o_punch.txt
│   ├── 09_idle_o.txt
│   ├── 10_idle_p.txt
│   ├── 11_double_o_uninterrupt.txt
│   ├── 12_rapid_o.txt ... 13_close_rapid_o.txt
│   ├── 14_jitter_o_0.txt through 14_jitter_o_9.txt
│   ├── 15_close_jitter_o_0.txt through 15_close_jitter_o_4.txt
│   ├── 16_s_hold_o.txt ... 26_walk_back_punch.txt
│   └── jitter_rapid_o.py
├── results_*/          # Regression test output logs per feature area
│   ├── results_verified/        # Verified-passing test outputs
│   ├── results_recovery/        # Recovery mechanic test outputs
│   ├── results_rootfix/         # Root motion fix validation
│   ├── results_rollfix/         # Roll mechanic fix validation
│   ├── results_jumpfix/         # Jump mechanic fix validation
│   ├── results_yfix/            # Y-position fix validation
│   ├── results_velocity/        # Velocity regression tests
│   ├── results_cancelfix/       # Input cancel fix validation
│   ├── results_inputfix/        # Input processing fix validation
│   ├── results_jitter/          # Jitter/noise tests
│   ├── results_nodefix/         # Scene graph node fix tests
│   └── results_nodefix/         # (duplicate directory)
├── run_scenario.sh            # Test runner script
├── ensure_xvfb.sh             # X virtual framebuffer for headless CI
└── Temporary/                 # CTest temporary output files
```

---

## `scripts/` — 107 Python/C/Shell RE Tools

```
scripts/
├── dz_*.py / dz_*.c           # ~40 files — DZ format RE scripts
│   ├── dz_decode_final.py, dz_decode_v2.py  # DZ decoder implementations
│   ├── dz_arm_emu.py through dz_arm_emu4.py # ARM emulation for DZ
│   ├── dz_disasm*.py           # DZ bytecode disassembly
│   ├── dz_final.py, dz_decompress.py        # DZ decompression tools
│   ├── dz_*_decompiled.c       # ~35 Ghidra-decompiled DZ functions
│   └── dz_ground_truth.sha256  # Verification hashes
│
├── download_*.py               # Asset download utilities
├── s3e_analyze.py              # S3E format analysis
├── search_s86_strings.py       # S86 string extraction
├── find_character.py           # Character data locator
├── verify_*.sh                 # Compilation verification scripts
├── inspect_pixels.py           # Pixel-level rendering debug
├── make_contact_sheet.py       # Atlas contact sheet generator
├── sim_render_legs.py          # Leg rendering simulation
├── dzip_emu.py                 # dzip emulation
├── check_*.py                  # Regression checking scripts
├── mock_windows.h              # Windows API stubs for cross-compile
└── CMakeLists.txt              # Script build config
```

---

## `sf2_symbian/` — Symbian S60v3 Port

```
sf2_symbian/
├── src/
│   ├── main.cpp                    # Symbian entry point
│   ├── clib_stubs.cpp/.h           # C library stubs for GCCE
│   ├── engine/
│   │   ├── input_symbian.cpp/.h    # Symbian input handling
│   │   ├── platform_symbian.cpp/.h # Symbian platform abstraction
│   │   └── renderer_symbian.cpp/.h # Symbian (S60) rendering
│   └── stl_stubs/                  # STL stubs for GCCE
├── group/
│   ├── resf2.mmp                   # MMP build file
│   └── bld.inf                     # Build definition
├── data/
│   ├── resf2.rss                   # Resource file
│   └── resf2_reg.rss              # Registration resource
└── sis/
    └── resf2.pkg                  # SIS installer package
```

---

## `reverse/` — Original Game Binaries

```
reverse/
├── binaries/
│   ├── ShadowFight2.s86         # Original x86 Windows binary
│   ├── ShadowFight2_android.bin # ARM Android binary (extracted)
│   ├── libs3e_android.so        # Marmalade S3E Android library
│   ├── s3e_native.dll           # Marmalade S3E Windows DLL
│   └── README.md
```

---

## `build/` — CMake Build Output

```
build/
├── bin/{Debug,Release}/
│   ├── resf2_app.exe          # Main game executable (GLFW)
│   ├── resf2_port.exe         # Port demo executable
│   ├── 15 test_*.exe          # Test executables
│   └── zlib[d].dll            # ZLIB dependency
├── lib/                       # Static libraries
├── engine/                    # Per-module CMake build dirs
├── tests/                     # Test build dirs
├── _deps/                     # FetchContent dependencies
│   ├── glfw-build/            # GLFW 3.4
│   └── zlib-build/            # ZLIB v1.3.1
└── reSF2.sln                  # Visual Studio solution
```

---

## `docs/` — 27 Reverse Engineering Documents

Key files in documentation sequence:
```
docs/
├── 00_stage1_summary.md        # Stage 1 RE summary
├── 01_apk_structure.md         # APK structure analysis
├── 02_native_libraries.md      # Native library breakdown
├── 03_java_layer.md            # Java/Android layer
├── 04_assets_inventory.md      # Asset inventory
├── 05_resource_formats.md      # Resource format overview
├── 06_engine_marmalade.md      # Marmalade SDK engine
├── 07_jni_and_loader.md        # JNI bridge analysis
├── 08_reverse_engineering_log.md # RE process log
├── 09_s3e_binary_format.md     # S3E binary format spec
├── 10_jni_registration_map.md  # JNI method map
├── 11_engine_architecture.md   # Engine architecture doc
├── 12_main_loop.md             # Main loop analysis
├── 13_animation_format.md      # Animation format spec
├── 14_tactics_format.md        # Tactics/ATF format spec
├── 15_resource_formats_final.md # Final resource format doc
├── 16_bin_animation_format.md  # .bin animation format
├── 17_detailed_reverse_plan.md # Detailed RE plan
├── 18_engine_flow_analysis.md  # Engine flow analysis
├── 19_complete_re_plan.md      # Complete RE plan
├── DEVELOPMEMT_PLAN.md         # Development roadmap
├── ghidra_decompilation_guide.md # Ghidra RE guide
├── s3e_function_analysis.md    # S3E function analysis
├── s3e_windows_binary_re.md    # Windows binary RE
└── s3e_reverse_engineering.md  # S3E RE overview
```

---

## Key File Reference

| File | Size | Role |
|------|------|------|
| `engine/game/game.hpp` | 255 KB | **Largest file** — Main `Game` class with all game state/behavior as template header |
| `engine/game/game.cpp` | 21 lines | Thin delegator — includes `game.hpp` |
| `engine/scene/scenes.cpp` | 24 KB | All scene implementations (loading, menu, battle, shops, etc.) |
| `engine/scene/scene_system.hpp/cpp` | 10 KB / 4 KB | Scene lifecycle management |
| `main.cpp` | 73 lines | GLFW main loop with CLI flags (`--assets`, `--input-script`, `--max-frames`, `--replay`, `--dump-state`) |
| `headless_main.cpp` | ~1900 lines | Headless test driver — produces 10 PNG screenshots through boot sequence stages |
| `main_port.cpp` | N/A | Modular port demo entry |
| `CMakeLists.txt` | 198 lines | Top-level CMake config (C++23, GLFW 3.4, ZLIB v1.3.1, sanitizer support) |
| `engine/CMakeLists.txt` | — | Per-module library targets |
| `tests/CMakeLists.txt` | — | 15 test executables with CTest |

---

## Build Targets

| Target | Type | Description | Dependencies |
|--------|------|-------------|--------------|
| `resf2_app` | Executable | Main GLFW game | resf2_game |
| `resf2_port` | Executable | Port demo (modular) | resf2_fight, resf2_format, resf2_platform, resf2_renderer, resf2_core, resf2_reverse, ZLIB, GLFW |
| `resf2_headless` | Executable | Software-only headless | resf2_renderer, resf2_reverse |
| `dz_extract` | Custom target | DZ archive extraction | dzip.exe |
| 15 test targets | Executables | Unit/integration tests | per-test module libs |

---

## Build Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `RESF2_BUILD_TESTS` | ON | Build test executables |
| `RESF2_BUILD_TOOLS` | ON | Build script tools |
| `RESF2_WERROR` | OFF | Treat warnings as errors |
| `RESF2_ENABLE_SAN` | OFF | Address/UB sanitizers (Clang/GCC) |
| `RESF2_BUILD_RUNTIME` | ON | Build resf2_app/port |
| `RESF2_USE_GLFW` | ON | GLFW platform backend |
| `RESF2_BUILD_HEADLESS` | OFF | Build resf2_headless |
