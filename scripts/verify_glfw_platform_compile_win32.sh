#!/usr/bin/env bash
# Verify the _WIN32 branch of engine/platform/glfw_platform.cpp compiles.
# We force-define _WIN32 and inject a mock <windows.h> (mock_windows.h)
# that provides the minimal types/macros/functions the Win32 path uses
# (SHORT, GetAsyncKeyState, VK_* constants). This catches syntax and
# type errors in the Win32-only code without needing a real Windows SDK.

set -euo pipefail

PROJ=/home/z/my-project
PREFIX="$PROJ/.local-prefix/usr"
SRC="$PROJ/engine/platform/glfw_platform.cpp"
MOCK_DIR="$PROJ/scripts"
OUT="$PROJ/build-test/glfw_platform.win32.o"

mkdir -p "$(dirname "$OUT")"

# We need to:
#   1. Force _WIN32 on (so the #ifdef _WIN32 branches activate).
#   2. Make #include <windows.h> resolve to our mock_windows.h. We do this
#      by adding -I with our mock dir FIRST in the include path, and
#      placing the mock as `windows.h` in that dir (symlink or copy).
#   3. Suppress GLFW_EXPOSE_NATIVE_WIN32-related warnings (we don't use
#      glfwGetWin32Window here).
#   4. Linux g++ doesn't know the __stdcall keyword (it's a Windows-target
#      only alternate keyword). GLFW/GL headers reference it when _WIN32
#      is set. We redefine __stdcall as an empty macro via -D__stdcall=
#      so the headers parse on Linux. This is purely a compile-test
#      hack — on real Windows with MSVC/MinGW, __stdcall is a real
#      calling-convention attribute and we don't touch it.
#
# Note: we do NOT pass -D_WIN32 via the compiler; g++ on Linux doesn't
# predefine _WIN32. We add it explicitly.

cp "$MOCK_DIR/mock_windows.h" "$MOCK_DIR/windows.h"

g++ -std=c++23 -c "$SRC" \
    -D_WIN32 \
    -D__stdcall= \
    -I"$MOCK_DIR" \
    -I"$PROJ/engine" \
    -I"$PREFIX/include" \
    -Wall -Wextra -Wpedantic \
    -Wno-unused-parameter \
    -o "$OUT"

rm -f "$MOCK_DIR/windows.h"

echo "OK: $SRC compiled cleanly (Win32 path with mock windows.h)"
ls -la "$OUT"
