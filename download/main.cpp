// main.cpp
//
// Interactive GLFW-based reSF2 engine driver. Implements the full game
// boot sequence for the Dojo location:
//
//   1. Loading screen (startLoading.xml assets)
//   2. Dojo location (params.xml + parallax background layers)
//   3. Player character (skeletal stick figure from skeleton.xml)
//   4. Punching bag at the enemy position
//   5. HUD overlay (money, energy, level, menu button)
//   6. Menu overlay (Map / Shop / Settings / Save / Exit)
//   7. Story dialog overlay (intro line from Sensei)
//
// Controls:
//   A / D  or  Left / Right   Move player
//   W / S  or  Up   / Down    Move camera (debug)
//   Space                     Hit (visual feedback on punching bag)
//   M  or  click menu button  Toggle menu overlay
//   T                         Toggle dialog overlay
//   1 / 2 / 3                 Zoom presets
//   Esc                       Quit (or close menu if open)

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>

#include "engine/platform/platform.hpp"
#include "engine/platform/glfw_platform.hpp"
#include "engine/runtime/loop.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/reverse/plist_atlas.hpp"
#include "engine/reverse/bitmap_font.hpp"
#include "engine/renderer/stb_image.h"

namespace plat = resf2::platform;
namespace rt = resf2::runtime;
namespace ren = resf2::renderer;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;

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

// Get the directory containing the executable. Used to find model XML files
// that are shipped with the repo (assets/models/) but may not be in the
// game's asset directory (they're inside files.dz which we can't read yet).
static std::filesystem::path get_exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// Build a list of search paths for model XML files.
// Searches: asset_root/models/, asset_root/assets/models/,
//           exe_dir/../../assets/models/ (repo-local)
static std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename) {
    namespace fs = std::filesystem;
    auto root = fs::path(asset_root);
    auto exe = get_exe_dir();
    return {
        root / "models" / filename,
        root / "assets" / "models" / filename,
        exe / ".." / ".." / "assets" / "models" / filename,
        exe / ".." / "assets" / "models" / filename,
        exe / "assets" / "models" / filename,
    };
}

// ---------- Asset types ----------

struct AtlasRef {
    std::unique_ptr<ren::Texture2D> texture;
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

// ---------- Animation system ----------
// .bin format (from Gymnast-Tool-Suite Blender plugin):
// u32 frame_count (LE) + per frame: 1 byte skip + u32 node_count (LE) + node_count * (X, Y, -Z) LE floats
// Node order = ALL skeleton.xml nodes in XML order (54 Node + 1 COM + 12 MacroNode = 67)
// Positions are ABSOLUTE. Local = abs - NPivot_world_pos.
struct AnimationData {
    std::string name;
    int frame_count = 0;
    std::vector<std::vector<std::tuple<float,float,float>>> node_positions;

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        auto sz = (size_t)f.tellg();
        if (sz < 4) return false;
        f.seekg(0);
        std::vector<uint8_t> data(sz);
        f.read((char*)data.data(), sz);
        frame_count = read_u32_le(data.data(), 0);
        if (frame_count <= 0 || frame_count > 10000) return false;
        node_positions.resize(frame_count);
        size_t offset = 4;
        for (int fi = 0; fi < frame_count; ++fi) {
            if (offset + 5 > sz) break;
            uint32_t nc = read_u32_le(data.data(), offset + 1);
            offset += 5;
            auto& nodes = node_positions[fi];
            for (uint32_t i = 0; i < nc && offset + 12 <= sz; ++i) {
                float fx, fy, fneg_z;
                memcpy(&fx, &data[offset], 4);
                memcpy(&fy, &data[offset+4], 4);
                memcpy(&fneg_z, &data[offset+8], 4);
                nodes.push_back({fx, fy, -fneg_z});
                offset += 12;
            }
        }
        return !node_positions.empty();
    }

    bool get_node_pos(int fi, int idx, float& x, float& y, float& z) const {
        if (fi < 0 || fi >= (int)node_positions.size()) return false;
        const auto& nodes = node_positions[fi];
        if (idx < 0 || idx >= (int)nodes.size()) return false;
        auto [nx, ny, nz] = nodes[idx];
        x = nx; y = ny; z = nz;
        return true;
    }

    static uint32_t read_u32_le(const uint8_t* p, size_t off) {
        return (uint32_t)p[off] | ((uint32_t)p[off+1] << 8) |
               ((uint32_t)p[off+2] << 16) | ((uint32_t)p[off+3] << 24);
    }
};

struct MoveDef {
    std::string name, filename, template_name;
    int first_frame = 0, priority = 0;
    int attack_start = -1, attack_end = -1;
    float damage = 0.0f;
};

struct BodyNode {
    std::string name;
    float x = 0, y = 0, z = 0;
};

struct BodyEdge {
    std::string name;
    std::string end1, end2;
    float length = 0;
};

struct BodyCapsule {
    std::string edge_name;
    float radius1 = 0, radius2 = 0;
    float margin1 = 0, margin2 = 0;
};

struct BodyMacroNode {
    std::string name;
    std::string children[4];
    float lcc[4] = {};
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

struct LoadingImg {
    std::unique_ptr<ren::Texture2D> texture;
    float x = 0, y = 0;
};

// ---------- Game states ----------

enum class GameState { Loading, Location };
enum class Overlay { None, Menu, Dialog };

// ---------- Game ----------

class Game final : public rt::IGame {
public:
    explicit Game(std::string asset_root) : asset_root_(std::move(asset_root)) {}

    void on_init(plat::Platform& platform) override {
        platform_ = &platform;
        std::printf("reSF2 initialized.\n");
        std::printf("Controls:\n");
        std::printf("  A/D or Left/Right  - move player\n");
        std::printf("  W/S or Up/Down     - move camera (debug)\n");
        std::printf("  Space              - hit (punch the bag)\n");
        std::printf("  M or click menu    - toggle menu\n");
        std::printf("  T                  - toggle dialog\n");
        std::printf("  1/2/3              - zoom presets\n");
        std::printf("  Esc                - quit / close overlay\n\n");

        renderer_ = std::make_unique<ren::Renderer>();
        if (!renderer_->init(platform.window_width(), platform.window_height())) {
            renderer_.reset(); return;
        }
        renderer_->set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        if (!asset_root_.empty()) load_loading_screen();
    }

    void on_update(plat::Platform& platform, uint32_t dt) override {
        const auto& input = platform.input();

        // Esc: close overlay if open, else quit
        if (input.keys_just_pressed[(size_t)plat::Key::Escape]) {
            if (overlay_ != Overlay::None) overlay_ = Overlay::None;
            else quit_requested_ = true;
        }
        // M: toggle menu
        if (input.keys_just_pressed[(size_t)plat::Key::M]) {
            overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
        }
        // T: toggle dialog
        if (input.keys_just_pressed[(size_t)plat::Key::T]) {
            overlay_ = (overlay_ == Overlay::Dialog) ? Overlay::None : Overlay::Dialog;
        }

        // Click: check menu button (left side)
        for (const auto& p : input.pointers) {
            if (p.just_pressed) {
                float btn_x = 10.0f, btn_y = 58.0f, btn_w = 130.0f, btn_h = 40.0f;
                if (p.x >= btn_x && p.x <= btn_x + btn_w &&
                    p.y >= btn_y && p.y <= btn_y + btn_h) {
                    overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
                }
            }
        }

        if (state_ == GameState::Loading) {
            loading_timer_ += dt;
            if (loading_timer_ > 1500) {
                init_location();
            }
        } else if (state_ == GameState::Location) {
            // MOVEMENT
            bool wl = input.keys_down[(size_t)plat::Key::A] || input.keys_down[(size_t)plat::Key::ArrowLeft];
            bool wr = input.keys_down[(size_t)plat::Key::D] || input.keys_down[(size_t)plat::Key::ArrowRight];
            if (hit_anim_ == 0) {
                if (wl && !wr) { facing_right_ = false;
                    if (current_anim_ != "step_back" && animations_.count("step_back")) play_animation("step_back", true);
                } else if (wr && !wl) { facing_right_ = true;
                    if (current_anim_ != "step_forward" && animations_.count("step_forward")) play_animation("step_forward", true);
                } else { if (current_anim_ != "fists_idle" && current_anim_.find("punch")==std::string::npos &&
                    current_anim_.find("kick")==std::string::npos && current_anim_.find("cut")==std::string::npos)
                    play_animation("fists_idle", true); }
            }
            cam_x_ = player_pos_x_ + 200.0f; renderer_->camera().set_target(cam_x_, cam_y_); renderer_->camera().set_zoom(zoom_);
            // COMBAT
            if (input.keys_just_pressed[(size_t)plat::Key::Space] && hit_anim_ == 0) {
                std::string an = "high_punch";
                if (wr) an = "double_punch"; else if (wl) an = "spinning_punch";
                else if (input.keys_down[(size_t)plat::Key::W]||input.keys_down[(size_t)plat::Key::ArrowUp]) an = "upper_cut";
                else if (input.keys_down[(size_t)plat::Key::S]||input.keys_down[(size_t)plat::Key::ArrowDown]) an = "low_punch";
                if (animations_.count(an)) { play_animation(an, false); hit_anim_ = (uint32_t)(animations_[an].frame_count*1000.0f/30.0f); }
            }
            if (input.keys_just_pressed[(size_t)plat::Key::K] && hit_anim_ == 0) {
                std::string an = "high_kick";
                if (input.keys_down[(size_t)plat::Key::S]||input.keys_down[(size_t)plat::Key::ArrowDown]) an = "sweep";
                else if (wl) an = "back_kick"; else if (wr) an = "front_kick";
                if (animations_.count(an)) { play_animation(an, false); hit_anim_ = (uint32_t)(animations_[an].frame_count*1000.0f/30.0f); }
            }
            if (hit_anim_ > 0) {
                hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
                if (!bag_hit_ && bag_model_ && location_) {
                    auto ai = animations_.find(current_anim_);
                    if (ai != animations_.end()) { int cf = (int)(anim_time_*30.0f); int fc = ai->second.frame_count;
                        if (cf >= fc/4 && cf <= fc*3/4) { float bx = location_->enemy_x - 857.0f;
                            if (std::abs(player_pos_x_-bx) < 400.0f) { bag_swing_ = 800; bag_hit_ = true; } } } }
                if (hit_anim_ == 0) { play_animation("fists_idle", true); bag_hit_ = false; }
            }
            if (bag_swing_ > 0) bag_swing_ -= std::min<uint32_t>(bag_swing_, dt);
            update_animation(dt);
            if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;
        }
    }

    void on_render(plat::Platform& platform) override {
        if (!renderer_) return;
        renderer_->begin_frame();
        if (state_ == GameState::Loading) render_loading_screen(platform);
        else if (state_ == GameState::Location) {
            render_location();
            render_punching_bag();
            render_character();
            render_hud(platform);
            if (overlay_ == Overlay::Menu)   render_menu_expanded(platform);
            if (overlay_ == Overlay::Dialog) render_dialog_overlay(platform);
        }
        renderer_->end_frame();
    }

    void on_shutdown(plat::Platform&) override {
        if (renderer_) renderer_->shutdown();
    }

    bool quit_requested() const noexcept { return quit_requested_; }

private:
    // ---------- Loading screen ----------
    void load_loading_screen() {
        auto root = std::filesystem::path(asset_root_);
        std::string xml_path;
        // Search for startLoading.xml in multiple possible paths
        for (const auto& dir : {root/"assets"/"1536"/"textures"/"fullscreen",
                                 root/"1536"/"textures"/"fullscreen",
                                 root/"assets"/"1536"/"fullscreen",
                                 root/"1536"/"fullscreen"}) {
            auto p = dir/"startLoading.xml";
            if (std::filesystem::exists(p)) { xml_path = p.string(); break; }
        }
        if (xml_path.empty()) {
            std::printf("  startLoading.xml not found, skipping loading screen\n");
            init_location();
            return;
        }
        auto xml = read_text(xml_path);
        load_scale_ = std::min(
            (float)platform_->window_width() / 1820.0f,
            (float)platform_->window_height() / 1024.0f);
        size_t pos = 0;
        while ((pos = xml.find("<Image", pos)) != std::string::npos) {
            auto end = xml.find("/>", pos);
            auto tag = xml.substr(pos, end - pos);
            auto file = xml_attr(tag, "File");
            auto x = tof(xml_attr(tag, "X"));
            auto y = tof(xml_attr(tag, "Y"));
            // Search for the image file in multiple paths
            std::filesystem::path img_path;
            for (const auto& base : {root/"assets"/"1536", root/"1536", root/"assets", root}) {
                auto p = base / file;
                if (std::filesystem::exists(p)) { img_path = p; break; }
            }
            if (!img_path.empty()) {
                auto data = read_file(img_path.string());
                int w, h, ch;
                auto* px = stbi_load_from_memory(
                    (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                if (px) {
                    auto tex = std::make_unique<ren::Texture2D>();
                    tex->init_rgba(w, h, px);
                    stbi_image_free(px);
                    loading_images_.push_back({std::move(tex), x, y});
                }
            }
            pos = end + 2;
        }
        if (loading_images_.empty()) {
            std::printf("  No loading images found, skipping loading screen\n");
            init_location();
        }
    }

    // Initialize the dojo location: load all assets and set up the scene.
    // Called either from the loading timer (after 1.5s) or directly from
    // load_loading_screen() if the loading screen is not available.
    void init_location() {
        load_location("dojo");
        state_ = GameState::Location;
        if (location_ && !location_->color.empty()) {
            auto c = std::stoul(location_->color, nullptr, 16);
            renderer_->set_clear_color(
                ((c>>16)&0xFF)/255.0f,
                ((c>>8)&0xFF)/255.0f,
                (c&0xFF)/255.0f, 1.0f);
        }
        load_skeleton();
        load_body_model();
        load_punching_bag_model();
        load_animations();
        load_moves();
        load_hud_textures();
        load_menu_textures();
        load_hud_font();
        if (location_) {
            // Use PlayerPositionX/Y from params.xml but offset to center
            // The original game centers the camera on the fight area,
            // not on the player. Player appears left-of-center.
            player_pos_x_ = location_->player_x - 857.0f;  // 690 - 857 = -167
            player_pos_y_ = location_->player_y;
        }
        // Camera follows player with offset (player on left third)
        cam_x_ = player_pos_x_ + 200.0f;
        cam_y_ = 0;
        zoom_ = 1.0f;
    }

    void render_loading_screen(plat::Platform& platform) {
        float tw = 1820.0f * load_scale_, th = 1024.0f * load_scale_;
        float ox = (platform.window_width() - tw) / 2.0f;
        float oy = (platform.window_height() - th) / 2.0f;
        for (auto& img : loading_images_) {
            if (!img.texture) continue;
            float w = img.texture->width() * load_scale_;
            float h = img.texture->height() * load_scale_;
            float x = ox + (img.x + 910.0f) * load_scale_;
            float y = oy + (img.y + 512.0f) * load_scale_;
            renderer_->draw_textured_quad_screen(*img.texture, x, y, w, h);
        }
    }

    // ---------- Location ----------
    void load_location(const std::string& name) {
        auto root = std::filesystem::path(asset_root_);
        std::string params_path;
        for (const auto& dir : {root/"assets"/"locations"/name,
                                 root/"locations"/name,
                                 root/"assets"/"1536"/"locations"/name}) {
            auto p = dir/"params.xml";
            if (std::filesystem::exists(p)) { params_path = p.string(); break; }
        }
        if (params_path.empty()) {
            std::printf("Location '%s' not found!\n", name.c_str()); return;
        }
        std::printf("Loading location: %s\n", params_path.c_str());
        auto xml = read_text(params_path);
        location_ = std::make_unique<GameLocation>(parse_location(xml));
        std::printf("  Player: (%.0f, %.0f)  Enemy: (%.0f, %.0f)\n",
                    location_->player_x, location_->player_y,
                    location_->enemy_x, location_->enemy_y);
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
        for (const auto& dir : {root/"assets"/"1536"/"locations"/loc,
                                 root/"assets"/"1536"/"textures",
                                 root/"assets"/"1536",
                                 root/"1536"/"locations"/loc,
                                 root/"1536"/"textures",
                                 root/"1536",
                                 root/"assets",
                                 root}) {
            auto pp = dir/(name+".plist"), pn = dir/(name+".png");
            if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
                auto result = plist::parse(read_text(pp.string()));
                if (!result) continue;
                auto png_data = read_file(pn.string());
                auto tex = std::make_unique<ren::Texture2D>();
                if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                         png_data.size())) continue;
                AtlasRef a;
                a.texture = std::move(tex);
                a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
                std::printf("  Atlas '%s': %zu frames\n",
                            name.c_str(), a.atlas->frames.size());
                atlases_[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
    }

    void render_location() {
        if (!location_) return;
        for (auto& layer : location_->layers) {
            if (layer.type != 1) continue;
            // Approximate parallax by shifting camera target by factor.
            float orig_tx = renderer_->camera().x();
            float orig_ty = renderer_->camera().y();
            // Save current target and apply parallax factor
            // (Camera2D doesn't expose target getters, so we use the
            // actual camera position via the public x()/y() methods.)
            // To keep things simple we just render this layer at full camera
            // (parallax = 1). True parallax would require set_target(fx, fy)
            // before each layer.
            for (auto& img : layer.images) {
                if (img.class_name == "pixel_1" && !img.color.empty()) {
                    unsigned long col = std::stoul(img.color, nullptr, 16);
                    ren::Color4B c{
                        (std::uint8_t)((col>>16)&0xFF),
                        (std::uint8_t)((col>>8)&0xFF),
                        (std::uint8_t)(col&0xFF), 255};
                    // World-space filled rect — use draw_filled_rect_screen
                    // with world-to-screen conversion.
                    auto it = atlases_.find(img.atlas_name);
                    if (it == atlases_.end()) {
                        // No atlas: render as a solid world-space rect.
                        // Y-UP: bottom = cam_y - hh, top = cam_y + hh
                        float hw = (float)platform_->window_width()  / (2.0f * zoom_);
                        float hh = (float)platform_->window_height() / (2.0f * zoom_);
                        float left = cam_x_ - hw, right = cam_x_ + hw;
                        float bottom = cam_y_ - hh, top = cam_y_ + hh;
                        // (x,y) = centre, bottom-left = (x-w/2, y-h/2) in Y-UP
                        float sx = (img.x - img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float sy = (1.0f - (img.y - img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float ex = (img.x + img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float ey = (1.0f - (img.y + img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float x = std::min(sx, ex), y = std::min(sy, ey);
                        float w = std::abs(ex - sx), h = std::abs(ey - sy);
                        renderer_->draw_filled_rect_screen(x, y, w, h, c);
                    }
                    continue;
                }
                auto it = atlases_.find(img.atlas_name);
                if (it == atlases_.end()) continue;
                auto& atlas = it->second;
                if (!atlas.texture || !atlas.atlas) continue;
                auto fit = atlas.atlas->name_index.find(img.class_name + ".png");
                if (fit == atlas.atlas->name_index.end()) {
                    fit = atlas.atlas->name_index.find(img.class_name);
                    if (fit == atlas.atlas->name_index.end()) continue;
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
                float px = img.x - img.w / 2.0f;
                float py = img.y - img.h / 2.0f;  // bottom-left (world Y-UP: +Y = up)
                renderer_->draw_textured_quad(*atlas.texture, px, py, img.w, img.h,
                                              u0, v0, u1, v1);
            }
        }
    }

    // ---------- Skeleton ----------
    void load_skeleton() {
        auto candidates = model_paths(asset_root_, "skeleton.xml");
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) { std::printf("  skeleton.xml NOT FOUND!\n"); return; }
        auto xml = read_text(path);

        // Parse <Nodes> section — find ALL Type="Node" tags (including Weapon-Node)
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start == std::string::npos || nodes_end == std::string::npos) return;
        std::string nodes_xml = xml.substr(nodes_start, nodes_end - nodes_start);
        size_t pos = 0;
        while ((pos = nodes_xml.find("Type=\"Node\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
            }
            pos = end + 2;
        }
        // Parse MacroNodes
        pos = 0;
        while ((pos = nodes_xml.find("Type=\"MacroNode\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos); auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts); auto sp = tag.find(' ');
            if (sp != std::string::npos) { SkelNode n; n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X")); n.y = tof(xml_attr(tag, "Y")); n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n; } pos = end + 2; }
        // Parse CenterOfMass
        pos = 0;
        while ((pos = nodes_xml.find("Type=\"CenterOfMass\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos); auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts); auto sp = tag.find(' ');
            if (sp != std::string::npos) { SkelNode n; n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X")); n.y = tof(xml_attr(tag, "Y")); n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n; } pos = end + 2; }
        // Build ordered_node_names_
        ordered_node_names_.clear(); pos = 0;
        while (true) { auto ts = nodes_xml.find('<', pos); if (ts == std::string::npos) break;
            auto te = nodes_xml.find("/>", ts); if (te == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, te - ts);
            if (tag.find("X=\"") != std::string::npos && tag.find("Y=\"") != std::string::npos) {
                auto sp = tag.find(' '); if (sp != std::string::npos) ordered_node_names_.push_back(tag.substr(1, sp - 1)); }
            pos = te + 2; }
        std::printf("  Skeleton: %zu nodes, %zu ordered\n", skeleton_nodes_.size(), ordered_node_names_.size());

        // Parse <Edges> section for Edge and Muscle types
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t ep = 0;
            while (true) {
                auto p1 = es.find("Type=\"Edge\"", ep);
                auto p2 = es.find("Type=\"Muscle\"", ep);
                size_t tp;
                if (p1 == std::string::npos && p2 == std::string::npos) break;
                if (p1 == std::string::npos) tp = p2;
                else if (p2 == std::string::npos) tp = p1;
                else tp = std::min(p1, p2);
                auto ts = es.rfind('<', tp);
                auto end = es.find("/>", tp);
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
        auto candidates = model_paths(asset_root_, "body.xml");
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) { std::printf("  body.xml NOT FOUND!\n"); return; }
        auto xml = read_text(path);
        body_model_ = std::make_unique<BodyModel>();

        // Parse <Nodes> for BODY-NodeN and BODY-MacroNodeN
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
                        BodyNode n; n.name = name;
                        n.x = tof(xml_attr(tag, "X"));
                        n.y = tof(xml_attr(tag, "Y"));
                        n.z = tof(xml_attr(tag, "Z"));
                        body_model_->nodes[n.name] = n;
                    } else if (type == "MacroNode") {
                        BodyMacroNode mn; mn.name = name;
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

        // Parse <Edges>
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
                        BodyEdge e; e.name = tag.substr(1, sp - 1);
                        e.end1 = xml_attr(tag, "End1");
                        e.end2 = xml_attr(tag, "End2");
                        body_model_->edges.push_back(e);
                    }
                }
                pos = end + 2;
            }
        }

        // Parse <Figures> for Capsules and Triangles
        auto figs_start = xml.find("<Figures>");
        auto figs_end = xml.find("</Figures>");
        if (figs_start != std::string::npos && figs_end != std::string::npos) {
            std::string fs = xml.substr(figs_start, figs_end - figs_start);
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
                body_model_->capsules.push_back(c);
                pos = end + 2;
            }
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

    // Resolve a node name to world coordinates (handles BodyNode, SkelNode, MacroNode).
    std::pair<float, float> resolve_body_node(const std::string& name,
                                              float world_cx, float world_cy,
                                              bool face_right, float pivot_local_y) {
        if (!body_model_) return {world_cx, world_cy};
        
        // Check if this node has an animated position
        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            float lx = ait->second.first, ly = ait->second.second;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }
        
        auto bit = body_model_->nodes.find(name);
        if (bit != body_model_->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }
        auto sit = skeleton_nodes_.find(name);
        if (sit != skeleton_nodes_.end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }
        auto mit = body_model_->macro_nodes.find(name);
        if (mit != body_model_->macro_nodes.end()) {
            float sum_lcc = 0, wx = 0, wy = 0;
            for (int i = 0; i < 4; ++i) {
                if (mit->second.children[i].empty()) continue;
                auto [cx, cy] = resolve_body_node(mit->second.children[i],
                                                  world_cx, world_cy, face_right, pivot_local_y);
                wx += cx * mit->second.lcc[i];
                wy += cy * mit->second.lcc[i];
                sum_lcc += mit->second.lcc[i];
            }
            if (std::abs(sum_lcc) > 1e-6f)
                return {wx / sum_lcc, wy / sum_lcc};
        }
        return {world_cx, world_cy};
    }

    // Render body model as capsule lines (GL renderer uses thin lines for now).
    void render_body_model() {
        if (!body_model_) return;
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float pivot_local_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 170.0f;

        // Build edge lookup from both body.xml edges and skeleton.xml edges
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : body_model_->edges)
            edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : skeleton_edges_)
            edge_map[name] = {e.end1, e.end2};

        // World-to-screen helper (for capsules that still use screen-space)
        float hw = (float)platform_->window_width() / (2.0f * zoom_);
        float hh = (float)platform_->window_height() / (2.0f * zoom_);
        float left = cam_x_ - hw, right = cam_x_ + hw;
        float bottom = cam_y_ - hh, top = cam_y_ + hh;
        auto w2s = [&](float wx, float wy, float& sx, float& sy) {
            sx = (wx - left) / (right - left) * platform_->window_width();
            sy = (1.0f - (wy - bottom) / (top - bottom)) * platform_->window_height();
        };

        // Render capsules as thick lines in world space
        ren::Color4B silhouette_col{20, 20, 25, 255};
        for (auto& c : body_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            float r = (c.radius1 + c.radius2) * 0.5f * 0.9f;
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            renderer_->draw_filled_circle_world(x1, y1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(x2, y2, ht, silhouette_col);
        }

        // Render triangles as filled silhouette
        for (auto& t : body_model_->triangles) {
            auto [tx0, ty0] = resolve_body_node(t.n1,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, silhouette_col);
        }
    }

    // ---------- Character rendering ----------
    // Skeleton local coords: Y-UP (0 = feet, positive = up).
    // World coords: Y-UP (cocos2d convention, positive = up).
    void render_character() {
        render_body_model();
    }

    // ---------- Punching bag (real 3D model from skeleton_punching_bag.xml) ----------
    void load_punching_bag_model() {
        auto skel_candidates = model_paths(asset_root_, "skeleton_punching_bag.xml");
        auto fig_candidates = model_paths(asset_root_, "punching_bag.xml");
        std::string skel_path, fig_path;
        for (const auto& p : skel_candidates)
            if (std::filesystem::exists(p)) { skel_path = p.string(); break; }
        for (const auto& p : fig_candidates)
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        if (skel_path.empty()) { std::printf("  skeleton_punching_bag.xml NOT FOUND!\n"); return; }

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
        // Position bag relative to player (same offset as player)
        float bag_cx = (location_->enemy_x - 857.0f);  // Apply same offset as player
        float pivot_ly = 109.0f;
        auto pit = bag_model_->nodes.find("NPivot");
        if (pit != bag_model_->nodes.end()) pivot_ly = pit->second.y;
        float bag_cy = location_->enemy_y;

        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : bag_model_->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }

        // Helper: convert world coords to screen coords (for fallback)
        float hw = (float)platform_->window_width() / (2.0f * zoom_);
        float hh = (float)platform_->window_height() / (2.0f * zoom_);
        float left = cam_x_ - hw, right = cam_x_ + hw;
        float bottom = cam_y_ - hh, top = cam_y_ + hh;
        auto w2s = [&](float wx, float wy, float& sx, float& sy) {
            sx = (wx - left) / (right - left) * platform_->window_width();
            sy = (1.0f - (wy - bottom) / (top - bottom)) * platform_->window_height();
        };

        ren::Color4B bag_col{100, 30, 30, 255};
        ren::Color4B chain_col{180, 180, 180, 255};
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

            // Draw as world-space thick line (2 triangles)
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float thickness = std::max(r, 1.0f);
            float ht = thickness;
            ren::Color4B col = is_main ? bag_col : chain_col;
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx_ = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx_, cy_, col);
            renderer_->draw_filled_triangle_world(ax, ay, cx_, cy_, dx_, dy_, col);
        }
    }

    // ---------- HUD textures (real game textures) ----------
    // ---------- Animation loading ----------
    void load_animations() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> dirs = {
            root/"assets"/"animations"/"binary", root/"animations"/"binary",
            root/"assets"/"animations", root/"animations" };
        const char* names[] = { "fists1_stance_idle", "high_punch", "heavy_punch",
            "low_punch", "double_punch", "spinning_punch", "upper_cut",
            "high_kick", "front_kick", "back_kick", "sweep",
            "step_forward", "step_back" };
        for (auto& n : names) for (auto& d : dirs) {
            auto p = d / (std::string(n) + ".bin");
            if (std::filesystem::exists(p)) { AnimationData a; a.name = n;
                if (a.load(p.string())) { animations_[n] = std::move(a); break; } } }
        if (animations_.count("fists1_stance_idle")) animations_["fists_idle"] = animations_["fists1_stance_idle"];
        std::printf("  Animations loaded: %zu\n", animations_.size());
    }
    void load_moves() {
        auto root = std::filesystem::path(asset_root_);
        std::string mp;
        for (auto& d : {root/"assets"/"animations", root/"animations"}) {
            auto p = d / "moves.xml"; if (std::filesystem::exists(p)) { mp = p.string(); break; } }
        if (mp.empty()) return;
        auto xml = read_text(mp); size_t pos = 0;
        while ((pos = xml.find("<Move ", pos)) != std::string::npos) {
            if (pos > 4 && xml.substr(pos-4, 4) == "<!--") { pos += 6; continue; }
            auto et = xml.find(">", pos); if (et == std::string::npos) break;
            auto tag = xml.substr(pos, et - pos); MoveDef m; m.name = xml_attr(tag, "Name");
            m.filename = xml_attr(tag, "FileName");
            auto me = xml.find("</Move>", pos); if (me == std::string::npos) { pos = et; continue; }
            if (!m.filename.empty()) moves_[m.name] = std::move(m); pos = me + 7; }
        std::printf("  Moves loaded: %zu\n", moves_.size());
    }
    
    // Update animation state and compute animated node positions
    void update_animation(uint32_t dt_ms) {
        anim_node_pos_.clear();
        auto it = animations_.find(current_anim_); if (it == animations_.end()) return;
        auto& anim = it->second; if (anim.frame_count == 0 || ordered_node_names_.empty()) return;
        int npi = -1; for (int i = 0; i < (int)ordered_node_names_.size(); ++i)
            if (ordered_node_names_[i] == "NPivot") { npi = i; break; }
        if (npi < 0) return;
        anim_time_ += (dt_ms / 1000.0f) * anim_speed_ / 30.0f;
        float ff = anim_time_ * 30.0f; int fi = (int)ff;
        if (anim_loop_) { if (anim.frame_count > 0) fi %= anim.frame_count; }
        else if (fi >= anim.frame_count) fi = anim.frame_count - 1;
        if (fi < 0) fi = 0; int ni = anim.frame_count > 0 ? ((fi+1) % anim.frame_count) : 0;
        float a = ff - (int)ff; if (a < 0) a = 0; if (a > 1) a = 1;
        float px0, py0, pz0, px1, py1, pz1;
        if (!anim.get_node_pos(fi, npi, px0, py0, pz0)) return;
        if (!anim.get_node_pos(ni, npi, px1, py1, pz1)) { px1 = px0; py1 = py0; }
        float npx = px0 + (px1-px0)*a, npy = py0 + (py1-py0)*a;
        if (current_anim_ == "step_forward" || current_anim_ == "step_back") {
            float d = npx - prev_npivot_x_; if (std::abs(d) < 50.0f) player_pos_x_ += d * 0.9f;
            prev_npivot_x_ = npx; } else { prev_npivot_x_ = npx; }
        auto pit = skeleton_nodes_.find("NPivot");
        float nry = pit != skeleton_nodes_.end() ? pit->second.y : 169.48f;
        for (int i = 0; i < (int)ordered_node_names_.size() && i < 67; ++i) {
            float x0, y0, z0, x1, y1, z1;
            if (!anim.get_node_pos(fi, i, x0, y0, z0)) continue;
            if (!anim.get_node_pos(ni, i, x1, y1, z1)) { x1 = x0; y1 = y0; }
            anim_node_pos_[ordered_node_names_[i]] = {x0+(x1-x0)*a - npx, y0+(y1-y0)*a - npy + nry};
        }
    }
    
    void play_animation(const std::string& name, bool loop = true) {
        if (animations_.count(name)) { current_anim_ = name; anim_time_ = 0.0f; anim_loop_ = loop; }
    }

    void load_hud_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/ for textures
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"panels"/"top",
                                      "batchPanelsTop");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"dojo",
                                      "batchButtonsDojo");
        }
        std::printf("  HUD textures loaded: %zu\n", hud_textures_.size());
    }

    void load_menu_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"screens",
                                      "batchButtonsMenuScreens");
        }
        // Move menu atlas textures into menu_textures_
        for (auto it = hud_textures_.begin(); it != hud_textures_.end(); ) {
            if (it->first.find("_normal") != std::string::npos ||
                it->first.find("_active") != std::string::npos ||
                it->first.find("_pushed") != std::string::npos ||
                it->first.find("_Normal") != std::string::npos ||
                it->first.find("_Active") != std::string::npos ||
                it->first.find("_Pushed") != std::string::npos) {
                menu_textures_[it->first] = std::move(it->second);
                it = hud_textures_.erase(it);
            } else {
                ++it;
            }
        }
        // Load scroll/roll textures for parchment menu UI
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            auto scroll_dir = base/"textures"/"scrolls"/"common";
            for (auto& name : {"MenuRoll_left", "MenuRoll_center", "MenuRoll_right",
                               "Roll_left", "Roll_center", "Roll_right",
                               "Paper_left", "Paper_right", "Shadow_roll"}) {
                auto path = scroll_dir / (std::string(name) + ".png");
                if (std::filesystem::exists(path)) {
                    auto data = read_file(path.string());
                    int w, h, ch;
                    auto* px = stbi_load_from_memory(
                        (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                    if (px) {
                        auto tex = std::make_unique<ren::Texture2D>();
                        tex->init_rgba(w, h, px);
                        stbi_image_free(px);
                        scroll_textures_[name] = std::move(tex);
                    }
                }
            }
        }
        std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                    menu_textures_.size(), scroll_textures_.size());
    }

    void load_texture_atlas_to_hud(
        const std::filesystem::path& dir, const std::string& atlas_name)
    {
        auto pp = dir / (atlas_name + ".plist");
        auto pn = dir / (atlas_name + ".png");
        if (!std::filesystem::exists(pp) || !std::filesystem::exists(pn)) return;
        auto result = plist::parse(read_text(pp.string()));
        if (!result) return;
        auto png_data = read_file(pn.string());
        // Use stb_image to decode the PNG so we can crop frames on the CPU.
        int aw, ah, ach;
        auto* atlas_px = stbi_load_from_memory(
            (const stbi_uc*)png_data.data(), (int)png_data.size(),
            &aw, &ah, &ach, 4);
        if (!atlas_px) return;
        for (auto& [name, idx] : result->name_index) {
            auto& frame = result->frames[idx];
            // All frames (rotated or not) are stored upright in the atlas.
            // Just crop directly using atlas dimensions.
            int fw = frame.atlas_w;
            int fh = frame.atlas_h;
            auto tex = std::make_unique<ren::Texture2D>();
            std::vector<std::uint8_t> px((size_t)fw * fh * 4);
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    int sx = frame.atlas_x + x;
                    int sy = frame.atlas_y + y;
                    if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                    int src_idx = (sy * aw + sx) * 4;
                    int dst_idx = (y * fw + x) * 4;
                    px[dst_idx+0] = atlas_px[src_idx+0];
                    px[dst_idx+1] = atlas_px[src_idx+1];
                    px[dst_idx+2] = atlas_px[src_idx+2];
                    px[dst_idx+3] = atlas_px[src_idx+3];
                }
            }
            tex->init_rgba(fw, fh, px.data());
            std::string n = name;
            if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
            hud_textures_[n] = std::move(tex);
        }
        stbi_image_free(atlas_px);
    }

    // ---------- HUD font ----------
    void load_hud_font() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> candidates = {
            root/"assets"/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"assets"/"1536"/"fonts"/"obelix.fnt",
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
        auto tex = std::make_unique<ren::Texture2D>();
        if (!tex->init_from_png((const uint8_t*)png_data.data(), png_data.size())) return;
        hud_font_tex_ = std::move(tex);
        std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                    fnt_path.c_str(), hud_font_->chars.size());
    }

    void render_text(const std::string& text, float x, float y,
                     float scale, ren::Color4B color) {
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
                renderer_->draw_textured_quad_screen(
                    *hud_font_tex_, px, py, pw, ph, u0, v0, u1, v1, color);
            }
            cx += ch.xadvance * scale;
        }
    }

    // ---------- HUD ----------
    void render_hud(plat::Platform& platform) {
        // Top panel background (real texture, tiled horizontally)
        auto panel_it = hud_textures_.find("Top_Panel");
        if (panel_it != hud_textures_.end()) {
            auto& tex = panel_it->second;
            float panel_h = 50.0f;
            float tile_w = panel_h * tex->width() / tex->height();
            float x = 0;
            float win_w = (float)platform.window_width();
            while (x < win_w) {
                float draw_w = std::min(tile_w, win_w - x);
                float u1 = draw_w / tile_w;
                renderer_->draw_textured_quad_screen(*tex, x, 0, draw_w, panel_h,
                                                     0, 0, u1, 1.0f);
                x += draw_w;
            }
        } else {
            ren::Color4B bar_bg{0, 0, 0, 180};
            renderer_->draw_filled_rect_screen(0, 0,
                (float)platform.window_width(), 50, bar_bg);
        }

        // Gold icon + amount
        auto gold_it = hud_textures_.find("gold");
        if (gold_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*gold_it->second, 10, 9, 32, 32);
        }
        render_text("72 450", 50, 15, 0.32f, {255, 240, 200, 255});

        // Energy icon + value
        auto energy_it = hud_textures_.find("energy");
        if (energy_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*energy_it->second, 180, 9, 32, 32);
        }
        render_text("5 / 5", 220, 15, 0.32f, {200, 230, 255, 255});

        // Level bar + level badge
        auto lvlbar_it = hud_textures_.find("Level_bar");
        if (lvlbar_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*lvlbar_it->second, 330, 15, 120, 20);
        }
        render_text("LVL 7", 460, 15, 0.30f, {255, 255, 255, 255});

        // Menu button (LEFT side, scroll/roll style)
        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;
        if (overlay_ != Overlay::Menu) {
            // Collapsed: scroll roll bar
            auto lit = scroll_textures_.find("MenuRoll_left");
            auto cit = scroll_textures_.find("MenuRoll_center");
            auto rit = scroll_textures_.find("MenuRoll_right");
            if (lit != scroll_textures_.end() && cit != scroll_textures_.end() &&
                rit != scroll_textures_.end()) {
                float cap_w = roll_h * lit->second->width() / lit->second->height();
                float min_roll_w = 130.0f;
                float center_w = min_roll_w - 2 * cap_w;
                renderer_->draw_textured_quad_screen(*lit->second, btn_x, btn_y, cap_w, roll_h);
                renderer_->draw_textured_quad_screen(*cit->second, btn_x + cap_w, btn_y, center_w, roll_h);
                renderer_->draw_textured_quad_screen(*rit->second, btn_x + cap_w + center_w, btn_y, cap_w, roll_h);
                render_text("MENU", btn_x + min_roll_w / 2 - 22, btn_y + 12, 0.22f,
                            {255, 240, 200, 255});
            } else {
                ren::Color4B bg{60, 40, 20, 230};
                renderer_->draw_filled_rect_screen(btn_x, btn_y, 120, roll_h, bg);
                render_text("MENU", btn_x + 40, btn_y + 12, 0.22f, {255, 240, 200, 255});
            }
        }

        // Bottom hint
        render_text("A/D - move    Space - hit    M - menu    T - dialog",
                    20, (float)(platform.window_height() - 40), 0.26f,
                    {200, 200, 200, 255});

        // Position label
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Pos: (%.0f, %.0f)",
                      player_pos_x_, player_pos_y_);
        render_text(buf, 20, (float)(platform.window_height() - 65), 0.26f,
                    {180, 180, 180, 255});
    }

    // ---------- Menu expanded (vertical scroll, matching original game) ----------
    // In the original game, the menu is a VERTICAL scroll on the left side.
    // Icons are stacked top-to-bottom in a single column.
    void render_menu_expanded(plat::Platform& platform) {
        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;

        auto lit = scroll_textures_.find("MenuRoll_left");
        auto cit = scroll_textures_.find("MenuRoll_center");
        auto rit = scroll_textures_.find("MenuRoll_right");

        if (lit == scroll_textures_.end() || cit == scroll_textures_.end() ||
            rit == scroll_textures_.end()) {
            ren::Color4B bg{60, 40, 20, 230};
            renderer_->draw_filled_rect_screen(btn_x, btn_y, 120, 400, bg);
            return;
        }

        auto& left_tex = lit->second;
        auto& center_tex = cit->second;
        auto& right_tex = rit->second;
        float cap_w = roll_h * left_tex->width() / left_tex->height();

        // Vertical layout: icons stacked top-to-bottom
        float icon_size = 48.0f;
        float icon_spacing = 6.0f;
        int n_items = 5;
        float paper_padding = 12.0f;
        float paper_w = icon_size + paper_padding * 2 + 20;  // narrow, tall
        float paper_h = n_items * (icon_size + icon_spacing) + paper_padding * 2;
        float center_w = paper_w - 2 * cap_w;

        // Roll bar (top, horizontal)
        renderer_->draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h);
        renderer_->draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h);
        renderer_->draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h);

        // Paper area (below roll, vertical)
        float paper_y = btn_y + roll_h - 3;
        ren::Color4B paper_bg{200, 170, 120, 245};
        renderer_->draw_filled_rect_screen(btn_x, paper_y, paper_w, paper_h, paper_bg);

        // Paper edges (top and bottom)
        auto pl_it = scroll_textures_.find("Paper_left");
        auto pr_it = scroll_textures_.find("Paper_right");
        if (pl_it != scroll_textures_.end()) {
            float pl_w = paper_w * pl_it->second->width() / pl_it->second->height();
            renderer_->draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_w);
        }
        if (pr_it != scroll_textures_.end()) {
            float pr_w = paper_w * pr_it->second->width() / pr_it->second->height();
            renderer_->draw_textured_quad_screen(*pr_it->second,
                btn_x, paper_y + paper_h - pr_w, pr_w, paper_w);
        }

        // Shadow below
        auto shadow_it = scroll_textures_.find("Shadow_roll");
        if (shadow_it != scroll_textures_.end()) {
            renderer_->draw_textured_quad_screen(*shadow_it->second,
                btn_x, paper_y + paper_h - 8, paper_w, 15);
        }

        // Menu icons (vertical stack) — 5 items matching original game
        const char* items[] = {"Dojo", "Map", "Shop", "Profile", "Settings"};
        float ix = btn_x + paper_padding + 10;
        float iy = paper_y + paper_padding;
        for (auto& name : items) {
            // Try different case patterns
            std::string tex_name = std::string(name) + "_normal";
            auto it = menu_textures_.find(tex_name);
            if (it == menu_textures_.end()) {
                it = menu_textures_.find(std::string(name) + "_Normal");
            }
            if (it != menu_textures_.end()) {
                // Get original dimensions and maintain aspect ratio
                int tex_w = it->second->width();
                int tex_h = it->second->height();
                float aspect = (float)tex_w / (float)tex_h;
                float draw_w = icon_size;
                float draw_h = icon_size;
                if (aspect > 1.0f) {
                    draw_h = icon_size / aspect;
                } else {
                    draw_w = icon_size * aspect;
                }
                float draw_x = ix + (icon_size - draw_w) * 0.5f;  // center horizontally
                float draw_y = iy + (icon_size - draw_h) * 0.5f;  // center vertically
                renderer_->draw_textured_quad_screen(*it->second, draw_x, draw_y,
                                                     draw_w, draw_h);
            }
            render_text(name, ix + icon_size + 5, iy + 8, 0.14f, {60, 40, 20, 255});
            iy += icon_size + icon_spacing;
        }
    }

    // ---------- Menu overlay ----------
    void render_menu_overlay(plat::Platform& platform) {
        // Dim background
        ren::Color4B dim{0, 0, 0, 160};
        renderer_->draw_filled_rect_screen(
            0, 0, (float)platform.window_width(), (float)platform.window_height(), dim);

        float panel_w = 480, panel_h = 420;
        float px = (platform.window_width() - panel_w) / 2.0f;
        float py = (platform.window_height() - panel_h) / 2.0f;
        ren::Color4B panel_bg{30, 30, 40, 240};
        renderer_->draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        ren::Color4B border{120, 90, 50, 255};
        renderer_->draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py, 3, panel_h, border);
        renderer_->draw_filled_rect_screen(px + panel_w - 3, py, 3, panel_h, border);

        render_text("MENU", px + panel_w/2 - 50, py + 30, 0.5f,
                    {255, 220, 120, 255});

        struct MenuItem { const char* label; const char* sub; };
        MenuItem items[] = {
            {"MAP",      "Travel to other locations"},
            {"SHOP",     "Buy weapons, armour, helmets"},
            {"SETTINGS", "Audio, graphics, controls"},
            {"SAVE",     "Save progress"},
            {"EXIT",     "Return to title screen"}
        };
        float by = py + 100;
        for (auto& it : items) {
            float bx = px + 30, bw = panel_w - 60, bh = 50;
            ren::Color4B btn_bg{60, 60, 80, 220};
            renderer_->draw_filled_rect_screen(bx, by, bw, bh, btn_bg);
            ren::Color4B btn_brd{100, 100, 130, 255};
            renderer_->draw_filled_rect_screen(bx, by, bw, 2, btn_brd);
            render_text(it.label, bx + 20, by + 10, 0.40f,
                        {255, 255, 255, 255});
            render_text(it.sub, bx + 120, by + 15, 0.28f,
                        {180, 180, 200, 255});
            by += 60;
        }
    }

    // ---------- Dialog overlay ----------
    void render_dialog_overlay(plat::Platform& platform) {
        float panel_w = (float)platform.window_width() - 100, panel_h = 140;
        float px = 50, py = platform.window_height() - panel_h - 60;
        ren::Color4B panel_bg{15, 15, 20, 230};
        renderer_->draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        ren::Color4B border{140, 100, 50, 255};
        renderer_->draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);

        render_text("SENSEI", px + 30, py + 15, 0.40f,
                    {255, 220, 120, 255});
        render_text("Welcome back, student.", px + 30, py + 55, 0.32f,
                    {230, 230, 230, 255});
        render_text("Train on the bag, then we will",
                    px + 30, py + 80, 0.32f, {230, 230, 230, 255});
        render_text("talk about your journey.",
                    px + 30, py + 105, 0.32f, {230, 230, 230, 255});

        ren::Color4B arrow{255, 220, 120, 255};
        float ax = px + panel_w - 30, ay = py + panel_h - 25;
        renderer_->draw_filled_rect_screen(ax, ay - 12, 12, 2, arrow);
        renderer_->draw_filled_rect_screen(ax, ay - 12, 2, 12, arrow);
        renderer_->draw_filled_rect_screen(ax + 10, ay - 12, 2, 12, arrow);
    }

private:
    plat::Platform* platform_ = nullptr;
    std::string asset_root_;
    std::unique_ptr<ren::Renderer> renderer_;

    GameState state_ = GameState::Loading;
    Overlay overlay_ = Overlay::None;
    uint32_t loading_timer_ = 0;
    float load_scale_ = 1.0f, zoom_ = 1.0f;
    std::vector<LoadingImg> loading_images_;

    std::unique_ptr<GameLocation> location_;
    std::unordered_map<std::string, AtlasRef> atlases_;
    std::unordered_map<std::string, SkelNode> skeleton_nodes_;
    std::unordered_map<std::string, SkelEdge> skeleton_edges_;
    std::vector<std::string> ordered_node_names_;
    std::unordered_map<std::string, MoveDef> moves_;
    std::string current_move_;
    bool bag_hit_ = false;
    float prev_npivot_x_ = 0.0f;
    std::unique_ptr<BodyModel> body_model_;
    std::unique_ptr<BodyModel> bag_model_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> hud_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> menu_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> scroll_textures_;

    std::shared_ptr<font::ParsedFont> hud_font_;
    std::unique_ptr<ren::Texture2D> hud_font_tex_;

    float player_pos_x_ = 0, player_pos_y_ = 0;
    float cam_x_ = 0, cam_y_ = 0;
    bool facing_right_ = true;
    int hit_anim_ = 0;    // ms remaining
    int bag_swing_ = 0;   // ms remaining
    bool quit_requested_ = false;
    
    // Animation state
    std::unordered_map<std::string, AnimationData> animations_;
    std::string current_anim_ = "fists_idle";
    float anim_time_ = 0.0f;  // seconds into current animation
    float anim_speed_ = 30.0f;  // FPS for animation playback
    bool anim_loop_ = true;
    
    // Animated node positions (override skeleton rest pose during animation)
    std::unordered_map<std::string, std::pair<float, float>> anim_node_pos_;  // name -> (x, y)
};

int main(int argc, char* argv[]) {
    std::string asset_root;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--assets" && i + 1 < argc) asset_root = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: resf2_app [--assets <path>]\n"); return 0;
        }
    }
    auto platform = std::make_unique<plat::GlfwPlatform>();
    plat::WindowConfig cfg;
    cfg.title = "reSF2 - Shadow Fight 2";
    cfg.width = 1280; cfg.height = 720; cfg.vsync = true;
    if (!platform->init(cfg)) {
        std::fprintf(stderr, "Platform init failed.\n"); return 1;
    }
    Game game(asset_root);
    if (!platform->make_gl_current()) {
        std::fprintf(stderr, "Failed to make GL context current.\n"); return 1;
    }
    game.on_init(*platform);
    auto last_ms = platform->now_ms();
    bool was_paused = false;
    while (true) {
        if (!platform->poll_events()) break;
        if (platform->should_quit()) break;
        if (game.quit_requested()) break;
        bool is_paused = platform->is_paused();
        if (is_paused && !was_paused) { game.on_pause(*platform); was_paused = true; }
        else if (!is_paused && was_paused) {
            game.on_resume(*platform); was_paused = false; last_ms = platform->now_ms();
        }
        if (is_paused) { platform->sleep_ms(100); continue; }
        auto now = platform->now_ms();
        auto dt = (std::min)(now > last_ms ? (uint32_t)(now - last_ms) : 0u, 200u);
        last_ms = now;
        game.on_update(*platform, dt);
        game.on_render(*platform);
        platform->swap_buffers();
    }
    game.on_shutdown(*platform);
    platform->shutdown();
    return 0;
}
