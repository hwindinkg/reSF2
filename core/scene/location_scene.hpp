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

// One `ParticleEffect` / `NewParticleEffect` emitter node inside a location
// layer (JS `QIa` L481-482 + `jh` ctor L1147-1148). The game runs BOTH tags
// through the same `jh` system (`Bf.zjb` L476-477 -> `QIa` -> `fXa(new jh)`),
// which draws an instanced billboard batch (`Ah`/`Xb`, L1147) from the
// effects atlas `E.get(1304)`, sampler keyed on `Params/@Frame`.
//
// STATUS: parsed only. The native port does NOT yet simulate (`jh.update`
// L1149-1151) or render (`Dv`/`Xb`/`Ah` L1147) location particles — that
// needs the effects atlas plus the renderer/effects owner (OPEN, D7). The
// emitter config is carried here so a future wave can drive it without
// re-parsing the XML. Range attrs follow the JS `Ie` reader (L1152-1153):
// "a,b" = random in [a,b], a single number = a fixed value (min == max).
struct ParticleLayer {
    std::string class_name;    // node/@ClassName (metadata; `jh` ignores it)
    float x = 0.0f;            // node/@X   (JS QIa L481)
    float y = 0.0f;            // node/@Y   (JS QIa L481)
    std::string frame;         // Params/@Frame         (JS jh L1148)
    float life_min = 0.0f;     // Params/@Life          (JS Ie L1147)
    float life_max = 0.0f;
    float gravity = 0.0f;      // Params/@Gravity       (JS jh L1147)
    float force_x_min = 0.0f;  // Params/@ForceX        (JS Ie L1147)
    float force_x_max = 0.0f;
    float force_y_min = 0.0f;  // Params/@ForceY
    float force_y_max = 0.0f;
    float rate = 0.0f;         // Params/@Rate          (JS jh L1147, u.H)
    int max_particles = 500;   // Params/@MaxParticles  (JS default 500)
    float ang_vel_min = 0.0f;  // Params/@AngVel        (JS Ie L1147)
    float ang_vel_max = 0.0f;
    float start_size_min = 0.0f;  // Params/@StartSize  (JS Ie L1147)
    float start_size_max = 0.0f;
    float start_rot_min = 0.0f;   // Params/@StartRotation (JS Ie L1147)
    float start_rot_max = 0.0f;
    float start_speed = 0.0f;     // Params/@StartSpeed    (JS jh L1148)
    float vel_x_min = 0.0f;       // Params/@VelocityX     (JS Ie L1148)
    float vel_x_max = 0.0f;
    float vel_y_min = 0.0f;       // Params/@VelocityY     (JS Ie L1148)
    float vel_y_max = 0.0f;
    float emitter_x = 0.0f;       // Params/@Emitter "x,y" (JS xkb L1151)
    float emitter_y = 0.0f;
    bool prewarm = false;         // Params/@Prewarm == "1" (JS jh L1149)
    std::string color;            // Params/@Color (1 or 2 packed ARGB)
};

struct Layer {
    std::string name;
    float factor = 1.0f;
    int type = 1;  // 1 = visual layer, 2 = ModelsViewer (fighters)
    // The `Scaling` attr (>0) -> `ij` (JS zjb L475-476: `b.ij=c>0`). Dojo:
    // every visual layer carries Scaling="1"; Type=2 has none (ij=false)
    // but takes the setScale branch via lEa() (JS L488).
    bool scaling = false;
    // Draw-depth (JS `Bf.init` L475: `c=0; ... c += -3` -> z = -3*layerIndex).
    // The renderer preserves XML order (already the sorted order here); the
    // value is carried against future depth sorting (audit D5).
    float z = 0.0f;
    std::vector<std::shared_ptr<Sprite>> sprites;
    // `ParticleEffect`/`NewParticleEffect` emitters (JS `QIa` L481-482).
    // Empty for the dojo (it ships none); populated for volcano / factory /
    // battlefield / autumn / ... . Parsed only — see ParticleLayer (OPEN).
    std::vector<ParticleLayer> particles;
};

// One animated SimpleEffect property (`Transparency` / `OscillationX/Y`),
// mirroring the JS `zh` timeline (L1144-1146): a looping list of
// (Period, Value, Ease) keys with a seed `Offset`. The JS per-frame update
// (`zh.update`, L1146) advances `ar` by the frame delta and wraps segment
// `wp`; `zh.Gb` (L1146) evaluates the current segment as a line (Ease == 0)
// or a parabola (Ease != 0) hitting both endpoints exactly:
//   e == 0 : value = ((B-A)/P)*t + A
//   e != 0 : value = e*(t+b)^2 + c, b = (B-A - e*P^2)/(2eP), c = A - e*b^2
// (coefficients precomputed by `zh.cmb`, L1145). This is the D6 OPEN curve.
// `keys` reuses `Sprite::TransKey` for the (Period, Value, Ease) triple.
struct EffectTimeline {
    std::vector<Sprite::TransKey> keys;
    float offset = 0.0f;  // JS `Mrb`/`irb` Offset -> seeds `zh.ar`
    float t = 0.0f;       // JS `zh.ar` (seconds into the current segment)
    std::size_t seg = 0;  // JS `zh.wp` (current segment index)
};

// Per-sprite SimpleEffect modifier state (JS `xl` fields, L1135-1139):
//   osc_x / osc_y   <- OscillationX/Y (`RW`/`SW`, `bXa`/`cXa` L1137)
//   speed_x/speed_y <- Speed X/Y (`Kta`/`Lta`, `zsb` L1138; per-frame px)
//   reappear_x/y    <- ReappearX/Y (`qX`/`rX`, `gsb`/`hsb` + `of` L1138)
// `acc_x`/`acc_y` are the JS `JM`/`KM` accumulators (XML X/Y + speed).
// `sprite` is a non-owning pointer into `Layer::sprites` (stable address).
// The SimpleEffect Rotation modifier (`$sb`/`Zsb`/`GXa`) and nested
// SimpleEffect are not represented here (OPEN, JS L481).
struct SpriteAnim {
    Sprite* sprite = nullptr;
    float base_x = 0.0f;  // XML X (JS setPosition L1138)
    float base_y = 0.0f;  // XML Y
    float acc_x = 0.0f;   // JS JM (speed accumulation + Reappear wrap)
    float acc_y = 0.0f;   // JS KM
    float speed_x = 0.0f;  // JS Kta (per-frame pixels, L1139)
    float speed_y = 0.0f;  // JS Lta
    EffectTimeline osc_x;  // JS RW
    EffectTimeline osc_y;  // JS SW
    bool reappear_x = false;
    bool reappear_y = false;
    float re_x_min = 0.0f, re_x_max = 0.0f;  // JS Zo.min/max (L1140)
    float re_y_min = 0.0f, re_y_max = 0.0f;

    bool animated() const {
        return !osc_x.keys.empty() || !osc_y.keys.empty() || reappear_x ||
               reappear_y || speed_x != 0.0f || speed_y != 0.0f;
    }
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

    // Fills `camera` with the game's Sya framing at the given focus. `Tf`
    // (L1971-1972) runs the hub through the LIVE fight camera: `Ut.Al`
    // receives the live `Go.ma` focus, so `Io = Lb.width/2 - focus` is
    // recomputed every frame (D3). Negative arguments fall back to the
    // spawn midpoint / spawn fighter span (the frame-0 value), so existing
    // callers keep the spawn framing until they pass the live focus.
    void default_camera(sf2::render::Camera& camera, float view_w, float view_h,
                        float focus_x = -1.0f, float fighter_span = -1.0f) const;

    // Advances time-animated scene elements: the SimpleEffect Transparency
    // `KWa` loop and the OscillationX/Y / ReappearX/Y / Speed modifier block
    // (JS `xl.ia` L1139, `bkb` L479-481). `dt` in seconds. A no-op when no
    // layer carries a timeline. The host calls it once per rendered frame.
    void update(float dt);

    // The ModelsViewer spawns (JS `Bf.zjb` L476: `Yia` = PlayerPosition,
    // `B_` = EnemyPosition). `has_spawns()` is false when the location has no
    // ModelsViewer layer.
    bool has_spawns() const { return has_spawns_; }
    float player_spawn_x() const { return player_spawn_x_; }
    float player_spawn_y() const { return player_spawn_y_; }
    float enemy_spawn_x() const { return enemy_spawn_x_; }
    float enemy_spawn_y() const { return enemy_spawn_y_; }

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
    // Per-SimpleEffect Oscillation/Reappear/Speed state (JS `bkb` L479-481),
    // advanced by `update(dt)`. Pointers target `Layer::sprites` elements.
    std::vector<SpriteAnim> anims_;
    std::vector<std::string> atlas_names_;
    std::size_t fighter_layer_ = npos;
    float arena_w_ = 0.0f;
    float arena_h_ = 0.0f;
    float arena_floor_ = 0.0f;
    std::uint32_t root_color_ = 0x000000u;  // default black (the dojo's Color)
    // ModelsViewer spawns (JS `Yia`/`B_`, Bf.zjb L476).
    bool has_spawns_ = false;
    float player_spawn_x_ = 0.0f, player_spawn_y_ = 0.0f;
    float enemy_spawn_x_ = 0.0f, enemy_spawn_y_ = 0.0f;
};

} // namespace sf2::scene
