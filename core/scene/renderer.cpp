// Scene renderer implementation: context lifecycle, texture cache, and the
// sprite draw path (world -> camera -> screen quad -> batch).

#include "scene/renderer.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>

#include "render/gl.hpp"
#include "render/texture_gpu.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"

namespace sf2::render {

namespace {

// Builds the 6 vertices of a quad in screen space for a sprite.
// `factor` is the layer parallax factor: the camera x offset is scaled by
// it (game's `Wrb(Io * bp)`), so background layers shift less than the
// camera when it pans. At fight start (camera centered on the arena,
// Io == 0) the parallax is a no-op.
void sprite_to_quad(const sf2::scene::Sprite& s, const Camera& camera, float factor,
                     float layer_scale, SpriteQuad& quad) {
    const sf2::scene::Transform& t = s.transform;

    const float half_w = t.scale_x * s.frame_w / 2.0f;
    const float half_h = t.scale_y * s.frame_h / 2.0f;

    // Anchor offset in local units (0..1 -> -half..+half).
    const float ax = (t.anchor_x - 0.5f) * 2.0f * half_w;
    const float ay = (t.anchor_y - 0.5f) * 2.0f * half_h;

    const float cx = s.solid ? 0.0f : s.frame_x + s.frame_w / 2.0f;
    const float cy = s.solid ? 0.0f : s.frame_y + s.frame_h / 2.0f;
    const float half_u = s.solid ? 0.0f : s.frame_w / 2.0f;
    const float half_v = s.solid ? 0.0f : s.frame_h / 2.0f;

    // Local corners (in world units, centered on anchor).
    const float lx[4] = {-half_w - ax, half_w - ax, -half_w - ax, half_w - ax};
    const float ly[4] = {-half_h - ay, -half_h - ay, half_h - ay, half_h - ay};
    // Normalized UV corners [0,1] (the shader samples in normalized space).
    // The game's atlas frames are top-left origin and drawn upright.
    const float u_norm = s.tex_w > 0.0f ? 1.0f / s.tex_w : 1.0f;
    const float v_norm = s.tex_h > 0.0f ? 1.0f / s.tex_h : 1.0f;
    const float u[4] = {(cx - half_u) * u_norm, (cx + half_u) * u_norm,
                        (cx - half_u) * u_norm, (cx + half_u) * u_norm};
    const float v[4] = {(cy - half_v) * v_norm, (cy - half_v) * v_norm,
                        (cy + half_v) * v_norm, (cy + half_v) * v_norm};

    // Trim compensation: the sprite's XML position (t.x, t.y) is the center
    // of the original untrimmed source frame. When a TexturePacker trim
    // removes empty rows/columns, the packed content sits at (trim_x, trim_y)
    // inside the source, so its center in source space is:
    //   (trim_x + frame_w/2, trim_y + frame_h/2)
    // Shifting the quad by (source_w/2 - trim_x - frame_w/2) * scale_x (and
    // similarly in Y) realigns the visible content to the intended position.
    // Zero when not trimmed (source_w == 0 sentinel).
    const float trim_adj_x = s.source_w > 0.0f
        ? (s.source_w / 2.0f - s.trim_x - s.frame_w / 2.0f) * t.scale_x
        : 0.0f;
    const float trim_adj_y = s.source_h > 0.0f
        ? (s.source_h / 2.0f - s.trim_y - s.frame_h / 2.0f) * t.scale_y
        : 0.0f;

    // The layer-node scale (JS L488): scaled layers (lEa||ij) carry
    // go.scale=Bj, so every child world coord is pre-multiplied by Bj
    // BEFORE the shared camera projection runs. Corner pairing preserved:
    // 0=(-w,-h) 1=(+w,-h) 2=(-w,+h) 3=(+w,+h).
    const float sx0 = (t.x + trim_adj_x) * layer_scale;
    const float sy0 = (t.y + trim_adj_y) * layer_scale;
    const float lx0 = lx[0] * layer_scale, lx1 = lx[1] * layer_scale;
    const float ly0 = ly[0] * layer_scale, ly1 = ly[2] * layer_scale;
    const float sx[4] = {
        camera.world_to_screen_x(sx0 + lx0, factor), camera.world_to_screen_x(sx0 + lx1, factor),
        camera.world_to_screen_x(sx0 + lx0, factor), camera.world_to_screen_x(sx0 + lx1, factor)};
    const float sy[4] = {
        camera.world_to_screen_y(sy0 + ly0), camera.world_to_screen_y(sy0 + ly0),
        camera.world_to_screen_y(sy0 + ly1), camera.world_to_screen_y(sy0 + ly1)};

    // Two triangles: (0,1,2) (2,1,3).
    quad.v[0] = {sx[0], sy[0], u[0], v[0], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[1] = {sx[1], sy[1], u[1], v[1], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[2] = {sx[2], sy[2], u[2], v[2], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[3] = {sx[2], sy[2], u[2], v[2], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[4] = {sx[1], sy[1], u[1], v[1], s.color_r, s.color_g, s.color_b, s.color_a};
    quad.v[5] = {sx[3], sy[3], u[3], v[3], s.color_r, s.color_g, s.color_b, s.color_a};
}

} // namespace

bool Renderer::init(int view_w, int view_h, bool hidden, GLFWwindow** out_window) {
    if (!glfw_context_create(view_w, view_h, hidden, &window_)) {
        return false;
    }
    if (out_window != nullptr) {
        *out_window = window_;
    }
    if (!batch_.init(view_w, view_h)) {
        std::fprintf(stderr, "renderer: sprite batch init failed\n");
        shutdown();
        return false;
    }
    return true;
}

void Renderer::shutdown() {
    batch_.shutdown();
    for (auto& kv : textures_) {
        if (kv.second != 0) {
            delete_texture(kv.second);
        }
    }
    textures_.clear();
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

GLuint Renderer::texture_for(const std::string& name, const sf2::data::Texture& tex) {
    const auto it = textures_.find(name);
    if (it != textures_.end()) {
        return it->second;
    }
    const GLuint id = upload_texture_rgba(tex);
    textures_[name] = id;
    return id;
}

void Renderer::texture_alias(const std::string& name, GLuint texture) {
    if (texture != 0) {
        textures_[name] = texture;
    }
}

GLuint Renderer::texture_lookup(const std::string& name) const {
    const auto it = textures_.find(name);
    return it != textures_.end() ? it->second : 0;
}

void Renderer::draw_sprite(const sf2::scene::Sprite& sprite, const Camera& camera,
                            float factor, float layer_scale) {
    SpriteQuad quad;
    sprite_to_quad(sprite, camera, factor, layer_scale, quad);
    GLuint texture = 0;
    if (!sprite.solid) {
        texture = textures_.count(sprite.texture_name) ? textures_[sprite.texture_name] : 0;
    }
    batch_.add_quad(quad, texture);
}

void Renderer::draw_triangles(const float* verts, std::size_t vertex_count,
                              float r, float g, float b, float a) {
    batch_.add_triangles(verts, vertex_count, r, g, b, a);
}
void Renderer::render_node(sf2::scene::Node& node, const Camera& camera) {
    node.render(*this);
    for (const auto& child : node.children()) {
        render_node(*child, camera);
    }
}

void Renderer::begin_frame(const Camera& camera) {
    camera_ = camera;
    gl::glViewport(0, 0, static_cast<int>(camera.view_w), static_cast<int>(camera.view_h));
    gl::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl::glClear(GL_COLOR_BUFFER_BIT);
    gl::glEnable(GL_BLEND);
    gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    batch_.set_viewport(static_cast<int>(camera.view_w), static_cast<int>(camera.view_h));
}

void Renderer::end_frame() {
    batch_.flush();
    glfwSwapBuffers(window_);
    glfwPollEvents();
}

} // namespace sf2::render
