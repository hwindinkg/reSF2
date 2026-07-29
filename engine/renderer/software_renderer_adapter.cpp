// engine/renderer/software_renderer_adapter.cpp
//
// Implementation of SoftwareRendererAdapter.

#include "software_renderer_adapter.hpp"

namespace resf2::renderer {

SoftwareRendererAdapter::~SoftwareRendererAdapter() {
    shutdown();
}

bool SoftwareRendererAdapter::init(int width, int height) {
    return renderer_.init(width, height);
}

void SoftwareRendererAdapter::shutdown() {
    texture_cache_.clear();
    renderer_.shutdown();
}

void SoftwareRendererAdapter::begin_frame() {
    renderer_.begin_frame();
}

void SoftwareRendererAdapter::end_frame() {
    renderer_.end_frame();
}

void SoftwareRendererAdapter::set_clear_color(float r, float g, float b, float a) {
    renderer_.set_clear_color(r, g, b, a);
}

void SoftwareRendererAdapter::draw_textured_quad(
    const ITexture& texture,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    Color4B color)
{
    const auto& soft_tex = resolve(texture);
    renderer_.draw_textured_quad(soft_tex, x, y, w, h, u0, v0, u1, v1, color);
}

void SoftwareRendererAdapter::draw_textured_quad_screen(
    const ITexture& texture,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    Color4B color)
{
    const auto& soft_tex = resolve(texture);
    renderer_.draw_textured_quad_screen(soft_tex, x, y, w, h, u0, v0, u1, v1, color);
}

void SoftwareRendererAdapter::draw_filled_rect_screen(
    float x, float y, float w, float h,
    Color4B color)
{
    renderer_.draw_filled_rect_screen(x, y, w, h, color);
}

void SoftwareRendererAdapter::draw_filled_triangle_screen(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Color4B color)
{
    // Software renderer doesn't have triangle methods, so we approximate
    // with a filled rectangle for now. This is a simplification.
    // For proper triangle rendering, we'd need to implement triangle rasterization.
    // For now, we'll skip this (it's mainly used for debug visualization).
    (void)x0; (void)y0; (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

void SoftwareRendererAdapter::draw_filled_triangle_world(
    float x0, float y0, float x1, float y1, float x2, float y2,
    Color4B color)
{
    // Software renderer doesn't have triangle methods, so we approximate
    // with a filled rectangle for now. This is a simplification.
    (void)x0; (void)y0; (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

void SoftwareRendererAdapter::draw_filled_circle_screen(
    float cx, float cy, float radius,
    Color4B color)
{
    renderer_.draw_filled_circle_screen(cx, cy, radius, color);
}

void SoftwareRendererAdapter::draw_filled_circle_world(
    float cx, float cy, float radius,
    Color4B color)
{
    // Software renderer doesn't have world-space circle, so we need to
    // convert to screen space first
    float sx, sy;
    if (renderer_.camera().world_to_screen(cx, cy, sx, sy)) {
        renderer_.draw_filled_circle_screen(sx, sy, radius * renderer_.camera().zoom, color);
    }
}

void SoftwareRendererAdapter::draw_line_screen(
    float x0, float y0, float x1, float y1,
    Color4B color)
{
    renderer_.draw_line_screen(x0, y0, x1, y1, color);
}

void SoftwareRendererAdapter::draw_line_world(
    float x0, float y0, float x1, float y1,
    Color4B color)
{
    renderer_.draw_line_world(x0, y0, x1, y1, color);
}

void SoftwareRendererAdapter::camera_set_target(float x, float y) {
    renderer_.camera().set_target(x, y);
}

void SoftwareRendererAdapter::camera_set_zoom(float zoom) {
    renderer_.camera().set_zoom(zoom);
}

const soft::Texture& SoftwareRendererAdapter::resolve(const ITexture& tex) {
    // Check cache
    auto it = texture_cache_.find(&tex);
    if (it != texture_cache_.end()) {
        return *it->second;
    }

    // Synthesize soft::Texture from ITexture's CPU pixels
    auto soft_tex = std::make_unique<soft::Texture>();
    auto pixels = tex.pixels();
    if (!pixels.empty()) {
        soft_tex->init_rgba(tex.width(), tex.height(), pixels.data());
    } else {
        // If no CPU pixels available, create empty texture
        soft_tex->init_rgba(tex.width(), tex.height(), nullptr);
    }

    // Cache and return
    texture_cache_[&tex] = std::move(soft_tex);
    return *texture_cache_[&tex];
}

}  // namespace resf2::renderer
