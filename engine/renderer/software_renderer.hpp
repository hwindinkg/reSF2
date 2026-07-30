// engine/renderer/software_renderer.hpp
//
// CPU-side software renderer that produces the same visual output as the
// GL renderer, but renders into an in-memory RGBA framebuffer. Used for:
//   - Headless testing on machines without a GPU / GL context
//   - Automated screenshot generation
//   - Debugging the rendering pipeline without window-system dependencies
//
// The public API mirrors resf2::renderer::Renderer (camera, draw_textured_quad,
// begin_frame/end_frame, etc.) so that the same Game code can drive either
// backend. The texture and atlas types are duplicated here as CPU-only
// variants (no GL handles), so the renderer is fully self-contained.

#pragma once

#include "renderer/itexture.hpp"
#include "renderer/renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace resf2::soft {

// Use the same Color4B as the GL renderer
using Color4B = renderer::Color4B;

// ---- CPU-side texture (RGBA8) ----
// Mirrors resf2::renderer::Texture2D but stores pixels in main memory.
struct Texture : public renderer::ITexture {
    bool init_rgba(int w, int h, const std::uint8_t* px);
    bool init_from_png(const std::uint8_t* data, std::size_t size);

    // Sample with nearest-neighbour, return RGBA8. Out-of-range -> (0,0,0,0).
    void sample(float u, float v, std::uint8_t& r, std::uint8_t& g,
                std::uint8_t& b, std::uint8_t& a) const noexcept;

    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int height() const noexcept override { return height_; }
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept override { return pixels_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;  // RGBA8, tightly packed, row-major top->bottom
};

// ---- Camera2D (mirrors renderer::Camera2D) ----
// WORLD COORDINATE SYSTEM: Y-UP (matches cocos2d-x 2.2.6 convention used
// by the original game).
//   - Origin (0,0) is at the CENTER of the visible area.
//   - Positive Y is UP (toward ceiling/sky).
//   - Negative Y is DOWN (toward floor/ground).
//   - In the Dojo: PlayerPositionY=-93 means player is 93 units below
//     center. Floor masks are at negative Y, ceiling masks at positive Y.
//   - Screen coordinates remain Y-DOWN (0,0 = top-left) as is standard
//     for raster displays.
struct Camera2D {
    float view_width = 1280.0f;
    float view_height = 720.0f;
    float x = 0.0f, y = 0.0f;
    float target_x = 0.0f, target_y = 0.0f;
    float zoom = 1.0f;
    float target_zoom = 1.0f;

    void set_target(float tx, float ty) { target_x = tx; target_y = ty; }
    void set_zoom(float z) { target_zoom = z; zoom = z; }

    void snap() { x = target_x; y = target_y; zoom = target_zoom; }

    // World (Y-UP) -> screen (Y-DOWN) transform.
    // Positive world Y maps to smaller screen Y (upward on screen).
    bool world_to_screen(float wx, float wy, float& sx, float& sy) const noexcept;
};

// ---- Software renderer ----
class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init(int width, int height);
    void shutdown();

    void resize(int width, int height);

    void begin_frame();
    void end_frame();

    // Draw a textured quad in WORLD coordinates (subject to camera transform).
    // (u0,v0) is the top-left texture coordinate, (u1,v1) is bottom-right.
    void draw_textured_quad(
        const Texture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f,
        float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    );

    // Draw a textured quad in SCREEN coordinates (ignores camera).
    void draw_textured_quad_screen(
        const Texture& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f,
        float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    );

    // Solid-color quad in screen coordinates (HUD backgrounds, buttons).
    void draw_filled_rect_screen(
        float x, float y, float w, float h,
        Color4B color
    );

    // Filled circle in screen coordinates (for menu button backdrop, dots).
    void draw_filled_circle_screen(
        float cx, float cy, float radius,
        Color4B color
    );

    // Filled circle in world coordinates (subject to the camera transform).
    void draw_filled_circle_world(
        float cx, float cy, float radius,
        Color4B color
    );

    // Filled triangle in screen coordinates.
    //
    // Needed for the fighter and punching-bag silhouettes: those are drawn as
    // capsules, i.e. two triangles forming the shaft plus a circle at each
    // end. Without triangle support only the end circles appeared, so every
    // body rendered as a string of disconnected beads.
    void draw_filled_triangle_screen(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    );

    // Filled triangle in world coordinates (subject to the camera transform).
    void draw_filled_triangle_world(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    );

    // 1-pixel-wide line in screen coordinates.
    void draw_line_screen(
        float x0, float y0, float x1, float y1,
        Color4B color
    );

    // Thick line in screen coordinates.
    void draw_line_screen_thick(
        float x0, float y0, float x1, float y1,
        Color4B color, float thickness
    );

    // 1-pixel-wide line in world coordinates (for skeletal stick figure).
    void draw_line_world(
        float x0, float y0, float x1, float y1,
        Color4B color
    );

    // Thick line in world coordinates.
    void draw_line_world_thick(
        float x0, float y0, float x1, float y1,
        Color4B color, float thickness
    );

    void set_clear_color(float r, float g, float b, float a = 1.0f) {
        clear_r_ = r; clear_g_ = g; clear_b_ = b; clear_a_ = a;
    }

    Camera2D& camera() { return camera_; }

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

    // Access the framebuffer for saving to PNG.
    const std::vector<std::uint8_t>& framebuffer() const noexcept { return fb_; }

    // Save current framebuffer to a PNG file. Returns true on success.
    bool save_png(const std::string& path) const;

private:
    // Plot a single pixel with alpha blending.
    void plot_blend(int x, int y, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b, std::uint8_t a) noexcept;

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> fb_;  // RGBA8 framebuffer

    float clear_r_ = 0, clear_g_ = 0, clear_b_ = 0, clear_a_ = 1;
    Camera2D camera_;
};

}  // namespace resf2::soft
