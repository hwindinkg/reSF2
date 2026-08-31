// GL context creation (GLFW), minimal runtime function loading, and
// framebuffer -> PNG capture (glReadPixels + stb_image_write).
//
// GL entry points are resolved with glfwGetProcAddress and stored in
// static function pointers in the sf2::render namespace (see gl_loader.inc).
// This avoids a generated loader (glad/GLAD): only the ~45 entry points the
// 2D renderer and capture path actually use are declared.

#include "render/gl.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

namespace sf2::render {

// ---------------------------------------------------------------------------
// Runtime GL function loader. The `gl::` function-pointer declarations are
// shared via gl.hpp (gl_loader.inc); the actual loading table lives here.
// ---------------------------------------------------------------------------

bool gl_load_functions() {
    using namespace gl;
#define SF2_GL_FUNC(ret, name, params)                                                             \
    name = reinterpret_cast<name##_t*>(glfwGetProcAddress(#name));                                 \
    if (name == nullptr) {                                                                         \
        std::fprintf(stderr, "gl loader: missing function %s\n", #name);                           \
        return false;                                                                              \
    }
#include "render/gl_loader.inc"
#undef SF2_GL_FUNC
    return true;
}

// ---------------------------------------------------------------------------
// Context creation
// ---------------------------------------------------------------------------

namespace {
void glfw_error_cb(int code, const char* desc) {
    std::fprintf(stderr, "glfw error %d: %s\n", code, desc);
}
} // namespace

bool glfw_context_create(int width, int height, bool hidden, GLFWwindow** out) {
    if (out == nullptr) {
        return false;
    }
    *out = nullptr;

    glfwSetErrorCallback(glfw_error_cb);
    if (glfwInit() != GLFW_TRUE) {
        std::fprintf(stderr, "gl: glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(width, height, "sf2_native", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "gl: glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!gl_load_functions()) {
        std::fprintf(stderr, "gl: failed to load GL functions\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }
    *out = window;
    return true;
}

// ---------------------------------------------------------------------------
// Framebuffer capture
// ---------------------------------------------------------------------------

bool gl_read_pixels_rgba(GLFWwindow* window, std::vector<std::uint8_t>& out_rgba, int* out_w,
                         int* out_h) {
    if (window == nullptr) {
        return false;
    }
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    if (w <= 0 || h <= 0) {
        return false;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    gl::glPixelStorei(0x0D05 /* GL_UNPACK_ALIGNMENT */, 1);
    // The read buffer is set by the caller (gl_read_buffer_front/back); the
    // capture path reads GL_FRONT (the just-presented frame after a swap).
    gl::glReadPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());

    // glReadPixels yields bottom-up rows; flip to top-left origin.
    std::vector<std::uint8_t> flipped(static_cast<std::size_t>(w) * h * 4);
    const int row_bytes = w * 4;
    for (int y = 0; y < h; ++y) {
        std::memcpy(flipped.data() + static_cast<std::size_t>(y) * row_bytes,
                    rgba.data() + static_cast<std::size_t>(h - 1 - y) * row_bytes,
                    static_cast<std::size_t>(row_bytes));
    }
    out_rgba = std::move(flipped);
    if (out_w != nullptr) {
        *out_w = w;
    }
    if (out_h != nullptr) {
        *out_h = h;
    }
    return true;
}

bool gl_capture_png(GLFWwindow* window, const std::string& path) {
    std::vector<std::uint8_t> rgba;
    int w = 0;
    int h = 0;
    if (!gl_read_pixels_rgba(window, rgba, &w, &h)) {
        return false;
    }
    const int ok = stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4);
    return ok != 0;
}

void gl_read_buffer_front(GLFWwindow* window) {
    if (window == nullptr) return;
    gl::glReadBuffer(0x0404 /* GL_FRONT */);
}

void gl_read_buffer_back(GLFWwindow* window) {
    if (window == nullptr) return;
    gl::glReadBuffer(0x0405 /* GL_BACK */);
}

} // namespace sf2::render
