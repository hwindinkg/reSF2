#pragma once

// Fighter: model + animation sampling + flat-triangle rendering.
//
// Mirrors the game's `wd` fighter (MODEL_FORMAT §2): one merged ragdoll
// body (`Dl`), an animation controller (`Te`) that writes per-bone absolute
// world positions each frame, and a CPU-skinned 2D mesh (z dropped, flat
// color fill, one draw call).
//
// World placement: the fighter's world position anchors the COM bone
// (`wd.oL` offsets all bones relative to the COM). Facing negates X
// (`Te.Qeb`). Clip bone i maps to merged model bone i (order-sensitive).

#include <cstdint>
#include <string>
#include <vector>

#include "scene/model.hpp"

namespace sf2::data {
struct anim_clip;
}

namespace sf2::scene {

// A rendered fighter: merged model + one animation clip sampled at a frame.
class Fighter {
public:
    // Model (already merged, skeleton-first) and rest bind positions.
    void set_model(const Model& model);

    // Per-bone world positions at frame `f` of `clip`, anchored so the
    // fighter's COM bone sits at (x, y). Bones beyond the clip's bone count
    // keep their bind position. Facing -1 mirrors X.
    void sample(const sf2::data::anim_clip& clip, int frame, float x, float y,
                int facing);

    // Flat fill color (RGB, 0..255).
    void set_color(std::uint32_t rgb) {
        color_r_ = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
        color_g_ = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
        color_b_ = static_cast<float>(rgb & 0xFF) / 255.0f;
    }
    float color_r() const { return color_r_; }
    float color_g() const { return color_g_; }
    float color_b() const { return color_b_; }

    const Model& model() const { return model_; }
    const std::vector<float>& positions() const { return pos_; }  // x,y pairs

    // Fills `out` with the triangle vertex list (screen-space x,y pairs, z
    // dropped). Returns the vertex count (3 * triangle count).
    std::size_t build_vertices(std::vector<float>& out) const;

    // World-space bounding box of the triangle-referenced bones.
    void triangle_bbox(float& min_x, float& min_y, float& max_x,
                       float& max_y) const;

private:
    Model model_;
    std::vector<float> pos_;  // per-bone [x, y] after sampling (world space)
    float color_r_ = 1.0f;
    float color_g_ = 1.0f;
    float color_b_ = 1.0f;
};

} // namespace sf2::scene
