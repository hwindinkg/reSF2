#pragma once

// 2D display object: a textured quad cut from an atlas frame.
//
// Corresponds to the game's `O` display object + `Ea` 2D render node
// (JS_MAP §1.5): a sprite has a texture, a frame rect in atlas pixels, a
// transform, and a per-sprite vertex color/tint (the game tints solid
// "pixel" fills and colored images via `Na.cd`).

#include <cstdint>
#include <string>
#include <vector>

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
    //   (trim_x + frame_w/2 - source_w/2, trim_y + frame_h/2 - source_h/2)
    // so the visible content aligns to its offset inside the full source
    // frame (JS R.Cb L1615: Em=qj + ba(frame) + R.Th L1615 translate
    // b-f+d: content center lands at x-(fa/2-off-frame/2), i.e. the
    // NEGATION of (source/2-trim-frame/2)).
    // Zero-initialised: no adjustment for un-trimmed sprites.
    float trim_x = 0.0f;    // spriteSourceSize.x in atlas pixels (JS wNa.x)
    float trim_y = 0.0f;    // spriteSourceSize.y in atlas pixels (JS wNa.y)
    float source_w = 0.0f;  // sourceSize.w untrimmed width (JS fa.x, 0=untrimmed)
    float source_h = 0.0f;  // sourceSize.h untrimmed height (JS fa.y, 0=untrimmed)

    // TexturePacker 90-degree packing flag (JS Iq.dL via pi.VJa L1703 +
    // Vs.Qq L1705 Pj.dL + le.frame.dL: bk L1765 transposed draw + Cq L1561
    // rotate(-90deg)). 0 in all 12823 shipped res frames, but plumbed so a
    // rotated frame samples un-rotated JS-exact instead of stretched.
    bool rotated = false;

    // True for solid-color fills (ClassName="pixel_1") that do not sample
    // the atlas texture.
    bool solid = false;

    // Draw-depth (JS `Qi.NWa`/`Dla` L487, L1599): every sprite inside a layer
    // carries z = -0.01 * spriteIndex (the layer's `QH` starts at 0). The
    // renderer preserves the XML/insertion order — which already equals the
    // sorted order for the shipped venues — so this is neutral today; it is
    // carried so a future depth sort cannot regress the composition (D5).
    float z = 0.0f;

    // SimpleEffect Transparency animation (JS `bkb` L478-481: `irb(Offset)` +
    // `KWa(Period,Value,Ease)` keys). Each key's `Value` (0..100) is the
    // alpha reached `Period` seconds after the previous key; the list loops.
    // Dojo layer_4: 45@1.1, 75@3.5, 55@2.1, 75@2.5 -> live alpha 0.45..0.75.
    // Empty = static (color_a holds the rest value). The Ease curve shape is
    // not pinned down by the audit (OPEN) — LocationScene::update evaluates
    // the segments linearly (endpoints exact).
    struct TransKey {
        float value = 100.0f;  // Point/@Value (percent)
        float period = 1.0f;   // Point/@Period (seconds)
        float ease = 0.0f;     // Point/@Ease (unused; OPEN)
    };
    std::vector<TransKey> trans_keys;
    float trans_t = 0.0f;  // seconds into the looping timeline

    Transform transform;

    void render(sf2::render::Renderer& r) override;
};

} // namespace sf2::scene
