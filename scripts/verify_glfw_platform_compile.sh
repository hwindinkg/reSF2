#!/usr/bin/env bash
# Verify engine/platform/glfw_platform.cpp compiles on Linux (non-_WIN32 path).
# We do NOT link — just compile to an object file. This catches syntax/type
# errors in the shared code paths (key_to_glfw, key_index_to_glfw, Impl struct,
# poll_events, etc.) and confirms the #else branches are well-formed.

set -euo pipefail

PROJ=/home/z/my-project
PREFIX="$PROJ/.local-prefix/usr"
SRC="$PROJ/engine/platform/glfw_platform.cpp"
OUT="$PROJ/build-test/glfw_platform.non_win.o"

mkdir -p "$(dirname "$OUT")"

# Include paths:
#   - engine/                for "platform/glfw_platform.hpp"
#   - PREFIX/include         for GLFW/glfw3.h and GL/gl.h
# We define nothing special — _WIN32 is NOT set on Linux, so the Win32-only
# branches (windows.h, GetAsyncKeyState, glfw_key_to_vk, prev_keys_down_) are
# skipped by the preprocessor and we exercise only the GLFW-callback path.
g++ -std=c++23 -c "$SRC" \
    -I"$PROJ/engine" \
    -I"$PREFIX/include" \
    -Wall -Wextra -Wpedantic \
    -o "$OUT"

echo "OK: $SRC compiled cleanly (non-Win32 path)"
ls -la "$OUT"
