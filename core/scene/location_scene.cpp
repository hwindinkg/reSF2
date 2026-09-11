// Location scene loader — params XML + atlas JSON + atlas texture -> sprite
// layer nodes, in the game's draw order (back to front).

#include "scene/location_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include "atlas.hpp"
#include "scene/sprite.hpp"
#include "texture.hpp"
#include "xml_doc.hpp"

namespace sf2::scene {

namespace {

// JS `jh` constants (L1149-1151): degrees->radians (`d*.0174532925199432`),
// the per-update gravity term `this.bab*9.81*.02` (added per CALL, NOT scaled
// by dt — an oracle quirk), and the alpha fade-in step (`view.alpha += .1`
// per call). A private deterministic LCG seed matches the effects layer so
// the sim never perturbs the fight's shared RNG (JS uses wall-clock `oa.eT`).
constexpr float kParticleDegToRad = 0.0174532925199432f;
constexpr float kParticleGravityScale = 9.81f * 0.02f;
constexpr float kParticleAlphaStep = 0.1f;
constexpr float kParticleFixedStep = 1.0f / 60.0f;  // JS `Prewarm` tick (L1149)
constexpr std::uint32_t kParticleSeed = 0x853C49E7u;

// A ClassName resolved against an atlas: the frame rect plus the pixel size
// of the atlas texture it lives in (for UV normalization).
struct FrameRef {
    sf2::data::atlas_frame frame;  // copied — atlases are parsed per-iteration
    int atlas_w = 0;
    int atlas_h = 0;
};

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

std::string read_file_text(const std::string& path) {
    std::vector<std::uint8_t> bytes = read_file_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

// Game's color parse: "0xRRGGBB" -> (R,G,B,1) in 0..1. Na.cd, JS L1448.
void parse_color(const std::string& hex, float& r, float& g, float& b) {
    unsigned int v = 0;
    try {
        const std::string h = hex.size() > 2 && hex[1] == 'x' ? hex.substr(2) : hex;
        v = static_cast<unsigned int>(std::stoul(h, nullptr, 16));
    } catch (...) {
        v = 0;
    }
    r = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
    g = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
    b = static_cast<float>(v & 0xFF) / 255.0f;
}

// Particle color parse: `Na.Rv(K.parseInt(s))` (JS L1448 + `Zib` L1152).
// `s` is a packed ARGB literal (e.g. "0xff5C3D2A"): R = bits 16-23,
// G = 8-15, B = 0-7, A = 24-31, each /255. Returns false (leaving the
// arguments untouched) on a malformed value, so the caller keeps white.
bool parse_argb(const std::string& s, float& r, float& g, float& b, float& a) {
    if (s.empty()) {
        return false;
    }
    unsigned long v = 0;
    try {
        v = std::stoul(s, nullptr, 16);  // base 16 accepts the "0x" prefix
    } catch (...) {
        return false;
    }
    const std::uint32_t argb = static_cast<std::uint32_t>(v);
    r = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
    g = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
    b = static_cast<float>(argb & 0xFFu) / 255.0f;
    a = static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
    return true;
}


// JS `zh.Gb` + `zh.cmb` (L1145-1146): evaluate segment `seg` at local time
// `t`. Ease 0 = line, Ease != 0 = parabola; both hit `A` at t=0 and `B` at
// t=Period (cmb solves `b`/`c` so `e*(P+b)^2+c == B`). This is the D6
// `Point/@Ease` curve the previous port ignored (it always lerped).
float zh_value(const std::vector<Sprite::TransKey>& keys, std::size_t n,
               std::size_t seg, float t) {
    const float a = keys[seg].value;
    const float b = keys[(seg + 1) % n].value;
    const float p = keys[seg].period;
    const float e = keys[seg].ease;
    if (p <= 0.0f) {
        return a;  // JS Gw==0 -> cmb zeroes b/c; degenerate
    }
    if (e == 0.0f) {
        return a + (b - a) * (t / p);
    }
    const float bc = (b - a - e * p * p) / (2.0f * e * p);
    const float cc = a - e * bc * bc;
    return e * (t + bc) * (t + bc) + cc;
}

// JS `zh.update` (L1146): advance the clock by `dt` (seconds) and roll the
// segment index while the accumulated time exceeds the segment's Period (a
// strict `>`, wrapping `wp` to 0 at the end of the list).
void advance_timeline(EffectTimeline& tl, float dt) {
    if (tl.keys.empty()) {
        return;
    }
    tl.t += dt;
    const std::size_t n = tl.keys.size();
    while (true) {
        const float per = tl.keys[tl.seg].period;
        if (per <= 0.0f) {  // degenerate JS Gw==0; do not spin
            tl.t = 0.0f;
            break;
        }
        if (tl.t <= per) {
            break;
        }
        tl.t -= per;
        if (++tl.seg >= n) {
            tl.seg = 0;
        }
    }
}

// JS `irb`/`Mrb`/`Nrb(Offset)` (L480-481): seed the timeline clock. The JS
// call is `zh.update(Offset)`, i.e. the same roll applied to the initial time.
void seed_timeline(EffectTimeline& tl) {
    if (tl.keys.empty()) {
        return;
    }
    tl.t = tl.offset;
    advance_timeline(tl, 0.0f);
}

// Current value of a modifier timeline (JS `zh.Gb`, L1146); 0 when inactive.
float timeline_value(const EffectTimeline& tl) {
    if (tl.keys.empty()) {
        return 0.0f;
    }
    return zh_value(tl.keys, tl.keys.size(), tl.seg, tl.t);
}

// JS `Ie` reader (L1152-1153): "a,b" -> [a,b] range (min != max), a single
// number -> a fixed value (min == max). Returns false when the attr is absent.
bool parse_range_attr(const pugi::xml_node& node, const char* name, float& lo,
                      float& hi) {
    const char* raw = node.attribute(name).value();
    if (raw == nullptr || raw[0] == '\0') {
        lo = hi = 0.0f;
        return false;
    }
    const char* comma = std::strchr(raw, ',');
    if (comma == nullptr) {
        lo = hi = static_cast<float>(std::atof(raw));
    } else {
        lo = static_cast<float>(std::atof(std::string(raw, comma).c_str()));
        hi = static_cast<float>(std::atof(comma + 1));
    }
    return true;
}

// JS `xkb` (L1151): "x,y" -> H(x,y). `parseFloat` stops at the comma, so a
// plain prefix parse is exact; a missing second component yields 0.
void parse_emitter_attr(const pugi::xml_node& node, float& ex, float& ey) {
    ex = ey = 0.0f;
    const char* raw = node.attribute("Emitter").value();
    if (raw == nullptr || raw[0] == '\0') {
        return;
    }
    const char* comma = std::strchr(raw, ',');
    ex = static_cast<float>(std::atof(raw));
    if (comma != nullptr) {
        ey = static_cast<float>(std::atof(comma + 1));
    }
}

// JS `QIa` L481-482 + `jh` ctor L1147-1148: one particle emitter. `a.st()`
// is the node's FIRST child (`st(){return this.children[0]}`), i.e. the
// `<Params>` element carrying every emitter attribute. The SIM runs in
// `LocationScene::update`; the render pass is OPEN (see ParticleLayer).
// `ordinal` seeds the private deterministic LCG so emitters do not phase-lock
// (JS draws from the wall-clock `oa.eT`; the native is deliberately stable).
ParticleLayer parse_particle(const pugi::xml_node& node, std::uint32_t ordinal) {
    ParticleLayer p;
    p.rng = kParticleSeed ^ (ordinal * 2654435761u);
    const char* cls = node.attribute("ClassName").value();
    p.class_name = cls != nullptr ? cls : "";
    p.x = sf2::data::xml_attr_float(node, "X");
    p.y = sf2::data::xml_attr_float(node, "Y");
    pugi::xml_node prm = node.child("Params");
    if (prm == nullptr) {
        prm = node;  // defensive: attrs on the emitter node itself
    }
    const char* frame = prm.attribute("Frame").value();
    p.frame = frame != nullptr ? frame : "";
    parse_range_attr(prm, "Life", p.life_min, p.life_max);
    p.gravity = sf2::data::xml_attr_float(prm, "Gravity");
    parse_range_attr(prm, "ForceX", p.force_x_min, p.force_x_max);
    parse_range_attr(prm, "ForceY", p.force_y_min, p.force_y_max);
    p.rate = sf2::data::xml_attr_float(prm, "Rate");
    p.max_particles = sf2::data::xml_attr_int(prm, "MaxParticles", 500);
    parse_range_attr(prm, "AngVel", p.ang_vel_min, p.ang_vel_max);
    parse_range_attr(prm, "StartSize", p.start_size_min, p.start_size_max);
    parse_range_attr(prm, "StartRotation", p.start_rot_min, p.start_rot_max);
    p.start_speed = sf2::data::xml_attr_float(prm, "StartSpeed");
    parse_range_attr(prm, "VelocityX", p.vel_x_min, p.vel_x_max);
    parse_range_attr(prm, "VelocityY", p.vel_y_min, p.vel_y_max);
    parse_emitter_attr(prm, p.emitter_x, p.emitter_y);
    const char* prewarm = prm.attribute("Prewarm").value();
    p.prewarm = prewarm != nullptr && std::strcmp(prewarm, "1") == 0;
    const char* color = prm.attribute("Color").value();
    p.color = color != nullptr ? color : "";
    // JS `Zib` (L1151-1152): "a,b" -> [Na.Rv(a), Na.Rv(b)]; a single value ->
    // [Na.Rv(a)]. Only `rP[0]` reaches the fragment (the shader's mix
    // parameter `a_t` is the always-zero `view.KXa`), so keep the first.
    if (!p.color.empty()) {
        const std::size_t comma = p.color.find(',');
        const std::string first =
            comma == std::string::npos ? p.color : p.color.substr(0, comma);
        parse_argb(first, p.tint_r, p.tint_g, p.tint_b, p.tint_a);
    }
    return p;
}

// The JS `bkb` modifier block (L479-481): OscillationX/Y (`Mrb`/`bXa`,
// `Nrb`/`cXa`), ReappearX/Y (`gsb`/`hsb` over `Zo`), Speed (`zsb`). The
// SimpleEffect Rotation modifier (`$sb`/`Zsb`/`GXa`) and nested SimpleEffect
// are not handled here (OPEN, L481).
void parse_simple_effect_modifiers(const pugi::xml_node& node, SpriteAnim& anim) {
    for (const pugi::xml_node child : node.children()) {
        const char* n = child.name();
        if (std::strcmp(n, "OscillationX") == 0 ||
            std::strcmp(n, "OscillationY") == 0) {
            EffectTimeline& tl =
                std::strcmp(n, "OscillationX") == 0 ? anim.osc_x : anim.osc_y;
            tl.offset = sf2::data::xml_attr_float(child, "Offset", 0.0f);
            for (const pugi::xml_node pt : child.children()) {
                if (std::strcmp(pt.name(), "Point") != 0) {
                    continue;
                }
                Sprite::TransKey k;
                k.value = sf2::data::xml_attr_float(pt, "Value", 0.0f);
                k.period = sf2::data::xml_attr_float(pt, "Period", 1.0f);
                k.ease = sf2::data::xml_attr_float(pt, "Ease", 0.0f);
                tl.keys.push_back(k);
            }
            seed_timeline(tl);
        } else if (std::strcmp(n, "ReappearX") == 0 ||
                   std::strcmp(n, "ReappearY") == 0) {
            const bool is_x = std::strcmp(n, "ReappearX") == 0;
            // JS `of(a)` (`new Ba(u.H(Min), u.H(Max))` -> .first/.second).
            const float mn = sf2::data::xml_attr_float(child, "Min", 0.0f);
            const float mx = sf2::data::xml_attr_float(child, "Max", 0.0f);
            if (is_x) {
                anim.reappear_x = true;
                anim.re_x_min = mn;
                anim.re_x_max = mx;
            } else {
                anim.reappear_y = true;
                anim.re_y_min = mn;
                anim.re_y_max = mx;
            }
        } else if (std::strcmp(n, "Speed") == 0) {
            // JS `zsb(X,Y)` L481 -> `Kta`/`Lta`, added per frame (L1139).
            anim.speed_x = sf2::data::xml_attr_float(child, "X", 0.0f);
            anim.speed_y = sf2::data::xml_attr_float(child, "Y", 0.0f);
        }
    }
}

// The Transparency timeline on a Picture SimpleEffect (JS bkb L478-481:
// `irb(Offset)` + `KWa(Period,Value,Ease)` keys; xl.ia per-frame
// `Y.wa(EO.Gb()/100)`; zh.Gb initial with ar=0 is the first Point Value).
// The rest alpha is the FIRST Point Value/100 (dojo layer_4: 45 -> 0.45).
// Each key's Value is the alpha reached `Period` seconds after the previous
// key; the list loops (LocationScene::update evaluates it). `Offset` seeds
// the clock (JS `irb` L481), and `KWa` clamps every Value to [0,100]
// (L1138). Returns the rest alpha; `sprite.trans_keys` holds the timeline.
float parse_transparency(const pugi::xml_node& node, Sprite& sprite) {
    for (const pugi::xml_node child : node.children()) {
        if (std::strcmp(child.name(), "Transparency") != 0) {
            continue;
        }
        sprite.trans_t = sf2::data::xml_attr_float(child, "Offset", 0.0f);
        float rest = 1.0f;
        bool first = true;
        for (const pugi::xml_node pt : child.children()) {
            if (std::strcmp(pt.name(), "Point") != 0) {
                continue;
            }
            Sprite::TransKey k;
            const float v = sf2::data::xml_attr_float(pt, "Value", 100.0f);
            k.value = std::max(0.0f, std::min(100.0f, v));
            k.period = sf2::data::xml_attr_float(pt, "Period", 1.0f);
            k.ease = sf2::data::xml_attr_float(pt, "Ease", 0.0f);
            sprite.trans_keys.push_back(k);
            if (first) {
                rest = k.value / 100.0f;  // already clamped to [0,100]
                first = false;
            }
        }
        return rest;
    }
    return 1.0f;
}

// The game's `ujb` (L477): "pixel_1" -> solid fill, else atlas sprite.
// Returns nullptr for Image elements that are not sprite draws.
std::shared_ptr<Sprite> make_image(const pugi::xml_node& node,
                                   const std::unordered_map<std::string, FrameRef>& frames) {
    auto sprite = std::make_shared<Sprite>();
    const char* cls = node.attribute("ClassName").value();
    sprite->texture_name = cls != nullptr ? cls : "";

    const float x = sf2::data::xml_attr_float(node, "X");
    const float y = sf2::data::xml_attr_float(node, "Y");
    sprite->transform.set_pos(x, y);
    // JS `R3a` L486-487: `s.Wg(rot)` with `rot = Rotation` attr (D9). The
    // renderer rotates the quad about its center anchor. `xml_attr_float`
    // reads the leading number, matching the JS numeric reader `u.H`.
    sprite->transform.rotation = sf2::data::xml_attr_float(node, "Rotation", 0.0f);

    const float w = sf2::data::xml_attr_float(node, "Width");
    const float h = sf2::data::xml_attr_float(node, "Height");

    if (sprite->texture_name == "pixel_1") {
        // Solid color fill; the game tints with the Color attr (default 0).
        sprite->solid = true;
        sprite->frame_w = w;
        sprite->frame_h = h;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (node.attribute("Color")) {
            parse_color(node.attribute("Color").value(), r, g, b);
        }
        sprite->color_r = r;
        sprite->color_g = g;
        sprite->color_b = b;
        sprite->color_a = 1.0f;
        return sprite;
    }

    const auto it = frames.find(sprite->texture_name);
    if (it == frames.end()) {
        std::fprintf(stderr, "location_scene: no atlas frame for ClassName=\"%s\"\n",
                     sprite->texture_name.c_str());
        return nullptr;
    }
    const FrameRef& ref = it->second;
    const sf2::data::atlas_frame& fr = ref.frame;
    sprite->frame_x = static_cast<float>(fr.x);
    sprite->frame_y = static_cast<float>(fr.y);
    sprite->frame_w = static_cast<float>(fr.w);
    sprite->frame_h = static_cast<float>(fr.h);
    sprite->tex_w = static_cast<float>(ref.atlas_w);
    sprite->tex_h = static_cast<float>(ref.atlas_h);

    // The game scales the sprite to the XML Width/Height (Rh/mj, L486-487:
    // `b.Rh(f/g); b.mj(a/h)` where g/h = fa.x/fa.y = source size, JS L486-487).
    // For trimmed sprites the XML dimensions refer to the original source art
    // (JS pi.VJa L1703: Iq(filename, frame Ec, spriteSourceSize Ec, sourceSize
    // fc, trimmed) + Vs.Qq L1705: Pj(id, name, fa=sourceSize, frame, yx,
    // qj=wNa offset) — fa carries sourceSize, qj the trim offset), not the
    // packed frame — use source_w/h so the scale maps correctly.
    // (Using the packed frame size inflates the scale by source/frame ratio,
    // rendering the sprite at the wrong physical size. Verified vs
    // fx.925b16c7.json "block/block_1": source 1024x1024, frame 46x82 at
    // offset (454,475) — scale must map XML w/h over 1024, not 46/82.)
    {
        const float scale_w = (fr.trimmed && fr.source_w > 0)
                                  ? static_cast<float>(fr.source_w)
                                  : static_cast<float>(fr.w);
        const float scale_h = (fr.trimmed && fr.source_h > 0)
                                  ? static_cast<float>(fr.source_h)
                                  : static_cast<float>(fr.h);
        if (w > 0.0f && h > 0.0f && scale_w > 0.0f && scale_h > 0.0f) {
            sprite->transform.set_scale(w / scale_w, h / scale_h);
        }
    }
    // Store trim fields (JS Vs.Qq L1705 qj=wNa offset + fa=sourceSize) so the
    // renderer can apply the sub-pixel position compensation that aligns the
    // packed content to the source-frame center. Rotated packing flag
    // (JS Iq.dL L1703 -> Pj.dL L1705 -> le.frame.dL, bk L1765 / Cq L1561).
    sprite->rotated = fr.rotated;
    if (fr.trimmed) {
        sprite->trim_x   = static_cast<float>(fr.offset_x);
        sprite->trim_y   = static_cast<float>(fr.offset_y);
        sprite->source_w = static_cast<float>(fr.source_w);
        sprite->source_h = static_cast<float>(fr.source_h);
    }

    if (node.attribute("Color")) {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        parse_color(node.attribute("Color").value(), r, g, b);
        sprite->color_r = r;
        sprite->color_g = g;
        sprite->color_b = b;
    }
    if (sf2::data::xml_attr_bool(node, "Flip", false)) {
        sprite->transform.scale_x = -sprite->transform.scale_x;
    }
    return sprite;
}

} // namespace

void LocationScene::load(const std::string& params_xml, const std::string& atlas_json,
                         const std::string& atlas_tex_path, const std::string& res_root) {
    (void)atlas_tex_path;  // the atlas texture is uploaded by the caller probe
    std::vector<std::string> jsons = {atlas_json};
    load(params_xml, jsons, res_root);
}

void LocationScene::load(const std::string& params_xml, const std::vector<std::string>& atlas_jsons,
                         const std::string& res_root) {
    res_root_ = res_root;

    sf2::data::xml_doc doc;
    const std::vector<std::uint8_t> params_bytes = read_file_bytes(params_xml);
    doc.parse(params_bytes.data(), params_bytes.size());
    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::strcmp(root.name(), "Root") != 0) {
        throw std::runtime_error("location_scene: params root element missing");
    }
    arena_w_ = sf2::data::xml_attr_float(root, "Width", 0.0f);
    arena_h_ = sf2::data::xml_attr_float(root, "Height", 0.0f);
    arena_floor_ = sf2::data::xml_attr_float(root, "Floor", 0.0f);
    // The Root Color (the fighters' silhouette fill, JS `Na.cd`). Default
    // black when the attr is absent.
    if (root.attribute("Color")) {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        parse_color(root.attribute("Color").value(), r, g, b);
        root_color_ = (static_cast<std::uint32_t>(r * 255.0f) << 16) |
                      (static_cast<std::uint32_t>(g * 255.0f) << 8) |
                      static_cast<std::uint32_t>(b * 255.0f);
    }

    // Parse all atlases into one ClassName -> frame map (later packs win on
    // collision; each frame remembers its owning atlas pixel size).
    std::unordered_map<std::string, FrameRef> frames;
    atlas_names_.clear();
    for (const std::string& json_path : atlas_jsons) {
        std::vector<std::uint8_t> json_bytes = read_file_bytes(json_path);
        const sf2::data::atlas a = sf2::data::atlas_parse(json_bytes.data(), json_bytes.size());
        atlas_names_.push_back(json_path);
        for (const auto& f : a.frames) {
            FrameRef ref;
            ref.frame = f;
            ref.atlas_w = a.w;
            ref.atlas_h = a.h;
            frames[f.name] = ref;
        }
    }

    layers_.clear();
    anims_.clear();
    fighter_layer_ = npos;
    int layer_index = 0;
    std::uint32_t emitter_ordinal = 0;  // unique LCG seed per emitter
    for (const pugi::xml_node layer_node : root.children()) {
        if (std::strcmp(layer_node.name(), "Layer") != 0) {
            continue;
        }
        auto layer = std::make_shared<Layer>();
        layer->name = "layer_" + std::to_string(layer_index);
        layer->factor = sf2::data::xml_attr_float(layer_node, "Factor", 1.0f);
        layer->type = sf2::data::xml_attr_int(layer_node, "Type", 1);
        // The `Scaling` attr -> `ij` (JS zjb L475-476: `b.ij=c>0`). Dojo:
        // every visual layer carries Scaling="1"; Type=2 has none but takes
        // the setScale branch via lEa() (JS L488 branch).
        layer->scaling = sf2::data::xml_attr_int(layer_node, "Scaling", 0) > 0;
        // JS `Bf.init` L475: `c = 0; ... c += -3` per layer -> z = -3*i.
        layer->z = -3.0f * static_cast<float>(layer_index);
        // The ModelsViewer (Type=2) layer is where the ORIGINAL game draws
        // the fighters (JS_RENDER §2.5 / §7). Its index splits the draw
        // order: layers before it are the background, layers after it
        // (floor / dust / glow / pixel_1 vignette) draw ON TOP of the
        // fighters. Its `<ModelsViewer>` child carries the raw container
        // spawns (JS `Yia`/`B_`, Bf.zjb L476) — parsed here (D8).
        if (layer->type == 2 && fighter_layer_ == npos) {
            fighter_layer_ = static_cast<std::size_t>(layer_index);
        }
        {
            const pugi::xml_node mv = layer_node.child("ModelsViewer");
            if (mv != nullptr) {
                player_spawn_x_ = sf2::data::xml_attr_float(mv, "PlayerPositionX", 0.0f);
                player_spawn_y_ = sf2::data::xml_attr_float(mv, "PlayerPositionY", 0.0f);
                enemy_spawn_x_ = sf2::data::xml_attr_float(mv, "EnemyPositionX", 0.0f);
                enemy_spawn_y_ = sf2::data::xml_attr_float(mv, "EnemyPositionY", 0.0f);
                has_spawns_ = true;
            }
        }
        ++layer_index;

        int sprite_index = 0;  // JS `Qi.QH` starts at 0 per layer (Dla L1599)
        for (const pugi::xml_node child : layer_node.children()) {
            std::shared_ptr<Sprite> sprite;
            if (std::strcmp(child.name(), "Image") == 0) {
                // Raw R3a placement (JS L486-487): X/Y straight through.
                sprite = make_image(child, frames);
            } else if (std::strcmp(child.name(), "SimpleEffect") == 0) {
                // Picture SimpleEffects draw a static frame at X/Y — the
                // game's xl Picture path (L478). The Transparency `KWa` loop
                // is stored and evaluated by update() (rest = first key).
                sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    sprite->color_a = parse_transparency(child, *sprite);
                    // Oscillation / Reappear / Speed (`bkb` L479-481): build
                    // the per-sprite modifier state before the sprite moves
                    // into the layer (the Sprite address stays stable).
                    SpriteAnim anim;
                    anim.sprite = sprite.get();
                    anim.base_x = sprite->transform.x;
                    anim.base_y = sprite->transform.y;
                    parse_simple_effect_modifiers(child, anim);
                    if (anim.animated()) {
                        anims_.push_back(std::move(anim));
                    }
                }
            } else if (std::strcmp(child.name(), "ParticleEffect") == 0 ||
                       std::strcmp(child.name(), "NewParticleEffect") == 0) {
                // JS `Bf.zjb` L476-477: both tags route to `QIa` ->
                // `fXa(new jh)`. Parse the emitter, run the ctor `Prewarm`
                // loop, attach it, then the `Qi.fXa` warm-up; `update` runs
                // the `jh` sim and `particle_draws()` exposes the render data
                // (the effects-atlas draw pass itself is OPEN, D7). No sprite
                // is emitted, so `sprite_index` (z) is unaffected.
                ParticleLayer emitter = parse_particle(child, emitter_ordinal++);
                // JS `jh` ctor L1149: `Prewarm=="1"` runs `update(1/60)`
                // until the accumulated time reaches `Life` max.
                if (emitter.prewarm) {
                    for (float t = 0.0f; t < emitter.life_max; t += kParticleFixedStep) {
                        step_particle_(emitter, kParticleFixedStep);
                    }
                }
                layer->particles.push_back(std::move(emitter));
                // JS `Qi.fXa` (L481): on attach the layer runs 150 warm-up
                // `update(1/60)` ticks, so every emitter is already mid-stream
                // on the first rendered frame (independent of `Prewarm`).
                for (int i = 0; i < 150; ++i) {
                    step_particle_(layer->particles.back(), kParticleFixedStep);
                }
                // JS `zjb` L477: `QIa` appends the emitter node to the layer
                // display list in document order; record it for interleaving.
                {
                    Layer::DrawItem item;
                    item.is_particle = true;
                    item.particle_index = layer->particles.size() - 1;
                    layer->draw_order.push_back(item);
                }
            }
            // ModelsViewer children are skipped (fighters are drawn by the
            // fight screen); ParticleEffect/NewParticleEffect are parsed above.
            if (sprite != nullptr) {
                // JS `Qi.NWa`/`Dla` L487/L1599: z = -0.01*spriteIndex.
                sprite->z = -0.01f * static_cast<float>(sprite_index);
                // JS `zjb` L477: `ujb`/`UIa` append the sprite in document
                // order. Weak ref so a later erase cannot dangle; index is
                // not used (sprites may be erased post-load).
                {
                    Layer::DrawItem item;
                    item.is_particle = false;
                    item.sprite = sprite;
                    layer->draw_order.push_back(item);
                }
                layer->sprites.push_back(std::move(sprite));
                ++sprite_index;
            }
        }
        // Only emitter-carrying layers need the ordered list; keep the common
        // (sprite-only) layer lean.
        if (layer->particles.empty()) {
            layer->draw_order.clear();
        }
        layers_.push_back(std::move(layer));
    }
}

void LocationScene::default_camera(sf2::render::Camera& camera, float view_w,
                                   float view_h, float focus_x, float fighter_span) const {
    camera.view_w = view_w;
    camera.view_h = view_h;
    camera.arena_h = arena_h_;
    camera.arena_floor = arena_floor_;
    // Sya hub framing (ma.Sya, JS L1833). The hub renders through the global
    // camera whose horizontal is 0 (Sya `b.C(0)` + `tMa(f)`), but the hub runs
    // the LIVE fight (`Tf` `YL` L1971-1972): `Ut.Al` receives the live `Go.ma`
    // focus, so `Io = Lb.width/2 - focus` is per-frame (D3/W1). Callers that
    // do not pass a focus get the spawn midpoint (frame 0): dojo
    // (690+973)/2 = 831.5 -> Io = 980 - 831.5 = +148.5.
    const float focus =
        focus_x >= 0.0f
            ? focus_x
            : (has_spawns_ ? (player_spawn_x_ + enemy_spawn_x_) * 0.5f : arena_w_ * 0.5f);
    // Renderer Io = arena_center_x - center_x with center_x = 0 (JS camera
    // position 0) -> arena_center_x carries Io directly.
    camera.arena_center_x = arena_w_ * 0.5f - focus;
    camera.center_x = 0.0f;
    // The Sya zoom (JS L1833): f = viewH/(arenaH*Bj), aspect clamp 0.45..1, the
    // narrow-screen clamp, the width fit min(viewW/(span*f+100),1) with the
    // spawn fighter span |enemyX - playerX| (qh.ECa, JS L845-846), and the
    // min-zoom 0.6..1.3 (-> 1.3 at 16:9).
    const float aspect = view_h > 0.0f ? view_w / view_h : 16.0f / 9.0f;
    const float e = arena_h_ > 0.0f ? arena_h_ : 560.0f;
    const float span = fighter_span >= 0.0f
                           ? fighter_span
                           : (has_spawns_ ? std::fabs(enemy_spawn_x_ - player_spawn_x_) : 283.0f);
    // The hub-statics layer zoom Bj (JS `Ut.xCa` L831): min(nC/(span+300),1),
    // then max(Bj, NW) with NW = nC/width. nC = viewW/(viewH/arenaH).
    float layer_zoom = 1.0f;
    {
        const float ira = e > 0.0f ? view_h / e : 1.0f;
        const float n_c = ira > 0.0f ? view_w / ira : view_w;
        const float xca = n_c / (span + 300.0f) < 1.0f ? n_c / (span + 300.0f) : 1.0f;
        const float nw = arena_w_ > 0.0f ? n_c / arena_w_ : 1.0f;
        layer_zoom = xca > nw ? xca : nw;
        if (layer_zoom < 1.0f) {
            layer_zoom = 1.0f;
        }
    }
    camera.layer_zoom = layer_zoom;
    // JS ma.Sya L1833: `e = m$a() = Lb.height * Bj` is the zoom denominator
    // (W5/D11), not the raw arena height.
    float f = e > 0.0f ? view_h / (e * layer_zoom) : 1.0f;
    f *= (aspect < 0.45f ? 0.45f : aspect > 1.0f ? 1.0f : aspect);
    if (aspect < 0.8f) {
        f *= 0.8f + ((std::max(0.5f, std::min(0.8f, aspect)) - 0.5f) / 0.3f) * 0.2f;
    }
    f *= std::min(1.0f, view_w / (span * f + 100.0f));
    const float dmin =
        0.6f + ((std::max(0.5f, std::min(1.0f, aspect)) - 0.5f) / 0.5f) * 0.7f;
    if (f < dmin) {
        f = dmin;
    }
    camera.zoom = f;
    // JS ma.Sya L1833 leaves the camera y at 0 (only the aspect<1 portrait
    // `b.D(...)` moves it); the per-layer `Xrb(F9*(1-Bj))` translate is folded
    // in by render_layer, NOT the projection (D1/W1).
    camera.center_y = 0.0f;
    if (aspect < 1.0f) {
        camera.center_y += std::round((view_h - e * camera.zoom) / 2.0f) / camera.zoom * 0.5f;
    }
}

void LocationScene::update(float dt) {
    // SimpleEffect Transparency loop (JS `bkb` L478-481 + `xl.ia` L1139 +
    // `zh` L1144-1146): each key's Value (percent) is reached `Period`
    // seconds after the previous key; the list loops. Segment i runs
    // value[i] -> value[(i+1) % n] over period[i] using the `Ease` curve
    // (`zh_value`: line for Ease 0, parabola otherwise; dojo layer_4 has
    // Ease +/-1), so the rest value (t=0) is value[0] = the parser's
    // initial color_a.
    for (const auto& layer : layers_) {
        for (const auto& sprite : layer->sprites) {
            const std::size_t nk = sprite->trans_keys.size();
            if (nk == 0) {
                continue;
            }
            float total = 0.0f;
            for (const auto& k : sprite->trans_keys) {
                total += std::max(0.0f, k.period);
            }
            if (total <= 0.0f) {
                continue;
            }
            sprite->trans_t += dt;
            while (sprite->trans_t >= total) {
                sprite->trans_t -= total;
            }
            std::size_t seg = 0;
            float acc = 0.0f;
            while (seg < nk) {
                const float per = std::max(0.0f, sprite->trans_keys[seg].period);
                if (per <= 0.0f || sprite->trans_t < acc + per) {
                    break;
                }
                acc += per;
                ++seg;
            }
            if (seg >= nk) {
                seg = nk - 1;
            }
            const float t = sprite->trans_t - acc;
            const float v = zh_value(sprite->trans_keys, nk, seg, t);
            sprite->color_a = std::max(0.0f, std::min(1.0f, v / 100.0f));
        }
    }

    // SimpleEffect Oscillation / Reappear / Speed (JS `xl.ia` L1139): per
    // frame `JM += Kta` (Speed X), `x = JM + RW.Gb()` (OscillationX) and
    // likewise for Y; a `Reappear` wraps `JM` into [min,max] once the
    // RENDERED coord leaves it (JS `Zo.Zwa` L1140). `P7` sprites are all
    // evaluated when the effect is active (`ia` L1139).
    const float frames = dt * 60.0f;  // `xl.ia` runs once per fixed 60 Hz tick
    for (SpriteAnim& a : anims_) {
        a.acc_x += a.speed_x * frames;
        a.acc_y += a.speed_y * frames;
        advance_timeline(a.osc_x, dt);
        advance_timeline(a.osc_y, dt);
        const float x = a.base_x + a.acc_x + timeline_value(a.osc_x);
        const float y = a.base_y + a.acc_y + timeline_value(a.osc_y);
        a.sprite->transform.x = x;
        a.sprite->transform.y = y;
        // `Zwa(a,b)` tests the rendered coord and stores the wrapped value
        // back into JM/KM for the NEXT frame (this frame's `Y.C(a)` already
        // ran, so there is no re-placement).
        if (a.reappear_x && (x > a.re_x_max || x < a.re_x_min)) {
            a.acc_x = x > a.re_x_max ? a.re_x_min - a.re_x_max + x
                                     : a.re_x_max - a.re_x_min + x;
        }
        if (a.reappear_y && (y > a.re_y_max || y < a.re_y_min)) {
            a.acc_y = y > a.re_y_max ? a.re_y_min - a.re_y_max + y
                                     : a.re_y_max - a.re_y_min + y;
        }
    }

    // Particle emitters (JS `jh.update` L1149-1151): emission, force/gravity
    // integration, life/alpha, reaping. `dt` is seconds; the gravity and alpha
    // steps are per-CALL in the oracle and stay per-call here.
    for (const auto& layer : layers_) {
        for (ParticleLayer& emitter : layer->particles) {
            step_particle_(emitter, dt);
        }
    }
}

// JS `Ie.Gb` (L1152): `min==max ? min : oa.eT(min,max)`; `oa.eT(a,b)` (L)
// is `a + Math.random()*(b-a)`. The native uses a private deterministic LCG
// so a pose/asset dump is reproducible (JS uses the wall-clock RNG).
float LocationScene::rand_range_(ParticleLayer& emitter, float lo, float hi) {
    if (lo == hi) {
        return lo;
    }
    emitter.rng = 1664525u * emitter.rng + 1013904223u;
    const float u = static_cast<float>(emitter.rng >> 8) * (1.0f / 16777216.0f);
    return lo + u * (hi - lo);
}

// JS `jh.Nvb` (L1150-1151): spawn one particle at a random emitter offset.
// The RNG draw order mirrors the oracle exactly (emitter offset x/y -> force
// x/y -> life -> start rotation -> velocity x/y -> angular velocity -> size).
void LocationScene::spawn_particle_(ParticleLayer& emitter) {
    Particle p;
    // JS update L1149: `Nvb(oa.eT(-t_.x/2,t_.x/2), oa.eT(-t_.y/2,t_.y/2))`.
    p.x = rand_range_(emitter, -emitter.emitter_x * 0.5f, emitter.emitter_x * 0.5f);
    p.y = rand_range_(emitter, -emitter.emitter_y * 0.5f, emitter.emitter_y * 0.5f);
    // JS `new H(v4a.Gb(), w4a.Gb(), 0, 1)` — the spawn-time force vector.
    p.force_x = rand_range_(emitter, emitter.force_x_min, emitter.force_x_max);
    p.force_y = rand_range_(emitter, emitter.force_y_min, emitter.force_y_max);
    // JS `new Cv(a, b, hA.Gb(), c)` — life.
    p.life = rand_range_(emitter, emitter.life_min, emitter.life_max);
    // JS `c.view.rotation = Vla.Gb()*.0174532925199432`.
    p.rotation_rad =
        rand_range_(emitter, emitter.start_rot_min, emitter.start_rot_max) * kParticleDegToRad;
    p.alpha = 0.0f;  // JS `c.view.alpha=0`
    // JS `d=velocityX.Gb(); e=-velocityY.Gb()` — VelocityY is NEGATED (L1151).
    const float vel_x = rand_range_(emitter, emitter.vel_x_min, emitter.vel_x_max);
    const float vel_y = -rand_range_(emitter, emitter.vel_y_min, emitter.vel_y_max);
    if (emitter.start_speed > 0.0f && vel_x != 0.0f && vel_y != 0.0f) {
        // JS L1151: ub = normalize((vel_x, vel_y)) * StartSpeed ...
        const float len = std::sqrt(vel_x * vel_x + vel_y * vel_y);
        p.vx = vel_x / len * emitter.start_speed;
        p.vy = vel_y / len * emitter.start_speed;
    }
    p.vx += vel_x;  // ... then `ub += (d, e)`.
    p.vy += vel_y;
    // JS `c.wY = wY.Gb()*.0174532925199432` (radians/second).
    p.ang_vel_rad =
        rand_range_(emitter, emitter.ang_vel_min, emitter.ang_vel_max) * kParticleDegToRad;
    // JS `d = vwb.Gb()/BA.qc.re.dt[cOa].fa.x` (L1151): dividing by the frame
    // `sourceSize.x` needs the effects atlas `E.get(1304)` (OPEN), so the raw
    // StartSize is carried and the renderer scales at draw time.
    p.start_size = rand_range_(emitter, emitter.start_size_min, emitter.start_size_max);
    emitter.live.push_back(p);  // JS `this.BA.pl.push(c.view)`
}

// JS `jh.update` (L1149-1151) for one emitter.
void LocationScene::step_particle_(ParticleLayer& emitter, float dt) {
    emitter.spawn_acc += dt;
    if (emitter.rate > 0.0f) {
        // JS `b=1/this.v3a` then a SINGLE `if ($P>=b)` (not a while), so a
        // large dt still emits at most one particle per update.
        const float interval = 1.0f / emitter.rate;
        if (emitter.spawn_acc >= interval) {
            emitter.spawn_acc -= interval;
            if (static_cast<int>(emitter.live.size()) < emitter.max_particles) {
                spawn_particle_(emitter);
            }
        }
    }
    for (Particle& p : emitter.live) {
        // JS: `d.hA-=a`; alpha fades IN at +.1/call and OUT as `alpha = hA`
        // once the remaining life drops below 1 (L1149).
        p.life -= dt;
        if (p.life < 1.0f) {
            p.alpha = p.life;
        } else {
            p.alpha += kParticleAlphaStep;
            if (p.alpha > 1.0f) {
                p.alpha = 1.0f;
            }
        }
        p.vx += p.force_x * dt;
        p.vy += p.force_y * dt;
        // JS quirk (L1149): gravity is added per update, NOT scaled by dt.
        p.vy += emitter.gravity * kParticleGravityScale;
        p.rotation_rad += p.ang_vel_rad * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
    }
    // JS removal loop (L1150): drop every particle whose life ran out. The
    // oracle also `BA.submit()`s the GPU batch here — that flush is the effects
    // renderer's job (OPEN, L1150); the sim only fills `live`.
    emitter.live.erase(
        std::remove_if(emitter.live.begin(), emitter.live.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        emitter.live.end());
}

std::vector<ParticleDraw> LocationScene::particle_draws() const {
    std::vector<ParticleDraw> out;
    for (const auto& layer : layers_) {
        for (const ParticleLayer& emitter : layer->particles) {
            for (const Particle& p : emitter.live) {
                out.push_back(make_particle_draw_(*layer, emitter, p));
            }
        }
    }
    return out;
}

ParticleDraw LocationScene::make_particle_draw_(const Layer& layer,
                                                const ParticleLayer& emitter,
                                                const Particle& p) const {
    ParticleDraw d;
    // JS `jh` ctor (L1148): the emitter node `Hd` carries `translate=(X,Y)`;
    // the batch owner `Xb` child carries `scale=(1,-1)` (L1149). The rendered
    // centre is therefore `(X + ca.x, Y - ca.y)` and the rotation flips sign.
    d.x = emitter.x + p.x;
    d.y = emitter.y - p.y;
    d.rotation_rad = -p.rotation_rad;
    d.alpha = p.alpha;
    d.start_size = p.start_size;
    d.factor = layer.factor;
    d.frame = emitter.frame;
    // JS `BA.rP[0] = Zib(Color)[0]` (L1151-1152); white when absent.
    d.color_r = emitter.tint_r;
    d.color_g = emitter.tint_g;
    d.color_b = emitter.tint_b;
    d.color_a = emitter.tint_a;
    return d;
}

void LocationScene::render_layers(sf2::render::Renderer& renderer,
                                  const sf2::render::Camera& camera, std::size_t begin,
                                  std::size_t end) const {
    const std::size_t stop = std::min(end, layers_.size());
    for (std::size_t i = begin; i < stop; ++i) {
        render_layer(renderer, *layers_[i], camera);
    }
}

void LocationScene::render_layer(sf2::render::Renderer& renderer, const Layer& layer,
                                 const sf2::render::Camera& camera) const {
    // The per-layer node scale (JS L488 `b.lEa()||b.ij?b.setScale(Bj)`):
    // Bj comes from the camera (computed once per frame by the caller —
    // default_camera for the hub statics, fight framing for the fight).
    // At Bj=1 both branches are the identity; the Xrb-vs-setScale split
    // only moves pixels when Bj!=1 (push-in / lens-zoom moments).
    const bool scaled = (layer.type == 2) || layer.scaling;
    const float ls = scaled ? camera.layer_zoom : 1.0f;
    // JS `Ut.Al` L826-827: `L.lEa()||L.ij ? L.setScale(Bj) : L.Xrb(F9*(1-Bj))`.
    // A scaled layer gets `setScale(Bj)` and translate_y stays 0; a non-scaled
    // layer keeps scale 1 and carries translate_y = F9*(1-Bj) (D4/W2). At
    // Bj=1 both branches are identity (only push-in/lens-zoom moves pixels).
    const float layer_y = scaled ? 0.0f : camera.f9() * (1.0f - camera.layer_zoom);

    // Emitter-free layers keep the previous sprite-only path byte-for-byte.
    if (layer.particles.empty()) {
        for (const auto& sprite : layer.sprites) {
            // The game's ujb (JS L477) draws pixel_1 masks opaque — they are
            // part of the arena frame (the side/top/bottom blackout around the
            // 1960x560 arena), so they draw like every other sprite.
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
        return;
    }

    // The location effects atlas `E.get(1304)` = `fight/particles.png`
    // (manifest L2490 token 1304; frames in token 1305). Lazy + cached.
    renderer.ensure_particle_atlas(res_root_);

    const auto draw_emitter = [&](const ParticleLayer& emitter) {
        for (const Particle& p : emitter.live) {
            renderer.draw_particle(make_particle_draw_(layer, emitter, p), camera, ls, layer_y);
        }
    };

    if (layer.draw_order.empty()) {
        // Defensive: a layer built without order info draws sprites then
        // emitters (document order was not recorded).
        for (const auto& sprite : layer.sprites) {
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
        for (const ParticleLayer& emitter : layer.particles) {
            draw_emitter(emitter);
        }
        return;
    }

    // JS `nja` L29: draw self, then each display-list child in list order.
    // `zjb` L476-477 appends sprites and emitters in document order, and the
    // WebGL context disables depth (L64), so this is the exact composition.
    for (const Layer::DrawItem& item : layer.draw_order) {
        if (item.is_particle) {
            if (item.particle_index < layer.particles.size()) {
                draw_emitter(layer.particles[item.particle_index]);
            }
        } else if (const std::shared_ptr<Sprite> sprite = item.sprite.lock()) {
            renderer.draw_sprite(*sprite, camera, layer.factor, ls, layer_y);
        }
    }
}

} // namespace sf2::scene
