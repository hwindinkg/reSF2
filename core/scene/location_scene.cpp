// Location scene loader — params XML + atlas JSON + atlas texture -> sprite
// layer nodes, in the game's draw order (back to front).

#include "scene/location_scene.hpp"

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
    if (w > 0.0f && h > 0.0f && fr.w > 0 && fr.h > 0) {
        sprite->transform.set_scale(w / static_cast<float>(fr.w),
                                    h / static_cast<float>(fr.h));
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
    int layer_index = 0;
    for (const pugi::xml_node layer_node : root.children()) {
        if (std::strcmp(layer_node.name(), "Layer") != 0) {
            continue;
        }
        auto layer = std::make_shared<Layer>();
        layer->name = "layer_" + std::to_string(layer_index);
        layer->factor = sf2::data::xml_attr_float(layer_node, "Factor", 1.0f);
        layer->type = sf2::data::xml_attr_int(layer_node, "Type", 1);
        ++layer_index;

        for (const pugi::xml_node child : layer_node.children()) {
            if (std::strcmp(child.name(), "Image") == 0) {
                std::shared_ptr<Sprite> sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    // [FIX R2 vertical] Minimal: offset by arena_h/2 (280) as per diagnosis.
                    sprite->transform.y -= arena_h_ * 0.5f;
                    layer->sprites.push_back(std::move(sprite));
                }
            } else if (std::strcmp(child.name(), "SimpleEffect") == 0) {
                // Picture SimpleEffects draw a static frame at X/Y — the
                // game's xl Picture path (L478). Static placement is enough
                // for the background probe.
                std::shared_ptr<Sprite> sprite = make_image(child, frames);
                if (sprite != nullptr) {
                    sprite->transform.y -= arena_h_ * 0.5f;
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
    camera.arena_center_x = 0.0f;
    camera.center_x = camera.arena_center_x;  // camera locked to arena center
    // Fit the arena width in the view (fight-start zoom from `ma.Sya`).
    camera.zoom = arena_w_ > 0.0f ? view_w / arena_w_ : 1.0f;
    // [FIX Phase 4b — dojo visible] The vertical center. The arena content
    // (bg sky at world y=20, floor at y=223.5) spans ~y -430..520; centering
    // on world y=0 compressed the bright sky/wall layers below the floor line
    // and left the black off-arena mattes covering the visible band — the
    // "dojo is black" symptom. Center so the FLOOR (the visible arena floor)
    // sits at ~0.61 of the view height, matching the oracle's fight view.
    const float floor_screen_y = view_h * 0.61f;
    const float vshift = ((arena_h_ / 2.0f - arena_floor_) / 2.0f) * (1.0f - camera.zoom);
    camera.center_y = arena_floor_ + vshift - (floor_screen_y - view_h / 2.0f) / camera.zoom;
}

void LocationScene::render_layer(sf2::render::Renderer& renderer, const Layer& layer,
                                 const sf2::render::Camera& camera) const {
    for (const auto& sprite : layer.sprites) {
        // [FIX Phase 4b — mattes smother the arena] The params' `pixel_1`
        // solid fills are the arena-BOUNDS blackout (the area outside the
        // arena). At the fight camera's vertical scale they cover the whole
        // visible band (world Y=320..520 maps over the fighter zone) and the
        // black fighters disappear into them — "no body". The oracle's
        // equivalent blackout is a thin border drawn by the arena system,
        // not these full-size fills; skip them in the fight view.
        if (sprite->solid) {
            continue;
        }
        renderer.draw_sprite(*sprite, camera, layer.factor);
    }
}

} // namespace sf2::scene
