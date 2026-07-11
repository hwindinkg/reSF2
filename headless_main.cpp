// headless_main.cpp
//
// Headless test driver for the reSF2 engine. Uses the software renderer to
// produce PNG screenshots at each stage of the boot sequence:
//
//   1. Loading screen (startLoading.xml assets loaded)
//   2. Dojo location (params.xml + atlas + parallax layers)
//   3. Dojo with player character (skeletal stick figure from skeleton.xml)
//   4. Dojo with player + punching bag (using btn_punching_bag icon)
//   5. Dojo with HUD overlay (real Top_Panel + gold + energy + level textures)
//   6. Dojo with menu button (left-side, expandable list with real menu icons)
//   7. Dojo with player at multiple positions (left/right movement demo)
//   8. Dojo with story dialog overlay
//
// COORDINATE SYSTEM (matches original game):
//   - World coordinates: Y-DOWN (positive Y = down toward floor).
//   - Origin (0,0) is at the vertical centre of the visible area.
//   - Floor in Dojo is at world Y ≈ +225 (bottom of visible area).
//   - PlayerPositionY in params.xml = world Y of model's NPivot (pelvis).
//   - Skeleton local coords: Y-UP (0 = feet, positive = up).
//     When placing in the world, we INVERT Y so head is up on screen.
//
// This binary does NOT require a GPU, GL context, or window system.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/renderer/software_renderer.hpp"
#include "engine/reverse/plist_atlas.hpp"
#include "engine/reverse/bitmap_font.hpp"
#include "engine/renderer/stb_image.h"

namespace plist = resf2::reverse::plist;
namespace font  = resf2::reverse::font;
namespace soft  = resf2::soft;

// ---------- Small helpers ----------

static std::vector<std::byte> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = (size_t)f.tellg(); if (!sz) return {};
    f.seekg(0); std::vector<std::byte> d(sz);
    f.read((char*)d.data(), (std::streamsize)sz); return d;
}

static std::string read_text(const std::string& path) {
    auto d = read_file(path); return std::string((const char*)d.data(), d.size());
}

static std::string xml_attr(const std::string& tag, const std::string& attr) {
    auto pos = tag.find(attr + "=\""); if (pos == std::string::npos) return "";
    pos += attr.size() + 2; auto end = tag.find('"', pos);
    return tag.substr(pos, end - pos);
}

static float tof(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    try { return std::stof(s); } catch (...) { return def; }
}

static int toi(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

// ---------- Asset types ----------

struct AtlasRef {
    std::shared_ptr<soft::Texture> texture;
    std::shared_ptr<plist::ParsedAtlas> atlas;
};

struct LayerImage {
    std::string atlas_name, class_name;
    float x = 0, y = 0, w = 0, h = 0;
    std::string color;
};

struct LocationLayer {
    int type = 0;
    float factor = 1.0f;
    std::string atlas_name;
    std::vector<LayerImage> images;
};

struct GameLocation {
    std::string color;
    float width = 0, height = 0;
    float floor_y = 0;
    float player_x = 0, player_y = 0;
    float enemy_x = 0, enemy_y = 0;
    std::vector<LocationLayer> layers;
};

struct SkelNode {
    std::string name;
    float x = 0, y = 0, z = 0;
};

struct SkelEdge {
    std::string name;
    std::string end1, end2;
    float radius = 0;
};

struct LoadingImg {
    std::shared_ptr<soft::Texture> texture;
    float x = 0, y = 0;
};

// ---------- Body model (body.xml) ----------
// The body.xml defines the visual + collision geometry of the fighter's
// body (skin/cloth). It contains:
//   - Nodes: BODY-NodeN (cloth simulation points, Y-up local coords)
//   - MacroNodes: composite nodes referencing skeleton joints
//   - Edges: segments between two nodes (BODY-EdgeN or named like EArm_1)
//   - Capsules: collision volumes (edge + radius1/radius2)
//   - Triangles: visual mesh faces (3 node refs)
// We render capsules as thick lines and triangles as filled polygons.

struct BodyNode {
    std::string name;
    float x = 0, y = 0, z = 0;
};

struct BodyMacroNode {
    std::string name;
    std::string children[4];
    float lcc[4] = {};  // linear combination coefficients
};

struct BodyEdge {
    std::string name;
    std::string end1, end2;
    float length = 0;
};

struct BodyCapsule {
    std::string edge_name;
    float radius1 = 0, radius2 = 0;
};

struct BodyTriangle {
    std::string n1, n2, n3;
};

struct BodyModel {
    std::unordered_map<std::string, BodyNode> nodes;
    std::unordered_map<std::string, BodyMacroNode> macro_nodes;
    std::vector<BodyEdge> edges;
    std::vector<BodyCapsule> capsules;
    std::vector<BodyTriangle> triangles;
};

// ---------- Game states ----------

enum class Overlay { None, Menu, Dialog };

// ---------- The Game ----------

class Game {
public:
    Game(std::string asset_root, std::string out_dir, int width, int height)
        : asset_root_(std::move(asset_root))
        , out_dir_(std::move(out_dir))
        , width_(width), height_(height)
    {
        std::filesystem::create_directories(out_dir_);
    }

    int run() {
        if (!renderer_.init(width_, height_)) {
            std::fprintf(stderr, "Renderer init failed\n"); return 1;
        }
        std::printf("reSF2 headless: %dx%d, assets=%s\n",
                    width_, height_, asset_root_.c_str());

        // === Stage 1: Loading screen ===
        load_loading_screen();
        renderer_.set_clear_color(0, 0, 0, 1);
        render_loading_screen();
        renderer_.save_png(out_dir_ + "/01_loading.png");
        std::printf("  -> 01_loading.png\n");

        // === Stage 2: Dojo (background only) ===
        load_location("dojo");
        if (!location_) { std::fprintf(stderr, "dojo not found\n"); return 1; }
        auto c = std::stoul(location_->color, nullptr, 16);
        renderer_.set_clear_color(((c>>16)&0xFF)/255.0f,
                                  ((c>>8)&0xFF)/255.0f,
                                  (c&0xFF)/255.0f, 1.0f);

        camera_setup(0, 0);

        render_location();
        renderer_.save_png(out_dir_ + "/02_dojo_background.png");
        std::printf("  -> 02_dojo_background.png\n");

        // === Stage 3: Dojo + player character ===
        load_skeleton();
        load_body_model();
        load_punching_bag_model();
        load_hud_textures();
        load_menu_textures();
        load_hud_font();
        player_pos_x_ = location_->player_x;
        // Use the original PlayerPositionY from params.xml as the NPivot
        // world Y. The original engine places the model at this position
        // and the background is designed to align with it.
        player_pos_y_ = location_->player_y;
        camera_setup(player_pos_x_, 0);

        render_location();
        render_character();
        renderer_.save_png(out_dir_ + "/03_dojo_player.png");
        std::printf("  -> 03_dojo_player.png\n");

        // === Stage 4: Dojo + player + punching bag ===
        render_location();
        render_punching_bag();
        render_character();
        renderer_.save_png(out_dir_ + "/04_dojo_punching_bag.png");
        std::printf("  -> 04_dojo_punching_bag.png\n");

        // === Stage 5: Dojo + player + punching bag + HUD ===
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        renderer_.save_png(out_dir_ + "/05_dojo_hud.png");
        std::printf("  -> 05_dojo_hud.png\n");

        // === Stage 6: Dojo + menu button (collapsed) ===
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        render_menu_button(false);
        renderer_.save_png(out_dir_ + "/06_dojo_menu_button.png");
        std::printf("  -> 06_dojo_menu_button.png\n");

        // === Stage 7: Dojo + menu expanded (list of menu items) ===
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        render_menu_button(true);
        renderer_.save_png(out_dir_ + "/07_dojo_menu_expanded.png");
        std::printf("  -> 07_dojo_menu_expanded.png\n");

        // === Stage 8: Movement demo (player shifted left) ===
        player_pos_x_ -= 150.0f;
        facing_right_ = true;
        camera_setup(player_pos_x_, 0);
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        render_menu_button(false);
        renderer_.save_png(out_dir_ + "/08_dojo_player_left.png");
        std::printf("  -> 08_dojo_player_left.png\n");

        // === Stage 9: Movement demo (player shifted right) ===
        player_pos_x_ = location_->player_x + 200.0f;
        camera_setup(player_pos_x_, 0);
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        render_menu_button(false);
        renderer_.save_png(out_dir_ + "/09_dojo_player_right.png");
        std::printf("  -> 09_dojo_player_right.png\n");

        // === Stage 10: Story dialog overlay ===
        player_pos_x_ = location_->player_x;
        camera_setup(player_pos_x_, 0);
        render_location();
        render_punching_bag();
        render_character();
        render_hud();
        render_menu_button(false);
        render_dialog_overlay();
        renderer_.save_png(out_dir_ + "/10_dojo_dialog.png");
        std::printf("  -> 10_dojo_dialog.png\n");

        std::printf("\nAll screenshots written to: %s\n", out_dir_.c_str());
        return 0;
    }

private:
    void camera_setup(float cx, float cy) {
        renderer_.camera().set_target(cx, cy);
        renderer_.camera().set_zoom((float)width_ / 1960.0f * 1.3f);
        renderer_.camera().snap();
    }

    // ---------- Loading screen ----------
    void load_loading_screen() {
        auto root = std::filesystem::path(asset_root_);
        std::string xml_path;
        for (const auto& dir : {root/"1536"/"textures"/"fullscreen",
                                 root/"1536"/"fullscreen"}) {
            auto p = dir/"startLoading.xml";
            if (std::filesystem::exists(p)) { xml_path = p.string(); break; }
        }
        if (xml_path.empty()) return;
        auto xml = read_text(xml_path);
        size_t pos = 0;
        while ((pos = xml.find("<Image", pos)) != std::string::npos) {
            auto end = xml.find("/>", pos);
            auto tag = xml.substr(pos, end - pos);
            auto file = xml_attr(tag, "File");
            auto x = tof(xml_attr(tag, "X"));
            auto y = tof(xml_attr(tag, "Y"));
            auto img_path = root/"1536"/file;
            if (!std::filesystem::exists(img_path)) img_path = root/file;
            if (std::filesystem::exists(img_path)) {
                auto data = read_file(img_path.string());
                auto tex = std::make_shared<soft::Texture>();
                if (tex->init_from_png((const std::uint8_t*)data.data(), data.size())) {
                    loading_images_.push_back({tex, x, y});
                }
            }
            pos = end + 2;
        }
        load_scale_ = std::min((float)width_ / 1820.0f, (float)height_ / 1024.0f);
    }

    void render_loading_screen() {
        float tw = 1820.0f * load_scale_, th = 1024.0f * load_scale_;
        float ox = (width_ - tw) / 2.0f, oy = (height_ - th) / 2.0f;
        for (auto& img : loading_images_) {
            float w = img.texture->width * load_scale_;
            float h = img.texture->height * load_scale_;
            float x = ox + (img.x + 910.0f) * load_scale_;
            float y = oy + (img.y + 512.0f) * load_scale_;
            renderer_.draw_textured_quad_screen(*img.texture, x, y, w, h);
        }
    }

    // ---------- Location ----------
    void load_location(const std::string& name) {
        auto root = std::filesystem::path(asset_root_);
        std::string params_path;
        for (const auto& dir : {root/"locations"/name, root/"1536"/"locations"/name}) {
            auto p = dir/"params.xml";
            if (std::filesystem::exists(p)) { params_path = p.string(); break; }
        }
        if (params_path.empty()) return;
        auto xml = read_text(params_path);
        location_ = std::make_unique<GameLocation>(parse_location(xml));
        for (auto& layer : location_->layers) {
            if (layer.atlas_name.empty()) continue;
            if (atlases_.count(layer.atlas_name)) continue;
            load_atlas(layer.atlas_name, name);
        }
    }

    GameLocation parse_location(const std::string& xml) {
        GameLocation loc;
        auto rp = xml.find("<Root");
        if (rp != std::string::npos) {
            auto end = xml.find('>', rp);
            auto tag = xml.substr(rp, end - rp);
            loc.color = xml_attr(tag, "Color");
            loc.width = tof(xml_attr(tag, "Width"));
            loc.height = tof(xml_attr(tag, "Height"));
            loc.floor_y = tof(xml_attr(tag, "Floor"));
        }
        size_t pos = 0;
        while ((pos = xml.find("<Layer", pos)) != std::string::npos) {
            auto end = xml.find('>', pos);
            auto tag = xml.substr(pos, end - pos);
            LocationLayer layer;
            layer.type = toi(xml_attr(tag, "Type"));
            layer.factor = tof(xml_attr(tag, "Factor"), 1.0f);
            layer.atlas_name = xml_attr(tag, "Atlas");
            auto le = xml.find("</Layer>", pos);
            if (le == std::string::npos) le = xml.size();
            size_t ip = pos;
            while ((ip = xml.find("<Image", ip)) != std::string::npos && ip < le) {
                auto ie = xml.find("/>", ip);
                if (ie == std::string::npos) break;
                auto itag = xml.substr(ip, ie - ip);
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                img.class_name = xml_attr(itag, "ClassName");
                img.x = tof(xml_attr(itag, "X"));
                img.y = tof(xml_attr(itag, "Y"));
                img.w = tof(xml_attr(itag, "Width"));
                img.h = tof(xml_attr(itag, "Height"));
                img.color = xml_attr(itag, "Color");
                layer.images.push_back(img);
                ip = ie + 2;
            }
            ip = pos;
            while ((ip = xml.find("<SimpleEffect", ip)) != std::string::npos && ip < le) {
                auto ie = xml.find(">", ip);
                if (ie == std::string::npos) break;
                auto ee = xml.find("</SimpleEffect>", ip);
                auto itag = xml.substr(ip, ie - ip);
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                img.class_name = xml_attr(itag, "ClassName");
                img.x = tof(xml_attr(itag, "X"));
                img.y = tof(xml_attr(itag, "Y"));
                img.w = tof(xml_attr(itag, "Width"));
                img.h = tof(xml_attr(itag, "Height"));
                img.color = xml_attr(itag, "Color");
                layer.images.push_back(img);
                ip = ee != std::string::npos ? ee + 15 : ie + 1;
            }
            auto mv = xml.find("ModelsViewer", pos);
            if (mv != std::string::npos && mv < le) {
                auto me = xml.find("/>", mv);
                auto mtag = xml.substr(mv, me - mv);
                loc.player_x = tof(xml_attr(mtag, "PlayerPositionX"));
                loc.player_y = tof(xml_attr(mtag, "PlayerPositionY"));
                loc.enemy_x = tof(xml_attr(mtag, "EnemyPositionX"));
                loc.enemy_y = tof(xml_attr(mtag, "EnemyPositionY"));
            }
            loc.layers.push_back(layer);
            pos = le + 8;
        }
        return loc;
    }

    void load_atlas(const std::string& name, const std::string& loc) {
        auto root = std::filesystem::path(asset_root_);
        for (const auto& dir : {root/"1536"/"locations"/loc,
                                 root/"1536"/"textures",
                                 root/"1536",
                                 root/"locations"/loc,
                                 root/"1536"/"textures"/"buttons"/loc,
                                 root}) {
            auto pp = dir/(name+".plist"), pn = dir/(name+".png");
            if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
                auto result = plist::parse(read_text(pp.string()));
                if (!result) continue;
                auto png_data = read_file(pn.string());
                auto tex = std::make_shared<soft::Texture>();
                if (!tex->init_from_png((const std::uint8_t*)png_data.data(),
                                        png_data.size())) continue;
                AtlasRef a;
                a.texture = tex;
                a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
                std::printf("  Atlas '%s': %zu frames\n",
                            name.c_str(), a.atlas->frames.size());
                atlases_[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
    }

    // Render a location layer image. Location coords are Y-UP (cocos2d
    // convention): (x,y) is the CENTER of the image, +Y is up.
    // draw_textured_quad takes the BOTTOM-LEFT corner in Y-UP world coords.
    void render_layer_image(const LayerImage& img, float cam_factor) {
        if (img.class_name == "pixel_1" && !img.color.empty()) {
            // Solid color rectangle (floor/ceiling mask)
            unsigned long col = std::stoul(img.color, nullptr, 16);
            soft::Color4B c{
                (std::uint8_t)((col>>16)&0xFF),
                (std::uint8_t)((col>>8)&0xFF),
                (std::uint8_t)(col&0xFF), 255};
            // Apply parallax by shifting camera
            float orig_x = renderer_.camera().x;
            renderer_.camera().x = orig_x * cam_factor;
            renderer_.camera().snap();
            // World rect (Y-UP): (x,y) = centre, bottom-left = (x-w/2, y-h/2)
            float wx0 = img.x - img.w/2.0f, wy0 = img.y - img.h/2.0f;
            float sx0, sy0, sx1, sy1;
            renderer_.camera().world_to_screen(wx0, wy0, sx0, sy0);          // bottom-left
            renderer_.camera().world_to_screen(wx0 + img.w, wy0 + img.h, sx1, sy1);  // top-right
            float x = std::min(sx0, sx1), y = std::min(sy0, sy1);
            float w = std::abs(sx1 - sx0), h = std::abs(sy1 - sy0);
            renderer_.draw_filled_rect_screen(x, y, w, h, c);
            renderer_.camera().x = orig_x;
            renderer_.camera().snap();
            return;
        }
        auto it = atlases_.find(img.atlas_name);
        if (it == atlases_.end()) return;
        auto& atlas = it->second;
        if (!atlas.texture || !atlas.atlas) return;
        auto fit = atlas.atlas->name_index.find(img.class_name + ".png");
        if (fit == atlas.atlas->name_index.end()) {
            fit = atlas.atlas->name_index.find(img.class_name);
            if (fit == atlas.atlas->name_index.end()) return;
        }
        auto& frame = atlas.atlas->frames[fit->second];
        float tw = (float)atlas.atlas->metadata.texture_w;
        float th = (float)atlas.atlas->metadata.texture_h;
        float u0, v0, u1, v1;
        if (frame.rotated) {
            u0 = frame.atlas_x / tw;
            v0 = frame.atlas_y / th;
            u1 = (frame.atlas_x + frame.atlas_h) / tw;
            v1 = (frame.atlas_y + frame.atlas_w) / th;
        } else {
            u0 = frame.atlas_x / tw;
            v0 = frame.atlas_y / th;
            u1 = (frame.atlas_x + frame.atlas_w) / tw;
            v1 = (frame.atlas_y + frame.atlas_h) / th;
        }
        // Apply parallax
        float orig_x = renderer_.camera().x;
        renderer_.camera().x = orig_x * cam_factor;
        renderer_.camera().snap();
        // (x,y) is centre in world Y-UP. Bottom-left = (x - w/2, y - h/2).
        float px = img.x - img.w / 2.0f;
        float py = img.y - img.h / 2.0f;
        renderer_.draw_textured_quad(*atlas.texture, px, py, img.w, img.h,
                                     u0, v0, u1, v1);
        renderer_.camera().x = orig_x;
        renderer_.camera().snap();
    }

    void render_location() {
        if (!location_) return;
        for (auto& layer : location_->layers) {
            if (layer.type != 1) continue;
            for (auto& img : layer.images) {
                render_layer_image(img, layer.factor);
            }
        }
    }

    // ---------- Skeleton ----------
    void load_skeleton() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> candidates = {
            root/"models"/"skeleton.xml",
            root/"skeleton.xml",
            "/home/z/my-project/upload/skeleton.xml"
        };
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) return;
        auto xml = read_text(path);
        // Only parse the <Nodes> section to avoid picking up node references
        // in <GroupsOfSelection> and <Edges> sections.
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start == std::string::npos || nodes_end == std::string::npos) return;
        std::string nodes_xml = xml.substr(nodes_start, nodes_end - nodes_start);
        size_t pos = 0;
        while ((pos = nodes_xml.find("<N", pos)) != std::string::npos) {
            char nc = pos + 2 < nodes_xml.size() ? nodes_xml[pos + 2] : 0;
            if (!std::isalpha((unsigned char)nc)) { pos += 2; continue; }
            // Find end of tag (either /> or >)
            auto end_self = nodes_xml.find("/>", pos);
            auto end_open = nodes_xml.find(">", pos);
            size_t end;
            if (end_self != std::string::npos && end_open != std::string::npos) {
                end = std::min(end_self, end_open);
            } else if (end_self != std::string::npos) {
                end = end_self;
            } else if (end_open != std::string::npos) {
                end = end_open;
            } else {
                break;
            }
            // Only parse self-closing tags (/>)
            bool self_closing = (end == end_self);
            if (!self_closing) { pos = end + 1; continue; }
            auto tag = nodes_xml.substr(pos, end - pos);
            auto sp = tag.find(' ');
            if (sp == std::string::npos) { pos = end + 2; continue; }
            SkelNode n;
            n.name = tag.substr(1, sp - 1);
            n.x = tof(xml_attr(tag, "X"));
            n.y = tof(xml_attr(tag, "Y"));
            n.z = tof(xml_attr(tag, "Z"));
            skeleton_nodes_[n.name] = n;
            pos = end + 2;
        }
        std::printf("  Skeleton: %zu nodes\n", skeleton_nodes_.size());

        // Parse <Edges> section for bone segments (EFoot_1, EArm_1, etc.)
        // Also parse Type="Muscle" entries (Muscle115, etc.) which are used
        // by body.xml capsules.
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t ep = 0;
            while (true) {
                auto p1 = es.find("Type=\"Edge\"", ep);
                auto p2 = es.find("Type=\"Muscle\"", ep);
                size_t pos;
                if (p1 == std::string::npos && p2 == std::string::npos) break;
                if (p1 == std::string::npos) pos = p2;
                else if (p2 == std::string::npos) pos = p1;
                else pos = std::min(p1, p2);
                auto ts = es.rfind('<', pos);
                auto end = es.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = es.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    SkelEdge e;
                    e.name = tag.substr(1, sp - 1);
                    e.end1 = xml_attr(tag, "End1");
                    e.end2 = xml_attr(tag, "End2");
                    e.radius = tof(xml_attr(tag, "Radius"));
                    skeleton_edges_[e.name] = e;
                }
                ep = end + 2;
            }
        }
        std::printf("  Skeleton: %zu edges\n", skeleton_edges_.size());
    }

    // ---------- Body model (body.xml) ----------
    void load_body_model() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> candidates = {
            root/"models"/"body.xml",
            root/"assets"/"models"/"body.xml",
            "/home/z/my-project/upload/body.xml",
            "/home/z/my-project/work/reSF2/assets/models/body.xml",
        };
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) return;
        auto xml = read_text(path);
        body_model_ = std::make_unique<BodyModel>();

        // Parse <Nodes> section for BODY-NodeN and BODY-MacroNode entries
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start != std::string::npos && nodes_end != std::string::npos) {
            std::string ns = xml.substr(nodes_start, nodes_end - nodes_start);
            size_t pos = 0;
            while ((pos = ns.find("<BODY-", pos)) != std::string::npos) {
                auto tag_start = ns.find("Type=\"", pos);
                auto end = ns.find("/>", pos);
                if (end == std::string::npos) break;
                if (tag_start != std::string::npos && tag_start < end) {
                    auto type_end = ns.find('"', tag_start + 6);
                    std::string type = ns.substr(tag_start + 6, type_end - tag_start - 6);
                    auto tag = ns.substr(pos, end - pos);
                    auto sp = tag.find(' ');
                    std::string name = tag.substr(1, sp - 1);
                    if (type == "Node") {
                        BodyNode n;
                        n.name = name;
                        n.x = tof(xml_attr(tag, "X"));
                        n.y = tof(xml_attr(tag, "Y"));
                        n.z = tof(xml_attr(tag, "Z"));
                        body_model_->nodes[n.name] = n;
                    } else if (type == "MacroNode") {
                        BodyMacroNode mn;
                        mn.name = name;
                        mn.children[0] = xml_attr(tag, "ChildNode1");
                        mn.children[1] = xml_attr(tag, "ChildNode2");
                        mn.children[2] = xml_attr(tag, "ChildNode3");
                        mn.children[3] = xml_attr(tag, "ChildNode4");
                        mn.lcc[0] = tof(xml_attr(tag, "LCC1"));
                        mn.lcc[1] = tof(xml_attr(tag, "LCC2"));
                        mn.lcc[2] = tof(xml_attr(tag, "LCC3"));
                        mn.lcc[3] = tof(xml_attr(tag, "LCC4"));
                        body_model_->macro_nodes[mn.name] = mn;
                    }
                }
                pos = end + 2;
            }
        }

        // Parse <Edges> section
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t pos = 0;
            while ((pos = es.find("<BODY-", pos)) != std::string::npos) {
                auto tag_start = es.find("Type=\"", pos);
                auto end = es.find("/>", pos);
                if (end == std::string::npos) break;
                if (tag_start != std::string::npos && tag_start < end) {
                    auto type_end = es.find('"', tag_start + 6);
                    std::string type = es.substr(tag_start + 6, type_end - tag_start - 6);
                    if (type == "Edge") {
                        auto tag = es.substr(pos, end - pos);
                        auto sp = tag.find(' ');
                        BodyEdge e;
                        e.name = tag.substr(1, sp - 1);
                        e.end1 = xml_attr(tag, "End1");
                        e.end2 = xml_attr(tag, "End2");
                        e.length = tof(xml_attr(tag, "Length"));
                        body_model_->edges.push_back(e);
                    }
                }
                pos = end + 2;
            }
        }

        // Parse <Figures> section for Capsules and Triangles
        auto figs_start = xml.find("<Figures>");
        auto figs_end = xml.find("</Figures>");
        if (figs_start != std::string::npos && figs_end != std::string::npos) {
            std::string fs = xml.substr(figs_start, figs_end - figs_start);
            // Capsules: <Capsule_XXX Type="Capsule" Edge="EdgeName" Radius1=".." Radius2=".."/>
            size_t pos = 0;
            while ((pos = fs.find("Type=\"Capsule\"", pos)) != std::string::npos) {
                // Find the tag start (search backward for '<')
                auto ts = fs.rfind('<', pos);
                auto end = fs.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = fs.substr(ts, end - ts);
                BodyCapsule c;
                c.edge_name = xml_attr(tag, "Edge");
                c.radius1 = tof(xml_attr(tag, "Radius1"));
                c.radius2 = tof(xml_attr(tag, "Radius2"));
                body_model_->capsules.push_back(c);
                pos = end + 2;
            }
            // Triangles: <XXX Type="Triangle" Node1=".." Node2=".." Node3=".."/>
            pos = 0;
            while ((pos = fs.find("Type=\"Triangle\"", pos)) != std::string::npos) {
                auto ts = fs.rfind('<', pos);
                auto end = fs.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = fs.substr(ts, end - ts);
                BodyTriangle t;
                t.n1 = xml_attr(tag, "Node1");
                t.n2 = xml_attr(tag, "Node2");
                t.n3 = xml_attr(tag, "Node3");
                body_model_->triangles.push_back(t);
                pos = end + 2;
            }
        }

        std::printf("  Body model: %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    body_model_->nodes.size(), body_model_->edges.size(),
                    body_model_->capsules.size(), body_model_->triangles.size());
    }

    // Render the body model as filled capsules (thick lines) + triangles.
    // Uses the same Y-inversion as the skeleton (local Y-up -> world Y-down).
    void render_body_model(float world_cx, float world_cy, bool face_right) {
        if (!body_model_) return;
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float pivot_local_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 170.0f;

        // Build edge name -> (end1, end2) lookup from BOTH body.xml edges
        // and skeleton.xml edges. Capsules in body.xml reference skeleton
        // edges (EFoot_1, EArm_1, etc.) which are defined in skeleton.xml.
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : body_model_->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }
        for (auto& [name, e] : skeleton_edges_) {
            edge_map[name] = {e.end1, e.end2};
        }

        // Render triangles as filled (semi-transparent dark gray)
        soft::Color4B tri_col{60, 60, 70, 200};
        for (auto& t : body_model_->triangles) {
            auto [x1, y1] = resolve_node(t.n1, world_cx, world_cy, face_right, pivot_local_y);
            auto [x2, y2] = resolve_node(t.n2, world_cx, world_cy, face_right, pivot_local_y);
            auto [x3, y3] = resolve_node(t.n3, world_cx, world_cy, face_right, pivot_local_y);
            // Convert to screen coords and draw filled triangle
            float sx1, sy1, sx2, sy2, sx3, sy3;
            renderer_.camera().world_to_screen(x1, y1, sx1, sy1);
            renderer_.camera().world_to_screen(x2, y2, sx2, sy2);
            renderer_.camera().world_to_screen(x3, y3, sx3, sy3);
            // Rasterize triangle
            float minx = std::min({sx1, sx2, sx3});
            float maxx = std::max({sx1, sx2, sx3});
            float miny = std::min({sy1, sy2, sy3});
            float maxy = std::max({sy1, sy2, sy3});
            int ix0 = std::max(0, (int)minx), ix1 = std::min(renderer_.width()-1, (int)maxx);
            int iy0 = std::max(0, (int)miny), iy1 = std::min(renderer_.height()-1, (int)maxy);
            float denom = (sy2-sy3)*(sx1-sx3) + (sx3-sx2)*(sy1-sy3);
            if (std::abs(denom) < 1e-6f) continue;
            for (int py = iy0; py <= iy1; ++py) {
                for (int px = ix0; px <= ix1; ++px) {
                    float fx = px + 0.5f, fy = py + 0.5f;
                    float w0 = ((sy2-sy3)*(fx-sx3) + (sx3-sx2)*(fy-sy3)) / denom;
                    float w1 = ((sy3-sy1)*(fx-sx3) + (sx1-sx3)*(fy-sy3)) / denom;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                        renderer_.draw_filled_rect_screen((float)px, (float)py, 1, 1, tri_col);
                    }
                }
            }
        }

        // Render capsules as thick lines
        soft::Color4B cap_col{200, 200, 210, 255};
        for (auto& c : body_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [n1, n2] = eit->second;
            auto [x1, y1] = resolve_node(n1, world_cx, world_cy, face_right, pivot_local_y);
            auto [x2, y2] = resolve_node(n2, world_cx, world_cy, face_right, pivot_local_y);
            // Radius in local units -> world units (scale 0.9)
            float r = (c.radius1 + c.radius2) * 0.5f * 0.9f;
            renderer_.draw_line_world_thick(x1, y1, x2, y2, cap_col, r * 2.0f);
        }
    }

    // Resolve a node name to world coordinates (handles BodyNode, SkelNode,
    // and MacroNode with recursive child resolution).
    // Both local and world coords are Y-UP — no inversion.
    std::pair<float, float> resolve_node(const std::string& name,
                                         float world_cx, float world_cy,
                                         bool face_right, float pivot_local_y) {
        if (!body_model_) return {world_cx, world_cy};
        // Check body model nodes first
        auto bit = body_model_->nodes.find(name);
        if (bit != body_model_->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }
        // Check skeleton nodes
        auto sit = skeleton_nodes_.find(name);
        if (sit != skeleton_nodes_.end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }
        // Check MacroNodes — compute as weighted average of children
        auto mit = body_model_->macro_nodes.find(name);
        if (mit != body_model_->macro_nodes.end()) {
            float sum_lcc = 0, wx = 0, wy = 0;
            for (int i = 0; i < 4; ++i) {
                if (mit->second.children[i].empty()) continue;
                auto [cx, cy] = resolve_node(mit->second.children[i],
                                             world_cx, world_cy, face_right, pivot_local_y);
                wx += cx * mit->second.lcc[i];
                wy += cy * mit->second.lcc[i];
                sum_lcc += mit->second.lcc[i];
            }
            if (std::abs(sum_lcc) > 1e-6f) {
                return {wx / sum_lcc, wy / sum_lcc};
            }
        }
        return {world_cx, world_cy};
    }

    // ---------- Character rendering ----------
    // Skeleton local coords: Y-UP (0 = feet, positive = up).
    // World coords: Y-UP (cocos2d convention, positive = up).
    // No Y inversion needed — both use Y-UP.
    //   world_y = player_pos_y + (local_y - pivot_local_y) * scale
    void render_character() {
        render_body_model(player_pos_x_, player_pos_y_, facing_right_);
        render_character_at(player_pos_x_, player_pos_y_, facing_right_);
    }

    void render_character_at(float world_cx, float world_cy, bool face_right) {
        soft::Color4B line_col{230, 230, 230, 255};
        soft::Color4B line_back{170, 170, 170, 255};
        soft::Color4B joint_col{255, 80, 80, 255};
        soft::Color4B head_col{255, 200, 100, 255};
        float bone_thick = 3.0f;

        // Pivot local Y (NPivot). We anchor the skeleton at this local Y.
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float pivot_local_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 170.0f;

        // Convert skeleton local (x, y) to world (x, y).
        // Both Y-UP: no inversion. Just offset by player position.
        auto node_world = [&](const std::string& name)
            -> std::pair<float, float>
        {
            auto it = skeleton_nodes_.find(name);
            if (it == skeleton_nodes_.end()) return {0, 0};
            float lx = it->second.x;
            float ly = it->second.y;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        };
        auto draw_bone = [&](const std::string& a, const std::string& b,
                             soft::Color4B col, float thick) {
            auto [ax, ay] = node_world(a);
            auto [bx, by] = node_world(b);
            renderer_.draw_line_world_thick(ax, ay, bx, by, col, thick);
        };
        auto draw_joint = [&](const std::string& a, soft::Color4B col, float r) {
            auto [ax, ay] = node_world(a);
            float sx, sy;
            renderer_.camera().world_to_screen(ax, ay, sx, sy);
            renderer_.draw_filled_circle_screen(sx, sy, r, col);
        };

        // Spine: pelvis -> stomach -> chest -> neck -> top
        draw_bone("NPivot", "NStomach", line_col, bone_thick);
        draw_bone("NStomach", "NChest", line_col, bone_thick);
        draw_bone("NChest", "NNeck", line_col, bone_thick);
        draw_bone("NNeck", "NTop", line_col, bone_thick);
        draw_bone("NNeck", "NHead", line_col, bone_thick);
        draw_joint("NHead", head_col, 10.0f);
        // Back arm (behind)
        draw_bone("NChest", "NShoulder_2", line_back, bone_thick);
        draw_bone("NShoulder_2", "NElbow_2", line_back, bone_thick);
        draw_bone("NElbow_2", "NWrist_2", line_back, bone_thick);
        draw_bone("NWrist_2", "NFingertips_2", line_back, bone_thick * 0.7f);
        // Back leg
        draw_bone("NPivot", "NHip_2", line_back, bone_thick);
        draw_bone("NHip_2", "NKnee_2", line_back, bone_thick);
        draw_bone("NKnee_2", "NAnkle_2", line_back, bone_thick);
        draw_bone("NAnkle_2", "NToe_2", line_back, bone_thick * 0.7f);
        // Front leg
        draw_bone("NPivot", "NHip_1", line_col, bone_thick);
        draw_bone("NHip_1", "NKnee_1", line_col, bone_thick);
        draw_bone("NKnee_1", "NAnkle_1", line_col, bone_thick);
        draw_bone("NAnkle_1", "NToe_1", line_col, bone_thick * 0.7f);
        // Front arm
        draw_bone("NChest", "NShoulder_1", line_col, bone_thick);
        draw_bone("NShoulder_1", "NElbow_1", line_col, bone_thick);
        draw_bone("NElbow_1", "NWrist_1", line_col, bone_thick);
        draw_bone("NWrist_1", "NFingertips_1", line_col, bone_thick * 0.7f);
        // Joints
        draw_joint("NPivot", joint_col, 5.0f);
        draw_joint("NChest", joint_col, 4.0f);
        draw_joint("NShoulder_1", joint_col, 4.0f);
        draw_joint("NShoulder_2", joint_col, 4.0f);
        draw_joint("NElbow_1", joint_col, 4.0f);
        draw_joint("NElbow_2", joint_col, 4.0f);
        draw_joint("NWrist_1", joint_col, 4.0f);
        draw_joint("NWrist_2", joint_col, 4.0f);
        draw_joint("NHip_1", joint_col, 4.0f);
        draw_joint("NHip_2", joint_col, 4.0f);
        draw_joint("NKnee_1", joint_col, 4.0f);
        draw_joint("NKnee_2", joint_col, 4.0f);
        draw_joint("NAnkle_1", joint_col, 4.0f);
        draw_joint("NAnkle_2", joint_col, 4.0f);
    }

    // ---------- Punching bag (real 3D model) ----------
    // Loads skeleton_punching_bag.xml (nodes + edges) and punching_bag.xml
    // (capsule figures). Renders as thick capsules:
    //   - Edge16/17 (NPivot->NNeck/NBottom): main bag body, Radius=25
    //   - Edge10/11/15: chain links, Radius=2
    // The bag hangs from Node12 (Fixed=1, Y=335 = ceiling anchor).
    void load_punching_bag_model() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> skel_candidates = {
            root/"models"/"skeleton_punching_bag.xml",
            root/"assets"/"models"/"skeleton_punching_bag.xml",
            "/home/z/my-project/upload/skeleton_punching_bag.xml",
            "/home/z/my-project/work/reSF2/assets/models/skeleton_punching_bag.xml",
        };
        std::vector<std::filesystem::path> fig_candidates = {
            root/"models"/"punching_bag.xml",
            root/"assets"/"models"/"punching_bag.xml",
            "/home/z/my-project/upload/punching_bag.xml",
            "/home/z/my-project/work/reSF2/assets/models/punching_bag.xml",
        };
        std::string skel_path, fig_path;
        for (const auto& p : skel_candidates)
            if (std::filesystem::exists(p)) { skel_path = p.string(); break; }
        for (const auto& p : fig_candidates)
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        if (skel_path.empty()) return;

        auto xml = read_text(skel_path);
        bag_model_ = std::make_unique<BodyModel>();

        // Parse <Nodes>
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start != std::string::npos && nodes_end != std::string::npos) {
            std::string ns = xml.substr(nodes_start, nodes_end - nodes_start);
            size_t pos = 0;
            while (true) {
                auto p1 = ns.find("Type=\"Node\"", pos);
                auto p2 = ns.find("Type=\"CenterOfMass\"", pos);
                size_t tp;
                if (p1 == std::string::npos && p2 == std::string::npos) break;
                if (p1 == std::string::npos) tp = p2;
                else if (p2 == std::string::npos) tp = p1;
                else tp = std::min(p1, p2);
                auto ts = ns.rfind('<', tp);
                auto end = ns.find("/>", tp);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = ns.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    BodyNode n;
                    n.name = tag.substr(1, sp - 1);
                    n.x = tof(xml_attr(tag, "X"));
                    n.y = tof(xml_attr(tag, "Y"));
                    n.z = tof(xml_attr(tag, "Z"));
                    bag_model_->nodes[n.name] = n;
                }
                pos = end + 2;
            }
        }

        // Parse <Edges>
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t pos = 0;
            while ((pos = es.find("Type=\"Edge\"", pos)) != std::string::npos) {
                auto ts = es.rfind('<', pos);
                auto end = es.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = es.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    BodyEdge e;
                    e.name = tag.substr(1, sp - 1);
                    e.end1 = xml_attr(tag, "End1");
                    e.end2 = xml_attr(tag, "End2");
                    e.length = tof(xml_attr(tag, "Length"));
                    bag_model_->edges.push_back(e);
                }
                pos = end + 2;
            }
        }

        // Parse punching_bag.xml <Figures> for capsules
        if (!fig_path.empty()) {
            auto fxml = read_text(fig_path);
            auto figs_start = fxml.find("<Figures>");
            auto figs_end = fxml.find("</Figures>");
            if (figs_start != std::string::npos && figs_end != std::string::npos) {
                std::string fs = fxml.substr(figs_start, figs_end - figs_start);
                size_t pos = 0;
                while ((pos = fs.find("Type=\"Capsule\"", pos)) != std::string::npos) {
                    auto ts = fs.rfind('<', pos);
                    auto end = fs.find("/>", pos);
                    if (ts == std::string::npos || end == std::string::npos) break;
                    auto tag = fs.substr(ts, end - ts);
                    BodyCapsule c;
                    c.edge_name = xml_attr(tag, "Edge");
                    c.radius1 = tof(xml_attr(tag, "Radius1"));
                    c.radius2 = tof(xml_attr(tag, "Radius2"));
                    bag_model_->capsules.push_back(c);
                    pos = end + 2;
                }
            }
        }

        std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                    bag_model_->nodes.size(), bag_model_->edges.size(),
                    bag_model_->capsules.size());
    }

    void render_punching_bag() {
        if (!bag_model_ || !location_) return;
        float bag_cx = location_->enemy_x;
        // Find NPivot local Y for the bag
        float pivot_ly = 109.0f;
        auto pit = bag_model_->nodes.find("NPivot");
        if (pit != bag_model_->nodes.end()) pivot_ly = pit->second.y;
        // Place bag using EnemyPositionY as the NPivot world Y
        float bag_cy = location_->enemy_y;

        // Build edge lookup
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : bag_model_->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }

        // Render capsules
        soft::Color4B bag_col{120, 40, 40, 255};     // dark red bag
        soft::Color4B chain_col{180, 180, 180, 255}; // gray chain
        for (auto& c : bag_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto& en1 = eit->second.first;
            auto& en2 = eit->second.second;
            auto nit1 = bag_model_->nodes.find(en1);
            auto nit2 = bag_model_->nodes.find(en2);
            if (nit1 == bag_model_->nodes.end() || nit2 == bag_model_->nodes.end()) continue;
            float x1 = bag_cx + nit1->second.x * 0.9f;
            float y1 = bag_cy + (nit1->second.y - pivot_ly) * 0.9f;
            float x2 = bag_cx + nit2->second.x * 0.9f;
            float y2 = bag_cy + (nit2->second.y - pivot_ly) * 0.9f;
            float r = (c.radius1 + c.radius2) * 0.5f * 0.9f;
            bool is_main = (c.radius1 >= 20 || c.radius2 >= 20);
            renderer_.draw_line_world_thick(x1, y1, x2, y2,
                                            is_main ? bag_col : chain_col,
                                            r * 2.0f);
        }
    }

    // ---------- HUD textures ----------
    void load_hud_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Load the panels/top atlas
        load_texture_atlas(root/"1536"/"textures"/"panels"/"top",
                           "batchPanelsTop");
        // Load the buttons/dojo atlas (for btn_punching_bag)
        load_texture_atlas(root/"1536"/"textures"/"buttons"/"dojo",
                           "batchButtonsDojo");
        // Also load individual HUD textures by name for convenience
        auto top_atlas = atlases_.find("batchPanelsTop");
        if (top_atlas != atlases_.end()) {
            for (auto& [name, idx] : top_atlas->second.atlas->name_index) {
                auto& frame = top_atlas->second.atlas->frames[idx];
                // Extract this frame into its own texture
                auto tex = extract_frame(top_atlas->second, frame);
                if (tex) {
                    std::string n = name;
                    // Remove .png extension
                    if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                    hud_textures_[n] = tex;
                }
            }
        }
        auto dojo_atlas = atlases_.find("batchButtonsDojo");
        if (dojo_atlas != atlases_.end()) {
            for (auto& [name, idx] : dojo_atlas->second.atlas->name_index) {
                auto& frame = dojo_atlas->second.atlas->frames[idx];
                auto tex = extract_frame(dojo_atlas->second, frame);
                if (tex) {
                    std::string n = name;
                    if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                    hud_textures_[n] = tex;
                }
            }
        }
        std::printf("  HUD textures loaded: %zu\n", hud_textures_.size());
    }

    void load_texture_atlas(const std::filesystem::path& dir,
                            const std::string& atlas_name) {
        auto pp = dir / (atlas_name + ".plist");
        auto pn = dir / (atlas_name + ".png");
        if (!std::filesystem::exists(pp) || !std::filesystem::exists(pn)) return;
        auto result = plist::parse(read_text(pp.string()));
        if (!result) return;
        auto png_data = read_file(pn.string());
        auto tex = std::make_shared<soft::Texture>();
        if (!tex->init_from_png((const std::uint8_t*)png_data.data(), png_data.size())) return;
        AtlasRef a;
        a.texture = tex;
        a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
        atlases_[atlas_name] = std::move(a);
    }

    std::shared_ptr<soft::Texture> extract_frame(const AtlasRef& atlas,
                                                  const plist::Frame& frame) {
        auto tex = std::make_shared<soft::Texture>();
        int w = frame.rotated ? frame.atlas_h : frame.atlas_w;
        int h = frame.rotated ? frame.atlas_w : frame.atlas_h;
        tex->init_rgba(w, h, nullptr);
        // Copy pixels from atlas
        auto& src = atlas.texture->pixels;
        int src_w = atlas.texture->width;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sx, sy;
                if (frame.rotated) {
                    // Rotated 90° CW: original (ox,oy) → atlas (oy, W-1-ox)
                    // W = atlas_w, H = atlas_h. Output: fw=W, fh=H
                    sx = frame.atlas_x + y;
                    sy = frame.atlas_y + (frame.atlas_w - 1 - x);
                } else {
                    sx = frame.atlas_x + x;
                    sy = frame.atlas_y + y;
                }
                if (sx < 0 || sy < 0 || sx >= src_w || sy >= atlas.texture->height) continue;
                int src_idx = (sy * src_w + sx) * 4;
                int dst_idx = (y * w + x) * 4;
                tex->pixels[dst_idx+0] = src[src_idx+0];
                tex->pixels[dst_idx+1] = src[src_idx+1];
                tex->pixels[dst_idx+2] = src[src_idx+2];
                tex->pixels[dst_idx+3] = src[src_idx+3];
            }
        }
        return tex;
    }

    // ---------- Menu textures ----------
    void load_menu_textures() {
        auto root = std::filesystem::path(asset_root_);
        load_texture_atlas(root/"1536"/"textures"/"buttons"/"menu"/"screens",
                           "batchButtonsMenuScreens");
        auto menu_atlas = atlases_.find("batchButtonsMenuScreens");
        if (menu_atlas != atlases_.end()) {
            for (auto& [name, idx] : menu_atlas->second.atlas->name_index) {
                auto& frame = menu_atlas->second.atlas->frames[idx];
                auto tex = extract_frame(menu_atlas->second, frame);
                if (tex) {
                    std::string n = name;
                    if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                    menu_textures_[n] = tex;
                }
            }
        }
        // Load scroll/roll textures for the parchment menu UI
        auto scroll_dir = root/"1536"/"textures"/"scrolls"/"common";
        for (auto& name : {"MenuRoll_left", "MenuRoll_center", "MenuRoll_right",
                           "Roll_left", "Roll_center", "Roll_right",
                           "Paper_left", "Paper_right", "Shadow_roll"}) {
            auto path = scroll_dir / (std::string(name) + ".png");
            if (std::filesystem::exists(path)) {
                auto data = read_file(path.string());
                auto tex = std::make_shared<soft::Texture>();
                if (tex->init_from_png((const std::uint8_t*)data.data(), data.size())) {
                    scroll_textures_[name] = tex;
                }
            }
        }
        std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                    menu_textures_.size(), scroll_textures_.size());
    }

    // ---------- HUD font ----------
    void load_hud_font() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> candidates = {
            root/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"1536"/"fonts"/"obelix.fnt",
        };
        std::string fnt_path, png_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) {
                fnt_path = p.string();
                auto png = p.parent_path() / (p.stem().string() + ".png");
                if (!std::filesystem::exists(png)) {
                    auto xml = read_text(fnt_path);
                    auto fp = xml.find("file=\"");
                    if (fp != std::string::npos) {
                        fp += 6;
                        auto end = xml.find('"', fp);
                        std::string png_name = xml.substr(fp, end - fp);
                        auto png2 = p.parent_path() / png_name;
                        if (std::filesystem::exists(png2)) png = png2;
                    }
                }
                if (std::filesystem::exists(png)) {
                    png_path = png.string();
                    break;
                }
            }
        }
        if (fnt_path.empty()) return;
        auto result = font::parse(read_text(fnt_path));
        if (!result) return;
        hud_font_ = std::make_shared<font::ParsedFont>(std::move(*result));
        auto png_data = read_file(png_path);
        hud_font_tex_ = std::make_shared<soft::Texture>();
        hud_font_tex_->init_from_png((const std::uint8_t*)png_data.data(),
                                      png_data.size());
        std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                    fnt_path.c_str(), hud_font_->chars.size());
    }

    void render_text(const std::string& text, float x, float y,
                     float scale, soft::Color4B color) {
        if (!hud_font_ || !hud_font_tex_) return;
        float cx = x;
        for (char c : text) {
            std::int32_t cp = (std::uint8_t)c;
            if (cp >= 0xC0 && cp <= 0xFF) cp = 0x0410 + (cp - 0xC0);
            auto it = hud_font_->char_index.find(cp);
            if (it == hud_font_->char_index.end()) {
                it = hud_font_->char_index.find(32);
                if (it == hud_font_->char_index.end()) continue;
            }
            auto& ch = hud_font_->chars[it->second];
            if (ch.width > 0 && ch.height > 0) {
                float u0 = (float)ch.x / hud_font_->common.scale_w;
                float v0 = (float)ch.y / hud_font_->common.scale_h;
                float u1 = (float)(ch.x + ch.width) / hud_font_->common.scale_w;
                float v1 = (float)(ch.y + ch.height) / hud_font_->common.scale_h;
                float px = cx + ch.xoffset * scale;
                float py = y + ch.yoffset * scale;
                float pw = ch.width * scale;
                float ph = ch.height * scale;
                renderer_.draw_textured_quad_screen(*hud_font_tex_, px, py, pw, ph,
                                                     u0, v0, u1, v1, color);
            }
            cx += ch.xadvance * scale;
        }
    }

    // ---------- HUD ----------
    // Uses real textures: Top_Panel (background bar), gold (coin), energy,
    // Level_bar, level (text badge).
    void render_hud() {
        // Top panel background (real texture, tiled horizontally)
        auto panel_it = hud_textures_.find("Top_Panel");
        if (panel_it != hud_textures_.end()) {
            auto& tex = panel_it->second;
            float panel_h = 50.0f;
            // Tile the panel texture horizontally to fill the screen width
            float tile_w = panel_h * tex->width / tex->height;
            float x = 0;
            while (x < width_) {
                float draw_w = std::min(tile_w, (float)width_ - x);
                float u1 = draw_w / tile_w;
                renderer_.draw_textured_quad_screen(*tex, x, 0, draw_w, panel_h,
                                                     0, 0, u1, 1.0f);
                x += draw_w;
            }
        } else {
            soft::Color4B bar_bg{0, 0, 0, 180};
            renderer_.draw_filled_rect_screen(0, 0, (float)width_, 50, bar_bg);
        }

        // Gold icon + amount
        auto gold_it = hud_textures_.find("gold");
        if (gold_it != hud_textures_.end()) {
            auto& tex = gold_it->second;
            float icon_size = 32.0f;
            renderer_.draw_textured_quad_screen(*tex, 10, 9, icon_size, icon_size);
        }
        render_text("72 450", 50, 15, 0.32f, {255, 240, 200, 255});

        // Energy icon + value
        auto energy_it = hud_textures_.find("energy");
        if (energy_it != hud_textures_.end()) {
            auto& tex = energy_it->second;
            float icon_size = 32.0f;
            renderer_.draw_textured_quad_screen(*tex, 180, 9, icon_size, icon_size);
        }
        render_text("5 / 5", 220, 15, 0.32f, {200, 230, 255, 255});

        // Level bar + level badge
        auto lvlbar_it = hud_textures_.find("Level_bar");
        if (lvlbar_it != hud_textures_.end()) {
            auto& tex = lvlbar_it->second;
            float bar_w = 120.0f, bar_h = 20.0f;
            renderer_.draw_textured_quad_screen(*tex, 330, 15, bar_w, bar_h);
        }
        render_text("LVL 7", 460, 15, 0.30f, {255, 255, 255, 255});

        // Bottom hint
        render_text("A/D - move    Space - hit    M - menu    T - dialog",
                    20, height_ - 35, 0.26f, {200, 200, 200, 255});

        // Position label
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Pos: (%.0f, %.0f)",
                      player_pos_x_, player_pos_y_);
        render_text(buf, 20, height_ - 60, 0.26f, {180, 180, 180, 255});
    }

    // ---------- Menu button (scroll/roll style, matching original) ----------
    // The original game uses a parchment scroll that unrolls vertically.
    // Top bar: MenuRoll_left + MenuRoll_center (stretched) + MenuRoll_right
    // Below: paper area with menu icon buttons
    void render_menu_button(bool expanded) {
        float roll_h = 40.0f;
        float btn_x = 10.0f, btn_y = 58.0f;

        auto left_it = scroll_textures_.find("MenuRoll_left");
        auto center_it = scroll_textures_.find("MenuRoll_center");
        auto right_it = scroll_textures_.find("MenuRoll_right");

        if (left_it == scroll_textures_.end() || center_it == scroll_textures_.end() ||
            right_it == scroll_textures_.end()) {
            // Fallback
            soft::Color4B bg{60, 40, 20, 230};
            renderer_.draw_filled_rect_screen(btn_x, btn_y, 120, roll_h, bg);
            render_text("MENU", btn_x + 40, btn_y + 12, 0.22f, {255, 240, 200, 255});
            return;
        }

        auto& left_tex = left_it->second;
        auto& center_tex = center_it->second;
        auto& right_tex = right_it->second;
        float cap_w = roll_h * left_tex->width / left_tex->height;

        if (!expanded) {
            // Collapsed: short roll bar with "MENU" text
            float min_roll_w = 130.0f;
            float center_w = min_roll_w - 2 * cap_w;
            renderer_.draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h);
            renderer_.draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h);
            renderer_.draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h);
            render_text("MENU", btn_x + min_roll_w / 2 - 22, btn_y + 12, 0.22f,
                        {255, 240, 200, 255});
        } else {
            // Expanded: full scroll with paper + icons
            float icon_size = 44.0f;
            float icon_spacing = 8.0f;
            int n_items = 7;
            float paper_padding = 15.0f;
            float paper_w = n_items * (icon_size + icon_spacing) + paper_padding * 2;
            float paper_h = icon_size + paper_padding * 2 + 20;
            float roll_w = paper_w;
            float center_w = roll_w - 2 * cap_w;

            // Roll bar (top)
            renderer_.draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h);
            renderer_.draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h);
            renderer_.draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h);

            // Paper area (below roll, slight overlap)
            float paper_y = btn_y + roll_h - 3;
            soft::Color4B paper_bg{200, 170, 120, 245};
            renderer_.draw_filled_rect_screen(btn_x, paper_y, paper_w, paper_h, paper_bg);

            // Paper left/right edges
            auto pl_it = scroll_textures_.find("Paper_left");
            auto pr_it = scroll_textures_.find("Paper_right");
            if (pl_it != scroll_textures_.end()) {
                float pl_w = paper_h * pl_it->second->width / pl_it->second->height;
                renderer_.draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_h);
            }
            if (pr_it != scroll_textures_.end()) {
                float pr_w = paper_h * pr_it->second->width / pr_it->second->height;
                renderer_.draw_textured_quad_screen(*pr_it->second,
                    btn_x + paper_w - pr_w, paper_y, pr_w, paper_h);
            }

            // Shadow below paper
            auto shadow_it = scroll_textures_.find("Shadow_roll");
            if (shadow_it != scroll_textures_.end()) {
                renderer_.draw_textured_quad_screen(*shadow_it->second,
                    btn_x, paper_y + paper_h - 8, paper_w, 15);
            }

            // Menu icons
            const char* items[] = {"Dojo", "Map", "Shop", "Profile",
                                   "Settings", "Fight", "Lottery"};
            float ix = btn_x + paper_padding;
            float iy = paper_y + paper_padding;
            for (auto& name : items) {
                std::string tex_name = std::string(name) + "_normal";
                auto it = menu_textures_.find(tex_name);
                if (it != menu_textures_.end()) {
                    renderer_.draw_textured_quad_screen(*it->second, ix, iy,
                                                         icon_size, icon_size);
                }
                render_text(name, ix, iy + icon_size + 2, 0.16f,
                            {60, 40, 20, 255});
                ix += icon_size + icon_spacing;
            }
        }
    }

    // ---------- Dialog overlay ----------
    void render_dialog_overlay() {
        float panel_w = (float)width_ - 100, panel_h = 140;
        float px = 50, py = (float)height_ - panel_h - 60;
        soft::Color4B panel_bg{15, 15, 20, 230};
        renderer_.draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        soft::Color4B border{140, 100, 50, 255};
        renderer_.draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_.draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);

        render_text("SENSEI", px + 30, py + 15, 0.40f,
                    {255, 220, 120, 255});
        render_text("Welcome back, student.", px + 30, py + 55, 0.32f,
                    {230, 230, 230, 255});
        render_text("Train on the bag, then we will",
                    px + 30, py + 80, 0.32f, {230, 230, 230, 255});
        render_text("talk about your journey.",
                    px + 30, py + 105, 0.32f, {230, 230, 230, 255});

        // Continue arrow
        soft::Color4B arrow{255, 220, 120, 255};
        float ax = px + panel_w - 30, ay = py + panel_h - 25;
        renderer_.draw_filled_rect_screen(ax, ay - 12, 12, 2, arrow);
        renderer_.draw_filled_rect_screen(ax, ay - 12, 2, 12, arrow);
        renderer_.draw_filled_rect_screen(ax + 10, ay - 12, 2, 12, arrow);
    }

private:
    std::string asset_root_, out_dir_;
    int width_ = 0, height_ = 0;
    soft::Renderer renderer_;

    std::vector<LoadingImg> loading_images_;
    float load_scale_ = 1.0f;

    std::unordered_map<std::string, AtlasRef> atlases_;
    std::unordered_map<std::string, std::shared_ptr<soft::Texture>> hud_textures_;
    std::unordered_map<std::string, std::shared_ptr<soft::Texture>> menu_textures_;
    std::unordered_map<std::string, std::shared_ptr<soft::Texture>> scroll_textures_;
    std::unique_ptr<GameLocation> location_;
    std::unordered_map<std::string, SkelNode> skeleton_nodes_;
    std::unordered_map<std::string, SkelEdge> skeleton_edges_;
    std::unique_ptr<BodyModel> body_model_;
    std::unique_ptr<BodyModel> bag_model_;

    std::shared_ptr<font::ParsedFont> hud_font_;
    std::shared_ptr<soft::Texture> hud_font_tex_;

    float player_pos_x_ = 0, player_pos_y_ = 0;
    bool facing_right_ = true;
};

int main(int argc, char* argv[]) {
    std::string asset_root, out_dir = "/home/z/my-project/download/screenshots";
    int width = 1280, height = 720;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--assets" && i + 1 < argc) asset_root = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::printf("Usage: resf2_headless --assets <path> [--out <dir>] "
                        "[--width W --height H]\n");
            return 0;
        }
    }
    if (asset_root.empty()) {
        asset_root = "/home/z/my-project/work/apk_extracted/apktool/assets/assets";
    }
    Game game(asset_root, out_dir, width, height);
    return game.run();
}
