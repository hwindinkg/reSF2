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
        root / "assets" / "assets" / "models" / filename,  // sf2/assets/assets/models/
        exe / ".." / ".." / ".." / "assets" / "models" / filename,
        exe / ".." / "assets" / "models" / filename,
        exe / "assets" / "models" / filename,
        exe / ".." / ".." / "assets" / "animations" / "binary" / filename,  // for .bin search
    };
}

// ---------- Asset types ----------

struct AtlasRef {
    std::unique_ptr<ren::Texture2D> texture;
    std::shared_ptr<plist::ParsedAtlas> atlas;
    // Pre-cropped textures for rotated frames (un-rotated during crop)
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> cropped;
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
// .bin format (VERIFIED from Gymnast-Tool-Suite Blender plugin source):
//
// File structure:
//   u32 frame_count              (LITTLE-ENDIAN)
//   frame_count * variable bytes (one record per frame)
//
// Each frame:
//   byte 0     : skip byte (type flag: 1=keyframe, 5=interframe)
//   bytes 1..4 : u32 node_count (LITTLE-ENDIAN)
//   bytes 5..  : node_count × 3 floats (X, Y, -Z), each LITTLE-ENDIAN f32
//
// Coordinate mapping (from plugin: struct.pack("fff", pos.x, pos.z, -pos.y)):
//   bin stores (blender.x, blender.z, -blender.y)
//   blender.x = game.X, blender.y = game.Z, blender.z = game.Y
//   So bin stores: (game.X, game.Y, -game.Z)
//
// Node order: ALL skeleton.xml nodes in XML order (54 Node + 1 COM + 12 MacroNode = 67)
// Body.xml nodes are NOT stored in .bin — they're derived from skeleton nodes at runtime.
//
// Positions are ABSOLUTE (world space). To get LOCAL positions, subtract
// NPivot's world position (NPivot is node index 18 in XML order).
// ---------- Move definition (from moves.xml) ----------
struct MoveDef {
    std::string name;
    std::string filename;
    std::string template_name;
    int first_frame = 0;
    int end_frame = 0;
    int priority = 0;
    
    // Attack interval (frames where hit detection is active)
    int attack_start = -1;
    int attack_end = -1;
    
    // Attack edges (body parts that deal damage)
    std::vector<std::string> attack_edges;
    
    // Damage value
    float damage = 0.0f;
    
    // Block interval (can block during these frames)
    int block_start = -1;
    
    // Uninterrupt interval (can't be interrupted)
    int uninterrupt_start = -1;
    int uninterrupt_end = -1;
    
    // Key combination
    std::vector<std::string> key_types;  // "Punch", "Kick", "Forward", etc.
};

struct AnimationData {
    std::string name;
    int frame_count = 0;
    // frames[fi][i] = (X, Y, Z) in game coords for node i at frame fi
    // Z is already converted from -Z to Z
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
            uint8_t skip = data[offset];
            uint32_t nc = read_u32_le(data.data(), offset + 1);
            offset += 5;
            
            auto& nodes = node_positions[fi];
            nodes.reserve(nc);
            for (uint32_t i = 0; i < nc; ++i) {
                if (offset + 12 > sz) break;
                float fx, fy, fneg_z;
                memcpy(&fx, &data[offset], 4);
                memcpy(&fy, &data[offset + 4], 4);
                memcpy(&fneg_z, &data[offset + 8], 4);
                // bin stores (game.X, game.Y, -game.Z)
                nodes.push_back({fx, fy, -fneg_z});
                offset += 12;
            }
        }
        return !node_positions.empty();
    }

    // Get animated (X, Y, Z) for node `idx` at frame `fi` (absolute world coords)
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

struct BodyNode {
    std::string name;
    float x = 0, y = 0, z = 0;
    float mass = 1.0f;
    bool fixed = false;
    float attenuation = 0.02f;  // damping coefficient
    bool cloth = false;
};

struct BodyEdge {
    std::string name;
    std::string end1, end2;
    float length = 0;
};

// Verlet physics state for a single node.
// Verlet integration: pos_new = 2*pos - pos_prev + acc * dt^2
// No explicit velocity — velocity is implicit (pos - pos_prev).
struct VerletNode {
    float x = 0, y = 0;       // current position
    float px = 0, py = 0;     // previous position (for Verlet integration)
    float mass = 1.0f;
    float inv_mass = 1.0f;    // 1/mass (0 if fixed)
    bool fixed = false;
    float attenuation = 0.02f;
};

struct VerletConstraint {
    std::string n1, n2;
    float length = 0;     // rest length
    float stiffness = 1.0f;  // 1.0 = rigid, <1.0 = soft
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

        // Animate menu expand/collapse (300ms transition)
        float target_progress = (overlay_ == Overlay::Menu) ? 1.0f : 0.0f;
        float anim_speed = 1000.0f / 300.0f;  // 300ms full transition
        if (menu_anim_progress_ < target_progress) {
            menu_anim_progress_ += (float)dt / anim_speed;
            if (menu_anim_progress_ > target_progress) menu_anim_progress_ = target_progress;
        } else if (menu_anim_progress_ > target_progress) {
            menu_anim_progress_ -= (float)dt / anim_speed;
            if (menu_anim_progress_ < target_progress) menu_anim_progress_ = target_progress;
        }

        if (state_ == GameState::Loading) {
            loading_timer_ += dt;
            if (loading_timer_ > 1500) {
                init_location();
            }
        } else if (state_ == GameState::Location) {
            // === MOVEMENT SYSTEM (root motion with input hysteresis) ===
            // Input on Windows can flicker (glfwGetKey returns RELEASE
            // intermittently for held keys). We use hysteresis: once moving,
            // require N frames of "no key" before switching to idle.
            
            bool want_move_left = input.keys_down[(size_t)plat::Key::A] ||
                                  input.keys_down[(size_t)plat::Key::ArrowLeft];
            bool want_move_right = input.keys_down[(size_t)plat::Key::D] ||
                                   input.keys_down[(size_t)plat::Key::ArrowRight];
            bool any_move = want_move_left || want_move_right;
            
            // Hysteresis: if no key pressed, increment no_key_frames_.
            // Only treat as "stopped" after 5 frames of no key.
            if (any_move) {
                no_key_frames_ = 0;
            } else {
                no_key_frames_++;
            }
            bool stopped = (no_key_frames_ > 5);
            
            if (hit_anim_ == 0) {
                if (want_move_left && !want_move_right) {
                    facing_right_ = false;
                    if (current_anim_ != "step_back" && animations_.count("step_back")) {
                        play_animation("step_back", true);
                    }
                } else if (want_move_right && !want_move_left) {
                    facing_right_ = true;
                    if (current_anim_ != "step_forward" && animations_.count("step_forward")) {
                        play_animation("step_forward", true);
                    }
                } else if (stopped) {
                    // Key released for 5+ frames — switch to idle
                    if (current_anim_ != "fists_idle" &&
                        current_anim_.find("punch") == std::string::npos &&
                        current_anim_.find("kick") == std::string::npos &&
                        current_anim_.find("cut") == std::string::npos) {
                        play_animation("fists_idle", true);
                    }
                }
                // If not stopped (key flicker), keep current animation — no switch
            }
            
            // Camera follows player (fixed Y, no W/S camera movement)
            cam_x_ = player_pos_x_ + 200.0f;
            renderer_->camera().set_target(cam_x_, cam_y_);
            renderer_->camera().set_zoom(zoom_);

            // === COMBAT SYSTEM (from moves.xml) ===
            // Punch moves: Space + direction
            if (input.keys_just_pressed[(size_t)plat::Key::Space] && hit_anim_ == 0) {
                std::string move_name, anim_name;
                
                if (want_move_right) {
                    move_name = "DoublePunch"; anim_name = "double_punch";
                } else if (want_move_left) {
                    move_name = "SpinningPunch"; anim_name = "spinning_punch";
                } else if (input.keys_down[(size_t)plat::Key::W] || input.keys_down[(size_t)plat::Key::ArrowUp]) {
                    move_name = "UpperCut"; anim_name = "upper_cut";
                } else if (input.keys_down[(size_t)plat::Key::S] || input.keys_down[(size_t)plat::Key::ArrowDown]) {
                    move_name = "LowPunch"; anim_name = "low_punch";
                } else {
                    move_name = "HighPunch"; anim_name = "high_punch";
                }

                if (animations_.count(anim_name)) {
                    std::printf("[COMBAT] Space → %s (anim '%s', %d frames)\n",
                                move_name.c_str(), anim_name.c_str(),
                                animations_[anim_name].frame_count);
                    play_animation(anim_name, false);
                    current_move_ = move_name;
                    int fc = animations_[anim_name].frame_count;
                    hit_anim_ = (uint32_t)(fc * 1000.0f / 30.0f);
                } else {
                    std::printf("[COMBAT] Space → %s BUT anim '%s' NOT loaded!\n",
                                move_name.c_str(), anim_name.c_str());
                }
            }
            
            // Kick moves: K + direction
            if (input.keys_just_pressed[(size_t)plat::Key::K] && hit_anim_ == 0) {
                std::string move_name, anim_name;
                if (input.keys_down[(size_t)plat::Key::S] || input.keys_down[(size_t)plat::Key::ArrowDown]) {
                    move_name = "Sweep"; anim_name = "sweep";
                } else if (want_move_left) {
                    move_name = "BackKick"; anim_name = "back_kick";
                } else if (want_move_right) {
                    move_name = "FrontKick"; anim_name = "front_kick";
                } else {
                    move_name = "HighKick"; anim_name = "high_kick";
                }
                
                if (animations_.count(anim_name)) {
                    std::printf("[COMBAT] K → %s (anim '%s', %d frames)\n",
                                move_name.c_str(), anim_name.c_str(),
                                animations_[anim_name].frame_count);
                    play_animation(anim_name, false);
                    current_move_ = move_name;
                    int fc = animations_[anim_name].frame_count;
                    hit_anim_ = (uint32_t)(fc * 1000.0f / 30.0f);
                } else {
                    std::printf("[COMBAT] K → %s BUT anim '%s' NOT loaded!\n",
                                move_name.c_str(), anim_name.c_str());
                }
            }

            // === UPDATE ANIMATION ===
            // Must run BEFORE hit detection so anim_node_pos_ and anim_time_
            // are synchronized with the current frame. Without this, hit detection
            // uses stale limb positions from the previous frame, causing hits to
            // trigger at the wrong time (or not at all).
            update_animation(dt);

            // Update hit timer and check for hit detection
            if (hit_anim_ > 0) {
                hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
                
                // Hit detection: check if the attacking limb actually reaches the bag.
                // Uses the move's Attack interval from moves.xml (Start/End frames).
                // Only triggers during the exact attack window (typically 2-4 frames).
                if (!bag_hit_ && bag_model_ && location_) {
                    auto anim_it = animations_.find(current_anim_);
                    if (anim_it != animations_.end()) {
                        int fc = anim_it->second.frame_count;
                        int current_frame = (int)(anim_time_ * 30.0f);
                        // Get attack interval from moves.xml
                        auto move_it = moves_.find(current_move_);
                        if (move_it != moves_.end() && move_it->second.attack_start > 0) {
                            int attack_start = move_it->second.attack_start;
                            int attack_end = move_it->second.attack_end > 0 ? 
                                           move_it->second.attack_end : attack_start;
                            // moves.xml uses 1-indexed frames, our animation is 0-indexed.
                            // Convert: subtract 1 from Start and End.
                            int frame_start = attack_start - 1;
                            int frame_end = attack_end - 1;
                            if (current_frame >= frame_start && current_frame <= frame_end) {
                                // Determine which limb is attacking
                                std::string limb_node;
                                bool is_kick = (current_move_.find("Kick") != std::string::npos ||
                                               current_move_.find("Sweep") != std::string::npos);
                                if (is_kick) {
                                    limb_node = "NToe_1";
                                } else {
                                    limb_node = "NWrist_1";
                                }
                                
                                // Get the limb's animated world position
                                auto ait = anim_node_pos_.find(limb_node);
                                if (ait != anim_node_pos_.end()) {
                                    float limb_lx = ait->second.first;
                                    float limb_ly = ait->second.second;
                                    auto pivot_it = skeleton_nodes_.find("NPivot");
                                    float pivot_ly = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 169.48f;
                                    float limb_wx = player_pos_x_ + (facing_right_ ? limb_lx : -limb_lx) * 1.0f;
                                    float limb_wy = player_pos_y_ + (limb_ly - pivot_ly) * 1.0f;
                                    
                                    // Bag center position (use Verlet node positions for accuracy)
                                    // Same coordinate system as player — no Y-invert
                                    float bag_cx = location_->enemy_x - 983.0f;
                                    float bag_cy = location_->enemy_y + 81.0f;
                                    // Check against the bag's NPivot Verlet position (more accurate)
                                    auto bv_it = bag_verlet_.find("NPivot");
                                    if (bv_it != bag_verlet_.end()) {
                                        bag_cx = bv_it->second.x;
                                        bag_cy = bv_it->second.y;
                                    }
                                    
                                    float dx = limb_wx - bag_cx;
                                    float dy = limb_wy - bag_cy;
                                    float dist = std::sqrt(dx*dx + dy*dy);
                                    
                                    // Hit threshold: 70 units (reliable hit detection)
                                    if (dist < 70.0f) {
                                        // Apply impulse to the nearest bag node based on hit height
                                        std::string target_node = "NNeck";
                                        if (limb_wy < bag_cy - 30) target_node = "NBottom";
                                        else if (limb_wy > bag_cy + 30) target_node = "Node4";
                                        float impulse_dir = (dx < 0) ? 1.0f : -1.0f;
                                        // Stronger impulse for heavier bag
                                        float impulse_strength = is_kick ? 25.0f : 18.0f;
                                        apply_bag_impulse(target_node, impulse_dir * impulse_strength, 0.0f);
                                        std::printf("[COMBAT] HIT! move=%s frame=%d/%d [%d-%d] limb=%s dist=%.1f → impulse on %s\n",
                                                    current_move_.c_str(), current_frame, fc,
                                                    frame_start, frame_end,
                                                    limb_node.c_str(), dist, target_node.c_str());
                                        bag_hit_ = true;
                                    }
                                }
                            }
                        }
                        // If no Attack interval found, skip hit detection entirely
                    }
                }
                
                if (hit_anim_ == 0) {
                    play_animation("fists_idle", true);
                    current_move_.clear();
                    bag_hit_ = false;
                }
            }

            // Update bag Verlet physics (original game uses Verlet integration)
            update_bag_verlet(dt / 1000.0f);

            // Zoom presets
            if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;
            // NO W/S camera movement — W/S are used for attack direction modifiers
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
            // Render menu expansion animation (also during transition)
            if (menu_anim_progress_ > 0.01f) render_menu_expanded(platform);
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
            // Player/enemy positions in params.xml use Y-DOWN, same as image
            // coordinates. Location images are Y-inverted in render_location
            // (world_y = -img.y). But player/enemy Y is used directly (NOT
            // inverted) because the skeleton model space already has Y-UP
            // with NPivot at Y=169 and feet at Y=73 (difference = 96).
            //
            // Floor (layer_3) at params y=225 → world_y = -225 (inverted image).
            // Floor top surface at -225 + 32 = -193.
            // Player NPivot at params y=-93 → world_y = -93 (direct).
            // Player feet at -93 - 96 = -189. Floor at -193. Gap = 4. ✓
            //
            // Bag: enemy_y = -105. Bag NPivot at -105.
            // Node12 (ceiling attachment) at -105 + 226 = 121.
            // Ceiling (layer_5) at params y=-202 → world_y = +202.
            // Need Node12 at ceiling: bag_cy + 226 = 202 → bag_cy = -24.
            // Offset from enemy_y: -24 - (-105) = 81.
            // bag_cy = enemy_y + 81.
            //
            // X offset: align bag with holder (layer_5 at x=-10).
            // bag_cx = enemy_x - offset = -10 → offset = enemy_x + 10 = 983.
            const float X_OFFSET = 983.0f;  // aligns bag with ceiling holder
            player_pos_x_ = location_->player_x - X_OFFSET;
            player_pos_y_ = location_->player_y;  // no invert, no offset
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
                atlases_[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
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
                    auto it = atlases_.find(img.atlas_name);
                    if (it == atlases_.end()) {
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
                
                // For rotated frames, use pre-cropped un-rotated texture
                std::string crop_name = img.class_name;
                if (atlas.cropped.count(crop_name)) {
                    // Use pre-cropped texture (already un-rotated)
                    auto& ctex = atlas.cropped[crop_name];
                    float world_y = -img.y;
                    float world_x = img.x - parallax_shift;
                    float quad_w = img.w;
                    float quad_h = img.h;
                    float px = world_x - quad_w / 2.0f;
                    float py = world_y - quad_h / 2.0f;
                    if (parallax_factor < 0.99f) {
                        float hw = (float)platform_->window_width() / (2.0f * zoom_);
                        float vis_left = cam_x_ - hw;
                        float vis_right = cam_x_ + hw;
                        float tile_w = quad_w;
                        float start_x = px;
                        while (start_x + tile_w > vis_left) start_x -= tile_w;
                        while (start_x < vis_left) start_x += tile_w;
                        start_x -= tile_w;
                        for (float tx = start_x; tx < vis_right; tx += tile_w) {
                            renderer_->draw_textured_quad(*ctex, tx, py, quad_w, quad_h);
                        }
                    } else {
                        renderer_->draw_textured_quad(*ctex, px, py,
                                                      quad_w, quad_h);
                    }
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
                float world_x = img.x - parallax_shift;
                float quad_w = img.w;
                float quad_h = img.h;
                float px = world_x - quad_w / 2.0f;
                float py = world_y - quad_h / 2.0f;  // bottom-left (world Y-UP: +Y = up)
                // For parallax layers (factor < 1), tile the image horizontally
                // to fill the screen. This prevents the background from flying
                // off-screen when the camera moves.
                if (parallax_factor < 0.99f) {
                    // Calculate visible world range
                    float hw = (float)platform_->window_width() / (2.0f * zoom_);
                    float vis_left = cam_x_ - hw;
                    float vis_right = cam_x_ + hw;
                    // Tile from leftmost visible to rightmost visible
                    float tile_w = quad_w;
                    float start_x = px;
                    // Find the leftmost tile that's visible
                    while (start_x + tile_w > vis_left) start_x -= tile_w;
                    while (start_x < vis_left) start_x += tile_w;
                    start_x -= tile_w;  // go one more to the left for safety
                    for (float tx = start_x; tx < vis_right; tx += tile_w) {
                        renderer_->draw_textured_quad(*atlas.texture, tx, py, quad_w, quad_h,
                                                      u0, v0, u1, v1);
                    }
                } else {
                    // For foreground layers (factor = 1.0), render once.
                    // No overlap — the pre-cropped texture fix should resolve gaps.
                    renderer_->draw_textured_quad(*atlas.texture, px, py, quad_w, quad_h,
                                                  u0, v0, u1, v1);
                }
            }
        }
        loc_logged = true;
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
        
        // Also parse Type="MacroNode" tags — these are weighted-average nodes
        // (e.g., MacroNode1_1, MacroNode2_1) used by capsule edges.
        // They have direct X, Y, Z rest-pose coordinates in the XML.
        pos = 0;
        int macro_count = 0;
        while ((pos = nodes_xml.find("Type=\"MacroNode\"", pos)) != std::string::npos) {
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
                macro_count++;
            }
            pos = end + 2;
        }
        
        // Also parse Type="CenterOfMass" (COM node)
        pos = 0;
        while ((pos = nodes_xml.find("Type=\"CenterOfMass\"", pos)) != std::string::npos) {
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
        
        // Build ordered_node_names_ — ALL nodes in XML order.
        // This matches the .bin node order (67 nodes: 54 Node + 1 COM + 12 MacroNode).
        ordered_node_names_.clear();
        pos = 0;
        while (true) {
            // Find next node tag (any Type)
            auto tag_start = nodes_xml.find('<', pos);
            if (tag_start == std::string::npos) break;
            auto tag_end = nodes_xml.find("/>", tag_start);
            if (tag_end == std::string::npos) break;
            auto tag = nodes_xml.substr(tag_start, tag_end - tag_start);
            // Check if this tag has X/Y attributes (is a node)
            if (tag.find("X=\"") != std::string::npos && tag.find("Y=\"") != std::string::npos) {
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    std::string name = tag.substr(1, sp - 1);
                    ordered_node_names_.push_back(name);
                }
            }
            pos = tag_end + 2;
        }
        std::printf("  Skeleton: %zu nodes (%d MacroNodes, ordered: %zu)\n",
                    skeleton_nodes_.size(), macro_count, ordered_node_names_.size());

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
                c.margin1 = tof(xml_attr(tag, "Margin1"));
                c.margin2 = tof(xml_attr(tag, "Margin2"));
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

        // Check if this node has an animated position (from .bin animation)
        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            float lx = ait->second.first, ly = ait->second.second;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }

        auto bit = body_model_->nodes.find(name);
        if (bit != body_model_->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto sit = skeleton_nodes_.find(name);
        if (sit != skeleton_nodes_.end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
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

        // Y normalization: keep feet on floor across all animations.
        // Different animations have different NPivot Y values in .bin files:
        //   fists_idle: NToe_2 ly = 82.27 (character stands taller → feet float)
        //   step/punch: NToe_2 ly = 64.60 (combat stance → feet on floor)
        // We use the step animation as reference (looks correct).
        // Adjustment = (reference_ly - current_ly) where reference_ly = 64.60.
        // SMOOTH the adjustment to prevent visual jumps when switching animations.
        float ly_lowest = pivot_local_y;
        for (auto& [name, pos] : anim_node_pos_) {
            if (pos.second < ly_lowest) ly_lowest = pos.second;
        }
        const float REF_FEET_LY = 64.60f;  // NToe_2 ly in step_forward.bin
        float target_y_adjust = REF_FEET_LY - ly_lowest;
        // Smoothly interpolate y_adjust to prevent jitter on animation switch
        y_adjust_smoothed_ += (target_y_adjust - y_adjust_smoothed_) * 0.15f;
        float world_cx = player_pos_x_;
        float world_cy = player_pos_y_ + y_adjust_smoothed_;

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

        // Render character as unified dark silhouette.
        // Render ALL capsules (including duplicates — they overlap to fill gaps
        // at joints). Apply Margin1/Margin2 to trim ends properly.
        ren::Color4B silhouette_col{20, 20, 25, 255};

        for (auto& c : body_model_->capsules) {
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

        // Render triangles (small parts)
        // Skip triangles that reference non-animated nodes (BODY-Node entries
        // from body.xml). These are cloth simulation nodes that don't have
        // per-node animation data in the .bin files. Rendering them at their
        // rest-pose positions while other triangle vertices are animated
        // causes visible stretching on the legs (especially around the calves
        // and ankles where BODY-Triangle-7..10 are located).
        for (auto& t : body_model_->triangles) {
            // Check if ALL three vertices are animated (in anim_node_pos_ or skeleton_nodes_)
            bool n1_animated = (anim_node_pos_.find(t.n1) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n1) != skeleton_nodes_.end());
            bool n2_animated = (anim_node_pos_.find(t.n2) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n2) != skeleton_nodes_.end());
            bool n3_animated = (anim_node_pos_.find(t.n3) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n3) != skeleton_nodes_.end());
            if (!n1_animated || !n2_animated || !n3_animated) {
                continue;  // Skip triangles with non-animated cloth nodes
            }
            auto [tx0, ty0] = resolve_body_node(t.n1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3,
                world_cx, world_cy, facing_right_, pivot_local_y);
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, silhouette_col);
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
                    n.mass = tof(xml_attr(tag, "Mass"), 1.0f);
                    n.fixed = (toi(xml_attr(tag, "Fixed")) != 0);
                    n.attenuation = tof(xml_attr(tag, "Attenuation"), 0.02f);
                    n.cloth = (toi(xml_attr(tag, "Cloth")) != 0);
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
                    c.margin1 = tof(xml_attr(tag, "Margin1"));
                    c.margin2 = tof(xml_attr(tag, "Margin2"));
                    bag_model_->capsules.push_back(c);
                    pos = end + 2;
                }
            }
        }
        std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                    bag_model_->nodes.size(), bag_model_->edges.size(),
                    bag_model_->capsules.size());
        init_bag_verlet();
    }

    // Initialize Verlet physics state from the bag's skeleton nodes.
    // Each node gets: position = (x, y), prev_position = (x, y) (at rest).
    // Fixed nodes (Fixed="1") have inv_mass = 0 and don't move.
    // Edges become distance constraints with rest length = edge.length.
    void init_bag_verlet() {
        if (!bag_model_) return;
        bag_verlet_.clear();
        bag_constraints_.clear();
        // World position of the bag's NPivot (where it hangs in the world)
        // Same coordinate system as player — no Y-invert, use params Y directly
        // with the same -45 offset to align with the floor.
        float bag_cx = location_ ? (location_->enemy_x - 983.0f) : 0.0f;
        float bag_cy = location_ ? (location_->enemy_y + 81.0f) : 0.0f;
        auto pit = bag_model_->nodes.find("NPivot");
        float pivot_ly = pit != bag_model_->nodes.end() ? pit->second.y : 109.0f;
        // Initialize nodes: world position = bag_center + (node_local - NPivot_local)
        for (auto& [name, n] : bag_model_->nodes) {
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
        for (auto& e : bag_model_->edges) {
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
        // Apply impulse: move prev position opposite to velocity direction
        n.px -= vx;
        n.py -= vy;
    }

    // Update bag Verlet physics.
    // 1. Integration: pos_new = 2*pos - prev + acc*dt^2 (gravity + damping)
    // 2. Satisfy constraints (multiple iterations for stiffness)
    // 3. Apply damping (attenuation)
    void update_bag_verlet(float dt) {
        if (!bag_verlet_init_ || !bag_model_) return;
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
        if (!bag_model_ || !location_) return;
        
        // Bag position: enemy_x from params.xml, adjusted to world space
        float bag_cx = location_->enemy_x - 983.0f;
        
        // Bag NPivot Y in model space = 109.0
        // The bag hangs from Node12 (Y=335) which is fixed at ceiling
        // Place bag so NPivot is at enemy_y
        // Same coordinate system as player — no Y-invert
        auto pit = bag_model_->nodes.find("NPivot");
        float pivot_ly = pit != bag_model_->nodes.end() ? pit->second.y : 109.0f;
        float bag_cy = location_->enemy_y + 81.0f;  // align with floor + offset
        
        // === BAG RENDERING (Verlet physics) ===
        // The bag's skeleton nodes are simulated with Verlet integration.
        // Node12 is fixed (ceiling attachment). Other nodes swing freely
        // subject to gravity + distance constraints (edges).
        // We render capsules using the current Verlet node positions.
        
        // Build edge lookup
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : bag_model_->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }
        
        // Render bag as unified silhouette (same approach as character)
        ren::Color4B bag_body_col{35, 35, 40, 255};      // dark neutral for bag body
        ren::Color4B bag_chain_col{160, 160, 160, 255};   // gray for chain
        
        for (auto& c : bag_model_->capsules) {
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
                auto nit1 = bag_model_->nodes.find(en1);
                auto nit2 = bag_model_->nodes.find(en2);
                if (nit1 == bag_model_->nodes.end() || nit2 == bag_model_->nodes.end()) continue;
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
    // ---------- Animation loading ----------
    void load_animations() {
        auto root = std::filesystem::path(asset_root_);
        auto exe = get_exe_dir();
        // Search for animation .bin files in multiple paths
        std::vector<std::filesystem::path> search_dirs = {
            root/"assets"/"animations"/"binary",
            root/"animations"/"binary",
            root/"assets"/"animations",
            root/"animations",
            exe / ".." / ".." / ".." / "assets" / "animations" / "binary",  // repo assets
        };
        
        // Load key animations (using actual game file names from moves.xml)
        const char* anim_names[] = {
            "fists1_stance_idle", "fists2_stance_idle",
            "high_punch", "heavy_punch", "low_punch",
            "double_punch", "spinning_punch", "upper_cut",
            "high_kick", "front_kick", "back_kick",
            "sweep", "low_kick", "high_knee_up",
            "axe_idle", "axe_stance_idle",
            "stance_1", "stance_2",
            "step_forward", "step_back",
            "back_flip", "back_flip_kick",
        };
        
        for (auto& name : anim_names) {
            std::string filename = std::string(name) + ".bin";
            for (auto& dir : search_dirs) {
                auto path = dir / filename;
                if (std::filesystem::exists(path)) {
                    AnimationData anim;
                    anim.name = name;
                    if (anim.load(path.string())) {
                        std::printf("  Animation '%s': %d frames\n", name, anim.frame_count);
                        animations_[name] = std::move(anim);
                        break;
                    }
                }
            }
        }
        
        // Map common names to actual files
        if (animations_.count("fists1_stance_idle") && !animations_.count("fists_idle")) {
            animations_["fists_idle"] = animations_["fists1_stance_idle"];
        }
        // Punch animations are loaded directly (high_punch, heavy_punch, low_punch)
        
        if (animations_.empty()) {
            std::printf("  Animations: NONE! Searched:\n");
            for (auto& dir : search_dirs) std::printf("    %s\n", dir.string().c_str());
            std::printf("  exe dir: %s\n", exe.string().c_str());
        }
        std::printf("  Animations loaded: %zu\n", animations_.size());
    }

    // ---------- Move definitions (from moves.xml) ----------
    void load_moves() {
        auto root = std::filesystem::path(asset_root_);
        auto exe = get_exe_dir();
        std::vector<std::filesystem::path> search_dirs = {
            root/"assets"/"animations",
            root/"animations",
            root/"assets",
            exe / ".." / ".." / ".." / "assets" / "animations",  // repo assets
        };
        
        std::string moves_path;
        for (auto& dir : search_dirs) {
            auto path = dir / "moves.xml";
            if (std::filesystem::exists(path)) { moves_path = path.string(); break; }
        }
        if (moves_path.empty()) {
            std::printf("  moves.xml NOT FOUND!\n");
            return;
        }
        
        auto xml = read_text(moves_path);
        // Simple XML parser for <Move> tags
        size_t pos = 0;
        while ((pos = xml.find("<Move ", pos)) != std::string::npos) {
            // Skip commented out moves
            if (pos > 4 && xml.substr(pos - 4, 4) == "<!--") {
                pos += 6;
                continue;
            }
            
            auto end_tag = xml.find(">", pos);
            if (end_tag == std::string::npos) break;
            auto tag = xml.substr(pos, end_tag - pos);
            
            MoveDef move;
            move.name = xml_attr(tag, "Name");
            move.filename = xml_attr(tag, "FileName");
            move.template_name = xml_attr(tag, "Template");
            move.first_frame = (int)tof(xml_attr(tag, "FirstFrame"));
            move.end_frame = (int)tof(xml_attr(tag, "EndFrame"));
            move.priority = (int)tof(xml_attr(tag, "Priority"));
            
            // Find </Move> to get inner content
            auto move_end = xml.find("</Move>", pos);
            if (move_end == std::string::npos) { pos = end_tag; continue; }
            auto inner = xml.substr(end_tag + 1, move_end - end_tag - 1);
            
            // Parse Intervals
            size_t ip = 0;
            while ((ip = inner.find("Type=\"Attack\"", ip)) != std::string::npos ||
                   (ip = inner.find("Name=\"Attack\"", ip)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts == std::string::npos || te == std::string::npos) break;
                auto iv_tag = inner.substr(ts, te - ts);
                move.attack_start = (int)tof(xml_attr(iv_tag, "Start"));
                move.attack_end = (int)tof(xml_attr(iv_tag, "End"));
                ip = te + 2;
            }
            
            // Parse attack edges
            ip = 0;
            while ((ip = inner.find("<Edge ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto e_tag = inner.substr(ip, te - ip);
                auto ename = xml_attr(e_tag, "Name");
                if (!ename.empty()) move.attack_edges.push_back(ename);
                ip = te + 2;
            }
            
            // Parse damage
            ip = 0;
            while ((ip = inner.find("<Damage ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto d_tag = inner.substr(ip, te - ip);
                auto val = xml_attr(d_tag, "Value");
                if (!val.empty()) {
                    move.damage = tof(val);
                    break;  // take first damage value
                }
                ip = te + 2;
            }
            
            // Parse keys
            ip = 0;
            while ((ip = inner.find("<Key ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto k_tag = inner.substr(ip, te - ip);
                auto ktype = xml_attr(k_tag, "Type");
                if (!ktype.empty()) move.key_types.push_back(ktype);
                ip = te + 2;
            }
            
            // Parse Block interval
            ip = 0;
            if ((ip = inner.find("Type=\"Block\"", 0)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts != std::string::npos && te != std::string::npos) {
                    auto b_tag = inner.substr(ts, te - ts);
                    move.block_start = (int)tof(xml_attr(b_tag, "Start"));
                }
            }
            
            // Parse Uninterrupt interval
            ip = 0;
            if ((ip = inner.find("Name=\"Uninterrupt\"", 0)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts != std::string::npos && te != std::string::npos) {
                    auto u_tag = inner.substr(ts, te - ts);
                    move.uninterrupt_start = (int)tof(xml_attr(u_tag, "Start"));
                    move.uninterrupt_end = (int)tof(xml_attr(u_tag, "End"));
                }
            }
            
            if (!move.filename.empty()) {
                moves_[move.name] = std::move(move);
            }
            pos = move_end + 7;
        }
        std::printf("  Moves loaded: %zu\n", moves_.size());
    }
    
    // Update animation state and compute per-node animated positions.
    //
    // The .bin stores ABSOLUTE world positions for all 67 skeleton.xml nodes
    // (in XML order). To get LOCAL positions (model-space), subtract NPivot's
    // world position from all nodes.
    //
    // Root motion: applied as delta from frame 0 NPivot position.
    void update_animation(uint32_t dt_ms) {
        anim_node_pos_.clear();
        anim_root_dx_ = 0.0f;
        anim_root_dy_ = 0.0f;

        auto it = animations_.find(current_anim_);
        if (it == animations_.end()) {
            if (!animations_.empty()) {
                current_anim_ = animations_.begin()->first;
                it = animations_.find(current_anim_);
            } else {
                return;
            }
        }

        auto& anim = it->second;
        if (anim.frame_count == 0 || ordered_node_names_.empty()) return;

        // Find NPivot index in ordered_node_names_
        int npivot_idx = -1;
        for (int i = 0; i < (int)ordered_node_names_.size(); ++i) {
            if (ordered_node_names_[i] == "NPivot") {
                npivot_idx = i;
                break;
            }
        }
        if (npivot_idx < 0) {
            std::printf("[ANIM] ERROR: NPivot not found in ordered_node_names_ (size=%zu)\n",
                        ordered_node_names_.size());
            return;
        }

        // Set anchor from frame 0 NPivot position
        if (!anim_anchor_set_) {
            float px, py, pz;
            if (anim.get_node_pos(0, npivot_idx, px, py, pz)) {
                anim_root_anchor_x_ = px;
                anim_root_anchor_y_ = py;
                anim_anchor_set_ = true;
            }
        }

        // Advance time
        float dt = dt_ms / 1000.0f;
        anim_time_ += dt * anim_speed_ / 30.0f;

        // Calculate current frame (with looping)
        float frame_f = anim_time_ * 30.0f;
        int frame_idx = (int)frame_f;
        bool anim_finished = false;
        if (anim_loop_) {
            if (anim.frame_count > 0)
                frame_idx = frame_idx % anim.frame_count;
        } else if (frame_idx >= anim.frame_count) {
            frame_idx = anim.frame_count - 1;
            anim_finished = true;
        }
        if (frame_idx < 0) frame_idx = 0;

        // For non-looping animations that have finished, don't interpolate
        // with frame 0 (which would cause wrap-around and backward movement).
        // Instead, stay exactly at the last frame.
        int next_idx;
        float alpha;
        if (anim_finished) {
            next_idx = frame_idx;
            alpha = 0.0f;
        } else {
            next_idx = anim.frame_count > 0
                ? ((frame_idx + 1) % anim.frame_count) : 0;
            alpha = frame_f - (int)frame_f;
            if (alpha < 0) alpha = 0;
            if (alpha > 1) alpha = 1;
        }

        // Get NPivot position at current frame (for root offset)
        float npx0, npy0, npz0, npx1, npy1, npz1;
        if (!anim.get_node_pos(frame_idx, npivot_idx, npx0, npy0, npz0)) return;
        if (!anim.get_node_pos(next_idx, npivot_idx, npx1, npy1, npz1)) {
            npx1 = npx0; npy1 = npy0; npz1 = npz0;
        }
        float npivot_x = npx0 + (npx1 - npx0) * alpha;
        float npivot_y = npy0 + (npy1 - npy0) * alpha;

        // Store animated NPivot Y for render_body_model normalization.
        // This prevents the character from "floating" in animations where
        // NPivot Y differs from the rest pose (e.g., idle vs step).
        anim_npivot_bin_y_ = npivot_y;

        // Root motion: delta accumulation with wrap-around filter.
        // For looping step animations, NPivot X goes 169→235 then wraps to 169.
        // We accumulate positive deltas (forward movement) and filter out
        // the large negative wrap-around delta (-66).
        // This gives natural non-linear movement matching the animation.
        if (current_anim_ == "step_forward" || current_anim_ == "step_back") {
            if (anim_anchor_set_) {
                float delta = npivot_x - prev_npivot_x_;
                // Filter out wrap-around (when animation loops, delta jumps ~±66)
                if (std::abs(delta) < 40.0f) {
                    player_pos_x_ += delta;
                }
            }
            prev_npivot_x_ = npivot_x;
        } else {
            prev_npivot_x_ = 0.0f;
        }

        // Get NPivot's rest-pose Y (from skeleton_nodes_)
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float npivot_rest_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 169.48f;

        // For each node in the .bin, compute LOCAL position and store in anim_node_pos_
        for (int i = 0; i < (int)ordered_node_names_.size() && i < 67; ++i) {
            const std::string& name = ordered_node_names_[i];
            
            float x0, y0, z0, x1, y1, z1;
            if (!anim.get_node_pos(frame_idx, i, x0, y0, z0)) continue;
            if (!anim.get_node_pos(next_idx, i, x1, y1, z1)) {
                x1 = x0; y1 = y0; z1 = z0;
            }
            
            // Interpolate
            float abs_x = x0 + (x1 - x0) * alpha;
            float abs_y = y0 + (y1 - y0) * alpha;
            
            // Convert to LOCAL: subtract NPivot's world position
            float local_x = abs_x - npivot_x;
            float local_y = abs_y - npivot_y;
            
            // Store: anim_node_pos_ maps node name -> (local_X, local_Y)
            // The Y needs to be relative to NPivot's rest Y (for rendering)
            // local_y is already relative to NPivot's current Y.
            // We need: local_y + npivot_rest_y (to get model-space Y)
            anim_node_pos_[name] = {local_x, local_y + npivot_rest_y};
        }

        // One-shot diagnostic: log when animation changes, to verify anim_node_pos_ is populated.
        // Prints NPivot + a few key leg nodes so we can see if animation data is sane.
        if (current_anim_ != last_logged_anim_) {
            last_logged_anim_ = current_anim_;
            std::printf("[ANIM] '%s' frame=%d/%d anim_node_pos_.size()=%zu  npivot_idx=%d\n",
                        current_anim_.c_str(), frame_idx, anim.frame_count,
                        anim_node_pos_.size(), npivot_idx);
            // Print leg nodes (NHip, NKnee, NAnkle) for both sides
            const char* leg_nodes[] = {"NPivot", "NHip_1", "NHip_2",
                                       "NKnee_1", "NKnee_2",
                                       "NAnkle_1", "NAnkle_2",
                                       "NToe_1", "NToe_2"};
            for (auto* n : leg_nodes) {
                auto ait = anim_node_pos_.find(n);
                auto sit = skeleton_nodes_.find(n);
                if (ait != anim_node_pos_.end() && sit != skeleton_nodes_.end()) {
                    std::printf("  %-10s anim_local=(%7.2f,%7.2f)  rest=(%7.2f,%7.2f)\n",
                                n, ait->second.first, ait->second.second,
                                sit->second.x, sit->second.y);
                } else if (sit != skeleton_nodes_.end()) {
                    std::printf("  %-10s NOT in anim_node_pos_!  rest=(%7.2f,%7.2f)\n",
                                n, sit->second.x, sit->second.y);
                }
            }
        }
    }
    
    void play_animation(const std::string& name, bool loop = true) {
        if (animations_.count(name)) {
            if (current_anim_ != name) {
                std::printf("[ANIM] play_animation('%s', loop=%d) — switching from '%s'\n",
                            name.c_str(), loop, current_anim_.c_str());
            }
            current_anim_ = name;
            anim_time_ = 0.0f;
            anim_loop_ = loop;
            // Reset anchor so update_animation() re-reads frame 0 root pos
            anim_anchor_set_ = false;
            anim_root_dx_ = 0.0f;
            anim_root_dy_ = 0.0f;
            prev_root_offset_ = 0.0f;  // Reset root motion offset for new animation
        }
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
        // Compute menu animation progress (smoothstep easing)
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);
        // Show collapsed roll when menu is closed OR animating
        if (menu_eased < 0.99f) {
            // Collapsed: scroll roll bar — sized to fit "MENU" text
            auto lit = scroll_textures_.find("MenuRoll_left");
            auto cit = scroll_textures_.find("MenuRoll_center");
            auto rit = scroll_textures_.find("MenuRoll_right");
            if (lit != scroll_textures_.end() && cit != scroll_textures_.end() &&
                rit != scroll_textures_.end()) {
                float cap_w = roll_h * lit->second->width() / lit->second->height();
                // Measure "MENU" text width at scale 0.22
                float text_w = 0.0f;
                if (hud_font_) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = hud_font_->char_index.find(cp);
                        if (it != hud_font_->char_index.end()) {
                            text_w += hud_font_->chars[it->second].xadvance * 0.22f;
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
                if (hud_font_) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = hud_font_->char_index.find(cp);
                        if (it != hud_font_->char_index.end()) {
                            text_h = std::max(text_h, (float)hud_font_->chars[it->second].height * 0.22f);
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
    void render_menu_expanded(plat::Platform& platform) {
        // Compute eased animation progress
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);  // smoothstep
        if (menu_eased < 0.01f) return;  // nothing to render

        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;

        auto lit = scroll_textures_.find("MenuRoll_left");
        auto cit = scroll_textures_.find("MenuRoll_center");
        auto rit = scroll_textures_.find("MenuRoll_right");

        if (lit == scroll_textures_.end() || cit == scroll_textures_.end() ||
            rit == scroll_textures_.end()) {
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
        auto pl_it = scroll_textures_.find("Paper_left");
        auto pr_it = scroll_textures_.find("Paper_right");
        if (pl_it != scroll_textures_.end()) {
            float pl_w = paper_w * pl_it->second->width() / pl_it->second->height();
            renderer_->draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_w);
        }
        if (pr_it != scroll_textures_.end() && menu_eased > 0.95f) {
            // Only show bottom edge when fully expanded
            float pr_w = paper_w * pr_it->second->width() / pr_it->second->height();
            renderer_->draw_textured_quad_screen(*pr_it->second,
                btn_x, paper_y + paper_h - pr_w, pr_w, paper_w);
        }

        // Shadow below (only when fully expanded)
        auto shadow_it = scroll_textures_.find("Shadow_roll");
        if (shadow_it != scroll_textures_.end() && menu_eased > 0.9f) {
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
            auto it = menu_textures_.find(tex_name);
            if (it == menu_textures_.end()) {
                it = menu_textures_.find(std::string(name) + "_Normal");
            }
            if (it != menu_textures_.end()) {
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
            auto it = menu_textures_.find(tex_name);
            if (it == menu_textures_.end()) {
                it = menu_textures_.find(std::string(name) + "_Normal");
            }
            if (it != menu_textures_.end()) {
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

    GameState state_ = GameState::Loading;
    Overlay overlay_ = Overlay::None;
    float menu_anim_progress_ = 0.0f;  // 0 = collapsed, 1 = fully expanded
    bool loc_icons_logged = false;  // one-shot diagnostic for menu icon sizes
    uint32_t loading_timer_ = 0;
    float load_scale_ = 1.0f, zoom_ = 1.0f;
    std::vector<LoadingImg> loading_images_;

    std::unique_ptr<GameLocation> location_;
    std::unordered_map<std::string, AtlasRef> atlases_;
    std::unordered_map<std::string, SkelNode> skeleton_nodes_;
    std::unordered_map<std::string, SkelEdge> skeleton_edges_;
    // Ordered list of ALL skeleton.xml node names (Node + COM + MacroNode)
    // in XML order. This matches the .bin node order.
    std::vector<std::string> ordered_node_names_;
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
    uint32_t step_cooldown_ = 0;  // ms remaining before next step animation can start
    bool step_active_ = false;    // true while a step is in progress
    uint32_t step_duration_ = 0;  // ms remaining in current step
    float step_start_x_ = 0;      // player X at start of step
    float step_displacement_ = 0; // total displacement for this step (+66 or -66)
    int bag_swing_ = 0;   // ms remaining (legacy, for compatibility)
    bool bag_hit_ = false;  // bag already hit during current attack
    float bag_swing_dir_ = 1.0f;  // +1 = swing right, -1 = swing left
    // Physics-based pendulum state for the punching bag.
    // The bag hangs from Node12 (fixed ceiling point) and swings as a pendulum.
    // On hit: an impulse is applied to bag_angle_vel_.
    // Each frame: spring restoring force + damping + integration.
    float bag_angle_ = 0.0f;       // current angle (radians, 0 = vertical)
    float bag_angle_vel_ = 0.0f;   // angular velocity (rad/sec)
    // Verlet physics state for the punching bag.
    // The original game uses Verlet integration for the bag's skeleton.
    // Each node has position + previous position. Edges are distance constraints.
    // Fixed nodes (Node12 = ceiling attachment) don't move.
    std::unordered_map<std::string, VerletNode> bag_verlet_;
    std::vector<VerletConstraint> bag_constraints_;
    bool bag_verlet_init_ = false;
    bool quit_requested_ = false;
    
    // Animation state
    std::unordered_map<std::string, AnimationData> animations_;
    std::unordered_map<std::string, MoveDef> moves_;
    std::string current_move_;  // Name of currently playing move (for hit detection)
    std::string current_anim_ = "fists_idle";
    float anim_time_ = 0.0f;  // seconds into current animation
    float anim_speed_ = 30.0f;  // FPS for animation playback
    bool anim_loop_ = true;

    // Animated node positions (override skeleton rest pose during animation)
    std::unordered_map<std::string, std::pair<float, float>> anim_node_pos_;  // name -> (x, y)

    // Root motion offset (delta from animation frame 0).
    // .bin float[1] = absolute root X, float[2] = absolute root Y.
    // We use the DELTA from frame 0 to move the whole model during animation
    // (e.g. lunge forward during punch, steps during walk). This is safe —
    // it moves the entire character without tearing, since all nodes shift
    // together. Per-node animation (limb movement) requires the unsolved
    // .bin node-mapping table and is therefore disabled.
    float anim_root_dx_ = 0.0f;
    float anim_root_dy_ = 0.0f;
    // Anchor: root position at frame 0 of the current animation (subtracted
    // so the model doesn't snap to the .bin's world coordinates).
    float anim_root_anchor_x_ = 0.0f;
    float anim_root_anchor_y_ = 0.0f;
    bool anim_anchor_set_ = false;
    float prev_npivot_x_ = 0.0f;  // for step root motion (previous frame)
    float prev_root_offset_ = 0.0f;  // offset from frame-0 NPivot (for root motion)
    float step_start_player_x_ = 0.0f;  // player X when step started (for absolute root motion)
    float y_adjust_smoothed_ = 0.0f;  // smoothed Y adjustment for feet normalization
    int no_key_frames_ = 0;  // frames with no movement key pressed (for hysteresis)
    float anim_npivot_bin_y_ = 169.48f;  // animated NPivot Y from .bin (for Y normalization)
    std::string last_logged_anim_;  // for one-shot diagnostic in update_animation
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
