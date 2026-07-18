#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "math.hpp"

namespace resf2::core {

struct DrawQuad {
    float x, y, w, h;
    float u0, v0, u1, v1;
    uint32_t color = 0xFFFFFFFF;
    uint32_t texture_id = 0;
    bool flip_x = false;
};

class Renderer2D {
public:
    virtual ~Renderer2D() = default;

    // Frame lifecycle
    virtual bool begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void clear(const Color& c) = 0;

    // Viewport / scissor
    virtual void set_viewport(int x, int y, int w, int h) = 0;
    virtual void set_scissor(int x, int y, int w, int h) = 0;

    // Transform stack
    virtual void push_transform(const Mat4& mat) = 0;
    virtual void pop_transform() = 0;

    // Textures
    virtual uint32_t create_texture(int w, int h, const void* rgba_data) = 0;
    virtual void destroy_texture(uint32_t id) = 0;

    // Drawing
    virtual void draw_quad(const DrawQuad& q) = 0;
    virtual void draw_rect(float x, float y, float w, float h, uint32_t color) = 0;
    virtual void draw_line(float x1, float y1, float x2, float y2, uint32_t color) = 0;

    // Batch
    virtual void flush() = 0;
};

} // namespace resf2::core
