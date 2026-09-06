#pragma once

// 2D display object: a textured quad cut from an atlas frame.
//
// Corresponds to the game's `O` display object + `Ea` 2D render node
// (JS_MAP §1.5): a sprite has a texture, a frame rect in atlas pixels, a
// transform, and a per-sprite vertex color/tint (the game tints solid
// "pixel" fills and colored images via `Na.cd`).

#include <cstdint>
#include <string>

#include "scene/node.hpp"

namespace sf2::scene {

struct Sprite : public Node {
    // Owning texture name (atlas class name or path); used only for
    // diagnostics — the renderer resolves `texture` to a GL texture.
    std::string texture_name;

    // Atlas frame rect (in texture pixels).
    float frame_x = 0.0f;
    float frame_y = 0.0f;
    float frame_w = 0.0f;
    float frame_h = 0.0f;

    // Owning atlas texture size in pixels (frame rect lives inside it).
    // Used to normalize UVs to [0,1] before sampling. 0 = unset (solid).
    float tex_w = 0.0f;
    float tex_h = 0.0f;

    // Vertex tint (RGBA, 0..1). White = no tint. The game applies a tint
    // for <Image Color="0x...."> and solid "pixel_1" fills.
    float color_r = 1.0f;
    float color_g = 1.0f;
    float color_b = 1.0f;
    float color_a = 1.0f;

    // Sprite trimming from TexturePacker's spriteSourceSize / sourceSize
    // (JS pi.VJa L1703: Iq(filename, frame Ec, spriteSourceSize Ec, sourceSize
    // fc, trimmed) + Vs.Qq L1705: Pj(id, name, fa, frame, yx, qj=wNa offset,
    // dL, Yd) — qj carries the trim offset, fa the source size).
    // When a packed frame is trimmed (smaller than the original art), the
    // rendered quad must be shifted by:
    //   (source_w/2 - trim_x - frame_w/2, source_h/2 - trim_y - frame_h/2)
    // so the visible content aligns to the center of the full source frame.
    // Zero-initialised: no adjustment for un-trimmed sprites.
    float trim_x = 0.0f;    // spriteSourceSize.x in atlas pixels (JS wNa.x)
    float trim_y = 0.0f;    // spriteSourceSize.y in atlas pixels (JS wNa.y)
    float source_w = 0.0f;  // sourceSize.w untrimmed width (JS fa.x, 0=untrimmed)
    float source_h = 0.0f;  // sourceSize.h untrimmed height (JS fa.y, 0=untrimmed)

    // True for solid-color fills (ClassName="pixel_1") that do not sample
    // the atlas texture.
    bool solid = false;

    Transform transform;

    void render(sf2::render::Renderer& r) override;
};

} // namespace sf2::scene
