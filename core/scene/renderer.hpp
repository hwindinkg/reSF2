#pragma once

// Scene renderer for 2D location layers.
//
// Owns the GL context, the sprite batch, the texture cache, and the camera.
// Implements the game's camera + parallax transform (PORT_AUDIT_SCENE W1/W2,
// Ut.Al L826-827 + ma.Sya L1833):
//
//   layer_translate.x = Io * Factor          (Wrb: layer x = camera offset x factor)
//   layer_translate.y = F9 * (1 - Bj)        (Xrb: NON-scaled layers only, L826)
//   F9 = (arena_height/2 - floor) / 2
//   Io = arena_center_x = Lb.width/2 - focus (recomputed per frame by the caller)
//
//   screen_x = (world_x + Io*Factor - center_x) * zoom + view_w/2
//   screen_y = (world_y                    - center_y) * zoom + view_h/2
//
// The JS render camera position is (0,0) (`N.Ta.K4` L79/85): `center_y` is 0
// except the aspect<1 portrait shift, and there is NO unconditional vertical
// term in the projection — the `Xrb(F9*(1-Bj))` shift is a per-layer
// translate the caller folds in (scaled layers get `setScale(Bj)` instead,
// translate_y = 0). `layer_y` on `draw_sprite` carries that term.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "render/gl_types.hpp"
#include "render/sprite_batch.hpp"

struct GLFWwindow;

namespace sf2::data {
struct Texture;
}
namespace sf2::scene {
struct Node;
struct Sprite;
} // namespace sf2::scene

namespace sf2::render {

struct Camera {
    // Camera position in world units (JS `N.Ta` position). `K4()` (L85) resets
    // it to (0,0) every frame; `Sya` only moves x via `b.C(0)` (the no-op
    // min-zoom path at 16:9) and y via the aspect<1 portrait shift. So the
    // fight/hub paths run at center_x = 0, center_y = 0.
    float center_x = 0.0f;
    float center_y = 0.0f;
    // Screen pixels per world unit.
    float zoom = 1.0f;
    // Screen size in pixels.
    float view_w = 1280.0f;
    float view_h = 720.0f;
    // Arena geometry (from the params Root element).
    float arena_h = 560.0f;
    float arena_floor = 80.0f;
    // Io = Lb.width/2 - focus (JS `Ut.Al` L826): the parallax reference,
    // recomputed per frame by the caller (`center_x` stays 0 on the fight
    // path after W1 — the old `kCxOff` re-center is removed in screens.cpp).
    float arena_center_x = 0.0f;
    // The layer zoom Bj (JS `Ut.Bj`, L826): the per-layer node scale.
    // Layers with Type=2 (lEa) or Scaling (ij) draw at world*Bj (setScale);
    // the rest draw at identity (their translate carries Xrb(F9*(1-Bj))).
    // Hub-statics: Bj=1 (xCa=1 at spawn span, reset, max(1,NW)).
    float layer_zoom = 1.0f;

    // JS `Ut.init`/`Ut.Al` F9 = (Lb.height/2 - Lb.ct)/2 (L823/L843). The
    // `Xrb(F9*(1-Bj))` vertical translate applies ONLY to non-scaled layers.
    float f9() const { return (arena_h / 2.0f - arena_floor) / 2.0f; }
    // Game's camera x offset (Io).
    float camera_offset_x() const { return arena_center_x - center_x; }

    // World -> screen for a layer with parallax factor `factor`.
    float world_to_screen_x(float world_x, float factor) const {
        const float io = camera_offset_x();
        return (world_x + io * factor - center_x) * zoom + view_w / 2.0f;
    }
    float world_to_screen_y(float world_y) const {
        return (world_y - center_y) * zoom + view_h / 2.0f;
    }
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the GLFW window (hidden if `hidden`), loads GL, initializes
    // the batch. Call once per process, before any texture upload.
    bool init(int view_w, int view_h, bool hidden, GLFWwindow** out_window);

    void shutdown();

    SpriteBatch& batch() { return batch_; }

    // Uploads a core/data texture and caches it under `name`; returns the
    // GL texture id (0 on failure).
    GLuint texture_for(const std::string& name, const sf2::data::Texture& tex);

    // Registers an already-uploaded GL texture under an additional name.
    // Used for atlas alias resolution (one GL texture, many ClassNames).
    void texture_alias(const std::string& name, GLuint texture);

    // Resolves a sprite's owning texture name -> GL texture (0 if unknown).
    GLuint texture_lookup(const std::string& name) const;

    // Draws a sprite through the camera. `factor` is the layer's parallax
    // factor (the game's `Wrb(Io * bp)` — layer x offset is scaled by it).
    // `layer_scale` is the layer node's own scale (the game's per-layer
    // `Qi.setScale(Bj)` vs `Xrb(F9*(1-Bj))` branch, JS L488): scaled layers
    // (Type=2 or Scaling) draw at world*Bj through the camera; Xrb layers
    // draw at identity. `layer_y` is the Xrb translate the NON-scaled branch
    // carries (F9*(1-Bj)); it is 0 for scaled layers (JS Ut.Al L826-827).
    void draw_sprite(const sf2::scene::Sprite& sprite, const Camera& camera,
                      float factor = 1.0f, float layer_scale = 1.0f,
                      float layer_y = 0.0f);

    // Draws a flat-color triangle list in screen space (already projected).
    // `verts` = 2 floats per vertex (x,y); color is RGBA 0..1. One draw call
    // (the game's Path2D flat fill — MODEL_FORMAT §2.3).
    void draw_triangles(const float* verts, std::size_t vertex_count, float r,
                        float g, float b, float a = 1.0f);

    // Render pass: renders `node` (and its children) through `camera`.
    void render_node(sf2::scene::Node& node, const Camera& camera);

    // Clear to `color` (RGBA bytes, e.g. 0x000000) and start a frame.
    void begin_frame(const Camera& camera);

    // Flush any remaining batched geometry and present.
    void end_frame();

    // Camera the current render pass is using (set by begin_frame).
    const Camera& current_camera() const { return camera_; }

    GLFWwindow* window() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    SpriteBatch batch_;
    std::map<std::string, GLuint> textures_;
    Camera camera_;
};

} // namespace sf2::render
