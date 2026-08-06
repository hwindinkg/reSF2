// engine/renderer/irenderer.hpp
//
// Abstract renderer interface for backend-agnostic rendering.
// Both GL (ren::Renderer) and software (soft::Renderer via adapter) implement this.

#pragma once

#include "itexture.hpp"
#include "renderer_types.hpp"

#include <cstdint>

namespace resf2::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Initialization
    virtual bool init(int width, int height) = 0;
    virtual void shutdown() = 0;

    // Frame lifecycle
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void set_clear_color(float r, float g, float b, float a = 1.0f) = 0;

    // Drawing - textured quads
    virtual void draw_textured_quad(
        const ITexture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    ) = 0;

    virtual void draw_textured_quad_screen(
        const ITexture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    ) = 0;

    // Drawing - primitives
    virtual void draw_filled_rect_screen(
        float x, float y, float w, float h,
        Color4B color
    ) = 0;

    virtual void draw_filled_triangle_screen(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    ) = 0;

    virtual void draw_filled_triangle_world(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    ) = 0;

    virtual void draw_filled_circle_screen(
        float cx, float cy, float radius,
        Color4B color
    ) = 0;

    virtual void draw_filled_circle_world(
        float cx, float cy, float radius,
        Color4B color
    ) = 0;

    virtual void draw_line_screen(
        float x0, float y0, float x1, float y1,
        Color4B color
    ) = 0;

    virtual void draw_line_world(
        float x0, float y0, float x1, float y1,
        Color4B color
    ) = 0;

    // Camera (flat methods, not Camera2D& accessor)
    virtual void camera_set_target(float x, float y) = 0;
    virtual void camera_set_zoom(float zoom) = 0;

    // [Wave 10A defect 4] Read one rendered pixel in SCREEN coordinates
    // (x, y = top-left, as drawn). Used by the [PIXEL] dump-state probe to
    // verify what a scene actually drew (e.g. the dialogue keeps the
    // location visible behind the parchment). Default: unsupported.
    virtual bool read_pixel(int x, int y, std::uint8_t rgb[3]) {
        (void)x; (void)y; (void)rgb;
        return false;
    }
};

}  // namespace resf2::renderer
