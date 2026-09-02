#pragma once

// Location scene: the game's `Bf` arena + `Qi` layers (JS_MAP §3.1/§7.3,
// dojo_params.xml / arena_params.xml).
//
// Builds sprites from the params XML (Image / SimpleEffect layers), the
// TexturePacker atlas JSON (ClassName -> frame rect) and the atlas texture
// (webp). Each layer carries a parallax Factor; sprites are children of the
// layer node. ModelsViewer layers (the fighters) are skipped — this phase
// renders the background only.

#include <memory>
#include <string>
#include <vector>

#include "scene/node.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"

namespace sf2::data {
struct Texture;
}

namespace sf2::scene {

struct Layer {
    std::string name;
    float factor = 1.0f;
    int type = 1;  // 1 = visual layer, 2 = ModelsViewer (fighters)
    std::vector<std::shared_ptr<Sprite>> sprites;
};

class LocationScene {
public:
    // Parses `params_xml`, resolves ClassNames via `atlas` and `atlas_tex`.
    // `res_root` is the res directory (e.g. "reference/www/res"); used to
    // load the atlas texture for SimpleEffect picture layers.
    // Throws std::runtime_error on malformed input.
    void load(const std::string& params_xml, const std::string& atlas_json,
              const std::string& atlas_tex_path, const std::string& res_root);

    // Multi-atlas variant: some arenas split frames across two TexturePacker
    // packs (e.g. arena.ca2949ef.json + arena-2.586e4f15.json). Frames from
    // all listed JSONs are merged into one ClassName -> frame map; the owning
    // atlas's pixel size is attached to each sprite for UV normalization.
    // The atlas textures themselves are uploaded by the caller probe.
    void load(const std::string& params_xml, const std::vector<std::string>& atlas_jsons,
              const std::string& res_root);

    // Uploads the atlas texture(s) into `renderer` and returns the layer
    // nodes in draw order (back to front).
    const std::vector<std::shared_ptr<Layer>>& layers() const { return layers_; }

    // ClassNames per atlas image, in load order — the caller uses this to
    // upload each atlas texture and alias every ClassName to its GL texture.
    const std::vector<std::string>& atlas_names() const { return atlas_names_; }

    // Fills `camera` with the game's fight-start framing: the camera is
    // centered on the arena (parallax offsets are 0) so the whole arena
    // width fits the view.
    void default_camera(sf2::render::Camera& camera, float view_w, float view_h) const;

    // Draws one layer's sprites through `renderer` (used by the probe).
    void render_layer(sf2::render::Renderer& renderer, const Layer& layer,
                      const sf2::render::Camera& camera) const;

    // Draws the layer range [begin, end) back-to-front (used by the fight
    // screen's split draw order: background layers -> fighters -> foreground
    // layers). The index of the ModelsViewer (Type=2) fighter layer splits
    // the range; see `fighter_layer()`.
    void render_layers(sf2::render::Renderer& renderer, const sf2::render::Camera& camera,
                       std::size_t begin, std::size_t end) const;

    // The index of the fighter layer (Type=2, ModelsViewer) in `layers()`,
    // or `npos` when the location has none. The original game draws the
    // FIGHTERS inside this layer: every layer before it is the background,
    // every layer after it (floor / dust / glow / pixel_1 vignette) is drawn
    // ON TOP of the fighters (JS_RENDER §7, "Что у нас не так" #1).
    std::size_t fighter_layer() const { return fighter_layer_; }

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    float arena_width() const { return arena_w_; }
    float arena_height() const { return arena_h_; }
    float arena_floor() const { return arena_floor_; }
    // The location Root Color (the `Root` element's Color attr, e.g.
    // "0x000000" for the dojo). The game's fighters are silhouettes filled
    // with this flat color (JS `Na.cd`); the fight screen sets the fighter
    // mesh color from it.
    std::uint32_t root_color() const { return root_color_; }

private:
    std::vector<std::shared_ptr<Layer>> layers_;
    std::vector<std::string> atlas_names_;
    std::size_t fighter_layer_ = npos;
    float arena_w_ = 0.0f;
    float arena_h_ = 0.0f;
    float arena_floor_ = 0.0f;
    std::uint32_t root_color_ = 0x000000u;  // default black (the dojo's Color)
};

} // namespace sf2::scene
