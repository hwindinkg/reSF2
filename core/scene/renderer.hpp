#pragma once

// Scene renderer for 2D location layers.
//
// Owns the GL context, the sprite batch, the texture cache, and the camera.
// Implements the game's camera + parallax transform (JS_MAP §3.1, L826-827):
//
//   layer_translate.x = Io * Factor          (Wrb: layer x = camera offset x factor)
//   layer_translate.y = F9 * (1 - zoom)      (Xrb: vertical focus shift on zoom)
//   F9 = (arena_height/2 - floor) / 2
//   Io = arena_center_x - camera_center_x
//
//   screen_x = (world_x + Io*Factor - center_x) * zoom + view_w/2
//   screen_y = (world_y + F9*(1-zoom)   - center_y) * zoom + view_h/2
//
// At fight start with the whole arena visible the camera locks to the arena
// center (Io = 0) and the parallax is dormant — it matters when the camera
// pans, which later phases (fight camera controller) will drive.

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
    // World point the camera looks at.
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
    float arena_center_x = 0.0f;

    // Game's vertical focus shift (F9 * (1-zoom)); 0 at zoom == 1.
    float layer_vshift() const { return ((arena_h / 2.0f - arena_floor) / 2.0f) * (1.0f - zoom); }
    // Game's camera x offset (Io).
    float camera_offset_x() const { return arena_center_x - center_x; }

    // World -> screen for a layer with parallax factor `factor`.
    float world_to_screen_x(float world_x, float factor) const {
        const float io = camera_offset_x();
        return (world_x + io * factor - center_x) * zoom + view_w / 2.0f;
    }
    float world_to_screen_y(float world_y) const {
        return (world_y + layer_vshift() - center_y) * zoom + view_h / 2.0f;
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
    void draw_sprite(const sf2::scene::Sprite& sprite, const Camera& camera,
                     float factor = 1.0f);

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
