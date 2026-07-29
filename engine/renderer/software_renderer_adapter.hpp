// engine/renderer/software_renderer_adapter.hpp
//
// Adapter that wraps soft::Renderer to implement the IRenderer interface.
// This allows the Game class to use the software renderer transparently
// through the same interface as the GL renderer.

#pragma once

#include "irenderer.hpp"
#include "software_renderer.hpp"

#include <memory>
#include <unordered_map>

namespace resf2::renderer {

class SoftwareRendererAdapter : public IRenderer {
public:
    SoftwareRendererAdapter() = default;
    ~SoftwareRendererAdapter() override;

    bool init(int width, int height);
    void shutdown();

    // IRenderer overrides
    void begin_frame() override;
    void end_frame() override;
    void set_clear_color(float r, float g, float b, float a = 1.0f) override;

    void draw_textured_quad(
        const ITexture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    ) override;

    void draw_textured_quad_screen(
        const ITexture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    ) override;

    void draw_filled_rect_screen(
        float x, float y, float w, float h,
        Color4B color
    ) override;

    void draw_filled_triangle_screen(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    ) override;

    void draw_filled_triangle_world(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    ) override;

    void draw_filled_circle_screen(
        float cx, float cy, float radius,
        Color4B color
    ) override;

    void draw_filled_circle_world(
        float cx, float cy, float radius,
        Color4B color
    ) override;

    void draw_line_screen(
        float x0, float y0, float x1, float y1,
        Color4B color
    ) override;

    void draw_line_world(
        float x0, float y0, float x1, float y1,
        Color4B color
    ) override;

    void camera_set_target(float x, float y) override;
    void camera_set_zoom(float zoom) override;

    // Access underlying software renderer (for screenshots, etc.)
    soft::Renderer& soft_renderer() { return renderer_; }
    const soft::Renderer& soft_renderer() const { return renderer_; }

private:
    soft::Renderer renderer_;

    // Cache of synthesized soft::Texture from ITexture's CPU pixels
    std::unordered_map<const ITexture*, std::unique_ptr<soft::Texture>> texture_cache_;

    // Lazily synthesize a soft::Texture from any ITexture's CPU pixels
    const soft::Texture& resolve(const ITexture& tex);
};

}  // namespace resf2::renderer
