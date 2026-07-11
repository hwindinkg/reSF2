// engine/renderer/software_renderer.cpp
//
// CPU-side software renderer implementation. Provides texture-mapped quad
// rasterisation with alpha blending, plus primitive 2D shapes for HUD
// rendering. The renderer targets the same visual conventions as the GL
// renderer:
//   - World coordinates: Y+ up, origin at world centre (or as configured
//     by the location XML).
//   - Screen coordinates: Y+ down, origin at top-left of the framebuffer.
//   - Texture (u,v): (0,0) top-left, (1,1) bottom-right.

#include "software_renderer.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace resf2::soft {

// ---- Texture ----

bool Texture::init_rgba(int w, int h, const std::uint8_t* px) {
    width = w; height = h;
    pixels.resize((size_t)w * h * 4);
    if (px) std::memcpy(pixels.data(), px, pixels.size());
    else    std::memset(pixels.data(), 0, pixels.size());
    return true;
}

bool Texture::init_from_png(const std::uint8_t* data, std::size_t size) {
    int w, h, ch;
    auto* px = stbi_load_from_memory(data, (int)size, &w, &h, &ch, 4);
    if (!px) return false;
    width = w; height = h;
    pixels.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return true;
}

void Texture::sample(float u, float v, std::uint8_t& r, std::uint8_t& g,
                     std::uint8_t& b, std::uint8_t& a) const noexcept {
    // Clamp u,v to [0,1]
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    int x = (int)(u * (width  - 1) + 0.5f);
    int y = (int)(v * (height - 1) + 0.5f);
    const std::uint8_t* p = &pixels[((size_t)y * width + x) * 4];
    r = p[0]; g = p[1]; b = p[2]; a = p[3];
}

// ---- Camera2D ----
// World (Y-UP) -> screen (Y-DOWN) transform.
// Positive world Y maps to smaller screen Y (upward on screen).
bool Camera2D::world_to_screen(float wx, float wy, float& sx, float& sy) const noexcept {
    float dx = (wx - x) * zoom;
    float dy = (wy - y) * zoom;
    sx = view_width  * 0.5f + dx;
    // Y-UP world -> Y-DOWN screen: invert Y
    sy = view_height * 0.5f - dy;
    return sx >= -1 && sx <= view_width + 1 && sy >= -1 && sy <= view_height + 1;
}

// ---- Renderer ----

bool Renderer::init(int width, int height) {
    width_ = width; height_ = height;
    fb_.assign((size_t)width * height * 4, 0);
    camera_.view_width  = (float)width;
    camera_.view_height = (float)height;
    return true;
}

void Renderer::shutdown() {
    fb_.clear(); fb_.shrink_to_fit();
}

void Renderer::resize(int width, int height) {
    width_ = width; height_ = height;
    fb_.assign((size_t)width * height * 4, 0);
    camera_.view_width  = (float)width;
    camera_.view_height = (float)height;
}

void Renderer::begin_frame() {
    std::uint8_t r = (std::uint8_t)(clear_r_ * 255.0f);
    std::uint8_t g = (std::uint8_t)(clear_g_ * 255.0f);
    std::uint8_t b = (std::uint8_t)(clear_b_ * 255.0f);
    std::uint8_t a = (std::uint8_t)(clear_a_ * 255.0f);
    for (size_t i = 0; i + 3 < fb_.size(); i += 4) {
        fb_[i] = r; fb_[i+1] = g; fb_[i+2] = b; fb_[i+3] = a;
    }
}

void Renderer::end_frame() {}

void Renderer::plot_blend(int x, int y, std::uint8_t r, std::uint8_t g,
                          std::uint8_t b, std::uint8_t a) noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    if (a == 0) return;
    size_t i = ((size_t)y * width_ + x) * 4;
    if (a == 255) {
        fb_[i] = r; fb_[i+1] = g; fb_[i+2] = b; fb_[i+3] = 255;
    } else {
        float af = a / 255.0f;
        float of = (255 - a) / 255.0f;
        fb_[i]   = (std::uint8_t)(r * af + fb_[i]   * of);
        fb_[i+1] = (std::uint8_t)(g * af + fb_[i+1] * of);
        fb_[i+2] = (std::uint8_t)(b * af + fb_[i+2] * of);
        fb_[i+3] = (std::uint8_t)(a  + fb_[i+3] * of);
    }
}

// ---- Textured quad (world space, Y-UP) ----
// (x,y) is the BOTTOM-LEFT corner in world coordinates (Y-UP: +Y is up).
// The quad extends right (+X) and up (+Y) from (x,y).
// UV mapping: (u0,v0) = top-left of the atlas frame (PNG top-left origin),
//             (u1,v1) = bottom-right of the atlas frame.
// World bottom (y) maps to atlas bottom (v1), world top (y+h) maps to atlas top (v0).
void Renderer::draw_textured_quad(
    const Texture& texture,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    Color4B color)
{
    // World corners (Y-UP): bottom-left, bottom-right, top-right, top-left
    float corners_world[4][2] = {
        {x,     y    },  // bottom-left
        {x + w, y    },  // bottom-right
        {x + w, y + h},  // top-right
        {x,     y + h},  // top-left
    };
    // UV corners: bottom-left of world -> bottom of atlas (v1), top -> top (v0)
    // PNG/atlas has top-left origin, so v0=top, v1=bottom.
    // World bottom (y) = atlas bottom (v1), world top (y+h) = atlas top (v0).
    float corners_uv[4][2] = {
        {u0, v1},  // bottom-left  -> atlas bottom-left
        {u1, v1},  // bottom-right -> atlas bottom-right
        {u1, v0},  // top-right    -> atlas top-right
        {u0, v0},  // top-left     -> atlas top-left
    };
    float sx[4], sy[4];
    for (int i = 0; i < 4; ++i)
        camera_.world_to_screen(corners_world[i][0], corners_world[i][1], sx[i], sy[i]);

    // Split into two triangles: (0,1,2) and (0,2,3).
    struct Tri { int a, b, c; };
    Tri tris[2] = {{0,1,2},{0,2,3}};
    for (const auto& tri : tris) {
        float ax = sx[tri.a], ay = sy[tri.a];
        float bx = sx[tri.b], by = sy[tri.b];
        float cx = sx[tri.c], cy = sy[tri.c];
        float au = corners_uv[tri.a][0], av = corners_uv[tri.a][1];
        float bu = corners_uv[tri.b][0], bv = corners_uv[tri.b][1];
        float cu = corners_uv[tri.c][0], cv = corners_uv[tri.c][1];
        float minx = std::min({ax, bx, cx});
        float maxx = std::max({ax, bx, cx});
        float miny = std::min({ay, by, cy});
        float maxy = std::max({ay, by, cy});
        int x0 = std::max(0, (int)std::floor(minx));
        int x1 = std::min(width_  - 1, (int)std::ceil(maxx));
        int y0 = std::max(0, (int)std::floor(miny));
        int y1 = std::min(height_ - 1, (int)std::ceil(maxy));
        float denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(denom) < 1e-6f) continue;
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                float fx = px + 0.5f, fy = py + 0.5f;
                float w0 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / denom;
                float w1 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / denom;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float u = w0 * au + w1 * bu + w2 * cu;
                float v = w0 * av + w1 * bv + w2 * cv;
                std::uint8_t tr, tg, tb, ta;
                texture.sample(u, v, tr, tg, tb, ta);
                ta = (std::uint8_t)((int)ta * color.a / 255);
                if (ta == 0) continue;
                tr = (std::uint8_t)((int)tr * color.r / 255);
                tg = (std::uint8_t)((int)tg * color.g / 255);
                tb = (std::uint8_t)((int)tb * color.b / 255);
                plot_blend(px, py, tr, tg, tb, ta);
            }
        }
    }
}

// ---- Textured quad (screen space) ----
void Renderer::draw_textured_quad_screen(
    const Texture& texture,
    float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    Color4B color)
{
    // (x,y) is top-left in screen coords (Y+ down).
    float corners_screen[4][2] = {
        {x,     y    },
        {x + w, y    },
        {x + w, y + h},
        {x,     y + h},
    };
    float corners_uv[4][2] = {
        {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
    };
    struct Tri { int a, b, c; };
    Tri tris[2] = {{0,1,2},{0,2,3}};
    for (const auto& tri : tris) {
        float ax = corners_screen[tri.a][0], ay = corners_screen[tri.a][1];
        float bx = corners_screen[tri.b][0], by = corners_screen[tri.b][1];
        float cx = corners_screen[tri.c][0], cy = corners_screen[tri.c][1];
        float au = corners_uv[tri.a][0], av = corners_uv[tri.a][1];
        float bu = corners_uv[tri.b][0], bv = corners_uv[tri.b][1];
        float cu = corners_uv[tri.c][0], cv = corners_uv[tri.c][1];
        float minx = std::min({ax, bx, cx});
        float maxx = std::max({ax, bx, cx});
        float miny = std::min({ay, by, cy});
        float maxy = std::max({ay, by, cy});
        int x0 = std::max(0, (int)std::floor(minx));
        int x1 = std::min(width_  - 1, (int)std::ceil(maxx));
        int y0 = std::max(0, (int)std::floor(miny));
        int y1 = std::min(height_ - 1, (int)std::ceil(maxy));
        float denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(denom) < 1e-6f) continue;
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                float fx = px + 0.5f, fy = py + 0.5f;
                float w0 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / denom;
                float w1 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / denom;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float u = w0 * au + w1 * bu + w2 * cu;
                float v = w0 * av + w1 * bv + w2 * cv;
                std::uint8_t tr, tg, tb, ta;
                texture.sample(u, v, tr, tg, tb, ta);
                ta = (std::uint8_t)((int)ta * color.a / 255);
                if (ta == 0) continue;
                tr = (std::uint8_t)((int)tr * color.r / 255);
                tg = (std::uint8_t)((int)tg * color.g / 255);
                tb = (std::uint8_t)((int)tb * color.b / 255);
                plot_blend(px, py, tr, tg, tb, ta);
            }
        }
    }
}

void Renderer::draw_filled_rect_screen(float x, float y, float w, float h,
                                       Color4B color) {
    int x0 = std::max(0, (int)std::floor(x));
    int y0 = std::max(0, (int)std::floor(y));
    int x1 = std::min(width_  - 1, (int)std::floor(x + w));
    int y1 = std::min(height_ - 1, (int)std::floor(y + h));
    for (int py = y0; py <= y1; ++py)
        for (int px = x0; px <= x1; ++px)
            plot_blend(px, py, color.r, color.g, color.b, color.a);
}

void Renderer::draw_filled_circle_screen(float cx, float cy, float radius,
                                         Color4B color) {
    int r = (int)std::ceil(radius);
    int x0 = std::max(0, (int)std::floor(cx - r));
    int x1 = std::min(width_  - 1, (int)std::ceil(cx + r));
    int y0 = std::max(0, (int)std::floor(cy - r));
    int y1 = std::min(height_ - 1, (int)std::ceil(cy + r));
    float r2 = radius * radius;
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            float dx = px + 0.5f - cx;
            float dy = py + 0.5f - cy;
            if (dx * dx + dy * dy <= r2)
                plot_blend(px, py, color.r, color.g, color.b, color.a);
        }
    }
}

void Renderer::draw_line_screen(float x0, float y0, float x1, float y1,
                                Color4B color) {
    draw_line_screen_thick(x0, y0, x1, y1, color, 1.0f);
}

void Renderer::draw_line_screen_thick(float x0, float y0, float x1, float y1,
                                      Color4B color, float thickness) {
    // Draw the line as a filled rectangle along the line direction.
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) {
        // Draw a small dot
        int r = (int)std::ceil(thickness * 0.5f);
        for (int py = (int)y0 - r; py <= (int)y0 + r; ++py)
            for (int px = (int)x0 - r; px <= (int)x0 + r; ++px) {
                float ddx = px + 0.5f - x0, ddy = py + 0.5f - y0;
                if (ddx * ddx + ddy * ddy <= thickness * thickness * 0.25f)
                    plot_blend(px, py, color.r, color.g, color.b, color.a);
            }
        return;
    }
    float ux = dx / len, uy = dy / len;
    float px_dir = -uy, py_dir = ux;
    float half_t = thickness * 0.5f;
    // Four corners
    float cx0 = x0 + px_dir * half_t, cy0 = y0 + py_dir * half_t;
    float cx1 = x0 - px_dir * half_t, cy1 = y0 - py_dir * half_t;
    float cx2 = x1 - px_dir * half_t, cy2 = y1 - py_dir * half_t;
    float cx3 = x1 + px_dir * half_t, cy3 = y1 + py_dir * half_t;
    // Draw filled rect by rasterising the polygon
    float minx = std::min({cx0, cx1, cx2, cx3});
    float maxx = std::max({cx0, cx1, cx2, cx3});
    float miny = std::min({cy0, cy1, cy2, cy3});
    float maxy = std::max({cy0, cy1, cy2, cy3});
    int x0i = std::max(0, (int)std::floor(minx));
    int x1i = std::min(width_  - 1, (int)std::ceil(maxx));
    int y0i = std::max(0, (int)std::floor(miny));
    int y1i = std::min(height_ - 1, (int)std::ceil(maxy));
    // For each pixel, check if it's inside the rectangle defined by the
    // line direction and perpendicular.
    for (int py = y0i; py <= y1i; ++py) {
        for (int px = x0i; px <= x1i; ++px) {
            float fx = px + 0.5f - x0, fy = py + 0.5f - y0;
            // Project onto line direction
            float along = fx * ux + fy * uy;
            float perp  = fx * px_dir + fy * py_dir;
            if (along >= -half_t && along <= len + half_t &&
                std::abs(perp) <= half_t) {
                plot_blend(px, py, color.r, color.g, color.b, color.a);
            }
        }
    }
}

void Renderer::draw_line_world(float x0, float y0, float x1, float y1,
                               Color4B color) {
    draw_line_world_thick(x0, y0, x1, y1, color, 1.0f);
}

void Renderer::draw_line_world_thick(float x0, float y0, float x1, float y1,
                                     Color4B color, float thickness) {
    float sx0, sy0, sx1, sy1;
    camera_.world_to_screen(x0, y0, sx0, sy0);
    camera_.world_to_screen(x1, y1, sx1, sy1);
    draw_line_screen_thick(sx0, sy0, sx1, sy1, color, thickness);
}

bool Renderer::save_png(const std::string& path) const {
    return stbi_write_png(path.c_str(), width_, height_, 4, fb_.data(), width_ * 4) != 0;
}

}  // namespace resf2::soft
