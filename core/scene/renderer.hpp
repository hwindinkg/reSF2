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
#include <vector>

#include "render/gl_types.hpp"
#include "render/sprite_batch.hpp"

struct GLFWwindow;

namespace sf2::data {
struct Texture;
}
namespace sf2::scene {
struct Node;
struct Sprite;
struct ParticleDraw;
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

    // Lazily loads the location particle effects atlas (JS `E.get(1304)` =
    // `fight/particles.png`, manifest L2490 token 1304; frames in the sibling
    // `fight/particles.json`, token 1305). Decodes the texture, parses the
    // TexturePacker frames and caches them for `draw_particle`. Idempotent:
    // the first call attempts the load, later calls return the cached result.
    // An empty/absent `res_root` is a no-op (returns false). Requires a
    // current GL context on the first (loading) call.
    bool ensure_particle_atlas(const std::string& res_root);

    // Draws one location particle billboard through the camera — the native
    // equivalent of the game's instanced billboard batch `Ah`/`Xb`
    // (`Ah.submit` L1150; the WebGL batch `ar` L1753-1755). The billboard is
    // the atlas frame `draw.frame` scaled by `draw.start_size / sourceSize.x`
    // (JS L1151 `jka`/`kka`), centred on the already-flipped world position,
    // rotated by `draw.rotation_rad` and tinted by `draw.color_* * alpha`.
    // `layer_scale`/`layer_y` are the owning layer's node transform, exactly
    // as `draw_sprite` receives them.
    void draw_particle(const sf2::scene::ParticleDraw& draw, const Camera& camera,
                       float layer_scale = 1.0f, float layer_y = 0.0f);

    // Draws a flat-color triangle list in screen space (already projected).
    // `verts` = 2 floats per vertex (x,y); color is RGBA 0..1. One draw call
    // (the game's Path2D flat fill — MODEL_FORMAT §2.3).
    void draw_triangles(const float* verts, std::size_t vertex_count, float r,
                        float g, float b, float a = 1.0f);

    // Effect-layer primitive: a screen-space tinted quad centered on (cx,cy)
    // in SCREEN pixels, size (w,h) in screen pixels, rotated `rotation_deg`
    // about its center (JS `R3a` L486 `Wg` + the effect sprites' `Ga`
    // center anchor). Used for the flat effect draws (magic fallback, the
    // ringout arrow bodies) — it emits through the same batch as
    // `draw_triangles` and does not touch the sprite/camera geometry.
    void draw_effect_quad(float cx, float cy, float w, float h, float rotation_deg,
                          float r, float g, float b, float a = 1.0f);

    // Textured screen-space quad with EXPLICIT corner positions and UVs —
    // the native stand-in for the JS partial-frame draw (`le.mode`, the
    // `EFilled` mode `vc.ho` L1663). The canvas backend draws it with
    // `drawImage(img, srcRect, dstRect)` (`dda` L802087 case 0-3) and the
    // WebGL backend writes the same two rects straight into the quad's
    // xy/uv arrays (`dda` L911151): the visible box is the frame's source
    // SUB-RECT and the destination is a SUB-RECT of the node box. A rotated
    // node (JS `Wg` -> `Ar`, L486) therefore draws a rotated, UV-CLIPPED
    // quad — the VS intro's brush wipe (`ik` L2069/2072).
    // `xy` = 4 (x,y) pairs in SCREEN pixels, order TL, TR, BL, BR; `uv` = 4
    // (u,v) pairs aligned to the same corners (atlas-normalized). `texture_name`
    // is resolved through the renderer's texture cache (0 = solid fill).
    void draw_textured_quad(const std::string& texture_name, const float* xy,
                            const float* uv, float r, float g, float b, float a);

    // Render pass: renders `node` (and its children) through `camera`.
    void render_node(sf2::scene::Node& node, const Camera& camera);

    // Screen-space clip (top-left origin, pixels): every draw issued between
    // push_clip and pop_clip is masked to the rect. This is the native
    // equivalent of the JS node clip `node.lL(gb)`:
    //   `lL(a){...b=new wg...b.lL(a)}` (sf2.js L1603) stores the rect on a
    //   per-node mask state; `wg.lL` (L1603) turns the `gb` (left/top/right/
    //   bottom) into a 4-corner polygon in the node's local space; the WebGL
    //   backend applies it with GL_STENCIL_TEST (`Vka`, 2960), the canvas
    //   backend with `Path2D` + `ctx.clip`.
    // The native renderer has no stencil pass, so this maps to the closest
    // axis-aligned equivalent, glScissor (documented in SpriteBatch::set_clip).
    // It clips the node AND its children (the JS state stacks per node), so
    // nested pushes intersect with the currently active rect.
    void push_clip(float x, float y, float w, float h);
    void pop_clip();

    // Clear to `color` (RGBA bytes, e.g. 0x000000) and start a frame.
    void begin_frame(const Camera& camera);

    // Flush any remaining batched geometry and present.
    void end_frame();

    // Camera the current render pass is using (set by begin_frame).
    const Camera& current_camera() const { return camera_; }

    GLFWwindow* window() const { return window_; }

private:
    // One screen-space clip rect (top-left origin, pixels); the clip stack
    // entry (see push_clip). Kept in float and rounded when handed to
    // glScissor so nested intersections stay exact.
    struct ClipRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    // Pushes the top of `clip_stack_` (or clears the clip when empty) into
    // the SpriteBatch.
    void apply_clip();

    // One frame of the particle effects atlas (JS `b.re.dt[id]`, L1148/L1151):
    // the packed rect `Nc`, the untrimmed `sourceSize` `fa`, the `yx` trim
    // flag and the owning texture size (for UV normalization).
    struct ParticleFrame {
        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;  // packed frame rect
        float src_w = 0.0f, src_h = 0.0f;                  // sourceSize (fa)
        float tex_w = 0.0f, tex_h = 0.0f;                  // atlas texture size
        bool trimmed = false;                              // Nc.yx
    };

    GLFWwindow* window_ = nullptr;
    SpriteBatch batch_;
    std::map<std::string, GLuint> textures_;
    Camera camera_;

    // Active clip stack (see push_clip); empty = no clip.
    std::vector<ClipRect> clip_stack_;

    // Location particle effects atlas (`E.get(1304)`), resolved lazily by
    // `ensure_particle_atlas` and drawn by `draw_particle`.
    std::map<std::string, ParticleFrame> particle_frames_;
    GLuint particle_texture_ = 0;
    bool particle_atlas_attempted_ = false;
};

} // namespace sf2::render
