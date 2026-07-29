// engine/renderer/renderer_types.hpp
//
// Common types used by both GL and software renderers.

#pragma once

#include <cstdint>

namespace resf2::renderer {

// ---- Color ----
struct Color4B {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct Color4F {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// ---- Rect ----
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

// ---- Mat4 (4x4 float matrix, column-major) ----
struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    static Mat4 ortho(float left, float right, float bottom, float top,
                      float nearZ = -1.0f, float farZ = 1.0f);
    static Mat4 identity();
};

}  // namespace resf2::renderer
