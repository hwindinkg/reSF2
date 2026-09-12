#pragma once

// Textured-quad sprite batching.
//
// Accumulates screen-space quads (x,y,u,v,color) and draws them with one
// draw call per texture. The shader is a minimal 2D pipeline: orthographic
// projection (screen pixels -> NDC), texture * vertex color. Alpha blending
// (SRC_ALPHA / ONE_MINUS_SRC_ALPHA) is enabled — the game's 2D renderer
// draws location sprites with premultiplied-ish alpha over the scene.

#include <cstdint>
#include <string>
#include <vector>

namespace sf2::render {

// One vertex: screen position, atlas UV, tint.
struct SpriteVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// A quad = 6 vertices (2 triangles), index-less (GL_TRIANGLES).
struct SpriteQuad {
    SpriteVertex v[6];
};

class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    // Creates GL resources (program, VAO, VBO). Must have a current context.
    bool init(int view_w, int view_h);

    void shutdown();

    void set_viewport(int view_w, int view_h);

    // Adds a quad for `texture` (0 = solid color, no texture sample).
    // Callers must have uploaded `texture` via the renderer's upload API.
    void add_quad(const SpriteQuad& quad, unsigned int texture);

    // Adds a flat-color triangle list (screen-space x,y pairs, z dropped) as
    // one solid-color draw (texture 0). `verts` = 2 floats per vertex.
    void add_triangles(const float* verts, std::size_t vertex_count, float r,
                       float g, float b, float a = 1.0f);

    // Screen-space clip rect (top-left origin, pixels). The native stand-in
    // for the JS node mask (`sf2.js` `lL` L1603 / Shop scroller `Gg.ba`
    // L1885); the JS WebGL backend actually draws the mask polygon into the
    // stencil buffer (`Vka`, GL_STENCIL_TEST 2960), which this axis-aligned
    // renderer has no pass for. `glScissor` is the closest faithful
    // equivalent for the scroller's axis-aligned viewer rect. glScissor uses
    // a BOTTOM-left origin, so the rect is flipped against the view height.
    // Changing the clip is a hard draw boundary: pending geometry is flushed
    // under the OLD clip first, so a batch never mixes clip states.
    // `enabled=false` clears the clip (a no-op state).
    void set_clip(bool enabled, int x, int y, int w, int h);

    // Draws everything currently batched and empties the buffer.
    void flush();

private:
    void draw_batch();

    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int white_tex_ = 0;  // 1x1 white texture for the solid-color path
    int uniform_proj_ = -1;
    int uniform_tex_ = -1;

    int view_w_ = 0;
    int view_h_ = 0;

    std::vector<SpriteVertex> vertices_;
    unsigned int current_texture_ = 0;  // 0 = none bound yet

    // Active clip (top-left origin, screen pixels); `clip_enabled_` false =
    // no clip (GL_SCISSOR_TEST disabled).
    bool clip_enabled_ = false;
    int clip_x_ = 0;
    int clip_y_ = 0;
    int clip_w_ = 0;
    int clip_h_ = 0;
};

} // namespace sf2::render
