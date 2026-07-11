// Minimal mock of <windows.h> for syntax-checking the _WIN32 branch of
// glfw_platform.cpp on Linux. We define ONLY the types, macros and
// functions that glfw_platform.cpp touches. This is NOT a real Windows
// SDK header — it exists purely so we can compile-test the Win32 code
// path from Linux.
#pragma once

#include <cstdint>

#pragma message("MOCK windows.h: included by " __FILE__)

using SHORT = std::int16_t;

// Calling-convention macros that GL/gl.h expects <windows.h> to define
// when _WIN32 is set. On real Windows these resolve to __stdcall etc.
// We must define APIENTRY to something that composes with `*` in
// `#define APIENTRYP APIENTRY *` — an empty macro would expand to just
// `*` and break GL/gl.h's function-pointer typedefs. g++ accepts
// __stdcall on x86_64 as a no-op attribute, so we use it.
#ifndef APIENTRY
#  define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#  define WINGDIAPI extern
#endif
// GL/gl.h falls back to defining GLAPI/GLAPIENTRY itself if they aren't
// already defined, so we don't need to provide them here.

// Virtual key codes used by glfw_key_to_vk(). Values match the real Win32
// SDK so the switch is exercised meaningfully.
#define VK_BACK        0x08
#define VK_TAB         0x09
#define VK_RETURN      0x0D
#define VK_ESCAPE      0x1B
#define VK_SPACE       0x20
#define VK_LEFT        0x25
#define VK_UP          0x26
#define VK_RIGHT       0x27
#define VK_DOWN        0x28
#define VK_F1          0x70
#define VK_F2          0x71
#define VK_F3          0x72
#define VK_F4          0x73
#define VK_F5          0x74
#define VK_F6          0x75
#define VK_F7          0x76
#define VK_F8          0x77
#define VK_F9          0x78
#define VK_F10         0x79
#define VK_F11         0x7A
#define VK_F12         0x7B
#define VK_LSHIFT      0xA0
#define VK_RSHIFT      0xA1
#define VK_LCONTROL    0xA2
#define VK_RCONTROL    0xA3
#define VK_LMENU       0xA4
#define VK_RMENU       0xA5

// Mock GetAsyncKeyState: returns 0 (key up) for every vk. The actual return
// value doesn't matter for compile-checking; we just need the symbol to
// exist with the right signature so the call site in poll_events() type-
// checks.
inline SHORT GetAsyncKeyState(int /*vkey*/) noexcept { return 0; }
