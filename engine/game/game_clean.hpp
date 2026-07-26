#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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
#include "engine/reverse/dz_reader.hpp"
#include "engine/scene/scene_system.hpp"
#include "engine/scene/scenes.hpp"
#include "engine/renderer/stb_image.h"
#include "engine/format/xml_doc.hpp"
#include "engine/format/stage_parser.hpp"
#include "engine/format/list_parser.hpp"
#include "engine/audio/audio.hpp"
#include "save.hpp"
#include "player.hpp"
#include "inventory.hpp"
#include "shop.hpp"
#include "location_manager.hpp"
#include "asset_manager.hpp"
#include "animation_player.hpp"
#include "combat.hpp"
#include "input_handler.hpp"

// Import commonly-used namespaces at file scope
// (helpers.cpp also uses these at file scope, so they must be here)
namespace plat = resf2::platform;
namespace rt = resf2::runtime;
namespace ren = resf2::renderer;
namespace fmt = resf2::format;
namespace aud = resf2::audio;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;
namespace scene = resf2::scene;
namespace save = resf2::save;
namespace player = resf2::player;
namespace inventory = resf2::inventory;
namespace shop = resf2::shop;

// ---------- Forward declarations for helper functions ----------
// These are defined in helpers.cpp and used by inline Game methods.
// They live at file scope (not in any namespace) for backward compatibility
// with the monolithic main.cpp they were extracted from.

std::vector<std::byte> read_file(const std::string& path);
std::string read_text(const std::string& path);
float tof(const std::string& s, float def = 0.0f);
int toi(const std::string& s, int def = 0);
std::filesystem::path get_exe_dir();
std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename);

// Debug log globals — defined in helpers.cpp
extern FILE* g_debug_log;
extern bool g_debug_log_enabled;
void debug_log_init(const std::string& path);
void debug_log(const char* fmt, ...);
void debug_log_close();

// All type definitions moved to types.hpp
#include "types.hpp"

namespace resf2::game {

// ---------- Game ----------
//
// The Game class is the SceneHost — it owns the SceneManager and implements
// the scene::SceneHost interface. Individual scenes (MainMenu, Battle, Map,
// etc.) call back into Game via the host interface to load assets, render
// the dojo, save progress, etc.

class Game final : public rt::IGame, public scene::SceneHost {
public:
    explicit Game(std::string asset_root, bool replay_mode = false, bool dump_state = false);
    ~Game() override;

    // Discover all location names by scanning the assets/locations/ directory.
    void discover_locations() {
        locations_.discover_locations(asset_root_);
    }

    const std::vector<std::string>& location_names() const { return locations_.location_names(); }
    size_t location_count() const { return locations_.location_names().size(); }

    void set_start_location(const std::string& name) {
        if (!name.empty()) {
            current_location_name_ = name;
            locations_.set_current_location_name(name);
            std::printf("[GAME] Start location set to: %s\n", name.c_str());
        }
    }

    void on_init(plat::Platform& platform) override;

void on_update(plat::Platform& platform, uint32_t dt) override;


void on_render(plat::Platform& platform) override;


void on_shutdown(plat::Platform&) override;


    bool quit_requested() const noexcept { return quit_requested_; }

    // ---------- scene::SceneHost implementation ----------
    //
    // These methods are called by the scenes (MainMenu, Battle, etc.) via
    // the SceneHost interface to interact with the game state.

void request_scene_transition(scene::SceneId to) override;


void host_load_location() override;


void host_reset_menu_state() override;


void host_load_battle_location(const std::string& location) override;


bool host_location_loaded() const noexcept override;


bool host_save_progress() override;


bool host_load_progress() override;


void host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) override;


const std::vector<std::pair<std::string, std::string>>& host_get_dialogue() const override;


void host_set_current_level(std::string level_id) override;


void host_add_completed_level(const std::string& level);


bool host_is_level_completed(const std::string& level) const;


std::string host_get_battle_result() const override;


const resf2::format::StageData* host_get_stages() const override;


void host_set_battle_location(std::string loc) override;


std::string host_get_battle_location() const override;


void host_set_battle_result(std::string result) override;


int host_get_currency() const override;


bool host_spend_currency(int amount) override;


void host_add_currency(int amount) override;


    // ---- Inventory / Shop ----

bool host_has_item(const std::string& item_id) const override;


std::vector<std::string> host_get_owned_items() const override;


std::string host_get_equipped(const std::string& slot) const override;


bool host_buy_item(const std::string& item_id) override;


bool host_sell_item(const std::string& item_id) override;


bool host_equip_item(const std::string& item_id) override;


bool host_unequip_item(const std::string& slot) override;


int host_get_player_level() const override;


int host_get_wins() const override;


int host_get_losses() const override;


const resf2::format::ListData* host_get_list_data() const override;


std::string host_get_current_level() const override;


    void host_add_win() override;

    void host_add_loss() override;

    // Sync the combat equipped_weapon_ from the inventory.
    // Called after loading save data and after equipping a weapon.
    void sync_equipped_weapon();

    // ---------- Audio hooks ----------

    void host_start_menu_music() override;

    void host_start_battle_music() override;

    void host_stop_music() override;

    void host_play_ui_click() override;

    void host_play_result_sound(const std::string& result) override;

    void host_render_text(const std::string& text, float x, float y, float scale, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) const override;

bool host_render_zone_bg(int zone_index, float x, float y, float w, float h) override;


void host_set_show_enemy(bool show) override;


void host_set_battle_mode(bool battle) override;


    // Access the current PlayerProfile (for tests and new code).
    const player::PlayerProfile& player_profile() const noexcept {
        return player_profile_;
    }

    // Called by MainMenuScene and BattleScene to update the dojo gameplay
    // (movement, combat, animation, physics, overlays).
    void host_update_gameplay(uint32_t dt);

    // Called by MainMenuScene and BattleScene to render the dojo scene
    // (background, character, bag, HUD, menu/dialog overlays).
void host_render_scene();


    // [ORIGINAL] Render the enemy using the SAME body_model (body.xml + head.xml)
    // as the player, with enemy-specific position/facing/animation state.
    // Full body + head model (capsules + triangles + skeleton edges).
    void render_enemy_fighter() {
        if (enemy_fighter_.is_dead || !assets_->body_model() || assets_->skeleton_nodes().empty()) return;
        auto np_it = assets_->skeleton_nodes().find("NPivot");
        if (np_it == assets_->skeleton_nodes().end()) return;
        float npivot_rest_y = np_it->second.y;
        float world_cx = enemy_pos_x_;
        float world_cy = enemy_pos_y_ + enemy_y_adjust_;
        // Enemy color: dark red (vs player black), white flash on hit, blue block
        ren::Color4B enemy_col = (enemy_hit_flash_ > 0) ?
            ren::Color4B{255, 180, 180, 255} : ren::Color4B{70, 30, 30, 255};
        if (enemy_fighter_.is_blocking) enemy_col = ren::Color4B{40, 40, 80, 255};
        // Get enemy animation frame
        std::string anim_name = enemy_anim_;
        if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
            anim_name = anim_name.substr(0, anim_name.size()-4);
        auto anim_it = assets_->animations().find(anim_name);
        int frame_idx = 0, next_idx = 0;
        float alpha = 0;
        bool has_anim = (anim_it != assets_->animations().end() && anim_it->second.frame_count > 0);
        if (has_anim) {
            auto& anim = anim_it->second;
            float f = enemy_anim_time_ * 20.0f;  // enemy uses fixed 20fps (no MoveDef mid_frames)
            if (f < 0) f = 0;
            int fi = (int)f;
            if (fi < 0) fi = 0;
            frame_idx = anim.frame_count > 0 ? fi % anim.frame_count : 0;
            next_idx = (frame_idx + 1) % anim.frame_count;
            alpha = f - (int)f;
        }
        // Compute animated NPivot (reference for all nodes — prevents stretching)
        float animated_npx = np_it->second.x, animated_npy = npivot_rest_y;
        if (has_anim) {
            for (int i = 0; i < (int)assets_->ordered_node_names().size() && i < 67; ++i) {
                if (assets_->ordered_node_names()[i] == "NPivot") {
                    float x0, y0, z0, x1, y1, z1;
                    if (anim_it->second.get_node_pos(frame_idx, i, x0, y0, z0) &&
                        anim_it->second.get_node_pos(next_idx, i, x1, y1, z1)) {
                        animated_npx = x0 + (x1 - x0) * alpha;
                        animated_npy = y0 + (y1 - y0) * alpha;
                    }
                    break;
                }
            }
        }
        // Build temp anim_node_pos for enemy
        std::unordered_map<std::string, std::pair<float, float>> enemy_node_pos;
        if (has_anim) {
            for (int i = 0; i < (int)assets_->ordered_node_names().size() && i < 67; ++i) {
                const std::string& name = assets_->ordered_node_names()[i];
                float x0, y0, z0, x1, y1, z1;
                if (anim_it->second.get_node_pos(frame_idx, i, x0, y0, z0) &&
                    anim_it->second.get_node_pos(next_idx, i, x1, y1, z1)) {
                    enemy_node_pos[name] = {x0 + (x1 - x0) * alpha, y0 + (y1 - y0) * alpha};
                }
            }
        }
        // Resolve enemy node to world coords (std::function for recursion)
        std::function<bool(const std::string&, float&, float&)> resolve = [&](const std::string& name, float& ox, float& oy) -> bool {
            float lx, ly;
            auto ait = enemy_node_pos.find(name);
            if (ait != enemy_node_pos.end()) {
                lx = ait->second.first; ly = ait->second.second;
            } else {
                auto sit = assets_->skeleton_nodes().find(name);
                if (sit != assets_->skeleton_nodes().end()) {
                    lx = sit->second.x; ly = sit->second.y;
                } else {
                    auto bit = assets_->body_model()->nodes.find(name);
                    if (bit != assets_->body_model()->nodes.end()) {
                        lx = bit->second.x; ly = bit->second.y;
                    } else {
                        auto mit = assets_->body_model()->macro_nodes.find(name);
                        if (mit != assets_->body_model()->macro_nodes.end()) {
                            float sum_lcc = 0, wxx = 0, wyy = 0;
                            for (int i = 0; i < 4; ++i) {
                                if (mit->second.children[i].empty()) continue;
                                float cx, cy;
                                if (!resolve(mit->second.children[i], cx, cy)) continue;
                                wxx += cx * mit->second.lcc[i];
                                wyy += cy * mit->second.lcc[i];
                                sum_lcc += mit->second.lcc[i];
                            }
                            if (std::abs(sum_lcc) > 1e-6f) {
                                ox = wxx / sum_lcc; oy = wyy / sum_lcc;
                                return true;
                            }
                            return false;
                        }
                        return false;
                    }
                }
            }
            float dx = lx - animated_npx;
            float dy = ly - animated_npy;
            ox = world_cx + (enemy_facing_right_ ? dx : -dx);
            oy = world_cy + dy;
            return true;
        };
        // Edge map
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->body_model()->edges) edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : assets_->skeleton_edges()) edge_map[name] = {e.end1, e.end2};
        // Render capsules
        for (auto& c : assets_->body_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            float x1, y1, x2, y2;
            if (!resolve(eit->second.first, x1, y1) || !resolve(eit->second.second, x2, y2)) continue;
            float mx1 = x1 + (x2 - x1) * c.margin1, my1 = y1 + (y2 - y1) * c.margin1;
            float mx2 = x2 - (x2 - x1) * c.margin2, my2 = y2 - (y2 - y1) * c.margin2;
            float r = (c.radius1 + c.radius2) * 0.5f;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2+px*ht, my2+py*ht,
                mx2-px*ht, my2-py*ht, enemy_col);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2-px*ht, my2-py*ht,
                mx1-px*ht, my1-py*ht, enemy_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, enemy_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, enemy_col);
        }

        // Render weapon capsule at hand if weapon model loaded
        if (assets_->enemy_weapon_model()) {
            ren::Color4B wcol{180, 155, 90, 255};
            if (enemy_hit_flash_ > 0) wcol = ren::Color4B{255, 255, 220, 255};
            float hx = 0, hy = 0;
            if (resolve("NHand_1", hx, hy) || resolve("NWrist_2", hx, hy) || resolve("NKnuckles_2", hx, hy)) {
                float dir = enemy_facing_right_ ? 1.0f : -1.0f;
                float ex = hx + dir * 30, ey = hy - 10;
                float ht = 6.0f;
                float dx = ex - hx, dy = ey - hy;
                float len = std::sqrt(dx*dx + dy*dy);
                if (len > 1.0f) {
                    float ux = dx / len, uy = dy / len;
                    float px = -uy, py = ux;
                    renderer_->draw_filled_triangle_world(hx+px*ht, hy+py*ht, ex+px*ht, ey+py*ht,
                        ex-px*ht, ey-py*ht, wcol);
                    renderer_->draw_filled_triangle_world(hx+px*ht, hy+py*ht, ex-px*ht, ey-py*ht,
                        hx-px*ht, hy-py*ht, wcol);
                    renderer_->draw_filled_circle_world(hx, hy, ht, wcol);
                    renderer_->draw_filled_circle_world(ex, ey, ht * 0.7f, wcol);
                }
            }
        }
        // Render skeleton edges with Radius (EHead, ENeck)
        for (auto& [ename, sedge] : assets_->skeleton_edges()) {
            if (sedge.radius <= 0) continue;
            bool has_capsule = false;
            for (auto& c : assets_->body_model()->capsules) {
                if (c.edge_name == ename) { has_capsule = true; break; }
            }
            if (has_capsule) continue;
            float x1, y1, x2, y2;
            if (!resolve(sedge.end1, x1, y1) || !resolve(sedge.end2, x2, y2)) continue;
            float r = sedge.radius;
            float mx1 = x1 + (x2 - x1) * sedge.margin1, my1 = y1 + (y2 - y1) * sedge.margin1;
            float mx2 = x2 - (x2 - x1) * sedge.margin2, my2 = y2 - (y2 - y1) * sedge.margin2;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) {
                renderer_->draw_filled_circle_world((mx1+mx2)*0.5f, (my1+my2)*0.5f, r, enemy_col);
                continue;
            }
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2+px*ht, my2+py*ht,
                mx2-px*ht, my2-py*ht, enemy_col);
            renderer_->draw_filled_triangle_world(mx1+px*ht, my1+py*ht, mx2-px*ht, my2-py*ht,
                mx1-px*ht, my1-py*ht, enemy_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, enemy_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, enemy_col);
        }
        // Render triangles (skip cloth-node AND MacroNode triangles)
        // [ORIGINAL] HEAD-Triangle references HEAD-MacroNode, which uses LCC
        // weights to compute position from skeleton children (NTop, NHeadF, etc.).
        // These LCC weights are calibrated for rest pose — when skeleton animates,
        // the weighted sum produces stretched/wrong positions. Without cloth
        // simulation, these triangles can't render correctly. Skip them.
        for (auto& t : assets_->body_model()->triangles) {
            auto is_non_skel = [&](const std::string& n) {
                return assets_->body_model()->nodes.count(n) > 0 ||
                       assets_->body_model()->macro_nodes.count(n) > 0;
            };
            if (is_non_skel(t.n1) || is_non_skel(t.n2) || is_non_skel(t.n3)) continue;
            float tx0, ty0, tx1, ty1, tx2, ty2;
            if (!resolve(t.n1, tx0, ty0) || !resolve(t.n2, tx1, ty1) ||
                !resolve(t.n3, tx2, ty2)) continue;
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, enemy_col);
        }
    }

    // Called by LoadingScene to render the loading screen.
    void host_render_loading();

private:
    // ---------- Loading screen ----------
    void load_loading_screen() {
        assets_->load_loading_screen(asset_root_,
            platform_->window_width(), platform_->window_height());
    }

    // Initialize the dojo location: load all assets and set up the scene.
    // Called by host_load_location() (SceneHost interface) when entering
    // MainMenu or Battle scene.
    void init_location();

    void render_loading_screen(plat::Platform& platform) {
        float tw = 1820.0f * load_scale_, th = 1024.0f * load_scale_;
        float ox = (platform.window_width() - tw) / 2.0f;
        float oy = (platform.window_height() - th) / 2.0f;
        for (auto& img : assets_->loading_images()) {
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
        locations_.load_location(name, asset_root_, assets_.get());
        location_ = locations_.location();
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
                // Decode atlas PNG for pre-cropping rotated frames
                int aw, ah, ach;
                auto* atlas_px = stbi_load_from_memory(
                    (const stbi_uc*)png_data.data(), (int)png_data.size(),
                    &aw, &ah, &ach, 4);
                auto tex = std::make_unique<ren::Texture2D>();
                if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                         png_data.size())) {
                    if (atlas_px) stbi_image_free(atlas_px);
                    continue;
                }
                AtlasRef a;
                a.texture = std::move(tex);
                a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
                // Pre-crop rotated frames into individual un-rotated textures
                if (atlas_px) {
                    for (auto& [fname, idx] : a.atlas->name_index) {
                        auto& frame = a.atlas->frames[idx];
                        if (!frame.rotated) continue;
                        // For rotated frames, atlas_w/atlas_h are ATLAS (post-rotation) dimensions.
                        // Original sprite dimensions are swapped.
                        int fw = frame.atlas_h;  // original width (swapped)
                        int fh = frame.atlas_w;  // original height (swapped)
                        auto ctex = std::make_unique<ren::Texture2D>();
                        std::vector<std::uint8_t> px((size_t)fw * fh * 4);
                        for (int y = 0; y < fh; ++y) {
                            for (int x = 0; x < fw; ++x) {
                                // Un-rotate 90° CCW (Cocos2d stores rotated 90° CW)
                                // Formula A (proven correct for location textures):
                                // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                                int sx = frame.atlas_x + (fh - 1 - y);
                                int sy = frame.atlas_y + x;
                                if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                                int src_idx = (sy * aw + sx) * 4;
                                int dst_idx = (y * fw + x) * 4;
                                px[dst_idx+0] = atlas_px[src_idx+0];
                                px[dst_idx+1] = atlas_px[src_idx+1];
                                px[dst_idx+2] = atlas_px[src_idx+2];
                                px[dst_idx+3] = atlas_px[src_idx+3];
                            }
                        }
                        ctex->init_rgba(fw, fh, px.data());
                        std::string n = fname;
                        if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                        a.cropped[n] = std::move(ctex);
                    }
                    stbi_image_free(atlas_px);
                }
                std::printf("  Atlas '%s': %zu frames, %zu pre-cropped\n",
                            name.c_str(), a.atlas->frames.size(), a.cropped.size());
                assets_->atlases()[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
    }

    // Recompute the world→viewport transform for the current location.
    //
    // [ORIGINAL] Location::load (ShadowFight2.s86 FUN_10144420) stores the
    // location box as +0x38 Width ("whole world width") and +0x3c Height
    // ("whole world height"). World units are location-atlas texels: every
    // <Image> in params.xml carries its source size in the same units. The
    // viewport therefore has to show exactly `Height` world units vertically —
    // the location's top layer draws mask rectangles (ClassName="pixel_1")
    // over everything outside that box so wider or taller screens stay clean.
    //
    // The previous code left zoom at 1.0, i.e. one world unit per screen
    // pixel, so on a 712 px tall window it showed 712 world units instead of
    // 560 — everything came out 1.27x too small and vertically off-centre.
    void update_camera() {
        cam_y_ = 0.0f;
        if (!location_ || location_->height <= 0.0f || !platform_) {
            zoom_ = 1.0f;
            cam_x_ = player_pos_x_;
            return;
        }
        const float vw = static_cast<float>(platform_->window_width());
        const float vh = static_cast<float>(platform_->window_height());

        // Vertical framing: show the full world height, centred on the origin.
        //
        // [HEURISTIC-TODO] This is the right order of magnitude but not yet the
        // original's exact frame. Two things are still unresolved and have to
        // be reversed out of Fight (ShadowFight2.s86 FUN_100b3860 — Location*
        // at +0x24c, embedded Camera at +0x250):
        //   1. the visible band may be narrower than Height — the location
        //      draws full-width mask rectangles (ClassName="pixel_1") at world
        //      y >= +226 and y <= -220, leaving 446 of the 560 units. Framing
        //      to that gap alone overshoots, so the rule involves Wall (305)
        //      and/or Floor (80) too.
        //   2. the fighter's model-space -> world-space mapping puts its feet
        //      at world ~-276 while player_pos_y - 96 says -189, so the sprite
        //      transform is off by ~87 units independently of the camera. That
        //      has to be fixed before the frame can be judged.
        zoom_ = vh / location_->height;

        // [HEURISTIC-TODO] Framing rule not yet confirmed against the binary.
        // The original drives its Camera object (ShadowFight2.s86 ctor
        // FUN_10070270, owned by Fight FUN_100b3860) through the animation
        // system — it has "Camera"/"Position" slots — so the exact follow law
        // still has to be reversed. Centring on the midpoint of the two
        // fighters reproduces the reference framing: with dojo's player at
        // world -290 and enemy at -7 it puts them at ~36% / ~64% of the
        // screen width, matching the original's first-launch screenshot.
        const float half_view_w = vw / (2.0f * zoom_);
        const float half_world_w = location_->width * 0.5f;
        float cx = (player_pos_x_ + enemy_pos_x_) * 0.5f;
        if (half_view_w >= half_world_w) {
            cx = 0.0f;
        } else {
            const float lo = -half_world_w + half_view_w;
            const float hi = half_world_w - half_view_w;
            cx = (cx < lo) ? lo : ((cx > hi) ? hi : cx);
        }
        cam_x_ = cx;
    }

    void render_location() {
        if (!location_) return;
        // Render ALL layers with parallax support.
        //
        // Coordinate system: params.xml uses the same coordinate system as
        // the player/enemy positions (Y-up, Y=0 near center). We render
        // images directly at their (img.x, img.y) positions.
        //
        // Parallax: layers with factor < 1 scroll slower than the camera.
        // parallax_shift = (1 - factor) * cam_x_ — shifts the layer's X
        // to create the illusion of depth.
        static bool loc_logged = false;
        for (auto& layer : location_->layers) {
            // Parallax: the layer's X position scrolls at `factor` of the camera speed.
            // factor=1.0 → layer moves with camera (foreground).
            // factor=0.5 → layer moves at half speed (appears further away).
            // factor=0.1 → layer barely moves (far background).
            // Implementation: shift the layer's X by -cam_x_ * (1 - factor).
            // When the camera moves right (cam_x_ increases), the layer shifts left
            // by (1-factor)*cam_x_, creating the parallax effect.
            float parallax_factor = layer.factor;
            if (parallax_factor <= 0.0f) parallax_factor = 1.0f;
            float parallax_shift = (1.0f - parallax_factor) * cam_x_;

                        if (!loc_logged) {
                std::printf("[LOC] layer: type=%d factor=%.2f atlas=%s images=%zu\n",
                            layer.type, layer.factor, layer.atlas_name.c_str(),
                            layer.images.size());
            }

            for (auto& img : layer.images) {
                if (!loc_logged) {
                    std::printf("[LOC]   img: cls='%s' x=%.0f y=%.0f w=%.0f h=%.0f color='%s'\n",
                                img.class_name.c_str(), img.x, img.y, img.w, img.h,
                                img.color.c_str());
                }
                if (img.class_name == "pixel_1" && !img.color.empty()) {
                    unsigned long col = std::stoul(img.color, nullptr, 16);
                    ren::Color4B c{
                        (std::uint8_t)((col>>16)&0xFF),
                        (std::uint8_t)((col>>8)&0xFF),
                        (std::uint8_t)(col&0xFF), 255};
                    auto it = assets_->atlases().find(img.atlas_name);
                    if (it == assets_->atlases().end()) {
                        // No atlas: render as a solid world-space rect.
                        float hw = (float)platform_->window_width()  / (2.0f * zoom_);
                        float hh = (float)platform_->window_height() / (2.0f * zoom_);
                        float left = cam_x_ - hw, right = cam_x_ + hw;
                        float bottom = cam_y_ - hh, top = cam_y_ + hh;
                        // params.xml uses Y-DOWN (Y=0 at top, positive Y = down).
                        // Our world is Y-UP (positive Y = up). Invert: world_y = -img.y
                        // Player at y=-93 in params → world y=+93 (above center). Correct.
                        // Floor at y=225 in params → world y=-225 (below center). Correct.
                        float world_x = img.x - parallax_shift;
                        float world_y = -img.y;
                        float sx = (world_x - img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float sy = (1.0f - (world_y - img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float ex = (world_x + img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float ey = (1.0f - (world_y + img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float x = std::min(sx, ex), y = std::min(sy, ey);
                        float w = std::abs(ex - sx), h = std::abs(ey - sy);
                        renderer_->draw_filled_rect_screen(x, y, w, h, c);
                    }
                    continue;
                }
                auto it = assets_->atlases().find(img.atlas_name);
                if (it == assets_->atlases().end()) {
                    // Atlas not found � render solid rect from location Color as fallback.
                    // Many locations lack atlas files; this prevents black screens.
                    // Atlas not found: render visible fallback so user can see layer structure.
                    // The location Color is often black, making it look like a parser bug.
                    // Use progressively lighter shades per image to show the layout.
                    {
                        float world_y = -img.y;
                        float world_x = img.x - parallax_shift;
                        float left = world_x - img.w / 2.0f;
                        float bottom = world_y - img.h / 2.0f;
                        float hw2 = (float)platform_->window_width() / (2.0f * zoom_);
                        float hh2 = (float)platform_->window_height() / (2.0f * zoom_);
                        float vis_left2 = cam_x_ - hw2, vis_right2 = cam_x_ + hw2;
                        float vis_bottom2 = cam_y_ - hh2, vis_top2 = cam_y_ + hh2;
                        if (left + img.w < vis_left2 || left > vis_right2 || bottom + img.h < vis_bottom2 || bottom > vis_top2) continue;
                        float sx = (left - vis_left2) / (vis_right2 - vis_left2) * platform_->window_width();
                        float sy = (1.0f - (bottom - vis_bottom2) / (vis_top2 - vis_bottom2)) * platform_->window_height();
                        float sw = img.w / (vis_right2 - vis_left2) * platform_->window_width();
                        float sh = img.h / (vis_top2 - vis_bottom2) * platform_->window_height();
                        // Use per-image Color from params.xml (<Image Color="RRGGBB" />) when available
                        uint8_t r = 100, g = 120, b = 160;
                        if (!img.color.empty()) {
                            unsigned long col = std::stoul(img.color, nullptr, 16);
                            r = (uint8_t)((col>>16)&0xFF);
                            g = (uint8_t)((col>>8)&0xFF);
                            b = (uint8_t)(col&0xFF);
                        } else if (location_ && !location_->color.empty()) {
                            unsigned long col = std::stoul(location_->color, nullptr, 16);
                            r = (uint8_t)((col>>16)&0xFF); if (r < 30) r = 60;
                            g = (uint8_t)((col>>8)&0xFF); if (g < 30) g = 80;
                            b = (uint8_t)(col&0xFF); if (b < 30) b = 100;
                        }
                        ren::Color4B c2{r, g, b, 200};
                        renderer_->draw_filled_rect_screen(sx, sy, sw, sh, c2);
                        // Draw a border to show individual image boundaries
                        ren::Color4B border{255, 255, 255, 60};
                        renderer_->draw_filled_rect_screen(sx, sy, sw, 2, border);
                        renderer_->draw_filled_rect_screen(sx, sy, 2, sh, border);
                        renderer_->draw_filled_rect_screen(sx + sw - 2, sy, 2, sh, border);
                        renderer_->draw_filled_rect_screen(sx, sy + sh - 2, sw, 2, border);
                    }
                    continue;
                }
                auto& atlas = it->second;
                if (!atlas.texture || !atlas.atlas) continue;
                auto fit = atlas.atlas->name_index.find(img.class_name + ".png");
                if (fit == atlas.atlas->name_index.end()) {
                    fit = atlas.atlas->name_index.find(img.class_name);
                    if (fit == atlas.atlas->name_index.end()) continue;
                }
                auto& frame = atlas.atlas->frames[fit->second];
                float img_off_x = (float)frame.offset_x;
                float img_off_y = (float)frame.offset_y;
                
                // For rotated frames, use pre-cropped un-rotated texture
                std::string crop_name = img.class_name;
                if (atlas.cropped.count(crop_name)) {
                    // Use pre-cropped texture (already un-rotated)
                    auto& ctex = atlas.cropped[crop_name];
                    float world_y = -img.y - img_off_y;
                    float world_x = img.x + img_off_x - parallax_shift;
                    float quad_w = img.w;
                    float quad_h = img.h;
                    float px = world_x - quad_w / 2.0f;
                    float py = world_y - quad_h / 2.0f;
                    renderer_->draw_textured_quad(*ctex, px, py,
                                                  quad_w, quad_h);
                    continue;
                }
                
                // Non-rotated frame: use atlas texture with UV mapping
                float tw = (float)atlas.atlas->metadata.texture_w;
                float th = (float)atlas.atlas->metadata.texture_h;
                float u0 = frame.atlas_x / tw;
                float v0 = frame.atlas_y / th;
                float u1 = (frame.atlas_x + frame.atlas_w) / tw;
                float v1 = (frame.atlas_y + frame.atlas_h) / th;
                float world_y = -img.y;
                float world_x = img.x + img_off_x - parallax_shift;
                float quad_w = img.w;
                float quad_h = img.h;
                float px = world_x - quad_w / 2.0f;
                float py = world_y - quad_h / 2.0f;
                renderer_->draw_textured_quad(*atlas.texture, px, py, quad_w, quad_h,
                                              u0, v0, u1, v1);
            }
        }
        loc_logged = true;
    }

    // ---------- Skeleton ----------
    void load_skeleton() {
        assets_->load_skeleton(asset_root_, current_location_name_);
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
        assets_->body_model() = std::make_unique<BodyModel>();
        assets_->parse_body_model_xml(xml, assets_->body_model().get(), "BODY-");
        // [ORIGINAL] Also load head.xml as part of the body model.
        // head.xml has HEAD- prefixed nodes/macronodes + BODY- prefixed edges
        // + HEAD-Triangle triangles. It references skeleton nodes NTop, NHeadF,
        // NHeadS_1, NHeadS_2 via MacroNodes. Without this, the character has
        // a capsule instead of a proper head model.
        auto head_candidates = model_paths(asset_root_, "head.xml");
        std::string head_path;
        for (const auto& p : head_candidates) {
            if (std::filesystem::exists(p)) { head_path = p.string(); break; }
        }
        if (!head_path.empty()) {
            auto head_xml = read_text(head_path);
            assets_->parse_body_model_xml(head_xml, assets_->body_model().get(), "HEAD-");
            std::printf("  head.xml loaded (merged into body model)\n");
        }
    }

    // [ORIGINAL] Parse a body/head model XML file into a BodyModel.
    // tag_prefix is "BODY-" for body.xml, "HEAD-" for head.xml.
    // Edges use "BODY-Edge" in both files; capsules use "Capsule_"; triangles
    // use "Type=\"Triangle\"" (prefix-agnostic). Nodes/MacroNodes use the prefix.
    void parse_body_model_xml(const std::string& xml, BodyModel* model, const std::string& tag_prefix) {
        fmt::XmlDocument doc;
        if (!doc.parse(xml)) {
            std::fprintf(stderr, "[body] xml_doc parse error: %s\n", doc.error().c_str());
            return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { std::printf("  body/head.xml: no <Scene>\n"); return; }

        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type == "Node") {
                    BodyNode n;
                    n.name = child.name;
                    n.x = tof(child.attr("X"));
                    n.y = tof(child.attr("Y"));
                    n.z = tof(child.attr("Z"));
                    model->nodes[n.name] = n;
                } else if (type == "MacroNode") {
                    BodyMacroNode mn;
                    mn.name = child.name;
                    mn.children[0] = child.attr("ChildNode1");
                    mn.children[1] = child.attr("ChildNode2");
                    mn.children[2] = child.attr("ChildNode3");
                    mn.children[3] = child.attr("ChildNode4");
                    mn.lcc[0] = tof(child.attr("LCC1"));
                    mn.lcc[1] = tof(child.attr("LCC2"));
                    mn.lcc[2] = tof(child.attr("LCC3"));
                    mn.lcc[3] = tof(child.attr("LCC4"));
                    model->macro_nodes[mn.name] = mn;
                }
            }
        }

        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                e.collisible = (child.attr("Collisible") == "1");
                model->edges.push_back(e);
            }
        }

        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                std::string type = child.attr("Type");
                if (type == "Capsule") {
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    model->capsules.push_back(c);
                } else if (type == "Triangle") {
                    BodyTriangle t;
                    t.n1 = child.attr("Node1");
                    t.n2 = child.attr("Node2");
                    t.n3 = child.attr("Node3");
                    model->triangles.push_back(t);
                }
            }
        }

        std::printf("  [%s] model: %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    tag_prefix.c_str(), model->nodes.size(), model->edges.size(),
                    model->capsules.size(), model->triangles.size());
    }

    // Resolve a node name to world coordinates (handles BodyNode, SkelNode, MacroNode).
    std::pair<float, float> resolve_body_node(const std::string& name,
                                              float world_cx, float world_cy,
                                              bool face_right, float pivot_local_y) {
        if (!assets_->body_model()) return {world_cx, world_cy};

        // Check if this node has an animated position (from .bin animation)
        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            float lx = ait->second.first, ly = ait->second.second;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }

        auto bit = assets_->body_model()->nodes.find(name);
        if (bit != assets_->body_model()->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto sit = assets_->skeleton_nodes().find(name);
        if (sit != assets_->skeleton_nodes().end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto mit = assets_->body_model()->macro_nodes.find(name);
        if (mit != assets_->body_model()->macro_nodes.end()) {
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
        if (!assets_->body_model()) return;
        auto pivot_it = assets_->skeleton_nodes().find("NPivot");
        float pivot_local_y = pivot_it != assets_->skeleton_nodes().end() ? pivot_it->second.y : stance_npivot_y_;

        // Y normalization: keep character at correct height.
        //
        // The .bin animation stores absolute node Y for all nodes.
        // anim_node_pos_[name].y = (abs_y - npivot_y + npivot_rest_y)
        // resolve_body_node: sy = world_cy + (ly - pivot_local_y)
        //
        // For NPivot: sy = world_cy (since ly = npivot_rest_y for NPivot)
        // So world_cy = NPivot world position.
        //
        // player_pos_y_ (-93) is the NPivot world position from params.xml.
        // y_adjust = 0 positions NPivot at player_pos_y_.
        //
        // But there's a +4 offset needed to align feet with floor surface:
        // Floor at world_y = -193 (layer_3 at y=225, height=64, surface = -225+32)
        // NPivot at -93. Feet (NToe) at -93 + (65.52 - 169.48) = -196.96
        // Floor surface at -193. Feet are 3.96 below surface.
        // y_adjust = +4 shifts everything up so feet are at -192.96 ≈ floor.
        //
        // For crouch: NPivot goes to 106.21 (down from 169.48).
        // NToe abs_y = 2.24 (stays at floor).
        // anim_node_pos_.y = (2.24 - 106.21 + 169.48) = 65.51
        // sy = (-93+4) + (65.51 - 169.48) = -89 - 103.97 = -192.97. ON FLOOR! ✓
        //
        // For jump: NPivot goes to 243.93 (up from 169.48).
        // NToe abs_y = 189.15 (feet go up).
        // anim_node_pos_.y = (189.15 - 243.93 + 169.48) = 114.70
        // sy = (-93+4) + (114.70 - 169.48) = -89 - 54.78 = -143.78
        // Floor at -193. Feet at -143.78 — 49 ABOVE floor! ✓ (character jumped up)
        //
        // For roll: NPivot goes to 20.11 (very low).
        // NToe abs_y = 0 (feet at floor).
        // anim_node_pos_.y = (0 - 20.11 + 169.48) = 149.37
        // sy = (-93+4) + (149.37 - 169.48) = -89 - 20.11 = -109.11
        // Floor at -193. Feet at -109 — 84 ABOVE floor! ✗ (character floating)
        //
        // Problem: roll has NPivot very low but feet at floor.
        // The formula sy = world_cy + abs_y - npivot_y gives:
        //   sy = -89 + 0 - 20.11 = -109.11 (wrong, should be -193)
        //
        // Wait: anim_node_pos_.y = (abs_y - npivot_y + npivot_rest_y)
        // sy = world_cy + (ly - pivot_local_y)
        //    = world_cy + (abs_y - npivot_y + npivot_rest_y - npivot_rest_y)
        //    = world_cy + abs_y - npivot_y
        // For roll: sy = -89 + 0 - 20.11 = -109.11. WRONG.
        //
        // But abs_y for NToe in roll = 0 (feet at floor in .bin).
        // npivot_y = 20.11. So abs_y - npivot_y = -20.11.
        // sy = world_cy - 20.11 = -89 - 20.11 = -109.11.
        //
        // For feet at floor (-193): world_cy = -193 + 20.11 = -172.89.
        // y_adjust = -172.89 - (-93) = -79.89.
        //
        // This is the lowest-node approach! But it doesn't work for jump
        // because lowest node stays low during jump.
        //
        // SOLUTION: Use NPivot-based y_adjust for jumps, lowest-node for rolls.
        // But we can't easily distinguish them.
        //
        // BETTER SOLUTION: The original game uses MoveInside alignment which
        // aligns a specific pivot node (NHeel_1 or NHeel_2) to the floor.
        // We should use NHeel_1 Y for alignment, not NPivot or lowest node.
        //
        // For now: use constant y_adjust = 4 (works for standing, crouch, jump).
        // Roll issue: character floats during roll, but roll is short (26 frames).
        // This is acceptable until we implement proper MoveInside alignment.
        // [ORIGINAL] MoveInside pipeline byte-verified (objdump on ShadowFight2.s86):
        //   Step 1 (fcn.10165c10): captures pivotID -> Model+0x58, node_array[pivotID] -> Model+0x5c
        //   Step 2 (fcn.10164c20): resolves new pivotID, calls fcn.10103690 (trivial accessor:
        //     return this+0x7c, 3 bytes), then fcn.10103e80(axis=2) — called ONCE in entire binary
        //   Step 3 (fcn.101661d0): reads Model[0xe8][axis=2][pivotID] (Vec3) via fcn.1028e490 (Vec3 copy)
        //   Post-Step3: playInfo copies Z->X and Z->Y (memcpy). All axes get same Vec3.
        //   fcn.1028e490 = Vec3 copy, fcn.1028e4c0 = Vec3 add, fcn.10102c70 = container accessor
        // [HEURISTIC-TODO] consumption formula (how Vec3 -> world transform) NOT yet traced.
        // The Vec3 from Step 3 is the per-axis displacement; how it is applied to produce
        // the render transform is unconfirmed. However, for Axis="X|Z" (ALL current player
        // moves), the PC sf2.js source shows dI=false -> Y = ShiftY, and ShiftY=0 for all
        // player moves. So this remains open only for hypothetical non-X|Z alignment.
        //
        // [ORIGINAL] MoveInside Y alignment — VERIFIED from PC version sf2.js.
        // See update_animation() for full documentation.
        // y_adjust = ShiftY = 0 for all Axis="X|Z" moves (verified from moves.xml).
        // y_adjust_smoothed_ is computed in update_animation() (before hit detection).
        // Here we just USE the already-computed value.
        float world_cx = player_pos_x_;
        float world_cy = player_pos_y_ + y_adjust_smoothed_;

        // Build edge lookup from both body.xml edges and skeleton.xml edges
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->body_model()->edges)
            edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : assets_->skeleton_edges())
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

        // Render character as unified dark silhouette.
        // Render ALL capsules (including duplicates — they overlap to fill gaps
        // at joints). Apply Margin1/Margin2 to trim ends properly.
        ren::Color4B silhouette_col{20, 20, 25, 255};

        for (auto& c : assets_->body_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                world_cx, world_cy, facing_right_, pivot_local_y);
            // Apply margin (trim capsule ends to prevent overlap artifacts)
            float m1 = c.margin1, m2 = c.margin2;
            float mx1 = x1 + (x2 - x1) * m1;
            float my1 = y1 + (y2 - y1) * m1;
            float mx2 = x2 - (x2 - x1) * m2;
            float my2 = y2 - (y2 - y1) * m2;
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = mx1 + px*ht, ay = my1 + py*ht;
            float bx = mx2 + px*ht, by = my2 + py*ht;
            float cx = mx2 - px*ht, cy_ = my2 - py*ht;
            float dx_ = mx1 - px*ht, dy_ = my1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            // Circle caps at both ends — fills gaps at joints
            renderer_->draw_filled_circle_world(mx1, my1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, silhouette_col);
        }

        // [ORIGINAL] Render skeleton edges that have a Radius but no capsule
        // in body.xml (e.g. EHead, ENeck). The original game renders these as
        // capsule-like shapes (thick lines with circle caps). skeleton.xml
        // defines <EHead Radius="12"> and <ENeck Radius="6"> — without this,
        // the character has NO HEAD (the body.xml capsules only cover torso/limbs).
        for (auto& [ename, sedge] : assets_->skeleton_edges()) {
            if (sedge.radius <= 0) continue;
            // Skip if this edge already has a capsule in body.xml
            bool has_capsule = false;
            for (auto& c : assets_->body_model()->capsules) {
                if (c.edge_name == ename) { has_capsule = true; break; }
            }
            if (has_capsule) continue;
            // Resolve endpoints
            auto it1 = assets_->skeleton_nodes().find(sedge.end1);
            auto it2 = assets_->skeleton_nodes().find(sedge.end2);
            if (it1 == assets_->skeleton_nodes().end() || it2 == assets_->skeleton_nodes().end()) continue;
            auto [x1, y1] = resolve_body_node(sedge.end1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(sedge.end2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            float r = sedge.radius;
            float m1 = sedge.margin1, m2 = sedge.margin2;
            float mx1 = x1 + (x2 - x1) * m1;
            float my1 = y1 + (y2 - y1) * m1;
            float mx2 = x2 - (x2 - x1) * m2;
            float my2 = y2 - (y2 - y1) * m2;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) {
                // Degenerate — draw as circle at midpoint
                renderer_->draw_filled_circle_world((mx1+mx2)*0.5f, (my1+my2)*0.5f, r, silhouette_col);
                continue;
            }
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = mx1 + px*ht, ay = my1 + py*ht;
            float bx = mx2 + px*ht, by = my2 + py*ht;
            float cx = mx2 - px*ht, cy_ = my2 - py*ht;
            float dx_ = mx1 - px*ht, dy_ = my1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            renderer_->draw_filled_circle_world(mx1, my1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, silhouette_col);
        }

        // Render triangles (small parts)
        // Skip triangles that reference non-animated nodes (BODY-Node entries
        // from body.xml). These are cloth simulation nodes that don't have
        // per-node animation data in the .bin files. Rendering them at their
        // rest-pose positions while other triangle vertices are animated
        // causes visible stretching on the legs (especially around the calves
        // and ankles where BODY-Triangle-7..10 are located).
        for (auto& t : assets_->body_model()->triangles) {
            // [ORIGINAL] Skip triangles that mix cloth nodes (BODY-Node/HEAD-Node)
            // with skeletal nodes. Cloth nodes (BODY-Node*) have rest-pose positions
            // in body.xml but NO per-frame animation data in .bin files — they're
            // physics-simulated (Verlet) in the original. Rendering them at rest
            // pose while other triangle vertices are animated causes severe
            // stretching (especially on legs/calves where BODY-Triangle 7-11
            // mix NAnkle/NKnee with BODY-Node*).
            //
            // Only render triangles where ALL 3 nodes are:
            //   - skeletal (in anim_node_pos_ or assets_->skeleton_nodes()), OR
            //   - MacroNodes (HEAD-MacroNode/BODY-MacroNode — these compute
            //     position from skeletal children via LCC weights, so they
            //     animate correctly)
            // [ORIGINAL] Skip triangles with cloth nodes (BODY-Node/HEAD-Node)
            // AND triangles with MacroNodes (HEAD-MacroNode/BODY-MacroNode).
            // MacroNodes use LCC weights calibrated for rest pose — when skeleton
            // animates, weighted sum produces stretched positions. Without cloth
            // simulation, only render triangles with pure skeletal nodes.
            auto is_non_skel = [&](const std::string& n) {
                return assets_->body_model()->nodes.count(n) > 0 ||
                       assets_->body_model()->macro_nodes.count(n) > 0;
            };
            if (is_non_skel(t.n1) || is_non_skel(t.n2) || is_non_skel(t.n3)) {
                continue;
            }
            auto can_resolve = [&](const std::string& n) {
                return anim_node_pos_.count(n) || assets_->skeleton_nodes().count(n);
            };
            if (!can_resolve(t.n1) || !can_resolve(t.n2) || !can_resolve(t.n3)) {
                continue;
            }
            auto [tx0, ty0] = resolve_body_node(t.n1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3,
                world_cx, world_cy, facing_right_, pivot_local_y);
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, silhouette_col);
        }

        // Render player's equipped weapon model (if loaded)
        // Weapon nodes are defined in their own model space; we render a simple
        // indicator at a fixed offset from the player's body center.
        if (assets_->weapon_model() && !assets_->weapon_model()->edges.empty()) {
            ren::Color4B wcol{200, 170, 100, 255};
            // Use NPivot position as the reference point for weapon placement
            float dir = facing_right_ ? 1.0f : -1.0f;
            float ox = world_cx + dir * 30.0f;
            float oy = world_cy + 10.0f;
            // Render weapon edges as simple lines/circles
            for (auto& e : assets_->weapon_model()->edges) {
                auto n1 = assets_->weapon_model()->nodes.find(e.end1);
                auto n2 = assets_->weapon_model()->nodes.find(e.end2);
                if (n1 == assets_->weapon_model()->nodes.end() || n2 == assets_->weapon_model()->nodes.end()) continue;
                float scale = 0.3f;
                float wx1 = ox + n1->second.x * scale;
                float wy1 = oy + n1->second.y * scale;
                float wx2 = ox + n2->second.x * scale;
                float wy2 = oy + n2->second.y * scale;
                float r = e.radius > 0 ? e.radius * scale : 3.0f;
                renderer_->draw_filled_circle_world(wx1, wy1, r, wcol);
                renderer_->draw_filled_circle_world(wx2, wy2, r * 0.7f, wcol);
            }
        }
    }

    // ---------- Character rendering ----------
    // Skeleton local coords: Y-UP (0 = feet, positive = up).
    // World coords: Y-UP (cocos2d convention, positive = up).
    //
    // Render ONLY the body silhouette (capsules + triangles).
    // The skeleton lines and joints are NOT rendered — they were causing
    // the "half black, half white squares" effect (white bones drawn over
    // dark silhouette). The original game renders only the silhouette.
    void render_character() {
        // Render body mesh (silhouette from capsules + triangles)
        render_body_model();
        // No skeleton lines, no joints — silhouette only.
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

        assets_->bag_model() = std::make_unique<BodyModel>();

        fmt::XmlDocument skel_doc;
        if (!skel_doc.parse(read_text(skel_path))) {
            std::fprintf(stderr, "[punching_bag] skel parse error: %s\n", skel_doc.error().c_str());
            init_bag_verlet();
            return;
        }
        auto* scene = skel_doc.root()->first_child("Scene");
        if (!scene) { std::printf("  skeleton_punching_bag.xml: no <Scene>\n"); init_bag_verlet(); return; }

        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type != "Node" && type != "CenterOfMass") continue;
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                n.attenuation = tof(child.attr("Attenuation"), 0.02f);
                n.cloth = (toi(child.attr("Cloth")) != 0);
                assets_->bag_model()->nodes[n.name] = n;
            }
        }

        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                e.collisible = (child.attr("Collisible") == "1");
                assets_->bag_model()->edges.push_back(e);
            }
        }

        if (!fig_path.empty()) {
            fmt::XmlDocument fig_doc;
            if (fig_doc.parse(read_text(fig_path))) {
                if (auto* fs = fig_doc.root()->first_child("Scene"); fs && (fs = fs->first_child("Figures"))) {
                    for (const auto& child : fs->children) {
                        if (child.attr("Type") != "Capsule") continue;
                        BodyCapsule c;
                        c.edge_name = child.attr("Edge");
                        c.radius1 = tof(child.attr("Radius1"));
                        c.radius2 = tof(child.attr("Radius2"));
                        c.margin1 = tof(child.attr("Margin1"));
                        c.margin2 = tof(child.attr("Margin2"));
                        assets_->bag_model()->capsules.push_back(c);
                    }
                }
            }
        }
        std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                    assets_->bag_model()->nodes.size(), assets_->bag_model()->edges.size(),
                    assets_->bag_model()->capsules.size());
        init_bag_verlet();
    }


    // ---------- Enemy weapon model ----------
    void load_enemy_weapon(const std::string& weapon_name) {
        auto candidates = model_paths(asset_root_, weapon_name.c_str());
        std::string fig_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        }
        if (fig_path.empty()) { std::printf("  Enemy weapon '%s' NOT FOUND!\n", weapon_name.c_str()); return; }
        assets_->enemy_weapon_model() = std::make_unique<BodyModel>();
        fmt::XmlDocument doc;
        if (!doc.parse(read_text(fig_path))) {
            std::fprintf(stderr, "[weapon] xml parse error: %s\n", doc.error().c_str());
            assets_->enemy_weapon_model().reset(); return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { assets_->enemy_weapon_model().reset(); return; }
        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type != "Node" && type != "CenterOfMass") continue;
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                n.attenuation = tof(child.attr("Attenuation"), 0.02f);
                assets_->enemy_weapon_model()->nodes[n.name] = n;
            }
        }
        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                assets_->enemy_weapon_model()->edges.push_back(e);
            }
        }
        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                if (child.attr("Type") != "Capsule") continue;
                BodyCapsule c;
                c.edge_name = child.attr("Edge");
                c.radius1 = tof(child.attr("Radius1"));
                c.radius2 = tof(child.attr("Radius2"));
                c.margin1 = tof(child.attr("Margin1"));
                c.margin2 = tof(child.attr("Margin2"));
                assets_->enemy_weapon_model()->capsules.push_back(c);
            }
        }
        std::printf("  Enemy weapon '%s': %zu nodes, %zu edges, %zu capsules\n",
                    weapon_name.c_str(), assets_->enemy_weapon_model()->nodes.size(),
                    assets_->enemy_weapon_model()->edges.size(), assets_->enemy_weapon_model()->capsules.size());
    }

    // Map weapon tactic name to model file path.
    // Tactic names like "Swords", "Axes", "Claws" map to "weapon_swords.xml" etc.
    // Returns empty string if no model file exists for this tactic.
    std::string weapon_tactic_to_model_file(const std::string& tactic) const {
        // Direct file name: tactic name lowercase + "s" for plurals
        std::string lower;
        for (char c : tactic) lower += (char)std::tolower(c);
        // Handle special mappings
        static const std::unordered_map<std::string, std::string> special = {
            {"Fists", ""},         // Unarmed — no weapon model
            {"TwoHanded", "weapon_composite_sword.xml"},
            {"BigSwords", "weapon_big_swords.xml"},
            {"CompositeSword", "weapon_composite_sword.xml"},
            {"CompositeSpear", "weapon_composite_spear.xml"},
            {"CompositeStaff", "weapon_composite_staff.xml"},
            {"CompositeScythe", "weapon_composite_scythe.xml"},
            {"GiantSword", "weapon_giant_sword.xml"},
            {"PowerFists", "weapon_power_fists.xml"},
            {"Glaivebow", "weapon_glaivebow.xml"},
            {"SilverGlaive", "weapon_silver_glaive.xml"},
            {"OneHandedSword", "weapon_one_handed_sword.xml"},
            {"NinjaSword", "weapon_ninja_sword.xml"},
            {"ShogunKatana", "weapon_katana.xml"},
            {"WandererStaff", "weapon_staff.xml"},
            {"TonfaGuns", "weapon_tonfa_guns.xml"},
            {"SharpTonfa", "weapon_sharp_tonfa.xml"},
            {"SteelClaws", "weapon_steel_claws.xml"},
            {"ShockerClaws", "weapon_shocker_claws.xml"},
            {"ButcherKnives", "weapon_butcher_knives.xml"},
            {"CrescentKnives", "weapon_crescent_knives.xml"},
            {"ElectroHammers", "weapon_electro_hammers.xml"},
            {"FireBatons", "weapon_fire_batons.xml"},
            {"BattleHammers", "weapon_battle_hammers.xml"},
            {"TwoHandedBlunt", "weapon_two_handed_cudgel.xml"},
            {"HermitSwords", "weapon_hermit_swords.xml"},
            {"Knobsticks", "weapon_knobsticks.xml"},
            {"MagariYari", "weapon_magari_yari.xml"},
            {"ShuangGou", "weapon_shuang_gou.xml"},
            {"ChineseSabers", "weapon_chinese_sabers.xml"},
            {"IndianKatar", "weapon_indian_katar.xml"},
            {"MonkKatars", "weapon_indian_katar.xml"},
            {"Shuriken", "weapon_knives.xml"},
            {"Kunai", "weapon_kunai.xml"},
            {"FireBall", "magic_fireball.xml"},
            {"Energyball", "magic_energy_ball.xml"},
            {"LightningArrow", "magic_lightning.xml"},
            {"MagicDeathRay", "magic_death_ray.xml"},
            {"MagicAsteroid", "magic_asteroid.xml"},
            {"MassBomb", "magic_mass_bomb.xml"},
            {"MagicBomb", "magic_mass_bomb.xml"},
            {"MagicFireAura", "magic_fire_aura.xml"},
            {"MagicAcidCloud", "magic_fire_aura.xml"},
            {"RootStun", "magic_root_stun.xml"},
            {"FirePillar", "magic_fire_pillar.xml"},
            {"Sawblade", "weapon_sawblade.xml"},
            {"DoubleScythe", "weapon_sectional_scythe.xml"},
        };
        auto it = special.find(tactic);
        if (it != special.end()) return it->second;
        // Generic: "weapon_<lowercased>.xml" — try common patterns
        // Handle s-ending (Swords → sword, Axes → axe, etc.)
        std::string try_name = "weapon_" + lower + ".xml";
        // Try with and without final 's'
        if (std::filesystem::exists(asset_root_ + "/assets/models/" + try_name)) return try_name;
        if (lower.size() > 1 && lower.back() == 's') {
            try_name = "weapon_" + lower.substr(0, lower.size()-1) + ".xml";
            if (std::filesystem::exists(asset_root_ + "/assets/models/" + try_name)) return try_name;
        }
        return try_name; // return best guess
    }

    // Load a weapon model for the player from a tactic name.
    // The weapon model is stored in assets_->weapon_model() for rendering.
    void load_player_weapon(const std::string& tactic) {
        std::string model_file = weapon_tactic_to_model_file(tactic);
        if (model_file.empty()) {
            assets_->weapon_model().reset();
            return;  // Fists — no weapon model
        }
        auto candidates = model_paths(asset_root_, model_file.c_str());
        std::string fig_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        }
        if (fig_path.empty()) {
            std::printf("  Player weapon '%s' model NOT FOUND (tried: %s)!\n",
                       tactic.c_str(), model_file.c_str());
            assets_->weapon_model().reset();
            return;
        }
        assets_->weapon_model() = std::make_unique<BodyModel>();
        fmt::XmlDocument doc;
        if (!doc.parse(read_text(fig_path))) {
            std::fprintf(stderr, "[weapon] parse error for %s: %s\n",
                        model_file.c_str(), doc.error().c_str());
            assets_->weapon_model().reset();
            return;
        }
        auto* scene = doc.root()->first_child("Scene");
        if (!scene) { assets_->weapon_model().reset(); return; }

        // Parse MacroNodes (weapons use MacroNode type, unlike body.xml which uses Node/COM)
        if (auto* ns = scene->first_child("Nodes")) {
            for (const auto& child : ns->children) {
                std::string type = child.attr("Type");
                if (type == "MacroNode") {
                    BodyMacroNode mn;
                    mn.name = child.name;
                    mn.children[0] = child.attr("ChildNode1");
                    mn.children[1] = child.attr("ChildNode2");
                    mn.children[2] = child.attr("ChildNode3");
                    mn.children[3] = child.attr("ChildNode4");
                    assets_->weapon_model()->macro_nodes[mn.name] = mn;
                }
                // Also store basic position info for rendering
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.mass = tof(child.attr("Mass"), 1.0f);
                n.fixed = (toi(child.attr("Fixed")) != 0);
                assets_->weapon_model()->nodes[n.name] = n;
            }
        }
        if (auto* es = scene->first_child("Edges")) {
            for (const auto& child : es->children) {
                if (child.attr("Type") != "Edge") continue;
                BodyEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                assets_->weapon_model()->edges.push_back(e);
            }
        }
        if (auto* fs = scene->first_child("Figures")) {
            for (const auto& child : fs->children) {
                if (child.attr("Type") == "Capsule") {
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    assets_->weapon_model()->capsules.push_back(c);
                }
            }
        }
        std::printf("  Player weapon '%s' (%s): %zu nodes, %zu edges, %zu capsules\n",
                    tactic.c_str(), model_file.c_str(),
                    assets_->weapon_model()->nodes.size(), assets_->weapon_model()->edges.size(),
                    assets_->weapon_model()->capsules.size());
    }

    // Initialize Verlet physics state from the bag's skeleton nodes.
    // Each node gets: position = (x, y), prev_position = (x, y) (at rest).
    // Fixed nodes (Fixed="1") have inv_mass = 0 and don't move.
    // Edges become distance constraints with rest length = edge.length.
    void init_bag_verlet() {
        if (!assets_->bag_model()) return;
        bag_verlet_.clear();
        bag_constraints_.clear();
        // World position of the bag's NPivot (where it hangs in the world)
        // Same coordinate system as player — no Y-invert, use params Y directly
        // with the same -45 offset to align with the floor.
        float bag_cx = location_ ? (location_->enemy_x - 983.0f) : 0.0f;
        float bag_cy = location_ ? (location_->enemy_y + 81.0f) : 0.0f;
        auto pit = assets_->bag_model()->nodes.find("NPivot");
        float pivot_ly = pit != assets_->bag_model()->nodes.end() ? pit->second.y : 109.0f;
        // Initialize nodes: world position = bag_center + (node_local - NPivot_local)
        for (auto& [name, n] : assets_->bag_model()->nodes) {
            VerletNode vn;
            vn.x = bag_cx + n.x * 1.0f;
            vn.y = bag_cy + (n.y - pivot_ly) * 1.0f;
            vn.px = vn.x;  // at rest, prev = current
            vn.py = vn.y;
            vn.mass = n.mass;
            vn.fixed = n.fixed;
            vn.inv_mass = n.fixed ? 0.0f : (n.mass > 0.001f ? 1.0f / n.mass : 1.0f);
            vn.attenuation = n.attenuation;
            bag_verlet_[name] = vn;
        }
        // Initialize constraints from edges
        for (auto& e : assets_->bag_model()->edges) {
            VerletConstraint c;
            c.n1 = e.end1;
            c.n2 = e.end2;
            // Compute rest length from actual node distance (or use edge.length)
            auto n1 = bag_verlet_.find(e.end1);
            auto n2 = bag_verlet_.find(e.end2);
            if (n1 != bag_verlet_.end() && n2 != bag_verlet_.end()) {
                float dx = n1->second.x - n2->second.x;
                float dy = n1->second.y - n2->second.y;
                c.length = std::sqrt(dx*dx + dy*dy);
            } else {
                c.length = e.length;
            }
            c.stiffness = 1.0f;
            bag_constraints_.push_back(c);
        }
        bag_verlet_init_ = true;
        std::printf("  Bag Verlet: %zu nodes, %zu constraints (Node12 fixed)\n",
                    bag_verlet_.size(), bag_constraints_.size());
    }

    // Apply an impulse to a bag node (called when hit).
    // Impulse = instantaneous velocity change = position offset added to prev pos.
    // In Verlet: vel = (pos - prev), so to add velocity v, set prev -= v * dt.
    void apply_bag_impulse(const std::string& node_name, float vx, float vy) {
        auto it = bag_verlet_.find(node_name);
        if (it == bag_verlet_.end()) return;
        auto& n = it->second;
        if (n.fixed) return;
        // Original Bl.strike: node.ma += impulse / node.weight
        // where ma = current position, weight = XML Mass attribute.
        // Modifying current position (x) directly — NOT prev (px).
        // In Verlet: x += delta => velocity += delta for next frame.
        n.x += vx * n.inv_mass;
        n.y += vy * n.inv_mass;
    }

    // Update bag Verlet physics.
    // 1. Integration: pos_new = 2*pos - prev + acc*dt^2 (gravity + damping)
    // 2. Satisfy constraints (multiple iterations for stiffness)
    // 3. Apply damping (attenuation)
    void update_bag_verlet(float dt) {
        if (!bag_verlet_init_ || !assets_->bag_model()) return;
        const float GRAVITY = -800.0f;  // downward acceleration (heavier bag)
        const int CONSTRAINT_ITERATIONS = 10;
        // 1. Verlet integration
        for (auto& [name, n] : bag_verlet_) {
            if (n.fixed) continue;
            // Verlet: new_pos = pos + (pos - prev) * (1 - attenuation) + acc * dt^2
            float vx = (n.x - n.px) * (1.0f - n.attenuation);
            float vy = (n.y - n.py) * (1.0f - n.attenuation);
            n.px = n.x;
            n.py = n.y;
            n.x += vx;
            n.y += vy + GRAVITY * dt * dt;
        }
        // 2. Satisfy distance constraints
        for (int iter = 0; iter < CONSTRAINT_ITERATIONS; ++iter) {
            for (auto& c : bag_constraints_) {
                auto n1 = bag_verlet_.find(c.n1);
                auto n2 = bag_verlet_.find(c.n2);
                if (n1 == bag_verlet_.end() || n2 == bag_verlet_.end()) continue;
                auto& a = n1->second;
                auto& b = n2->second;
                float dx = b.x - a.x;
                float dy = b.y - a.y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 0.0001f) continue;
                float diff = (dist - c.length) / dist;
                float w1 = a.inv_mass;
                float w2 = b.inv_mass;
                float wsum = w1 + w2;
                if (wsum < 0.0001f) continue;
                float f = c.stiffness * diff;
                a.x += dx * f * (w1 / wsum);
                a.y += dy * f * (w1 / wsum);
                b.x -= dx * f * (w2 / wsum);
                b.y -= dy * f * (w2 / wsum);
            }
        }
    }

    void render_punching_bag() {
        if (!assets_->bag_model() || !location_) return;
        
        // Bag position: enemy_x from params.xml, adjusted to world space
        float bag_cx = location_->enemy_x - 983.0f;
        
        // Bag NPivot Y in model space = 109.0
        // The bag hangs from Node12 (Y=335) which is fixed at ceiling
        // Node12 world Y should be at ceiling level.
        // Ceiling (layer_5) is at params y=-202 → world_y = +202 (inverted).
        // Node12 local Y = 335, NPivot local Y = 109.
        // Node12 world Y = bag_cy + (335 - 109) = bag_cy + 226
        // We need Node12 at world Y = 202 (ceiling):
        //   bag_cy + 226 = 202 → bag_cy = -24
        // enemy_y = -105. bag_cy = enemy_y + offset = -105 + 81 = -24. ✓
        //
        // BUT: the bag appears too high. The issue is that the player's
        // y_adjust_smoothed_ adds ~+82 units (REF_FEET_LY - ly_lowest),
        // making the player appear higher. The bag doesn't have this
        // adjustment, so it looks relatively higher.
        //
        // FIX: apply the same y_adjust to the bag's rendering Y, so the
        // bag and player are in the same coordinate space.
        auto pit = assets_->bag_model()->nodes.find("NPivot");
        float pivot_ly = pit != assets_->bag_model()->nodes.end() ? pit->second.y : 109.0f;
        float bag_cy = location_->enemy_y + 81.0f + y_adjust_smoothed_;
        
        // === BAG RENDERING (Verlet physics) ===
        // The bag's skeleton nodes are simulated with Verlet integration.
        // Node12 is fixed (ceiling attachment). Other nodes swing freely
        // subject to gravity + distance constraints (edges).
        // We render capsules using the current Verlet node positions.
        
        // Build edge lookup
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : assets_->bag_model()->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }
        
        // Render bag as unified silhouette (same approach as character)
        ren::Color4B bag_body_col{35, 35, 40, 255};      // dark neutral for bag body
        ren::Color4B bag_chain_col{160, 160, 160, 255};   // gray for chain
        
        for (auto& c : assets_->bag_model()->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto& en1 = eit->second.first;
            auto& en2 = eit->second.second;
            
            // Get node positions from Verlet state (if available) or fall back to rest pose
            float x1, y1, x2, y2;
            if (bag_verlet_init_) {
                auto v1 = bag_verlet_.find(en1);
                auto v2 = bag_verlet_.find(en2);
                if (v1 == bag_verlet_.end() || v2 == bag_verlet_.end()) continue;
                x1 = v1->second.x;
                y1 = v1->second.y;
                x2 = v2->second.x;
                y2 = v2->second.y;
            } else {
                auto nit1 = assets_->bag_model()->nodes.find(en1);
                auto nit2 = assets_->bag_model()->nodes.find(en2);
                if (nit1 == assets_->bag_model()->nodes.end() || nit2 == assets_->bag_model()->nodes.end()) continue;
                x1 = bag_cx + nit1->second.x * 1.0f;
                y1 = bag_cy + (nit1->second.y - pivot_ly) * 1.0f;
                x2 = bag_cx + nit2->second.x * 1.0f;
                y2 = bag_cy + (nit2->second.y - pivot_ly) * 1.0f;
            }
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            bool is_main = (c.radius1 >= 20 || c.radius2 >= 20);
            
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            
            ren::Color4B col = is_main ? bag_body_col : bag_chain_col;
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx_ = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx_, cy_, col);
            renderer_->draw_filled_triangle_world(ax, ay, cx_, cy_, dx_, dy_, col);
            renderer_->draw_filled_circle_world(x1, y1, ht, col);
            renderer_->draw_filled_circle_world(x2, y2, ht, col);
        }
    }

    // ---------- HUD textures (real game textures) ----------
    // ---------- Animation loading (DYNAMIC: scan directory) ----------
    void load_animations() {
        assets_->load_animations(asset_root_);
    }

    // ---------- Move definitions (from moves.xml) ----------
    void load_moves() {
        assets_->load_moves(asset_root_);
    }

    void update_animation(uint32_t dt_ms);
    void play_animation(const std::string& name, bool loop = true, int priority = 0);

    // Find the best matching move from moves.xml for the given input context.
    // Returns nullptr if no move matches. Sets candidate_count to the number
    // of valid candidates found.
    const MoveDef* find_best_move(
        const std::string& cur_direction,
        const std::string& cur_move_type,
        bool block_all_combat,
        bool in_attack,
        bool is_uninterrupt,
        bool past_attack_interval,
        int& candidate_count
    );

    void load_hud_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/ for textures
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"panels"/"top",
                                      "batchPanelsTop");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"dojo",
                                      "batchButtonsDojo");
            // [ORIGINAL] Load fight HUD textures: health bars, energy, timers.
            load_texture_atlas_to_hud(base/"textures"/"fight"/"bars",
                                      "batchFightBars");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"fight",
                                      "batchButtonsFight");
            // [ORIGINAL] Load hit effect textures: hit_blade (18-frame spark
            // animation), hit labels (Aggressive, Brutal, Critical, etc.)
            load_texture_atlas_to_hud(base/"textures"/"effects"/"fight",
                                      "hit_blade");
            load_texture_atlas_to_hud(base/"textures"/"fight"/"hits",
                                      "hitBatch");
        }
        std::printf("  HUD textures loaded: %zu\n", assets_->hud_textures().size());
    }

    void load_menu_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"screens",
                                      "batchButtonsMenuScreens");
        }
        // Move menu atlas textures into assets_->menu_textures()
        for (auto it = assets_->hud_textures().begin(); it != assets_->hud_textures().end(); ) {
            if (it->first.find("_normal") != std::string::npos ||
                it->first.find("_active") != std::string::npos ||
                it->first.find("_pushed") != std::string::npos ||
                it->first.find("_Normal") != std::string::npos ||
                it->first.find("_Active") != std::string::npos ||
                it->first.find("_Pushed") != std::string::npos) {
                assets_->menu_textures()[it->first] = std::move(it->second);
                it = assets_->hud_textures().erase(it);
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
                        assets_->scroll_textures()[name] = std::move(tex);
                    }
                }
            }
        }
        std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                    assets_->menu_textures().size(), assets_->scroll_textures().size());
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
            // Handle rotated frames:
            // For rotated frames, do NOT swap dimensions (atlas_w/atlas_h are original).
            // The atlas region has swapped dimensions, but we create the texture
            // at original dimensions.
            int fw = frame.atlas_w;
            int fh = frame.atlas_h;
            auto tex = std::make_unique<ren::Texture2D>();
            std::vector<std::uint8_t> px((size_t)fw * fh * 4);
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    int sx, sy;
                    if (frame.rotated) {
                        // Un-rotate 90° CCW (no swap, formula A):
                        // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                        sx = frame.atlas_x + (fh - 1 - y);
                        sy = frame.atlas_y + x;
                    } else {
                        sx = frame.atlas_x + x;
                        sy = frame.atlas_y + y;
                    }
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
            assets_->hud_textures()[n] = std::move(tex);
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
        assets_->hud_font() = std::make_shared<font::ParsedFont>(std::move(*result));
        auto png_data = read_file(png_path);
        auto tex = std::make_unique<ren::Texture2D>();
        if (!tex->init_from_png((const uint8_t*)png_data.data(), png_data.size())) return;
        assets_->hud_font_tex() = std::move(tex);
        std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                    fnt_path.c_str(), assets_->hud_font()->chars.size());
    }

    // [ORIGINAL] Load sound effects from the original mobile assets.
    // SF2 sounds are in assets/sounds/*.wav (16-bit PCM, Marmalade s3eAudio).
    // We load key combat sounds: punches, kicks, hits, bodyfalls, blocks.
    void load_sounds() {
        auto& eng = aud::AudioEngine::instance();
        eng.init();  // defaults to NullAudioBackend (no OpenAL yet)
        auto root = std::filesystem::path(asset_root_);
        // Search paths for sounds (mobile APK layout + extracted layout)
        std::vector<std::filesystem::path> sound_dirs = {
            root/"assets"/"assets"/"sounds",
            root/"assets"/"sounds",
            root/"sounds",
        };
        std::filesystem::path sound_dir;
        for (const auto& d : sound_dirs) {
            if (std::filesystem::exists(d)) { sound_dir = d; break; }
        }
        if (sound_dir.empty()) {
            std::printf("[audio] sounds dir not found\n");
            return;
        }
        // [ORIGINAL] Key SF2 sound files (from assets/sounds/):
        // f_pl_attack1-4.wav — player punch/kick attack swings
        // bodyfall1/3.wav — body hit ground
        // armor.wav — armor hit
        // coin_hit1-4.wav — coin pickup
        // disk.wav, energy_flask5.wav — pickups
        std::vector<std::string> needed = {
            "f_pl_attack1", "f_pl_attack2", "f_pl_attack3", "f_pl_attack4",
            "bodyfall1", "bodyfall3", "armor", "coin_hit1", "disk", "energy_flask5",
            "buy", "f_cough"
        };
        int loaded = 0;
        for (const auto& name : needed) {
            auto path = sound_dir / (name + ".wav");
            if (std::filesystem::exists(path)) {
                if (eng.load_sound_file(name, path.string())) loaded++;
            }
        }
        std::printf("[audio] Loaded %d/%zu sounds from %s\n",
                    loaded, needed.size(), sound_dir.string().c_str());
    }

    // Play a sound by name (no-op if not loaded or backend is null)
    void play_sound(const std::string& name, float volume = 1.0f) {
        aud::AudioEngine::instance().play(name, volume, false);
    }

    void render_text(const std::string& text, float x, float y,
                     float scale, ren::Color4B color) {
        if (!assets_->hud_font() || !assets_->hud_font_tex()) return;
        float cx = x;
        for (char c : text) {
            std::int32_t cp = (std::uint8_t)c;
            if (cp >= 0xC0 && cp <= 0xFF) cp = 0x0410 + (cp - 0xC0);
            auto it = assets_->hud_font()->char_index.find(cp);
            if (it == assets_->hud_font()->char_index.end()) {
                it = assets_->hud_font()->char_index.find(32);
                if (it == assets_->hud_font()->char_index.end()) continue;
            }
            auto& ch = assets_->hud_font()->chars[it->second];
            if (ch.width > 0 && ch.height > 0) {
                float u0 = (float)ch.x / assets_->hud_font()->common.scale_w;
                float v0 = (float)ch.y / assets_->hud_font()->common.scale_h;
                float u1 = (float)(ch.x + ch.width) / assets_->hud_font()->common.scale_w;
                float v1 = (float)(ch.y + ch.height) / assets_->hud_font()->common.scale_h;
                float px = cx + ch.xoffset * scale;
                float py = y + ch.yoffset * scale;
                float pw = ch.width * scale;
                float ph = ch.height * scale;
                renderer_->draw_textured_quad_screen(
                    *assets_->hud_font_tex(), px, py, pw, ph, u0, v0, u1, v1, color);
            }
            cx += ch.xadvance * scale;
        }
    }

    // ---------- HUD ----------
    void render_hud(plat::Platform& platform) {
        // Top panel background (real texture, tiled horizontally)
        auto panel_it = assets_->hud_textures().find("Top_Panel");
        if (panel_it != assets_->hud_textures().end()) {
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
        auto gold_it = assets_->hud_textures().find("gold");
        if (gold_it != assets_->hud_textures().end()) {
            renderer_->draw_textured_quad_screen(*gold_it->second, 10, 9, 32, 32);
        }
        render_text("72 450", 50, 15, 0.32f, {255, 240, 200, 255});

        // Energy icon + value
        auto energy_it = assets_->hud_textures().find("energy");
        if (energy_it != assets_->hud_textures().end()) {
            renderer_->draw_textured_quad_screen(*energy_it->second, 180, 9, 32, 32);
        }
        render_text("5 / 5", 220, 15, 0.32f, {200, 230, 255, 255});

        // Level bar + level badge
        auto lvlbar_it = assets_->hud_textures().find("Level_bar");
        if (lvlbar_it != assets_->hud_textures().end()) {
            renderer_->draw_textured_quad_screen(*lvlbar_it->second, 330, 15, 120, 20);
        }
        render_text("LVL 7", 460, 15, 0.30f, {255, 255, 255, 255});

        // [ORIGINAL] Dojo is a TRAINING area — NO health bars, NO victory/defeat.
        // Health bars only appear in real fights (map battles). In Dojo, the
        // player practices moves against a training dummy (bag or enemy fighter).
        // The enemy fighter in Dojo is a sparring partner, not a real opponent.
        // B key toggles between punching bag and enemy fighter.
        if (total_frame_count_ < 360) {  // ~6 seconds at 60fps
            uint8_t hint_alpha = (total_frame_count_ < 300) ? 200 :
                (uint8_t)(200 * (360 - total_frame_count_) / 60);
            std::string hint = "WASD move | O punch | P kick | S+D roll | B toggle enemy";
            float hint_w = hint.size() * 8.0f * 0.3f;
            render_text(hint, ((float)platform.window_width() - hint_w) / 2.0f,
                (float)platform.window_height() - 60.0f, 0.3f,
                {220, 220, 220, hint_alpha});
        }
        // Menu button (LEFT side, scroll/roll style)
        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;
        // Compute menu animation progress (smoothstep easing)
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);
        // Show collapsed roll when menu is closed OR animating
        if (menu_eased < 0.99f) {
            // Collapsed: scroll roll bar — sized to fit "MENU" text
            auto lit = assets_->scroll_textures().find("MenuRoll_left");
            auto cit = assets_->scroll_textures().find("MenuRoll_center");
            auto rit = assets_->scroll_textures().find("MenuRoll_right");
            if (lit != assets_->scroll_textures().end() && cit != assets_->scroll_textures().end() &&
                rit != assets_->scroll_textures().end()) {
                float cap_w = roll_h * lit->second->width() / lit->second->height();
                // Measure "MENU" text width at scale 0.22
                float text_w = 0.0f;
                if (assets_->hud_font()) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = assets_->hud_font()->char_index.find(cp);
                        if (it != assets_->hud_font()->char_index.end()) {
                            text_w += assets_->hud_font()->chars[it->second].xadvance * 0.22f;
                        }
                    }
                }
                float roll_w = text_w + 2 * cap_w + 16.0f;  // text + caps + padding
                float center_w = roll_w - 2 * cap_w;
                // Fade out the collapsed roll as menu expands
                float alpha = 1.0f - menu_eased;
                ren::Color4B roll_col{255, 255, 255, (uint8_t)(alpha * 255)};
                renderer_->draw_textured_quad_screen(*lit->second, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*cit->second, btn_x + cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*rit->second, btn_x + cap_w + center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
                // Center "MENU" text on the roll
                ren::Color4B text_col{255, 240, 200, (uint8_t)(alpha * 255)};
                float text_x = btn_x + (roll_w - text_w) / 2.0f;
                // Measure actual text height for vertical centering
                float text_h = 0.0f;
                if (assets_->hud_font()) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = assets_->hud_font()->char_index.find(cp);
                        if (it != assets_->hud_font()->char_index.end()) {
                            text_h = std::max(text_h, (float)assets_->hud_font()->chars[it->second].height * 0.22f);
                        }
                    }
                }
                float text_y = btn_y + (roll_h - text_h) / 2.0f;
                render_text("MENU", text_x, text_y, 0.22f, text_col);
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
    // The scroll "unrolls" from top to bottom with a smooth animation.
    void render_menu_expanded(plat::Platform& /*platform*/) {
        // Compute eased animation progress
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);  // smoothstep
        if (menu_eased < 0.01f) return;  // nothing to render

        // Dark backdrop behind expanded scroll menu for contrast
        float ww = (float)platform_->window_width();
        float wh = (float)platform_->window_height();
        ren::Color4B backdrop{0, 0, 0, (uint8_t)(menu_eased * 120)};
        if (backdrop.a > 5)
            renderer_->draw_filled_rect_screen(0, 0, ww, wh, backdrop);

        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;

        auto lit = assets_->scroll_textures().find("MenuRoll_left");
        auto cit = assets_->scroll_textures().find("MenuRoll_center");
        auto rit = assets_->scroll_textures().find("MenuRoll_right");

        if (lit == assets_->scroll_textures().end() || cit == assets_->scroll_textures().end() ||
            rit == assets_->scroll_textures().end()) {
            ren::Color4B bg{60, 40, 20, 230};
            renderer_->draw_filled_rect_screen(btn_x, btn_y, 120, 400 * menu_eased, bg);
            return;
        }

        auto& left_tex = lit->second;
        auto& center_tex = cit->second;
        auto& right_tex = rit->second;
        float cap_w = roll_h * left_tex->width() / left_tex->height();

        // Vertical layout: icons stacked top-to-bottom
        float icon_size = 56.0f;  // larger icons to match original game
        float icon_spacing = 8.0f;
        int n_items = 5;
        float paper_padding = 14.0f;
        float paper_w = icon_size + paper_padding * 2 + 30;  // wider for text labels
        float full_paper_h = n_items * (icon_size + icon_spacing) + paper_padding * 2;
        // Animate paper height: scroll unrolls from top to bottom
        float paper_h = full_paper_h * menu_eased;
        float center_w = paper_w - 2 * cap_w;

        // Roll bar (top, horizontal) — sized to fit paper width
        float roll_alpha = (menu_eased > 0.05f) ? 1.0f : menu_eased / 0.05f;
        ren::Color4B roll_col{255, 255, 255, (uint8_t)(roll_alpha * 255)};
        renderer_->draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);

        // Paper area (below roll, vertical) — clips to animated height
        float paper_y = btn_y + roll_h - 3;
        ren::Color4B paper_bg{200, 170, 120, 245};
        renderer_->draw_filled_rect_screen(btn_x, paper_y, paper_w, paper_h, paper_bg);

        // Paper edges (top and bottom)
        auto pl_it = assets_->scroll_textures().find("Paper_left");
        auto pr_it = assets_->scroll_textures().find("Paper_right");
        if (pl_it != assets_->scroll_textures().end()) {
            float pl_w = paper_w * pl_it->second->width() / pl_it->second->height();
            renderer_->draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_w);
        }
        if (pr_it != assets_->scroll_textures().end() && menu_eased > 0.95f) {
            // Only show bottom edge when fully expanded
            float pr_w = paper_w * pr_it->second->width() / pr_it->second->height();
            renderer_->draw_textured_quad_screen(*pr_it->second,
                btn_x, paper_y + paper_h - pr_w, pr_w, paper_w);
        }

        // Shadow below (only when fully expanded)
        auto shadow_it = assets_->scroll_textures().find("Shadow_roll");
        if (shadow_it != assets_->scroll_textures().end() && menu_eased > 0.9f) {
            renderer_->draw_textured_quad_screen(*shadow_it->second,
                btn_x, paper_y + paper_h - 8, paper_w, 15);
        }

        // Menu icons (vertical stack) — only render icons that fit within the animated height
        // All icons rendered with uniform scaling: scale = icon_size / max_texture_dimension
        // This ensures all icons appear the same size on screen while preserving aspect ratio.
        const char* items[] = {"Dojo", "Map", "Shop", "Profile", "Settings"};
        // Find max texture dimension across all icons for uniform scaling
        int max_tex_dim = 1;
        for (auto& name : items) {
            std::string tex_name = std::string(name) + "_normal";
            auto it = assets_->menu_textures().find(tex_name);
            if (it == assets_->menu_textures().end()) {
                it = assets_->menu_textures().find(std::string(name) + "_Normal");
            }
            if (it != assets_->menu_textures().end()) {
                max_tex_dim = std::max(max_tex_dim, std::max(it->second->width(), it->second->height()));
            }
        }
        float uniform_scale = icon_size / (float)max_tex_dim;
        float ix = btn_x + paper_padding + 10;
        float iy = paper_y + paper_padding;
        for (int idx = 0; idx < 5; ++idx) {
            float icon_y = iy + idx * (icon_size + icon_spacing);
            // Skip icons that haven't been revealed yet (below the unrolled height)
            if (icon_y + icon_size > paper_y + paper_h) break;

            auto& name = items[idx];
            // Try different case patterns for the texture name
            std::string tex_name = std::string(name) + "_normal";
            auto it = assets_->menu_textures().find(tex_name);
            if (it == assets_->menu_textures().end()) {
                it = assets_->menu_textures().find(std::string(name) + "_Normal");
            }
            if (it != assets_->menu_textures().end()) {
                // Uniform scale: all icons scaled by same factor, preserving aspect ratio
                float draw_w = it->second->width() * uniform_scale;
                float draw_h = it->second->height() * uniform_scale;
                // Center within the icon_size × icon_size slot
                float draw_x = ix + (icon_size - draw_w) * 0.5f;
                float draw_y = icon_y + (icon_size - draw_h) * 0.5f;
                renderer_->draw_textured_quad_screen(*it->second, draw_x, draw_y,
                                                     draw_w, draw_h);
                if (!loc_icons_logged) {
                    std::printf("[MENU] icon '%s': tex %dx%d → draw %.0fx%.0f (scale=%.2f)\n",
                                name, it->second->width(), it->second->height(),
                                draw_w, draw_h, uniform_scale);
                }
            }
            render_text(name, ix + icon_size + 5, icon_y + 10, 0.16f, {60, 40, 20, 255});
        }
        loc_icons_logged = true;
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

    // --- Module instances ---
    // Combat system: owns all combat/fighter/AI state.
    // Game member variables below reference this via mutable accessors.
    Combat combat_;
    // Animation player: owns animation state (interpolation, node positions, root motion).
    // Game member variables below reference this via mutable accessors.
    AnimationPlayer anim_player_;

    // --- Scene management ---
    scene::SceneManager scene_manager_;
    std::string current_location_name_ = "dojo";
    resf2::game::LocationManager locations_;
    bool location_loaded_ = false;

    // --- Dialogue / story state ---
    std::vector<std::pair<std::string, std::string>> dialogue_lines_;
    size_t dialogue_index_ = 0;

    // --- Level / progress state ---
    std::string current_level_;
    std::string battle_location_;
    std::string battle_result_;  // "victory" / "defeat" / ""
    std::vector<std::string> completed_levels_;
    int currency_ = 1000;  // starting gold
    int player_wins_ = 0;
    int player_losses_ = 0;
    resf2::format::ListData list_data_;
    bool list_data_loaded_ = false;

    // Persistence: SaveManager writes/reads the save file on disk.
    // PlayerProfile holds the authoritative player state (synced on save/load).
    resf2::save::SaveManager save_manager_;
    resf2::player::PlayerProfile player_profile_;

    // Inventory and Shop
    resf2::inventory::Inventory inventory_;
    resf2::shop::ShopManager shop_manager_;

    // stage_data_ and stages_loaded_ live in AssetManager (assets_)

    // --- Combat state (owned by combat_ module) ---
    // All combat/fighter/AI state lives in Combat.
    // Reference aliases below make existing code work without changes.
    FighterState& player_fighter_ = combat_.player_fighter();
    FighterState& enemy_fighter_ = combat_.enemy_fighter();
    float& enemy_ai_timer_ = combat_.mutable_enemy_ai_timer();
    float& enemy_ai_decision_interval_ = combat_.mutable_enemy_ai_decision_interval();
    int& enemy_ai_state_ = combat_.mutable_enemy_ai_state();
    float& enemy_attack_cooldown_ = combat_.mutable_enemy_attack_cooldown();
    float& player_hit_flash_ = combat_.mutable_player_hit_flash();
    float& enemy_hit_flash_ = combat_.mutable_enemy_hit_flash();
    float& combo_timer_ = combat_.mutable_combo_timer();

    // [ORIGINAL] Hit effect: uses original hit_blade texture (18-frame spark
    // animation from assets/1536/textures/effects/fight/hit_blade.plist).
    // The original SF2 renders this sprite at the hit point, cycling through
    // frames 1-18 over ~0.3s, then removing it. Falls back to colored circles
    // if the texture is not loaded.
    // HitSpark type lives in types.hpp (namespace resf2::game).
    std::vector<HitSpark> hit_sparks_;
    void spawn_hit_sparks(float x, float y, int /*count*/ = 1) {
        // [ORIGINAL] Spawn a single hit_blade effect at the hit point.
        // count is ignored — the original uses ONE animated sprite, not N circles.
        HitSpark s;
        s.x = x + ((float)(std::rand() % 20) - 10.0f);
        s.y = y + ((float)(std::rand() % 20) - 10.0f);
        s.age = 0;
        s.lifetime = 0.36f;  // 18 frames at 50fps ≈ 0.36s
        s.scale = 0.8f + (float)(std::rand() % 40) / 100.0f;
        hit_sparks_.push_back(s);
    }
    void update_and_render_hit_sparks(float dt_sec) {
        for (auto& s : hit_sparks_) s.age += dt_sec;
        hit_sparks_.erase(std::remove_if(hit_sparks_.begin(), hit_sparks_.end(),
            [](const HitSpark& s) { return s.age >= s.lifetime; }), hit_sparks_.end());
        for (const auto& s : hit_sparks_) {
            float t = s.age / s.lifetime;
            // hit_blade has 18 frames (hit_blade_1.png .. hit_blade_18.png)
            int frame = (int)(t * 18.0f) + 1;
            if (frame > 18) frame = 18;
            std::string tex_name = "hit_blade_" + std::to_string(frame);
            auto it = assets_->hud_textures().find(tex_name);
            if (it != assets_->hud_textures().end()) {
                // Draw the hit_blade sprite at the hit point (world space)
                float sz = 80.0f * s.scale;
                renderer_->draw_textured_quad(*it->second,
                    s.x - sz/2, s.y - sz/2, sz, sz, 0, 0, 1, 1);
            } else {
                // Fallback: colored circle (only if texture not loaded)
                float radius = (3.0f + t * 8.0f) * s.scale;
                uint8_t alpha = (uint8_t)(255 * (1.0f - t));
                ren::Color4B c{255, (uint8_t)(180 + std::rand() % 75),
                               (uint8_t)(40 + std::rand() % 80), alpha};
                renderer_->draw_filled_circle_world(s.x, s.y, radius, c);
            }
        }
    }

    // --- Enemy skeleton fighter state ---
    // State owned by combat_ module; access via mutable references.
    float& enemy_pos_x_ = combat_.mutable_enemy_pos_x();
    float& enemy_pos_y_ = combat_.mutable_enemy_pos_y();
    float& enemy_anim_time_ = combat_.mutable_enemy_anim_time();
    std::string& enemy_anim_ = combat_.mutable_enemy_anim();
    bool& enemy_facing_right_ = combat_.mutable_enemy_facing_right();
    float& enemy_y_adjust_ = combat_.mutable_enemy_y_adjust();
    bool& enemy_attacking_ = combat_.mutable_enemy_attacking();
    float& enemy_attack_duration_ = combat_.mutable_enemy_attack_duration();
    bool& is_battle_mode_ = combat_.mutable_is_battle_mode();
    bool& show_enemy_ = combat_.mutable_show_enemy();

    // ---------- Weapon system ----------
    // Currently equipped weapon type. Used to filter moves by tactic_weapon.
    // Move selection only allows moves whose tactic_weapon matches this value.
    std::string equipped_weapon_ = "Fists";
    // Cycle list for weapon switching (J key cycles, N key to previous)
    std::vector<std::string> weapon_cycle_list_ = {
        "Fists", "Swords", "Axes", "Claws", "Knuckles", "Daggers",
        "Katana", "Spear", "Staff", "Glaive", "TwoHanded", "CompositeSword",
        "CompositeSpear", "CompositeStaff", "CompositeScythe",
        "BigSwords", "Sai", "Tonfa", "Fans", "Kusarigama", "Nunchaku",
        "NinjaSword", "Sickles", "Batons", "Knobsticks",
        "Rifle", "GiantSword", "PowerFists", "Machete",
        "FireBall", "Energyball", "LightningArrow", "Shuriken"
    };
    int weapon_cycle_index_ = 0;

    // Check if a move is allowed for the currently equipped weapon.
    // Returns true if the move has no tactic_weapon requirement,
    // or if the tactic_weapon matches the equipped weapon (substring match
    // supports pipe-delimited lists like "Swords|ShuangGou|ChineseSabers").
    bool is_weapon_allowed(const MoveDef& move) const {
        if (move.tactic_weapon.empty()) return true;
        std::string haystack = "|" + move.tactic_weapon + "|";
        return haystack.find("|" + equipped_weapon_ + "|") != std::string::npos;
    }

    // Cycle equipped weapon forward or backward.
    void cycle_weapon(int direction) {
        if (weapon_cycle_list_.empty()) return;
        weapon_cycle_index_ = (weapon_cycle_index_ + direction) % (int)weapon_cycle_list_.size();
        if (weapon_cycle_index_ < 0) weapon_cycle_index_ += (int)weapon_cycle_list_.size();
        equipped_weapon_ = weapon_cycle_list_[weapon_cycle_index_];
        // Load the weapon model for the new weapon
        load_player_weapon(equipped_weapon_);
        std::printf("[WEAPON] Switched to: %s (index %d/%zu)\n",
                    equipped_weapon_.c_str(), weapon_cycle_index_, weapon_cycle_list_.size());
    }

    // ---------- Projectile system (magic/ranged) ----------
    struct Projectile {
        float x, y;
        float vx, vy;
        float lifetime = 2.0f;      // seconds remaining
        float age = 0;              // seconds since launch
        float damage = 15.0f;
        float radius = 8.0f;
        std::string type;           // "FireBall", "Energyball", "MagicDeathRay", "Shuriken", etc.
        bool from_player = true;
        bool active = true;
        uint8_t r = 255, g = 100, b = 50;  // color
    };
    std::vector<Projectile> projectiles_;

    // Spawn a projectile from the player (or enemy) toward the target.
    void spawn_projectile(const std::string& magic_type, float from_x, float from_y,
                          bool facing_right, bool from_player = true) {
        Projectile p;
        p.x = from_x + (facing_right ? 40.0f : -40.0f);
        p.y = from_y + 10.0f;
        float speed = 400.0f;
        p.vx = facing_right ? speed : -speed;
        p.vy = 0.0f;
        p.type = magic_type;
        p.from_player = from_player;
        p.lifetime = 2.0f;
        p.age = 0;

        // Color by magic type
        if (magic_type == "FireBall") { p.r = 255; p.g = 100; p.b = 50; p.damage = 20; p.radius = 10; }
        else if (magic_type == "Energyball") { p.r = 100; p.g = 200; p.b = 255; p.damage = 25; p.radius = 12; }
        else if (magic_type == "LightningArrow") { p.r = 255; p.g = 255; p.b = 0; p.damage = 30; p.radius = 6; p.vy = -30; }
        else if (magic_type == "MagicDeathRay") { p.r = 200; p.g = 50; p.b = 255; p.damage = 35; p.radius = 14; }
        else if (magic_type == "MagicAsteroid") { p.r = 150; p.g = 80; p.b = 20; p.damage = 40; p.radius = 16; p.vy = -100; }
        else if (magic_type == "MassBomb" || magic_type == "MagicBomb") { p.r = 255; p.g = 50; p.b = 50; p.damage = 50; p.radius = 18; }
        else if (magic_type == "Iceball") { p.r = 150; p.g = 200; p.b = 255; p.damage = 20; p.radius = 9; }
        else if (magic_type == "MagicFireAura") { p.r = 255; p.g = 150; p.b = 50; p.damage = 10; p.radius = 20; }
        else if (magic_type == "RootStun") { p.r = 50; p.g = 200; p.b = 50; p.damage = 5; p.radius = 12; p.vy = -50; }
        else if (magic_type == "Shuriken") { p.r = 200; p.g = 200; p.b = 200; p.damage = 12; p.radius = 5; }
        else if (magic_type == "Rifle" || magic_type == "Blaster") { p.r = 255; p.g = 255; p.b = 200; p.damage = 18; p.radius = 4; speed = 600; p.vx = facing_right ? speed : -speed; }
        else { p.r = 200; p.g = 100; p.b = 200; p.damage = 15; p.radius = 8; }

        projectiles_.push_back(p);
        std::printf("[PROJECTILE] Fired %s (%.0f,%.0f) v=(%.0f,%.0f)\n",
                   magic_type.c_str(), p.x, p.y, p.vx, p.vy);
    }

    // Update all active projectiles (movement, lifetime, hit detection)
    void update_projectiles(float dt_sec) {
        // Heuristic: which projectile types to auto-fire on attack
        static const std::vector<std::string> projectile_types = {
            "FireBall", "Energyball", "LightningArrow", "MagicDeathRay",
            "MagicAsteroid", "MassBomb", "MagicBomb", "Iceball",
            "MagicFireAura", "RootStun", "Shuriken", "Rifle", "Blaster"
        };

        // Auto-spawn: if the current move's tactic_weapon indicates a projectile weapon,
        // and we're at the attack interval start, spawn a projectile.
        // This is a simplified heuristic — the original game uses dedicated
        // magic/ranged move templates with projectile spawning events.
        if (!current_move_.empty() && hit_this_interval_ == false) {
            auto mit = assets_->moves().find(current_move_);
            if (mit != assets_->moves().end() && mit->second.is_attack) {
                // Check if equipped weapon is a projectile type
                bool is_projectile = false;
                for (auto& pt : projectile_types) {
                    if (equipped_weapon_.find(pt) != std::string::npos) {
                        is_projectile = true;
                        break;
                    }
                }
                if (is_projectile) {
                    // Check attack interval timing from the move's Interval list
                    for (auto& iv : mit->second.intervals) {
                        if (iv.type != "Attack" && iv.name != "Attack") continue;
                        float anim_progress = anim_time_ * anim_fps_;
                        if (anim_progress >= iv.start && anim_progress <= iv.start + 1.0f) {
                            spawn_projectile(equipped_weapon_, player_pos_x_, player_pos_y_,
                                            facing_right_, true);
                            hit_this_interval_ = true;  // prevent re-fire same interval
                            break;
                        }
                    }
                }
            }
        }

        // Update existing projectiles
        for (auto& p : projectiles_) {
            if (!p.active) continue;
            p.x += p.vx * dt_sec;
            p.y += p.vy * dt_sec;
            p.age += dt_sec;
            p.lifetime -= dt_sec;
            if (p.lifetime <= 0) { p.active = false; continue; }

            // Hit detection against enemy (player projectiles)
            if (p.from_player) {
                float dist = std::sqrt(std::pow(p.x - enemy_pos_x_, 2) +
                                       std::pow(p.y - (enemy_pos_y_ + enemy_y_adjust_), 2));
                if (dist < 60.0f && !enemy_fighter_.is_dead) {
                    // Hit!
                    enemy_fighter_.health -= p.damage;
                    if (enemy_fighter_.health < 0) enemy_fighter_.health = 0;
                    enemy_hit_flash_ = 0.15f;
                    p.active = false;
                    // Add hit spark
                    HitSpark spark;
                    spark.x = p.x; spark.y = p.y;
                    spark.age = 0; spark.lifetime = 0.3f;
                    spark.scale = 1.0f + p.damage / 30.0f;
                    hit_sparks_.push_back(spark);
                    std::printf("[PROJECTILE] Hit! damage=%.0f enemy_hp=%.0f\n",
                               p.damage, enemy_fighter_.health);
                    if (enemy_fighter_.health <= 0) {
                        enemy_fighter_.is_dead = true;
                        std::printf("[COMBAT] Enemy defeated by %s!\n", equipped_weapon_.c_str());
                    }
                }
            }
        }
        // Clean up inactive projectiles
        projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
            [](const Projectile& p) { return !p.active; }), projectiles_.end());
    }

    // Render active projectiles as colored circles
    void render_projectiles() {
        for (auto& p : projectiles_) {
            if (!p.active) continue;
            float pulse_scale = 1.0f + 0.1f * std::sin(p.age * 15.0f);
            float r = p.radius * pulse_scale;
            renderer_->draw_filled_circle_world(p.x, p.y, r,
                ren::Color4B{p.r, p.g, p.b, 200});
            // Glow effect (larger, transparent)
            if (r > 4.0f) {
                renderer_->draw_filled_circle_world(p.x, p.y, r * 1.5f,
                    ren::Color4B{p.r, p.g, p.b, 80});
            }
        }
    }

    Overlay overlay_ = Overlay::None;
    float menu_anim_progress_ = 0.0f;  // 0 = collapsed, 1 = fully expanded
    bool loc_icons_logged = false;  // one-shot diagnostic for menu icon sizes
    float load_scale_ = 1.0f, zoom_ = 1.0f;
    resf2::game::GameLocation* location_ = nullptr;

    float player_pos_x_ = 0, player_pos_y_ = 0;
    float cam_x_ = 0, cam_y_ = 0;
    bool facing_right_ = true;
    uint32_t& hit_anim_ = combat_.mutable_hit_anim();  // ms remaining
    uint32_t& step_cooldown_ = combat_.mutable_step_cooldown();
    bool& step_active_ = combat_.mutable_step_active();
    uint32_t& step_duration_ = combat_.mutable_step_duration();
    float& step_start_x_ = combat_.mutable_step_start_x();
    float& step_displacement_ = combat_.mutable_step_displacement();
    int& bag_swing_ = combat_.mutable_bag_swing();
    bool& hit_this_interval_ = combat_.mutable_hit_this_interval();
    float& bag_swing_dir_ = combat_.mutable_bag_swing_dir();
    // Physics-based pendulum state for the punching bag.
    // The bag hangs from Node12 (fixed ceiling point) and swings as a pendulum.
    // On hit: an impulse is applied to bag_angle_vel_.
    // Each frame: spring restoring force + damping + integration.
    float& bag_angle_ = combat_.mutable_bag_angle();
    float& bag_angle_vel_ = combat_.mutable_bag_angle_vel();
    // Verlet physics state for the punching bag.
    // The original game uses Verlet integration for the bag's skeleton.
    // Each node has position + previous position. Edges are distance constraints.
    // Fixed nodes (Node12 = ceiling attachment) don't move.
    std::unordered_map<std::string, VerletNode> bag_verlet_;
    std::vector<VerletConstraint> bag_constraints_;
    bool bag_verlet_init_ = false;
    bool quit_requested_ = false;
    
    // Animation state (owned by anim_player_ module)
    // Reference aliases below make existing code work without changes.
    std::string& current_anim_ = anim_player_.mutable_current_anim();
    float& anim_time_ = anim_player_.mutable_anim_time();
    float& anim_speed_ = anim_player_.mutable_anim_speed();
    bool& anim_loop_ = anim_player_.mutable_anim_loop();
    float& anim_fps_ = anim_player_.mutable_anim_fps();
    std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos_ = anim_player_.mutable_anim_node_pos();
    float& anim_root_dx_ = anim_player_.mutable_anim_root_dx();
    float& anim_root_dy_ = anim_player_.mutable_anim_root_dy();
    float& anim_root_anchor_x_ = anim_player_.mutable_anim_root_anchor_x();
    float& anim_root_anchor_y_ = anim_player_.mutable_anim_root_anchor_y();
    bool& anim_anchor_set_ = anim_player_.mutable_anim_anchor_set();
    float& prev_npivot_x_ = anim_player_.mutable_prev_npivot_x();
    bool& prev_npivot_set_ = anim_player_.mutable_prev_npivot_set();
    float& prev_npivot_y_ = anim_player_.mutable_prev_npivot_y();
    bool& prev_npivot_y_set_ = anim_player_.mutable_prev_npivot_y_set();
    int& prev_frame_idx_ = anim_player_.mutable_prev_frame_idx();
    float& jump_y_offset_ = anim_player_.mutable_jump_y_offset();
    float& prev_root_offset_ = anim_player_.mutable_prev_root_offset();
    float& committed_root_x_ = anim_player_.mutable_committed_root_x();
    float& prev_root_offset_x_ = anim_player_.mutable_prev_root_offset_x();
    float& prev_root_offset_y_ = anim_player_.mutable_prev_root_offset_y();
    float& step_start_player_x_ = anim_player_.mutable_step_start_player_x();
    bool& anim_facing_right_ = anim_player_.mutable_anim_facing_right();
    float& y_adjust_smoothed_ = anim_player_.mutable_y_adjust_smoothed();
    uint64_t& total_frame_count_ = anim_player_.mutable_total_frame_count();
    int& priority_ = anim_player_.mutable_priority();
    // Combat state aliases (owned by combat_ member)
    std::string& current_move_ = combat_.mutable_current_move();
    int& no_key_frames_ = combat_.mutable_no_key_frames();
    int& move_state_ = combat_.mutable_move_state();
    bool& start_stance_playing_ = combat_.mutable_start_stance_playing();
    bool& need_switch_to_idle_ = combat_.mutable_need_switch_to_idle();
    uint32_t& step_cooldown_ms_ = combat_.mutable_step_cooldown();
    bool& is_uninterrupt_ = combat_.mutable_is_uninterrupt();
    // Module instances (owned via PImpl, initialized in game.cpp)
    std::unique_ptr<AssetManager> assets_;
    InputHandler input_handler_;

    bool replay_mode_ = false;  // skip menus, go directly to Battle  // true when current frame is in Uninterrupt interval
    bool dump_state_ = false;  // --dump-state: print structured state every frame

    // Animation debug/TODO state
    float stance_npivot_y_ = 106.0f;     // NPivot Y from stance anim (default from params.xml)
    float anim_npivot_bin_y_ = 0.0f;     // NPivot Y from current .bin animation frame
    std::string last_logged_anim_;       // last animation name logged (suppress duplicates)
};

} // namespace resf2::game


