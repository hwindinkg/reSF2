#!/usr/bin/env bash
# Compile-check main.cpp with local GLFW headers (no linking).
# This verifies the scene system integration compiles cleanly.

set -euo pipefail

PROJ=/home/z/my-project
PREFIX="$PROJ/.local-prefix/usr"
SRC="$PROJ/main.cpp"
OUT="$PROJ/build-full/main.o"

mkdir -p "$(dirname "$OUT")"

# Compile main.cpp to an object file (no linking — we just want to catch
# compile errors in the scene system integration).
# -I paths:
#   - engine/          for "engine/scene/scene_system.hpp" etc.
#   - PREFIX/include   for GLFW/glfw3.h and GL/gl.h
# We define nothing special — _WIN32 is NOT set, so the Win32-only
# branches in glfw_platform.cpp are skipped.
g++ -std=c++23 -c "$SRC" \
    -I"$PROJ" \
    -I"$PREFIX/include" \
    -Wall -Wextra -Wpedantic \
    -Wno-unused-parameter \
    -Wno-old-style-cast \
    -Wno-conversion \
    -o "$OUT" 2>&1 | head -60

echo "---"
if [ -f "$OUT" ]; then
    echo "OK: $SRC compiled"
    ls -la "$OUT"
else
    echo "FAIL: $SRC did not compile"
    exit 1
fi
