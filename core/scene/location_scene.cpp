// Location scene loader — params XML + atlas JSON + atlas texture -> sprite
// layer nodes, in the game's draw order (back to front).

#include "scene/location_scene.hpp"

#include <algorithm>
#include <cstdio>
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

// The Transparency loop on a Picture SimpleEffect at rest (JS bkb L478-479:
// `irb(Offset)` + `KWa(Period,Value,Ease)` keys; xl.ia per-frame
// `Y.wa(EO.Gb()/100)`; zh.Gb initial with ar=0 is the first Point Value).
// The background probe is static, so the loop is evaluated at rest:
// Offset=0 -> first Point Value/100 (dojo layer_4: 45 -> 0.45). Effects
// without Transparency stay opaque.
float simple_effect_alpha(const pugi::xml_node& node) {
    for (const pugi::xml_node child : node.children()) {
        if (std::strcmp(child.name(), "Transparency") != 0) {
            continue;
        }
        for (const pugi::xml_node pt : child.children()) {
            if (std::strcmp(pt.name(), "Point") == 0 && pt.attribute("Value")) {
                const float v = sf2::data::xml_attr_float(pt, "Value", 100.0f);
                return std::max(0.0f, std::min(1.0f, v / 100.0f));
            }
        }
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

    // The game scales the sprite to the XML Width/Height (Rh/mj, L486).
    // For trimmed sprites the XML dimensions refer to the original source art,
    // not the packed frame — use source_w/h so the scale maps correctly.
    // (Using the packed frame size inflates the scale by source/frame ratio,
    // rendering the sprite at the wrong physical size.)
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
    // Store trim fields so the renderer can apply the sub-pixel position
    // compensation that aligns the packed content to the source-frame center.
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
    (void)res_root;

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
    fighter_layer_ = npos;
    int layer_index = 0;
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
        // The ModelsViewer (Type=2) layer is where the ORIGINAL game draws
        // the fighters (JS_RENDER §2.5 / §7). Its index splits the draw
        // order: layers before it are the background, layers after it
        // (floor / dust / glow / pixel_1 vignette) draw ON TOP of the
        // fighters.
        if (layer->type == 2 && fighter_layer_ == npos) {
            fighter_layer_ = static_cast<std::size_t>(layer_index);
        }
        ++layer_index;

        for (const pugi::xml_node child : layer_node.children()) {
            if (std::strcmp(child.name(), "Image") == 0) {
                std::shared_ptr<Sprite> sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    // Raw R3a placement (JS L486-487): X/Y straight through.
                    layer->sprites.push_back(std::move(sprite));
                }
            } else if (std::strcmp(child.name(), "SimpleEffect") == 0) {
                // Picture SimpleEffects draw a static frame at X/Y — the
                // game's xl Picture path (L478). Static placement is enough
                // for the background probe. Transparency loop at rest: the
                // first Point Value/100 (xl.ia `Y.wa(EO.Gb()/100)` with
                // zh ar=0 -> first key; dojo layer_4 -> 0.45 dim).
                std::shared_ptr<Sprite> sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    sprite->color_a = simple_effect_alpha(child);
                    layer->sprites.push_back(std::move(sprite));
                }
            }
            // ModelsViewer / ParticleEffect layers are skipped (fighters,
            // effects) — background-only milestone.
        }
        layers_.push_back(std::move(layer));
    }
}

void LocationScene::default_camera(sf2::render::Camera& camera, float view_w,
                                   float view_h) const {
    camera.view_w = view_w;
    camera.view_h = view_h;
    camera.arena_h = arena_h_;
    camera.arena_floor = arena_floor_;
    // Static Sya hub framing (ma.Sya, JS L1833): the hub renders through
    // the global camera, whose horizontal is 0 (Sya `b.C(0)` + `tMa(f)`) —
    // but the hub runs the LIVE fight (`Tf` `YL` L1971-1972), so the `Ut.Al`
    // layer shift (L826-827 `Wrb(Io*bp)`) applies. Focus = spawn midpoint
    // `z9a` L475 (690+973)/2 = 831.5; Io = width/2 - focus = 980-831.5 =
    // +148.5. Renderer Io = arena_center_x - center_x, so arena_center_x =
    // 148.5 with center_x = 0 reproduces the JS layer shift exactly.
    camera.arena_center_x = 148.5f;
    camera.center_x = 0.0f;
    // The Sya zoom (JS L1833): f = viewH/arenaH, aspect clamp 0.45..1, the
    // narrow-screen clamp, the width fit min(viewW/(span*f+100),1) with the
    // dojo fight-start span |973-690| = 283 (qh.ECa, JS L845-846), and the
    // min-zoom 0.6..1.3 (-> 1.3 at 16:9). Bj = 1 (m$a = Lb.height).
    const float aspect = view_h > 0.0f ? view_w / view_h : 16.0f / 9.0f;
    const float e = arena_h_ > 0.0f ? arena_h_ : 560.0f;
    float f = e > 0.0f ? view_h / e : 1.0f;
    f *= (aspect < 0.45f ? 0.45f : aspect > 1.0f ? 1.0f : aspect);
    if (aspect < 0.8f) {
        f *= 0.8f + ((std::max(0.5f, std::min(0.8f, aspect)) - 0.5f) / 0.3f) * 0.2f;
    }
    const float span = 283.0f;
    f *= std::min(1.0f, view_w / (span * f + 100.0f));
    const float dmin =
        0.6f + ((std::max(0.5f, std::min(1.0f, aspect)) - 0.5f) / 0.5f) * 0.7f;
    if (f < dmin) {
        f = dmin;
    }
    camera.zoom = f;
    // The hub-statics layer zoom (JS `Ut.Bj`, L826): at the spawn span
    // ECa=283 (JS L845-846) xCa=min(nC/(283+300),1); nC at 16:9 1280x720
    // =1280/(720/560)=995.6 -> 995.6/583>1 -> 1; then `Bj=1` reset +
    // max(Bj,NW) with NW=nC/width=0.508 -> Bj stays 1. Stored on the camera
    // so render_layer can apply the L488 setScale branch JS-exactly.
    {
        const float ira = e > 0.0f ? view_h / e : 1.0f;
        const float n_c = ira > 0.0f ? view_w / ira : view_w;
        const float xca = n_c / (span + 300.0f) < 1.0f ? n_c / (span + 300.0f) : 1.0f;
        const float nw = arena_w_ > 0.0f ? n_c / arena_w_ : 1.0f;
        camera.layer_zoom = xca > nw ? xca : nw;
        if (camera.layer_zoom < 1.0f) {
            camera.layer_zoom = 1.0f;
        }
    }
    // The vertical target: the hub renders through the GLOBAL camera, which
    // Sya leaves at y=0 (L1833 sets only `tMa(f)` + portrait `b.D`; no `Al`
    // call, so layers sit at identity and screen = world*f + view/2). The
    // renderer's projection adds layer_vshift, so center_y = vshift
    // reproduces the identity mapping exactly (F9 = (Lb.height/2-ct)/2,
    // JS Ut.init L843; vshift = -30 at zoom 1.3).
    camera.center_y = ((arena_h_ / 2.0f - arena_floor_) / 2.0f) * (1.0f - camera.zoom);
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
    for (const auto& sprite : layer.sprites) {
        // The game's ujb (JS L477) draws pixel_1 masks opaque — they are
        // part of the arena frame (the side/top/bottom blackout around the
        // 1960x560 arena), so they draw like every other sprite.
        renderer.draw_sprite(*sprite, camera, layer.factor, ls);
    }
}

} // namespace sf2::scene
