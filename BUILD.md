# reSF2 — Build Instructions

## Quick start (Windows)

```cmd
git clone https://github.com/hwindinkg/reSF2.git
cd reSF2
build.bat
```

This will:
1. Auto-download GLFW 3.4 via CMake FetchContent (first time only, ~2 min)
2. Build `resf2_app.exe` with Visual Studio 2022
3. Output: `build\bin\Release\resf2_app.exe`

Run it:
```cmd
build\bin\Release\resf2_app.exe --assets .
```

A 1280×720 window opens with the Dojo battle scene.

## Quick start (Linux)

```bash
git clone https://github.com/hwindinkg/reSF2.git
cd reSF2
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON
make -j$(nproc)
./bin/resf2_app --assets ..
```

## Prerequisites

### Windows
- **CMake 3.24+**: https://cmake.org/download/
- **Visual Studio 2022** (Build Tools are sufficient):
  https://visualstudio.microsoft.com/downloads/
  - Install the "Desktop development with C++" workload
- **Git**: https://git-scm.com/
- GLFW 3.4 + zlib are auto-downloaded by CMake (no manual install needed)

### Linux (Debian/Ubuntu)
```bash
sudo apt install build-essential cmake git libgl1-mesa-dev zlib1g-dev
# GLFW is auto-downloaded by CMake
```

### macOS
```bash
xcode-select --install
brew install cmake git
# GLFW is auto-downloaded by CMake
```

## Build options

| Option | Default | Description |
|--------|---------|-------------|
| `RESF2_BUILD_RUNTIME` | ON | Build `resf2_app` executable |
| `RESF2_USE_GLFW` | ON | Build GLFW platform backend (desktop) |
| `RESF2_BUILD_TESTS` | ON | Build unit tests |
| `RESF2_BUILD_TOOLS` | OFF | Build offline asset tools |
| `RESF2_WERROR` | OFF | Treat warnings as errors |
| `RESF2_ENABLE_SAN` | OFF | Enable ASan + UBSan (Clang/GCC only) |

## Build commands (manual)

### Windows (Visual Studio)
```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON
cmake --build build --config Release --parallel
```

### Linux/macOS (Make/Ninja)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON
cmake --build build --parallel
```

## Running tests

```cmd
cd build
ctest --output-on-failure -C Release
```

## Using the engine with game assets

After building, run with the path to the repository root (which contains the
extracted `assets/` and `sf2/` folders):

```cmd
build\bin\Release\resf2_app.exe --assets .
```

The engine loads:
- `assets/` — pre-extracted game files (models, animations, moves.xml, locations,
  sounds, fonts, HUD textures). These ship with the repo.
- `sf2/assets/` — original mobile APK assets (used for sounds/*.wav, music/*.mp3).

### Asset extraction (if you want to re-extract from APK)
1. Rename `Shadow_Fight_2_1.9.21.apk` to `.zip`
2. Extract the `assets/` folder to `sf2/assets/`
3. `files.dz` and `animations.dz` are in `sf2/assets/assets/` — the engine
   reads `animations.dz` (type-8 gzip) directly. `files.dz` (type-4 custom)
   is [HEURISTIC-TODO] — the engine falls back to the pre-extracted `assets/`
   folder for now.

## Gameplay (Dojo battle)

The Dojo is a playable battle scene:

| Key | Action |
|-----|--------|
| **W/A/S/D** | Jump / Left / Duck / Right |
| **O** | Punch (W=upper, S=low, D=heavy, A=spinning, S+A=elbow) |
| **P** | Kick (S=sweep, D=front, A=back, S+D=dodge reverse) |
| **W** | Jump (W+D=front flip, W+A=back flip) |
| **S+D / S+A** | Forward roll / Back roll |
| **S (hold)** | Duck (crouch) |
| **Block** | Automatic (when idle, not attacking) |
| **R** | Restart battle (after victory/defeat) |
| **M** | Toggle menu |
| **T** | Toggle dialog |
| **Esc** | Quit / close overlay |

### Combat
- **Player** (black silhouette, left): punch/kick the enemy. Health bar bottom-left.
- **Enemy** (red silhouette, right): AI opponent that approaches, attacks,
  blocks, retreats. Health bar bottom-right + floating bar above enemy.
- **Victory**: reduce enemy health to 0 → "VICTORY" overlay.
- **Defeat**: if player health reaches 0 → "DEFEAT" overlay.
- **R** restarts the battle at any time after it ends.

### HUD
- Top panel: gold, energy, level (stub values)
- Bottom-left: player health (green/yellow/red) + energy bar (blue)
- Bottom-right: enemy health (mirrored)
- Hit flash: white flash on the damaged fighter's bar

## Troubleshooting

### "find_package(glfw3) failed"
This should not happen — GLFW is auto-downloaded via CMake FetchContent.
If it fails (no internet during configure), you can install GLFW manually:
- Windows: `vcpkg install glfw3`
- Linux: `sudo apt install libglfw3-dev`
- macOS: `brew install glfw`

Then add to cmake: `-Dglfw3_DIR=/path/to/glfw3Config.cmake`

### "Cannot find -lGL" (Linux)
```bash
sudo apt install libgl1-mesa-dev
```

### "Cannot find -lz" (Linux)
```bash
sudo apt install zlib1g-dev
```

### "Threads not found" (Windows)
This is a CMake issue with MSVC. The build.bat script handles this
automatically. If building manually, ensure you're using the Visual
Studio generator, not MinGW.

### Blank screen / no assets
Make sure you run with `--assets .` from the repo root. The engine needs
the `assets/` folder (which ships with the repo) to load the Dojo scene,
character model, animations, and sounds.

### No sound
The audio engine uses a NullAudioBackend by default (no sound output).
To enable sound, integrate an OpenAL backend in `engine/audio/audio.cpp`
(future work). Sound files ARE loaded (you'll see "Loaded N/12 sounds" in
the console), just not played through a real audio device yet.

### Character floats / sinks
The Y-positioning uses an interim formula ([HEURISTIC-TODO] in main.cpp).
The character should stand on the floor in the Dojo. If Y is wrong, check
the `[ROOT]` log lines for `npy` (NPivot Y) and `ry` (render Y) values.
