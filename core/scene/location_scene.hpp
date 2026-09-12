#pragma once

// Location scene: the game's `Bf` arena + `Qi` layers (JS_MAP §3.1/§7.3,
// dojo_params.xml / arena_params.xml).
//
// Builds sprites from the params XML (Image / SimpleEffect layers), the
// TexturePacker atlas JSON (ClassName -> frame rect) and the atlas texture
// (webp). Each layer carries a parallax Factor; sprites are children of the
// layer node. ModelsViewer layers (the fighters) are skipped — this phase
// renders the background only.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "scene/node.hpp"
#include "scene/renderer.hpp"
#include "scene/sprite.hpp"

namespace sf2::data {
struct Texture;
}

namespace sf2::scene {

// One live particle — JS `Cv` (L1152) wrapping the `Dv` view (L1649):
// `ca` = position, `ub` = velocity, `force` = spawn-time force vector,
// `wY` = angular velocity, `hA` = life, `view.{alpha,rotation,jka,kka}` = the
// billboard. `rotation_rad`/`ang_vel_rad` are radians (JS `d*.0174532925199`,
// L1151). `start_size` stays raw: the JS divides it by the frame
// `sourceSize.x` at spawn (L1151), which needs the unowned effects atlas.
struct Particle {
    float x = 0.0f;             // JS `ca.x` / `view.x`
    float y = 0.0f;             // JS `ca.y` / `view.y`
    float vx = 0.0f;            // JS `ub.x`
    float vy = 0.0f;            // JS `ub.y`
    float force_x = 0.0f;       // JS `force.x` (ForceX draw, L1150)
    float force_y = 0.0f;       // JS `force.y` (ForceY draw, L1150)
    float rotation_rad = 0.0f;  // JS `view.rotation` (radians)
    float ang_vel_rad = 0.0f;   // JS `wY` (radians/second)
    float life = 0.0f;          // JS `hA` (seconds remaining)
    float alpha = 0.0f;         // JS `view.alpha`
    float start_size = 0.0f;    // JS `vwb.Gb()` raw (renderer / sourceSize.x)
};

// One `ParticleEffect` / `NewParticleEffect` emitter node inside a location
// layer (JS `QIa` L481-482 + `jh` ctor L1147-1148). The game runs BOTH tags
// through the same `jh` system (`Bf.zjb` L476-477 -> `QIa` -> `fXa(new jh)`),
// which draws an instanced billboard batch (`Ah`/`Xb`, L1147) from the
// effects atlas `E.get(1304)`, sampler keyed on `Params/@Frame`.
//
// STATUS: the SIM is ported (`jh.update` L1149-1151: emission, force/gravity
// integration, life/alpha, cap). The RENDER pass is OPEN (D7): it needs the
// effects atlas `E.get(1304)` (Frame name -> id `b.re.et.v[Frame].id`, L1148),
// the instanced billboard batch `Xb`/`Ah` (`Ah.submit`, L1150; gradient start/
// end colour `BA.rP = Zib(Color)`, L1151) and the per-particle scale divisor
// `sourceSize.x` (L1151) — all owned by the renderer/effects layer, not this
// file. `particle_draws()` exposes the layer-local draws that renderer needs.
//
// D7 ROUTING (CONFIRMED N/A for locations): the `OnBackground` -> `Gfb`
// bg/fg split is a FIGHT-effect property, never a location-layer one. `Gfb`
// is parsed only by `Yl.parse` (L729 `this.Gfb=u.ka(a.attributes.get(
// "OnBackground"),!1)`) and consumed only by `tl.Nt` (L842
// `a.Gfb?this.Gq.Nt(a):this.Hq.Nt(a)`), whose `tl` container is attached
// inside the ModelsViewer layer (`tl.init` L843 `a.hn.go.nd(this.go)`) and
// fed by the fight `cv` effects manager (`tl.ZP` L844). Location layers never
// route through `tl`: `Bf.zjb` L476-477 sends every child to the owning `Qi`
// — `Image`->`ujb`(NWa L487), `SimpleEffect`->`UIa`(pWa L488),
// `ParticleEffect`/`NewParticleEffect`->`QIa`(fXa L488) — and `UWa` L832
// appends the layer nodes to the Render node in XML order. No shipped
// location params XML carries an `OnBackground` attribute (0 of 45). The
// native layer-ordered `draw_order` interleave is therefore JS-exact; do NOT
// add `Gfb` routing here. Only the effects-atlas billboard *draw*
// (`Ah.submit`, L1150) is still OPEN, and it is owned by the renderer.
// Range attrs follow the JS `Ie` reader (L1152-1153): "a,b" = random in [a,b],
// a single number = a fixed value (min == max).
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
    // JS `Zib(Color)[0]` (L1151-1152) after `Na.Rv` (L1448): the packed-ARGB
    // START colour, normalised to RGBA 0..1. White when `Color` is absent.
    // The end colour (`Zib[1]`) is parsed by the JS but never reaches a
    // fragment (the shader mix parameter `a_t` is always 0) — not stored.
    float tint_r = 1.0f;
    float tint_g = 1.0f;
    float tint_b = 1.0f;
    float tint_a = 1.0f;

    // ---- runtime state (JS `jh` fields, L1147-1151) -----------------------
    float spawn_acc = 0.0f;       // JS `$P` — spawn-interval accumulator (s)
    std::uint32_t rng = 0u;       // private deterministic LCG (see note below)
    std::vector<Particle> live;   // JS `pl` — the live particle list
};

// One live particle draw, ready for the effects renderer (JS `Dv` view +
// `Ah.pl`, L1147/L1649). Position/rotation are LAYER-LOCAL and already carry
// the emitter node transform: the batch node `Xb` has `scale=(1,-1)`
// (JS L1149), so the rendered centre is `(emitter.x + ca.x, emitter.y - ca.y)`
// and the rotation is negated. The renderer resolves `frame` through the
// effects atlas `E.get(1304)` and divides `start_size` by the frame's
// `sourceSize.x` (JS L1151).
struct ParticleDraw {
    float x = 0.0f;             // JS `emitter.x + ca.x` (layer-local)
    float y = 0.0f;             // JS `emitter.y - ca.y` (the Xb y-flip)
    float rotation_rad = 0.0f;  // JS `view.rotation`, negated for the flip
    float alpha = 0.0f;         // JS `view.alpha`
    float start_size = 0.0f;    // JS `view.jka`/`kka` before /sourceSize.x
    float factor = 1.0f;        // owning layer parallax Factor (`Qi.bp`)
    std::string frame;          // JS `Params/@Frame` (atlas 1304)
    // JS `Ah.rP[0]` = `Zib(Color)[0]` (L1151-1152), an `Na.Rv` ARGB colour
    // (L1448). White when the emitter carries no `Color`. The WebGL batch
    // shader (L1750) mixes `rP[0]`/`rP[1]` by the per-vertex `a_t`, which is
    // the view's `KXa` — initialised 0 (L1649) and never assigned, so only
    // `rP[0]` reaches the fragment. `rP[1]` (the end colour) is therefore
    // inert in the shipped build; see `Renderer::draw_particle`.
    float color_r = 1.0f;
    float color_g = 1.0f;
    float color_b = 1.0f;
    float color_a = 1.0f;
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
    // battlefield / autumn / ... . Simulated by `update`; drawn by
    // `render_layer` via `make_particle_draw_` + `Renderer::draw_particle`.
    std::vector<ParticleLayer> particles;

    // XML child draw order for layers that carry emitters (JS `zjb` L476-477
    // appends every `Image` / `SimpleEffect` / `ParticleEffect` to the layer
    // display list in document order; `nja` L29 renders self then children in
    // list order). The WebGL context runs with `depth:false` (L64) and
    // `u_zndc = -0.001*z` (L1488) only feeds `gl_Position.z`, so z does NOT
    // sort — document order does. Emitters must therefore interleave with
    // sprites, e.g. dark_room layer 4 / factory layer 4 (emitter BEFORE
    // sprites). Empty for emitter-free layers (sprite-only fast path).
    // `sprite` is a weak reference so a post-load erase (the hub drops
    // `dojo_punch_bag_holder`) can never dangle here.
    struct DrawItem {
        bool is_particle = false;
        std::size_t particle_index = 0;  // valid when `is_particle`
        std::weak_ptr<Sprite> sprite;    // valid when `!is_particle`
    };
    std::vector<DrawItem> draw_order;
};

// One animated SimpleEffect property (`Transparency` / `OscillationX/Y` /
// `Rotation`), mirroring the JS `zh` timeline (L1144-1146): a looping list of
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

// One `Sequention` frame (JS `ni.frames` L1142 + `R.Cb` L1616): the resolved
// TexturePacker frame the sequence steps through. `name` is also the texture
// alias the caller registers per page, so swapping frames swaps the sampled
// texture. Frames may live on different atlas pages (e.g. autumn + autumn-2):
// `ni.init` L1142 walks the page chain (`for(b=a;b!=null;b=b.nextPage)`) and
// collects matching frames from EVERY attached page, so this list spans pages.
// The caller must therefore alias every page's `frame_names` to that page's GL
// texture — see `atlas_pages()` (single page for dojo).
struct SequenceFrame {
    std::string name;
    float frame_x = 0.0f;
    float frame_y = 0.0f;
    float frame_w = 0.0f;
    float frame_h = 0.0f;
    float tex_w = 0.0f;
    float tex_h = 0.0f;
    bool trimmed = false;
    float trim_x = 0.0f;
    float trim_y = 0.0f;
    float source_w = 0.0f;
    float source_h = 0.0f;
    bool rotated = false;
};

// The `Sequention` playhead (JS `ni` L1141-1144) plus the outer `xl`
// sequence clock (JS `xl.vqb` L1136-1137, `k4a`/`ia`/`uma` L1138-1140).
// The frame stepper is `ni` (`mP` seconds/frame = `Speed/60`; `grb` selects
// the frame range, `hc` is the current frame). The outer clock measures in
// 1/60 s units (`uB = Speed*frames.length + 1`, `eG = Pause`); it feeds
// `ni.ia` while the sequence plays and calls `ni.nxa` (hide + reset) during
// the `Pause` tail, then loops.
struct SequenceAnim {
    std::vector<SequenceFrame> frames;
    float mP = 0.03f;    // JS `ni.mP` = Speed/60 (default 0.03, L1141)
    std::size_t mv = 0;  // JS `ni.mv` (first frame index)
    std::size_t qu = 0;  // JS `ni.Qu` (last frame index)
    std::size_t ux = 0;  // JS `ni.UX` (frame count in the active range)
    std::size_t hc = 0;  // JS `ni.hc` (frame currently shown)
    float qe = 0.0f;     // JS `ni.Qe` (frame-clock accumulator, seconds)
    int k9 = 1;          // JS `ni.K9` (step direction)
    bool lj = true;      // JS `ni.LJ` (playing)
    float uB = 0.0f;     // JS `xl.uB` = Speed*frames.length + 1 (vqb L1137)
    float eG = 0.0f;     // JS `xl.eG` = Pause (`ALa` L1137)
    float bs = 0.0f;     // JS `xl.bs` (units played within `uB`)
    float gl = 0.0f;     // JS `xl.Gl` (units elapsed in the `Pause` tail)
    float uoa = 0.0f;    // JS `xl.Uoa` = Offset/100 (`XXa` L1136)
};

// Per-sprite SimpleEffect modifier state (JS `xl` fields, L1135-1139):
//   osc_x / osc_y   <- OscillationX/Y (`RW`/`SW`, `bXa`/`cXa` L1137)
//   rot             <- Rotation (`lO`, `GXa` L1137; `rot_start` = `$sb`)
//   speed_x/speed_y <- Speed X/Y (`Kta`/`Lta`, `zsb` L1138; per-frame px)
//   reappear_x/y    <- ReappearX/Y (`qX`/`rX`, `gsb`/`hsb` + `of` L1138)
// `acc_x`/`acc_y` are the JS `JM`/`KM` accumulators (XML X/Y + speed).
// `sprite` is a non-owning pointer into `Layer::sprites` (stable address).
// A nested `SimpleEffect` (JS `bkb` L481 `case "SimpleEffect"` -> `xl.nd`)
// is a separate layer sprite registered here as a child: it starts with
// `hidden=true` (JS `OX`) and is only driven after this parent launches it
// (`kdb` on `EndAnimChildLaunch`/`ReappearChildLaunch`, `vOa` L1140).
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
    EffectTimeline rot;    // JS lO (Rotation modifier, GXa L1137)
    float rot_start = 0.0f;  // JS kta ($sb StartAngle, L1137)
    bool reappear_x = false;
    bool reappear_y = false;
    float re_x_min = 0.0f, re_x_max = 0.0f;  // JS Zo.min/max (L1140)
    float re_y_min = 0.0f, re_y_max = 0.0f;
    SequenceAnim seq;                  // Sequention state (empty for Picture)
    std::vector<SpriteAnim*> children;  // nested SimpleEffect (JS Fpa)
    bool has_parent = false;       // JS As != null
    bool hidden = false;           // JS OX (true = ia gated off)
    bool visible = true;           // JS Y.R / $m (false = not drawn)
    bool first = true;             // JS Lqa (k4a runs once)
    bool end_launch = false;       // JS Fra (EndAnimChildLaunch)
    bool reappear_launch = false;  // JS Gra (ReappearChildLaunch)
    bool hide_paused = false;      // JS Wqa (HidePaused)
};

// One TexturePacker atlas page loaded by `load` (JS `Bf.init` L474: the
// primary `locations/<name>/<name>.png` plus the `-2`, `-3`, … pages attached
// to the same atlas via `c.eXa(...)`; `ni.init` L1142 walks `b.nextPage`).
// Location textures are split across pages for 45 of the 46 shipped
// locations (only dojo is single-page; e.g. autumn + autumn-2), so a frame
// swap in a `Sequention` can cross pages. The caller uploads each page's image
// and aliases exactly `frame_names` to that page's GL texture; then a
// `Sequention` frame's `texture_name` resolves to the correct page. This is
// the scene-side half of the JS page chain; the `core/app/*` loaders must pass
// every page JSON to `load` and alias each page (see the note in the .cpp).
struct AtlasPage {
    std::string json_path;                 // the TexturePacker JSON for this page
    std::vector<std::string> frame_names;  // ClassNames packed in this page
    int width = 0;                         // atlas pixel size (UV normalization)
    int height = 0;
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

    // The loaded atlas pages in load order, each with its JSON path, pixel
    // size and the ClassNames packed in it. The caller uploads each page's
    // image (sibling of `json_path`, resolved by the location prefix — the
    // packer hash stems differ between JSON and image) and calls
    // `Renderer::texture_alias(frame_name, gl)` for every `frame_names` entry,
    // so a multi-page `Sequention` swap samples the right texture. Empty until
    // `load` succeeds.
    const std::vector<AtlasPage>& atlas_pages() const { return atlas_pages_; }

    // JSON paths of the loaded pages (same order as `atlas_pages()`), kept for
    // callers that only need the page list.
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
    // `KWa` loop, the OscillationX/Y / ReappearX/Y / Speed / Rotation modifier
    // block, and the `Sequention` frame timeline (JS `xl.ia` L1138-1140,
    // `bkb` L479-481). `dt` in seconds. A no-op when no layer carries a
    // timeline. The host calls it once per rendered frame.
    void update(float dt);

    // JS `jh` live draws (L1149): flattens every live particle into
    // layer-local draws (position/alpha/rotation/start_size/frame + the
    // owning layer's parallax Factor and `Zib(Color)` start colour). The
    // renderer resolves `frame` through the effects atlas `E.get(1304)` and
    // divides `start_size` by the frame `sourceSize.x` (L1148/L1151). Empty
    // for locations without particles. `render_layer` draws these in XML
    // child order (see `Layer::draw_order`); this flat list is the composed
    // view of every layer.
    std::vector<ParticleDraw> particle_draws() const;

    // The res root this scene was loaded from; used to lazily load the
    // effects atlas (`Renderer::ensure_particle_atlas`).
    const std::string& res_root() const { return res_root_; }

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
    // Root `Wall` -> JS `Bf.NU` (L474). The fighters are clamped to the
    // x-range [Wall, Width-Wall] (JS `ca.ggb` L383 `v.tFa=location.NU`,
    // `v.NKa=location.width-NU`; `Al.ia` L582 `fha`), so the caller must feed
    // the per-location value — it ranges 80..250 across the shipped
    // locations (dojo=80, the old hard-coded value). 0 when absent.
    float arena_wall() const { return arena_wall_; }
    // Root `PositionY` -> JS `Bf.Tza` (L474). The camera-bounds rule reads it
    // (L867 `this.eC=-a.location.Tza`); all shipped locations use -93/-94.
    // 0 when absent (the caller falls back as needed).
    float arena_position_y() const { return arena_position_y_; }
    // The location Root Color (the `Root` element's Color attr, e.g.
    // "0x000000" for the dojo). The game's fighters are silhouettes filled
    // with this flat color (JS `Na.cd`); the fight screen sets the fighter
    // mesh color from it.
    std::uint32_t root_color() const { return root_color_; }

    // JS `Bf.height` — the Root `Height` attr (`Bf.init` L474 stores it;
    // `Ut.m$a` L823 returns `Lb.height * Bj`, the `ma.Sya` L1833 render-zoom
    // denominator). `load` publishes the value of the location it just parsed;
    // the fight camera reads it back (`FightCamera::framing` ->
    // `framing_sya_impl`), because that camera keeps its own `arena_h` but has
    // no pointer to the scene. 0 until a location has been loaded, so callers
    // with no scene (the standalone demos) keep their own default.
    static float active_arena_height();

private:
    // JS `jh.update` (L1149-1151) for one emitter: advance the spawn clock,
    // emit at most one particle, integrate force/gravity/life/alpha, reap the
    // dead. `update()` drives every emitter; `load` runs the ctor `Prewarm`
    // loop and the `Qi.fXa` 150-tick attach warm-up (L481).
    void step_particle_(ParticleLayer& emitter, float dt);
    // JS `jh.Nvb` (L1150-1151): spawn one particle at a random emitter offset.
    void spawn_particle_(ParticleLayer& emitter);
    // JS `Ie.Gb` (L1152) over `oa.eT` (uniform in [lo,hi]) on a private LCG.
    float rand_range_(ParticleLayer& emitter, float lo, float hi);
    // Builds one renderer-ready draw from a live particle: applies the emitter
    // node offset and the batch node `Xb` y-flip (`x+X`, `y-Y`, negated
    // rotation, L1148-1149) and carries the `Zib(Color)` start colour.
    ParticleDraw make_particle_draw_(const Layer& layer, const ParticleLayer& emitter,
                                     const Particle& p) const;

    std::vector<std::shared_ptr<Layer>> layers_;
    // Per-SimpleEffect state (JS `bkb` L479-481), advanced by `update(dt)`.
    // Pointers target `Layer::sprites` elements; nested SimpleEffect children
    // (JS `xl.Fpa`) point into this same list, so it must not reallocate
    // after parsing (built once, then only iterated).
    std::vector<std::unique_ptr<SpriteAnim>> anims_;
    // Sprites hidden by the SimpleEffect visibility path (JS `Y.R(false)` /
    // `Nka`, L1136) — a launched nested effect that finishes hides itself
    // (`Wwb` L1140). Rebuilt every `update`; `render_layer` skips members.
    std::unordered_set<const Sprite*> hidden_sprites_;
    std::vector<std::string> atlas_names_;
    // Per-page frame lists for the multi-page aliasing contract (see
    // `AtlasPage`): the caller aliases every page, not just the primary one.
    std::vector<AtlasPage> atlas_pages_;
    // Res root passed to `load`; `render_layer` uses it to lazily load the
    // location effects atlas (`E.get(1304)`).
    std::string res_root_;
    std::size_t fighter_layer_ = npos;
    float arena_w_ = 0.0f;
    float arena_h_ = 0.0f;
    float arena_floor_ = 0.0f;
    float arena_wall_ = 0.0f;        // Root Wall (JS NU, L474)
    float arena_position_y_ = 0.0f;  // Root PositionY (JS Tza, L474)
    std::uint32_t root_color_ = 0x000000u;  // default black (the dojo's Color)
    // ModelsViewer spawns (JS `Yia`/`B_`, Bf.zjb L476).
    bool has_spawns_ = false;
    float player_spawn_x_ = 0.0f, player_spawn_y_ = 0.0f;
    float enemy_spawn_x_ = 0.0f, enemy_spawn_y_ = 0.0f;
};

} // namespace sf2::scene
