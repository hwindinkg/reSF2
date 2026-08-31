#pragma once

// GL context + function loading + framebuffer capture.
//
// Lightest correct approach: GLFW (FetchContent) creates the window and the
// GL context; all GL functions are loaded at runtime via
// glfwGetProcAddress with a minimal loader (no glad/GLAD generation step
// needed — we declare the small set of 1.x/2.x/3.x entry points the sprite
// renderer and capture path use). Renderer code includes gl3.h (the
// <GL/gl3.h> core-profile header shipped in this directory) so it sees
// GL* constants and prototypes, but every function is still resolved via
// the loader — gl3.h's prototypes are only used for the #define-less names.
//
// Portable C++17 — no Win32 API directly (GLFW abstracts it).

#include <cstdint>
#include <string>
#include <vector>

#include "render/gl_types.hpp"

struct GLFWwindow;

namespace sf2::render {

// Runtime-resolved GL entry points (see gl_loader.inc). Declared inline so
// every TU that includes gl.hpp shares the same function-pointer objects;
// gl_load_functions() assigns them once per context.
namespace gl {
#define SF2_GL_FUNC(ret, name, params) using name##_t = ret params; inline name##_t* name = nullptr;
#include "render/gl_loader.inc"
#undef SF2_GL_FUNC
} // namespace gl

// Loads every GL function the renderer uses. Must be called with a current
// context. Returns false if any required function is missing.
bool gl_load_functions();

// Creates a GLFW window with a GL 3.3 core context. `hidden` = no window is
// shown (used for offscreen probes); fall back to a visible window when
// offscreen rendering misbehaves on a driver. Returns false on failure.
bool glfw_context_create(int width, int height, bool hidden, GLFWwindow** out);

// Reads the back buffer into an RGBA byte array (row-major, top-left
// origin, matching stb_image conventions).
bool gl_read_pixels_rgba(GLFWwindow* window, std::vector<std::uint8_t>& out_rgba,
                         int* out_w, int* out_h);

// glReadPixels + stb_image_write PNG. `path` must end in ".png".
bool gl_capture_png(GLFWwindow* window, const std::string& path);

} // namespace sf2::render
