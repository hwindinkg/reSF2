# reSF2 — Build Instructions

## Quick start (Windows)

```cmd
git clone https://github.com/hwindinkg/reSF2.git
cd reSF2
build.bat
```

This will:
1. Auto-download GLFW 3.4 via CMake FetchContent (first time only, ~2 min)
2. Build `resf2_runtime.exe` with Visual Studio 2022
3. Output: `build\bin\Release\resf2_runtime.exe`

Run it:
```cmd
build\bin\Release\resf2_runtime.exe
```

A 1280×720 window opens. Press ESC or close the window to exit.

## Quick start (Linux)

```bash
git clone https://github.com/hwindinkg/reSF2.git
cd reSF2
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON
make -j$(nproc)
./bin/resf2_runtime
```

## Prerequisites

### Windows
- **CMake 3.24+**: https://cmake.org/download/
- **Visual Studio 2022** (Build Tools are sufficient):
  https://visualstudio.microsoft.com/downloads/
  - Install the "Desktop development with C++" workload
- **Git**: https://git-scm.com/
- GLFW is auto-downloaded by CMake (no manual install needed)

### Linux (Debian/Ubuntu)
```bash
sudo apt install build-essential cmake git libgl1-mesa-dev
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
| `RESF2_BUILD_RUNTIME` | ON | Build `resf2_runtime` executable |
| `RESF2_USE_GLFW` | ON | Build GLFW platform backend (desktop) |
| `RESF2_BUILD_TESTS` | ON | Build unit tests |
| `RESF2_WERROR` | OFF | Treat warnings as errors |
| `RESF2_ENABLE_SAN` | OFF | Enable ASan + UBSan (Clang/GCC only) |

## Build commands

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

Expected output:
```
100% tests passed, 0 tests failed out of 4
```

## Using the engine with game assets

After building, the runtime can load assets from a directory:

```cmd
resf2_runtime.exe --assets C:\path\to\extracted\apk\assets
```

To extract assets from the APK:
1. Rename `Shadow_Fight_2_1.9.21.apk` to `.zip`
2. Extract the `assets/` folder
3. For `.dz` archives: use `dzip.exe -d assets\files.dz` and
   `dzip.exe -d assets\animations.dz`

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

### "Threads not found" (Windows)
This is a CMake issue with MSVC. The build.bat script handles this
automatically. If building manually, ensure you're using the Visual
Studio generator, not MinGW.
