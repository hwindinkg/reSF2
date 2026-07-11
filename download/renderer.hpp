// engine/renderer/renderer.hpp
//
// Cocos2d-x 2.x-style renderer on GLES2 / OpenGL 2.1.
//
// Stage 7.2 implementation. Provides:
// - GLSL shader program cache (using the 17 shaders extracted from .s3e)
// - Texture2D (loads .png via stb_image, vendored)
// - TextureCache (LRU cache of loaded textures)
// - SpriteBatchNode (batched sprite rendering from atlas)
// - Camera2D (orthographic projection with follow + shake + zoom)
// - Renderer (main entry point: begin_frame / draw_sprite / end_frame)

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

// ---- Texture2D ----
// Wraps an OpenGL texture. Loaded from PNG via stb_image.
class Texture2D {
public:
    Texture2D();
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    // Load from raw RGBA pixel data
    void init_rgba(int width, int height, const std::uint8_t* pixels);

    // Load from PNG file data
    bool init_from_png(const std::uint8_t* data, std::size_t size);

    // Bind to a texture unit
    void bind(std::uint32_t unit = 0) const;

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    std::uint32_t gl_id() const noexcept { return gl_id_; }

private:
    std::uint32_t gl_id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

// ---- ShaderProgram ----
// Wraps a GLSL program (vertex + fragment shader).
class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // Compile from source
    bool init(const std::string& vertex_src, const std::string& fragment_src);

    // Use this program
    void use() const;

    // Set uniforms
    void set_uniform_mat4(const char* name, const Mat4& mat) const;
    void set_uniform_1i(const char* name, int value) const;
    void set_uniform_4f(const char* name, float x, float y, float z, float w) const;
    void set_uniform_1f(const char* name, float value) const;

    std::uint32_t gl_id() const noexcept { return program_; }

private:
    std::uint32_t program_ = 0;
    std::uint32_t vertex_shader_ = 0;
    std::uint32_t fragment_shader_ = 0;
};

// ---- SpriteVertex ----
// Vertex format for sprite rendering.
struct SpriteVertex {
    float x, y;          // position (screen coords)
    float u, v;          // texture coords
    std::uint8_t r, g, b, a;  // color (packed RGBA)
};

// ---- SpriteBatch ----
// Batches sprite draws into a single VBO + draw call.
class SpriteBatch {
public:
    SpriteBatch();
    ~SpriteBatch();

    bool init();

    // Begin a batch with a texture and shader
    void begin(const Texture2D& texture, const ShaderProgram& shader,
               const Mat4& mvp);

    // Draw a sprite (4 vertices = 2 triangles)
    void draw_quad(
        float x, float y, float w, float h,    // position + size (Y-UP world: bottom-left origin)
        float u0, float v0, float u1, float v1, // texture coords
        Color4B color = {255, 255, 255, 255}
    );

    // Draw a sprite in screen space (Y-DOWN: top-left origin)
    void draw_quad_screen(
        float x, float y, float w, float h,
        float u0, float v0, float u1, float v1,
        Color4B color = {255, 255, 255, 255}
    );

    // Draw a filled triangle (3 vertices, screen space Y-DOWN)
    void draw_triangle(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color = {255, 255, 255, 255}
    );

    // Flush all queued sprites to GPU
    void flush();

    // End the batch (auto-flushes)
    void end();

private:
    std::uint32_t vbo_ = 0;
    std::uint32_t vao_ = 0;  // VAO (not used on GLES2, but used on desktop GL)
    std::vector<SpriteVertex> vertices_;
    const Texture2D* current_texture_ = nullptr;
    const ShaderProgram* current_shader_ = nullptr;
    bool batching_ = false;
    static constexpr std::size_t kMaxVertices = 65536;
};

// ---- Camera2D ----
// 2D orthographic camera with smooth follow + shake + zoom.
class Camera2D {
public:
    Camera2D() = default;
    Camera2D(float view_width, float view_height);

    // Update camera (call once per frame)
    void update(std::uint32_t dt_ms);

    // Get the view-projection matrix
    Mat4 view_projection() const;

    // Target position (camera follows this)
    void set_target(float x, float y) { target_x_ = x; target_y_ = y; }

    // Zoom (1.0 = default, >1 = zoomed in)
    void set_zoom(float zoom) { target_zoom_ = zoom; }

    // Shake (amplitude in pixels, duration in ms)
    void shake(float amplitude, std::uint32_t duration_ms);

    float x() const noexcept { return x_; }
    float y() const noexcept { return y_; }
    float zoom() const noexcept { return zoom_; }

private:
    float view_width_;
    float view_height_;
    float x_ = 0, y_ = 0;
    float target_x_ = 0, target_y_ = 0;
    float zoom_ = 1.0f;
    float target_zoom_ = 1.0f;

    // Shake state
    float shake_amplitude_ = 0;
    float shake_decay_ = 0;
    std::uint32_t shake_duration_ = 0;
    std::uint32_t shake_elapsed_ = 0;
};

// ---- Renderer ----
// Main renderer entry point. Owns the shader cache, texture cache,
// sprite batch, and camera.
class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(int width, int height);
    void shutdown();

    void resize(int width, int height);

    // Frame lifecycle
    void begin_frame();
    void end_frame();

    // Draw a textured quad (the most common operation)
    void draw_textured_quad(
        const Texture2D& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    );

    // Draw a textured quad in screen space (top-left origin, Y down).
    // Bypasses the camera transform — used for HUD / UI overlays.
    void draw_textured_quad_screen(
        const Texture2D& texture,
        float x, float y, float w, float h,
        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
        Color4B color = {255, 255, 255, 255}
    );

    // Solid-color rectangle in screen space.
    void draw_filled_rect_screen(
        float x, float y, float w, float h,
        Color4B color
    );

    // Filled triangle in screen space (Y-DOWN, top-left origin).
    void draw_filled_triangle_screen(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    );

    // Filled triangle in world space (Y-UP, uses camera projection).
    void draw_filled_triangle_world(
        float x0, float y0, float x1, float y1, float x2, float y2,
        Color4B color
    );

    // Filled circle in screen space (approximated with triangle fan).
    void draw_filled_circle_screen(
        float cx, float cy, float radius,
        Color4B color
    );

    // Filled circle in world space (for capsule caps).
    void draw_filled_circle_world(
        float cx, float cy, float radius,
        Color4B color
    );

    // Line in screen space (1px wide via GL_LINES).
    void draw_line_screen(
        float x0, float y0, float x1, float y1,
        Color4B color
    );

    // Line in world space (1px wide).
    void draw_line_world(
        float x0, float y0, float x1, float y1,
        Color4B color
    );

    // Clear color
    void set_clear_color(float r, float g, float b, float a = 1.0f);

    // Camera
    Camera2D& camera() { return camera_; }

    // Shader cache
    const ShaderProgram& default_shader() const { return *default_shader_; }

private:
    std::unique_ptr<ShaderProgram> default_shader_;
    std::unique_ptr<SpriteBatch> batch_;
    Camera2D camera_;
    int width_ = 0;
    int height_ = 0;
    std::unique_ptr<Texture2D> white_tex_;  // 1x1 white pixel for filled shapes
};

}  // namespace resf2::renderer
