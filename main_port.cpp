// reSF2 — Modular Engine Demo (Phase 4)
//
// Demonstrates the new modular architecture with real game rendering.
//   - State stack (engine/core/state.hpp)
//   - Location background rendering with parallax (Dojo)
//   - Camera scrolling via arrow keys
//   - Asset loading via DZ archives + plist/texture parsers
//
// Build: cmake --build build --target resf2_port
// Run:   build/bin/Release/resf2_port.exe

#include "engine/core/state.hpp"
#include "engine/core/math.hpp"
#include "engine/core/renderer2d.hpp"
#include "engine/format/json_atlas.hpp"
#include "engine/format/location_parser.hpp"
#include "engine/fight/animation.hpp"
#include "engine/fight/moves.hpp"
#include "engine/reverse/dz_reader.hpp"
#include "engine/reverse/plist_atlas.hpp"

#include "engine/platform/platform.hpp"
#include "engine/platform/glfw_platform.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/renderer/stb_image.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

namespace fs = std::filesystem;
namespace plat = resf2::platform;
namespace ren = resf2::renderer;
namespace core = resf2::core;

// ========== Helpers ==========

struct LoadedFrame {
    ren::Texture2D* texture = nullptr;
    float w = 0, h = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool owned = false;
};

struct LoadedAtlas {
    std::unordered_map<std::string, LoadedFrame> frames;
    std::vector<std::shared_ptr<ren::Texture2D>> owned_textures;
    LoadedAtlas() = default;
    LoadedAtlas(LoadedAtlas&&) = default;
    LoadedAtlas& operator=(LoadedAtlas&&) = default;
    // Allow copy for vector push_back
    LoadedAtlas(const LoadedAtlas& other) = default;
    LoadedAtlas& operator=(const LoadedAtlas& other) = default;
};

// ========== Move Definition (local, matching moves.xml structure) ==========
struct MoveDef {
    std::string name;
    std::string filename;
    std::string template_name;
    int first_frame = 0;
    int end_frame = 0;
    int priority = 0;
    bool is_attack = false;
    int attack_start = -1;
    int attack_end = -1;
    float damage = 0;
    float impulse_x = 0;
    float impulse_y = 0;
    std::vector<std::string> attack_edges;
    int block_start = -1;
    int uninterrupt_start = -1;
    int uninterrupt_end = -1;
    std::vector<std::string> key_types;
    int key_count = 1;
    std::string direction;
    std::string move_type;
    std::string weapon_filter;
    std::string tactic_weapon;
    std::string required_current_animation;
    bool is_jump = false;
    bool is_step = false;
    bool is_block = false;
    bool is_stance = false;
    bool is_idle = false;
    float distance_min = 0;
    float distance_max = 0;
    bool has_distance_cond = false;
};

// ========== Simple XML tag scraper ==========
static std::string xml_attr(const std::string& xml, const std::string& tag,
                            size_t pos, const std::string& attr) {
    auto ap = xml.find(attr + "=\"", pos);
    if (ap == std::string::npos) return {};
    ap += attr.size() + 2;
    auto ep = xml.find('"', ap);
    return xml.substr(ap, ep - ap);
}

static float xml_attr_f(const std::string& xml, const std::string& tag,
                        size_t pos, const std::string& attr, float def = 0) {
    auto v = xml_attr(xml, tag, pos, attr);
    return v.empty() ? def : std::stof(v);
}

static std::vector<size_t> find_tags(const std::string& xml, const std::string& tag) {
    std::vector<size_t> positions;
    size_t pos = 0;
    while (true) {
        auto p = xml.find("<" + tag, pos);
        if (p == std::string::npos) break;
        positions.push_back(p + 1);
        pos = p + tag.size();
    }
    return positions;
}

// ========== Move loading from moves.xml ==========
static std::unordered_map<std::string, MoveDef> load_moves_from_xml(const std::string& xml) {
    std::unordered_map<std::string, MoveDef> moves;
    size_t pos = 0;
    while ((pos = xml.find("<Move ", pos)) != std::string::npos) {
        if (pos > 4 && xml.substr(pos - 4, 4) == "<!--") { pos += 6; continue; }
        auto end_tag = xml.find(">", pos);
        if (end_tag == std::string::npos) break;
        auto tag = xml.substr(pos, end_tag - pos);
        MoveDef move;
        move.name = xml_attr(tag, "", 0, "Name");
        move.filename = xml_attr(tag, "", 0, "FileName");
        move.template_name = xml_attr(tag, "", 0, "Template");
        move.first_frame = (int)xml_attr_f(tag, "", 0, "FirstFrame");
        move.end_frame = (int)xml_attr_f(tag, "", 0, "EndFrame");
        move.priority = (int)xml_attr_f(tag, "", 0, "Priority");
        move.tactic_weapon = xml_attr(tag, "", 0, "TacticWeapon");
        std::string type_attr = xml_attr(tag, "", 0, "Type");
        move.is_attack = (type_attr == "ATTACK");
        // Parse template
        if (!move.template_name.empty()) {
            std::string tmpl = move.template_name;
            size_t st = 0;
            std::vector<std::string> parts;
            while (st < tmpl.size()) {
                auto sep = tmpl.find('|', st);
                if (sep == std::string::npos) { parts.push_back(tmpl.substr(st)); break; }
                parts.push_back(tmpl.substr(st, sep - st));
                st = sep + 1;
            }
            for (auto& p : parts) {
                if (p == "1key") move.key_count = 1;
                else if (p == "2key") move.key_count = 2;
                else if (p == "3key") move.key_count = 3;
                else if (p == "Central") move.direction = "Central";
                else if (p == "Forward") move.direction = "Forward";
                else if (p == "Back") move.direction = "Back";
                else if (p == "Up") move.direction = "Up";
                else if (p == "Down") move.direction = "Down";
                else if (p == "UpForward") move.direction = "UpForward";
                else if (p == "UpBack") move.direction = "UpBack";
                else if (p == "DownForward") move.direction = "DownForward";
                else if (p == "DownBack") move.direction = "DownBack";
                else if (p == "Punch") move.move_type = "Punch";
                else if (p == "Kick") move.move_type = "Kick";
                else if (p == "Jump") { move.move_type = "Jump"; move.is_jump = true; }
                else if (p == "Step") { move.is_step = true; }
                else if (p == "DoubleStep") { move.is_step = true; }
                else if (p == "Block") { move.is_block = true; }
                else if (p == "Stance") { move.is_stance = true; }
                else if (p == "IdleStance") { move.is_stance = true; move.is_idle = true; }
                else if (p == "Unarmed") { move.weapon_filter = "Unarmed"; }
            }
        }
        auto move_end = xml.find("</Move>", pos);
        if (move_end == std::string::npos) { pos = end_tag; continue; }
        auto inner = xml.substr(end_tag + 1, move_end - end_tag - 1);
        // Parse Attack interval
        size_t ip = 0;
        while ((ip = inner.find("Type=\"Attack\"", ip)) != std::string::npos ||
               (ip = inner.find("Name=\"Attack\"", ip)) != std::string::npos) {
            auto ts = inner.rfind('<', ip);
            auto te = inner.find("/>", ip);
            if (ts != std::string::npos && te != std::string::npos) {
                auto iv = inner.substr(ts, te - ts);
                move.attack_start = (int)xml_attr_f(iv, "", 0, "Start");
                move.attack_end = (int)xml_attr_f(iv, "", 0, "End");
            }
            ip = te + 2;
        }
        // Parse attack edges
        ip = 0;
        while ((ip = inner.find("<Edge ", ip)) != std::string::npos) {
            auto te = inner.find("/>", ip);
            if (te == std::string::npos) break;
            auto ename = xml_attr(inner, "", ip, "Name");
            if (!ename.empty()) move.attack_edges.push_back(ename);
            ip = te + 2;
        }
        // Parse damage
        ip = 0;
        while ((ip = inner.find("<Damage ", ip)) != std::string::npos) {
            auto te = inner.find("/>", ip);
            if (te != std::string::npos) {
                auto val = xml_attr(inner, "", ip, "Value");
                if (!val.empty()) { move.damage = std::stof(val); break; }
            }
            ip = te + 2;
        }
        // Parse impulse
        ip = inner.find("<Impulse ");
        if (ip != std::string::npos) {
            auto te = inner.find("/>", ip);
            if (te != std::string::npos) {
                move.impulse_x = xml_attr_f(inner, "", ip, "X");
                move.impulse_y = xml_attr_f(inner, "", ip, "Y");
            }
        }
        // Parse keys
        ip = 0;
        while ((ip = inner.find("<Key ", ip)) != std::string::npos) {
            auto te = inner.find("/>", ip);
            if (te != std::string::npos) {
                auto kt = xml_attr(inner, "", ip, "Type");
                if (!kt.empty()) move.key_types.push_back(kt);
            }
            ip = te + 2;
        }
        // Parse block interval
        ip = 0;
        if ((ip = inner.find("Type=\"Block\"", 0)) != std::string::npos) {
            auto ts = inner.rfind('<', ip);
            auto te = inner.find("/>", ip);
            if (ts != std::string::npos && te != std::string::npos) {
                move.block_start = (int)xml_attr_f(inner, "", ts, "Start");
            }
        }
        // Parse Uninterrupt interval
        ip = 0;
        if ((ip = inner.find("Name=\"Uninterrupt\"", 0)) != std::string::npos) {
            auto ts = inner.rfind('<', ip);
            auto te = inner.find("/>", ip);
            if (ts != std::string::npos && te != std::string::npos) {
                move.uninterrupt_start = (int)xml_attr_f(inner, "", ts, "Start");
                move.uninterrupt_end = (int)xml_attr_f(inner, "", ts, "End");
            }
        }
        // Parse CurrentAnimation condition (for 3key combos)
        ip = 0;
        if ((ip = inner.find("<CurrentAnimation ", 0)) != std::string::npos) {
            auto te = inner.find("/>", ip);
            if (te != std::string::npos) {
                move.required_current_animation = xml_attr(inner, "", ip, "Name");
            }
        }
        pos = move_end + 7; // past "</Move>"
        // Convert 1-indexed frames from moves.xml to 0-indexed (as in original main.cpp)
        if (move.attack_start > 0) move.attack_start--;
        if (move.attack_end > 0) move.attack_end--;
        if (move.uninterrupt_start > 0) move.uninterrupt_start--;
        if (move.uninterrupt_end > 0) move.uninterrupt_end--;
        if (move.block_start > 0) move.block_start--;
        if (!move.name.empty() && !move.filename.empty())
            moves[move.name] = move;
    }
    return moves;
}

static std::vector<std::byte> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static std::string read_file_text(const std::string& path) {
    auto bytes = read_file_bytes(path);
    if (bytes.empty()) return {};
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Load texture from file (supports PNG, WebP, KTX via stb_image)
static bool load_texture_file(const std::string& path, ren::Texture2D& out) {
    auto bytes = read_file_bytes(path);
    if (bytes.empty()) return false;
    return out.init_from_memory(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

// Load JSON atlas (sf2_pc format) + WebP/KTX texture
static LoadedAtlas load_pc_atlas(const std::string& base_path) {
    LoadedAtlas result;
    std::string json_path = base_path + ".json";
    std::string tex_path = base_path + ".webp";

    auto json_text = read_file_text(json_path);
    if (json_text.empty()) {
        std::printf("[atlas] %s.json: not found\n", base_path.c_str());
        return result;
    }

    auto parsed = resf2::format::parse_json_atlas(json_text);
    if (!parsed) {
        std::printf("[atlas] %s.json: parse error (%s)\n",
                    base_path.c_str(), resf2::format::to_string(parsed.error()));
        return result;
    }

    auto& atlas = *parsed;

    // Determine texture path: scan directory for a matching texture file
    // sf2_pc uses hash-based filenames (dojo.d31b1e71.json + dojo.b920e18e.webp)
    // The hash differs between JSON and texture, so match on the semantic name prefix
    std::string full_tex_path = base_path;
    size_t last_slash = full_tex_path.find_last_of("/\\");
    std::string dir = (last_slash != std::string::npos)
        ? full_tex_path.substr(0, last_slash + 1) : "";
    std::string base_name = (last_slash != std::string::npos)
        ? full_tex_path.substr(last_slash + 1) : full_tex_path;
    // Strip trailing hash: "dojo.d31b1e71" -> "dojo"
    size_t dot = base_name.find('.');
    if (dot != std::string::npos) base_name = base_name.substr(0, dot);

    auto texture = std::make_unique<ren::Texture2D>();
    bool loaded = false;
    try {
        printf("[atlas] scanning dir: '%s' base='%s'\n", dir.c_str(), base_name.c_str());
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            auto fname = entry.path().filename().string();
            printf("[atlas]   file: '%s'\n", fname.c_str());
            // Match "basename.*.webp" or "basename.*.ktx" etc
            bool size_ok = fname.size() > base_name.size() + 2;
            bool prefix_ok = fname.compare(0, base_name.size(), base_name) == 0;
            bool dot_ok = fname[base_name.size()] == '.';
            printf("[atlas]     size_ok=%d prefix_ok=%d dot_ok=%d (base='%s' flen=%zu)\n", size_ok, prefix_ok, dot_ok, base_name.c_str(), fname.size());
            if (size_ok && prefix_ok && dot_ok) {
                std::string ext = entry.path().extension().string();
                printf("[atlas]     ext='%s'\n", ext.c_str());
                if (ext == ".webp" || ext == ".ktx" || ext == ".png" || ext == ".avif") {
                    auto fpath = entry.path().string();
                    printf("[atlas]     trying: '%s'\n", fpath.c_str());
                    if (load_texture_file(fpath, *texture)) {
                        printf("[atlas]     OK!\n");
                        loaded = true;
                        break;
                    } else {
                        printf("[atlas]     FAILED\n");
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        printf("[atlas] dir scan error: %s\n", e.what());
    }
    if (!loaded) {
        std::printf("[atlas] Failed to load texture for %s (dir=%s base=%s)\n", base_path.c_str(), dir.c_str(), base_name.c_str());
        return result;
    }

    // Build frame map
    for (auto& f : atlas.frames) {
        LoadedFrame lf;
        lf.texture = texture.get();
        lf.w = static_cast<float>(f.sw > 0 ? f.sw : f.w);
        lf.h = static_cast<float>(f.sh > 0 ? f.sh : f.h);
        lf.u0 = static_cast<float>(f.x) / atlas.meta.w;
        lf.v0 = static_cast<float>(f.y) / atlas.meta.h;
        lf.u1 = static_cast<float>(f.x + f.w) / atlas.meta.w;
        lf.v1 = static_cast<float>(f.y + f.h) / atlas.meta.h;
        lf.owned = false;
        result.frames[f.filename] = std::move(lf);
    }
    result.owned_textures.push_back(std::move(texture));

    std::printf("[atlas] %s.json: %zu frames (%dx%d)\n",
                base_path.c_str(), result.frames.size(), atlas.meta.w, atlas.meta.h);
    return result;
}

// Load mobile plist atlas + PNG (fallback)
static LoadedAtlas load_mobile_atlas(const std::string& base_path) {
    LoadedAtlas result;
    std::string plist_path = base_path + ".plist";
    std::string png_path = base_path + ".png";

    auto plist_xml = read_file_text(plist_path);
    if (plist_xml.empty()) {
        std::printf("[atlas] %s.plist: not found\n", base_path.c_str());
        return result;
    }

    auto parsed = resf2::reverse::plist::parse(plist_xml);
    if (!parsed) {
        std::printf("[atlas] %s.plist: parse error\n", base_path.c_str());
        return result;
    }

    auto& atlas = *parsed;
    auto png_bytes = read_file_bytes(png_path);
    if (png_bytes.empty()) {
        std::printf("[atlas] %s.png: not found\n", base_path.c_str());
        return result;
    }

    auto texture = std::make_unique<ren::Texture2D>();
    if (!texture->init_from_png(
            reinterpret_cast<const uint8_t*>(png_bytes.data()), png_bytes.size())) {
        std::printf("[atlas] %s.png: texture load failed\n", base_path.c_str());
        return result;
    }

    for (auto& f : atlas.frames) {
        std::string name = f.name;
        if (name.ends_with(".png")) name.resize(name.size() - 4);
        LoadedFrame lf;
        lf.texture = texture.get();
        lf.w = static_cast<float>(f.source_w);
        lf.h = static_cast<float>(f.source_h);
        lf.u0 = static_cast<float>(f.atlas_x) / atlas.metadata.texture_w;
        lf.v0 = static_cast<float>(f.atlas_y) / atlas.metadata.texture_h;
        lf.u1 = static_cast<float>(f.atlas_x + f.atlas_w) / atlas.metadata.texture_w;
        lf.v1 = static_cast<float>(f.atlas_y + f.atlas_h) / atlas.metadata.texture_h;
        lf.owned = false;
        result.frames[name] = std::move(lf);
    }
    result.owned_textures.push_back(std::move(texture));
    std::printf("[atlas] %s.plist: %zu frames\n", base_path.c_str(), result.frames.size());
    return result;
}

// Try PC first, fallback to mobile
static LoadedAtlas load_atlas(const std::string& name, const std::string& pc_root, const std::string& mobile_root) {
    // PC: sf2_pc/www/res/locations/{name}/{name}.json + .webp
    std::string pc_path = pc_root + "/locations/" + name + "/" + name;
    auto pc_result = load_pc_atlas(pc_path);
    if (!pc_result.frames.empty()) return pc_result;

    // Mobile: assets/1536/locations/{name}/{name}.plist + .png
    std::string mobile_path = mobile_root + "/1536/locations/" + name + "/" + name;
    return load_mobile_atlas(mobile_path);
}

// ========== Renderer2D adapter ==========
class Ren2DAdapter final : public core::Renderer2D {
public:
    ren::Renderer& r_;
    explicit Ren2DAdapter(ren::Renderer& r) : r_(r) {}

    bool begin_frame() override { return true; }
    void end_frame() override {}
    void clear(const core::Color& c) override {
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void set_viewport(int x, int y, int w, int h) override { r_.resize(w, h); }
    void set_scissor(int x, int y, int w, int h) override {}
    void push_transform(const core::Mat4&) override {}
    void pop_transform() override {}
    uint32_t create_texture(int, int, const void*) override { return 0; }
    void destroy_texture(uint32_t) override {}

    void draw_quad(const core::DrawQuad& q) override {
        ren::Color4B c;
        c.r = (uint8_t)((q.color >> 24) & 0xFF);
        c.g = (uint8_t)((q.color >> 16) & 0xFF);
        c.b = (uint8_t)((q.color >> 8) & 0xFF);
        c.a = (uint8_t)(q.color & 0xFF);
        r_.draw_filled_rect_screen(q.x, q.y, q.w, q.h, c);
    }
    void draw_rect(float x, float y, float w, float h, uint32_t color) override {
        ren::Color4B c;
        c.r = (uint8_t)((color >> 24) & 0xFF);
        c.g = (uint8_t)((color >> 16) & 0xFF);
        c.b = (uint8_t)((color >> 8) & 0xFF);
        c.a = (uint8_t)(color & 0xFF);
        r_.draw_filled_rect_screen(x, y, w, h, c);
    }
    void draw_line(float, float, float, float, uint32_t) override {}
    void flush() override {}
};

// ========== Character rendering helpers ==========
static void draw_capsule_world(ren::Renderer& r,
    float x1, float y1, float x2, float y2,
    float radius, ren::Color4B color) {

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) {
        // Degenerate: just a circle
        r.draw_filled_circle_world(x1, y1, radius, color);
        return;
    }
    // Unit perpendicular
    float perp_x = -dy / len * radius;
    float perp_y = dx / len * radius;

    // Four corners of the capsule body quad
    float ax = x1 + perp_x, ay = y1 + perp_y;
    float bx = x1 - perp_x, by = y1 - perp_y;
    float cx = x2 + perp_x, cy = y2 + perp_y;
    float dx_ = x2 - perp_x, dy_ = y2 - perp_y;

    // Body quad as two triangles
    r.draw_filled_triangle_world(ax, ay, bx, by, cx, cy, color);
    r.draw_filled_triangle_world(bx, by, cx, cy, dx_, dy_, color);

    // End caps
    r.draw_filled_circle_world(x1, y1, radius, color);
    r.draw_filled_circle_world(x2, y2, radius, color);
}

// ========== Location Scene ==========
class LocationScene : public core::State {
    resf2::format::LocationData loc_;
    std::vector<LoadedAtlas> atlases_;
    bool loaded_ = false;
    fs::path root_{"assets"};
    float camera_x_ = 0;
    float bound_left_ = 0, bound_right_ = 1960;
    float bg_r_ = 0.15f, bg_g_ = 0.08f, bg_b_ = 0.035f;
    int frame_count_ = 0;
    float fps_ = 0;
    float fps_timer_ = 0;
    int screen_w_ = 800, screen_h_ = 640;
    std::string location_name_ = "dojo";

    // ===== Skeleton =====
    struct Skeleton {
        std::vector<std::string> node_names;
        std::unordered_map<std::string, int> name_to_idx;
        struct Edge {
            int i1, i2;
            float radius;
        };
        std::vector<Edge> edges;
        std::vector<float> rest_x, rest_y;
    };
    Skeleton skeleton_;

    // ===== Animation =====
    std::unordered_map<std::string, resf2::fight::AnimationClip> clips_;
    resf2::fight::AnimationPlayer player_;
    resf2::fight::AnimationPlayer enemy_player_;
    bool char_loaded_ = false;
    std::string current_anim_ = "fists1_stance_idle";
    bool show_weapon_ = false;
    float player_health_ = 100, enemy_health_ = 100;
    bool prev_keys_[512] = {};
    // Combat state
    float enemy_ai_timer_ = 3.0f;
    float ko_timer_ = 0;
    bool ko_ = false;
    float round_timer_ = 99.0f;
    float shake_amp_ = 0, shake_dur_ = 0;
    // Jump physics (Y-DOWN convention: negative = up)
    float player_base_y_ = 0;
    float player_jump_y_ = 0;
    float player_vel_y_ = 0;
    float enemy_base_y_ = 0;
    float enemy_jump_y_ = 0;
    float enemy_vel_y_ = 0;
    float gravity_ = 1500.0f;
    float player_hitstun_ = 0;
    float enemy_hitstun_ = 0;
    bool player_facing_right_ = true;

    // ===== Move/Combat System =====
    std::unordered_map<std::string, MoveDef> moves_;
    std::string current_move_;
    float anim_time_ = 0;
    int move_state_ = 0; // 0=IDLE, 1=MV_BACK, 2=MV_FWD, 10=ATTACK
    bool is_uninterrupt_ = false;
    std::string enemy_current_move_;
    std::string enemy_current_anim_;
    float enemy_anim_time_ = 0;
    int enemy_move_state_ = 0;
    bool enemy_is_uninterrupt_ = false;
    // Step latching
    int fwd_held_ms_ = 0;
    int back_held_ms_ = 0;
    float step_play_time_ = 0;
    float enemy_step_timer_ = 0;
    // Hit detection per-frame flag
    bool player_hit_this_frame_ = false;
    bool enemy_hit_this_frame_ = false;
    // Animation speed (20fps for fighting game)
    float anim_speed_ = 20.0f;
    uint64_t total_frame_count_ = 0;
    bool prev_up_state_ = false;

    const LoadedFrame* find_frame(const std::string& name) const {
        for (auto& a : atlases_) {
            auto it = a.frames.find(name);
            if (it != a.frames.end()) return &it->second;
        }
        return nullptr;
    }

    void load_skeleton(const fs::path& path) {
        auto xml = read_file_text(path.string());
        if (xml.empty()) {
            std::printf("[skeleton] not found: %s\n", path.string().c_str());
            return;
        }

        // Extract skeleton node names in XML order.
        // skeleton.xml uses the tag name as the node name (e.g. <NTop ...> = "NTop").
        skeleton_.node_names.clear();
        skeleton_.name_to_idx.clear();

        std::vector<std::string> tag_names = {
            "NTop", "NNeck", "NShoulder_2", "NShoulder_1",
            "NElbow_2", "NElbow_1", "NWrist_2", "NWrist_1",
            "NFingertipsSS_2", "NFingertipsSS_1",
            "NHip_2", "NHip_1", "NKnee_2", "NKnee_1",
            "NAnkle_2", "NAnkle_1", "NToe_2", "NToe_1",
            "NPivot",
            "Weapon-Node1_1", "Weapon-Node2_1", "Weapon-Node3_1", "Weapon-Node4_1",
            "Weapon-Node1_2", "Weapon-Node2_2", "Weapon-Node3_2", "Weapon-Node4_2",
            "NStomach", "NChest",
            "NToeTip_2", "NHeel_2", "NHeel_1", "NToeS_2", "NToeTip_1", "NToeS_1",
            "NKnuckles_2", "NKnucklesS_2", "NKnuckles_1", "NKnucklesS_1",
            "NFingertips_2", "NFingertips_1", "NFingertipsS_2", "NFingertipsS_1",
            "NHead",
            "NChestS_2", "NChestS_1", "NStomachS_2", "NStomachS_1",
            "NChestF", "NStomachF", "NPelvisF",
            "NHeadS_2", "NHeadS_1", "NHeadF",
            "COM",
            "MacroNode1_2", "MacroNode2_2", "MacroNode3_2", "MacroNode4_2",
            "MacroNode5_2", "MacroNode6_2",
            "MacroNode1_1", "MacroNode2_1", "MacroNode3_1", "MacroNode4_1",
            "MacroNode5_1", "MacroNode6_1",
        };

        for (auto& tn : tag_names) {
            auto ps = find_tags(xml, tn);
            for (auto pos : ps) {
                if (skeleton_.name_to_idx.count(tn)) continue;
                int idx = (int)skeleton_.node_names.size();
                skeleton_.node_names.push_back(tn);
                skeleton_.name_to_idx[tn] = idx;
                skeleton_.rest_x.push_back(xml_attr_f(xml, tn, pos, "X"));
                skeleton_.rest_y.push_back(xml_attr_f(xml, tn, pos, "Y"));
            }
        }

        // Collect visual edges (those with Radius attribute).
        // Edges have unique tag names (EHead, ENec, etc.) with Type="Edge".
        size_t ed_pos = 0;
        while ((ed_pos = xml.find("Type=\"Edge\"", ed_pos)) != std::string::npos) {
            auto e1 = xml_attr(xml, "", ed_pos, "End1");
            auto e2 = xml_attr(xml, "", ed_pos, "End2");
            auto rad_str = xml_attr(xml, "", ed_pos, "Radius");
            ed_pos += 10; // past Type="Edge"
            if (e1.empty() || e2.empty() || rad_str.empty()) continue;

            auto it1 = skeleton_.name_to_idx.find(e1);
            auto it2 = skeleton_.name_to_idx.find(e2);
            if (it1 == skeleton_.name_to_idx.end() || it2 == skeleton_.name_to_idx.end())
                continue;

            float r = std::stof(rad_str);
            if (r <= 0) continue;

            skeleton_.edges.push_back({it1->second, it2->second, r});
        }

        std::printf("[skeleton] %zu nodes, %zu edges\n",
                    skeleton_.node_names.size(), skeleton_.edges.size());
    }

    struct CharColors {
        ren::Color4B skin, pants, torso, shoe, belt, head;
    };

    void render_one_character(ren::Renderer& r, const resf2::fight::AnimationPlayer& player,
                              float pos_x, float pos_y, bool flip_x,
                              const CharColors& colors) {
        if (!player.is_playing()) return;
        auto& pose = player.current_pose();
        if (pose.positions.size() < skeleton_.node_names.size()) return;

        int pivot_idx = skeleton_.name_to_idx.count("NPivot") ?
                        skeleton_.name_to_idx.at("NPivot") : 18;
        float np_x = pose.positions[pivot_idx].x;
        float np_y = pose.positions[pivot_idx].y;

        // Compute world positions for all nodes
        std::vector<float> wx(skeleton_.node_names.size());
        std::vector<float> wy(skeleton_.node_names.size());
        int n = (int)std::min(pose.positions.size(), skeleton_.node_names.size());
        for (int i = 0; i < n; i++) {
            float lx = pose.positions[i].x - np_x;
            float ly = pose.positions[i].y - np_y;
            wx[i] = pos_x + (flip_x ? -lx : lx);
            wy[i] = pos_y + ly;
        }

        // Draw each edge as a capsule with body-part-specific color
        for (auto& e : skeleton_.edges) {
            bool valid = e.i1 < n && e.i2 < n;
            if (!valid) continue;

            auto& n1 = skeleton_.node_names[e.i1];
            auto& n2 = skeleton_.node_names[e.i2];
            bool is_leg = (n1.find("Hip") != std::string::npos || n1.find("Knee") != std::string::npos ||
                           n2.find("Hip") != std::string::npos || n2.find("Knee") != std::string::npos ||
                           n1.find("Ankle") != std::string::npos || n2.find("Ankle") != std::string::npos ||
                           n1.find("Toe") != std::string::npos || n2.find("Toe") != std::string::npos ||
                           n1.find("Heel") != std::string::npos || n2.find("Heel") != std::string::npos);
            bool is_torso = (n1.find("Stomach") != std::string::npos || n2.find("Stomach") != std::string::npos ||
                            n1.find("Chest") != std::string::npos || n2.find("Chest") != std::string::npos ||
                            n1.find("Pelvis") != std::string::npos || n2.find("Pelvis") != std::string::npos ||
                            n1.find("Groin") != std::string::npos || n2.find("Groin") != std::string::npos);
            bool is_head = (n1.find("Head") != std::string::npos || n2.find("Head") != std::string::npos ||
                           n1.find("Neck") != std::string::npos || n2.find("Neck") != std::string::npos ||
                           n1.find("Top") != std::string::npos || n2.find("Top") != std::string::npos);
            bool is_arm = (n1.find("Shoulder") != std::string::npos || n2.find("Shoulder") != std::string::npos ||
                          n1.find("Elbow") != std::string::npos || n2.find("Elbow") != std::string::npos ||
                          n1.find("Wrist") != std::string::npos || n2.find("Wrist") != std::string::npos ||
                          n1.find("Knuckle") != std::string::npos || n2.find("Knuckle") != std::string::npos);
            bool is_foot = (n1.find("Toe") != std::string::npos || n2.find("Toe") != std::string::npos ||
                           n1.find("Heel") != std::string::npos || n2.find("Heel") != std::string::npos ||
                           n1.find("Instep") != std::string::npos || n2.find("Instep") != std::string::npos);

            ren::Color4B c = colors.skin;
            if (is_head) c = colors.head;
            else if (is_torso) c = colors.torso;
            else if (is_leg) c = colors.pants;
            else if (is_arm) c = colors.skin;
            else if (is_foot) c = colors.shoe;
            else c = colors.belt;

            draw_capsule_world(r, wx[e.i1], wy[e.i1], wx[e.i2], wy[e.i2], e.radius, c);
        }
    }

    void render_characters(ren::Renderer& r) {
        CharColors player_colors = {
            {230, 180, 140, 255},  // skin
            {80, 60, 120, 255},    // pants
            {180, 40, 40, 255},    // torso
            {60, 40, 30, 255},     // shoe
            {160, 120, 40, 255},   // belt
            {230, 180, 140, 255},  // head
        };
        CharColors enemy_colors = {
            {160, 120, 100, 255},  // skin (darker)
            {60, 50, 40, 255},     // pants
            {100, 30, 30, 255},    // torso
            {40, 30, 20, 255},     // shoe
            {100, 80, 30, 255},    // belt
            {160, 120, 100, 255},  // head
        };

        float player_y = loc_.player_y;  // NOT negated — skeleton model space is Y-UP with NPivot at 169
        float enemy_y = loc_.enemy_y;
        float player_cx = loc_.player_x - loc_.width * 0.5f;
        float enemy_cx = loc_.enemy_x - loc_.width * 0.5f;
        render_one_character(r, player_, player_cx, player_y, !player_facing_right_, player_colors);
        render_one_character(r, enemy_player_, enemy_cx, enemy_y, player_facing_right_, enemy_colors);
    }

    void render_weapon(ren::Renderer& r) {
        if (!show_weapon_ || !player_.is_playing()) return;
        auto& pose = player_.current_pose();
        if (pose.positions.size() < 22) return;

        float px = loc_.player_x - loc_.width * 0.5f;
        float py = loc_.player_y;
        int pivot_idx = skeleton_.name_to_idx.count("NPivot") ? skeleton_.name_to_idx.at("NPivot") : 18;
        float np_x = pose.positions[pivot_idx].x;
        float np_y = pose.positions[pivot_idx].y;

        int w1 = skeleton_.name_to_idx.count("Weapon-Node1_1") ? skeleton_.name_to_idx.at("Weapon-Node1_1") : 19;
        int w3 = skeleton_.name_to_idx.count("Weapon-Node3_1") ? skeleton_.name_to_idx.at("Weapon-Node3_1") : 21;
        if (w1 >= (int)pose.positions.size() || w3 >= (int)pose.positions.size()) return;

        float wx1 = pose.positions[w1].x - np_x + px;
        float wy1 = pose.positions[w1].y - np_y + py;
        float wx3 = pose.positions[w3].x - np_x + px;
        float wy3 = pose.positions[w3].y - np_y + py;

        auto steel = ren::Color4B{180, 190, 200, 255};
        auto hilt = ren::Color4B{120, 80, 40, 255};

        draw_capsule_world(r, wx1, wy1, wx3, wy3, 5, steel);
        float dx = wx1 - wx3, dy = wy1 - wy3;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1) {
            float hx = wx1 + dx / len * 15;
            float hy = wy1 + dy / len * 15;
            draw_capsule_world(r, wx1, wy1, hx, hy, 3, hilt);
        }
    }

    void render_ui(ren::Renderer& r) {
        float bar_w = 260, bar_h = 22;
        float bar_y = 20;
        float bar_x_left = 30;
        float bar_x_right = (float)screen_w_ - 30 - bar_w;

        auto bg = ren::Color4B{40, 40, 40, 200};
        auto border = ren::Color4B{180, 180, 160, 255};
        auto hp_green = ren::Color4B{50, 200, 50, 255};
        auto hp_red = ren::Color4B{200, 30, 30, 255};
        auto hp_yellow = ren::Color4B{200, 200, 30, 255};

        auto draw_bar = [&](float bx, float hp, ren::Color4B fill_color) {
            r.draw_filled_rect_screen(bx - 2, bar_y - 2, bar_w + 4, bar_h + 4, border);
            r.draw_filled_rect_screen(bx, bar_y, bar_w, bar_h, bg);
            float fill = bar_w * (hp / 100.0f);
            if (fill > 0)
                r.draw_filled_rect_screen(bx, bar_y, fill, bar_h, fill_color);
        };

        auto player_fill = player_health_ > 50 ? hp_green : (player_health_ > 25 ? hp_yellow : hp_red);
        auto enemy_fill = enemy_health_ > 50 ? hp_green : (enemy_health_ > 25 ? hp_yellow : hp_red);
        draw_bar(bar_x_left, player_health_, player_fill);
        draw_bar(bar_x_right, enemy_health_, enemy_fill);

        // Round timer bar (center-top)
        {
            float tbw = 100, tbh = 12;
            float tb_x = screen_w_ * 0.5f - tbw / 2;
            float tb_y = 8;
            r.draw_filled_rect_screen(tb_x - 1, tb_y - 1, tbw + 2, tbh + 2, border);
            r.draw_filled_rect_screen(tb_x, tb_y, tbw, tbh, bg);
            float fill = tbw * (round_timer_ / 99.0f);
            if (fill > 0) {
                auto tc = round_timer_ > 30 ? hp_green : (round_timer_ > 10 ? hp_yellow : hp_red);
                r.draw_filled_rect_screen(tb_x, tb_y, fill, tbh, tc);
            }
        }

        // KO overlay
        if (ko_) {
            float ox = screen_w_ * 0.5f, oy = screen_h_ * 0.4f;
            r.draw_filled_rect_screen(ox - 110, oy - 45, 220, 90, {200, 30, 30, 200});
            r.draw_filled_rect_screen(ox - 105, oy - 40, 210, 80, {0, 0, 0, 220});
            // Simple "K.O." indicator using filled blocks
            float by = oy - 15, bh = 30;
            // K
            float bx = ox - 70;
            r.draw_filled_rect_screen(bx, by, 8, bh, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx + 22, by, 8, bh, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx + 8, by + 11, 14, 8, {255, 50, 50, 255});
            // O
            bx = ox - 18;
            r.draw_filled_rect_screen(bx, by, 8, bh, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx + 28, by, 8, bh, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx + 8, by, 20, 8, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx + 8, by + 22, 20, 8, {255, 50, 50, 255});
            // ! 
            bx = ox + 45;
            r.draw_filled_rect_screen(bx, by, 8, bh, {255, 50, 50, 255});
            r.draw_filled_rect_screen(bx, by + bh + 4, 8, 8, {255, 50, 50, 255});
        }
    }

public:
    void set_assets_root(const fs::path& p) { root_ = p; }
    void set_screen_size(int w, int h) { screen_w_ = w; screen_h_ = h; }
    void set_location_name(const std::string& n) { location_name_ = n; }

    void on_enter() override {
        std::printf("[%s] loading assets...\n", location_name_.c_str());
    }

    // Raw input state recorded for update()
    bool raw_key_up_ = false, raw_key_down_ = false;
    bool raw_key_left_ = false, raw_key_right_ = false;
    bool punch_just_pressed_ = false, kick_just_pressed_ = false;

    void set_input_state(const bool* keys) {
        // Store raw keys for update() to process
        raw_key_up_ = keys[(size_t)plat::Key::W] || keys[(size_t)plat::Key::ArrowUp];
        raw_key_down_ = keys[(size_t)plat::Key::S] || keys[(size_t)plat::Key::ArrowDown];
        raw_key_left_ = keys[(size_t)plat::Key::A] || keys[(size_t)plat::Key::ArrowLeft];
        raw_key_right_ = keys[(size_t)plat::Key::D] || keys[(size_t)plat::Key::ArrowRight];
        // Arrow keys scroll camera for debug
        float cam_speed = 800.0f * (1.0f / 60.0f);
        if (keys[(size_t)plat::Key::ArrowLeft]) camera_x_ -= cam_speed;
        if (keys[(size_t)plat::Key::ArrowRight]) camera_x_ += cam_speed;

        // Just-pressed detection for punch/kick
        size_t oi = (size_t)plat::Key::O;
        size_t pi = (size_t)plat::Key::P;
        size_t sp = (size_t)plat::Key::Space;
        size_t kk = (size_t)plat::Key::K;
        punch_just_pressed_ = keys[oi] && !prev_keys_[oi];
        if (keys[sp] && !prev_keys_[sp]) punch_just_pressed_ = true;
        kick_just_pressed_ = keys[pi] && !prev_keys_[pi];
        if (keys[kk] && !prev_keys_[kk]) kick_just_pressed_ = true;
        prev_keys_[oi] = keys[oi];
        prev_keys_[pi] = keys[pi];
        prev_keys_[sp] = keys[sp];
        prev_keys_[kk] = keys[kk];

        // Debug keys (always active)
        size_t k8 = (size_t)plat::Key::Num8;
        if (keys[k8] && !prev_keys_[k8]) show_weapon_ = !show_weapon_;
        prev_keys_[k8] = keys[k8];

        size_t k9 = (size_t)plat::Key::Num9;
        if (keys[k9] && !prev_keys_[k9]) {
            enemy_health_ = std::max(0.0f, enemy_health_ - 15);
            player_health_ = std::max(0.0f, player_health_ - 5);
        }
        prev_keys_[k9] = keys[k9];
    }

    void update(float dt) override {
        frame_count_++;
        fps_timer_ += dt;
        if (fps_timer_ >= 1.0f) {
            fps_ = frame_count_ / fps_timer_;
            frame_count_ = 0;
            fps_timer_ = 0;
        }

        if (char_loaded_) {
            if (player_.is_playing())
                player_.update(dt);
            if (enemy_player_.is_playing())
                enemy_player_.update(dt);
        }

        // Combat logic (only after scene is fully loaded)
        if (loaded_) {
            // Camera auto-follows player X (center-origin)
            camera_x_ = (loc_.player_x - loc_.width * 0.5f) + screen_w_ * 0.25f;

            // Hitstun decay
            if (player_hitstun_ > 0) player_hitstun_ -= dt;
            if (enemy_hitstun_ > 0) enemy_hitstun_ -= dt;

            // Jump physics (apply to player and enemy Y in Y-DOWN)
            auto update_jump = [&](float& base, float& jump, float& vel, float dt) {
                if (jump != 0 || vel != 0) {
                    vel += gravity_ * dt;
                    jump += vel * dt;
                    if (jump > 0) { jump = 0; vel = 0; }
                }
            };
            update_jump(player_base_y_, player_jump_y_, player_vel_y_, dt);
            update_jump(enemy_base_y_, enemy_jump_y_, enemy_vel_y_, dt);
            loc_.player_y = player_base_y_ + player_jump_y_;
            loc_.enemy_y = enemy_base_y_ + enemy_jump_y_;

            // Round timer
            if (!ko_) {
                round_timer_ -= dt;
                if (round_timer_ <= 0) {
                    round_timer_ = 0;
                    ko_ = true;
                    ko_timer_ = 0;
                    std::printf("[fight] Time over! ");
                    if (player_health_ > enemy_health_) std::printf("Player wins!\n");
                    else if (enemy_health_ > player_health_) std::printf("Enemy wins!\n");
                    else std::printf("Draw!\n");
                }
            }

            if (!ko_) {
            // === FACING ===
            bool facing = loc_.player_x < loc_.enemy_x;
            player_facing_right_ = facing;

            // === INPUT PROCESSING (facing-relative) ===
            bool key_up = raw_key_up_;
            bool key_down = raw_key_down_;
            bool key_left = raw_key_left_;
            bool key_right = raw_key_right_;
            bool key_forward = player_facing_right_ ? key_right : key_left;
            bool key_back = player_facing_right_ ? key_left : key_right;

            // Step latching
            if (key_forward) fwd_held_ms_ = 200;
            else if (fwd_held_ms_ > 0) fwd_held_ms_ -= (int)(dt * 1000);
            if (key_back) back_held_ms_ = 200;
            else if (back_held_ms_ > 0) back_held_ms_ -= (int)(dt * 1000);

            bool in_attack = (move_state_ == 10 && !current_move_.empty());
            bool attack_anim_playing = in_attack && player_.is_playing();

            // Any finished non-looping anim (attack, hit reaction, jump) → idle.
            // Looping anims (idle, steps) never set finished_, so this is safe.
            // Note: is_playing() stays true after finish (clip_ is not nulled).
            if (player_.is_playing() && player_.is_finished()) {
                move_state_ = 0;
                current_move_.clear();
                current_anim_ = "fists1_stance_idle";
                auto it = clips_.find("fists1_stance_idle");
                if (it != clips_.end()) player_.play(&it->second, true);
            }

            // === UNINTERRUPT CHECK ===
            is_uninterrupt_ = false;
            if (attack_anim_playing && !current_move_.empty()) {
                auto mit = moves_.find(current_move_);
                if (mit != moves_.end()) {
                    auto& move = mit->second;
                    int cur_frame = (int)(player_.time() * anim_speed_);
                    if (move.uninterrupt_start >= 0 && cur_frame >= move.uninterrupt_start &&
                        (move.uninterrupt_end < 0 || cur_frame <= move.uninterrupt_end)) {
                        is_uninterrupt_ = true;
                    }
                }
            }
            // Enemy uninterrupt check (attacks in this window can't be interrupted by hits)
            enemy_is_uninterrupt_ = false;
            if (enemy_move_state_ == 10 && enemy_player_.is_playing() && !enemy_current_move_.empty()) {
                auto mit = moves_.find(enemy_current_move_);
                if (mit != moves_.end()) {
                    auto& move = mit->second;
                    int cur_frame = (int)(enemy_player_.time() * anim_speed_);
                    if (move.uninterrupt_start >= 0 && cur_frame >= move.uninterrupt_start &&
                        (move.uninterrupt_end < 0 || cur_frame <= move.uninterrupt_end)) {
                        enemy_is_uninterrupt_ = true;
                    }
                }
            }

            // === COMBAT MOVE SELECTION ===
            if (punch_just_pressed_ || kick_just_pressed_) {
                std::printf("[input] %s pressed, in_attack=%d unint=%d\n",
                    punch_just_pressed_ ? "PUNCH" : "KICK", in_attack, is_uninterrupt_);
                std::string cur_direction = "Central";
                if (key_up && key_forward) cur_direction = "UpForward";
                else if (key_up && key_back) cur_direction = "UpBack";
                else if (key_down && key_forward) cur_direction = "DownForward";
                else if (key_down && key_back) cur_direction = "DownBack";
                else if (key_up) cur_direction = "Up";
                else if (key_down) cur_direction = "Down";
                else if (key_forward) cur_direction = "Forward";
                else if (key_back) cur_direction = "Back";

                std::string cur_move_type = punch_just_pressed_ ? "Punch" : "Kick";

                bool block_all_combat = attack_anim_playing && !is_uninterrupt_;
                const MoveDef* best_move = nullptr;

                for (auto& [name, move] : moves_) {
                    if (move.filename.empty() || move.template_name.empty()) continue;
                    if (move.template_name.find("Titan") != std::string::npos &&
                        move.template_name.find("NotTitan") == std::string::npos) continue;
                    if (move.move_type != cur_move_type) continue;
                    if (block_all_combat) continue;
                    if (attack_anim_playing && is_uninterrupt_) {
                        if (move.key_count != 3) continue;
                        if (!move.required_current_animation.empty() &&
                            current_move_ != move.required_current_animation) continue;
                    } else {
                        if (move.key_count == 3) continue;
                    }
                    if (move.direction != cur_direction) continue;
                    // Exact weapon match: empty or "Fists" only.
                    // ("PowerFists" contains "Fists" substring but is a different
                    // weapon with a 209-node skeleton — must be excluded!)
                    if (!move.tactic_weapon.empty() && move.tactic_weapon != "Fists") continue;
                    std::string an = move.filename;
                    if (an.size() > 4 && an.substr(an.size() - 4) == ".bin")
                        an = an.substr(0, an.size() - 4);
                    if (!clips_.count(an)) continue;
                    if (!best_move || move.priority > best_move->priority)
                        best_move = &move;
                }

                if (best_move) {
                    std::string an = best_move->filename;
                    if (an.size() > 4 && an.substr(an.size() - 4) == ".bin")
                        an = an.substr(0, an.size() - 4);
                    auto it = clips_.find(an);
                    if (it != clips_.end()) {
                        player_.play(&it->second, false);
                        current_anim_ = an;
                        current_move_ = best_move->name;
                        anim_time_ = 0;
                        move_state_ = 10;
                        is_uninterrupt_ = false;
                        player_hit_this_frame_ = false;
                        std::printf("[player] %s -> %s (prio=%d) anim='%s'\n",
                                    cur_move_type.c_str(), best_move->name.c_str(), best_move->priority, an.c_str());
                    }
                } else {
                    std::printf("[input] NO BEST MOVE for %s dir=%s (block_all=%d)\n",
                        cur_move_type.c_str(), cur_direction.c_str(), block_all_combat);
                }
            }

            // === MOVE KEYS (no-punch/kick direction moves: steps, jump) ===
            if (!attack_anim_playing) {
                // Jump (locks state so steps can't interrupt it mid-air)
                if (key_up && !prev_up_state_) {
                    if (player_jump_y_ == 0) {
                        player_vel_y_ = -500.0f;
                        player_jump_y_ = -1.0f;
                        auto it = clips_.find("jump");
                        if (it != clips_.end()) {
                            player_.play(&it->second, false);
                            current_anim_ = "jump";
                            current_move_ = "Jump";
                            move_state_ = 10; // lock until jump anim finishes
                        }
                    }
                }
                // Step movement (facing-relative displacement)
                float step_dir = player_facing_right_ ? 1.0f : -1.0f;
                if (key_forward && !key_back) {
                    if (current_anim_ != "step_forward") {
                        auto it = clips_.find("step_forward");
                        if (it != clips_.end()) {
                            player_.play(&it->second, true);
                            current_anim_ = "step_forward";
                            current_move_ = "StepForward";
                            move_state_ = 2;
                        }
                    }
                    loc_.player_x += step_dir * 300.0f * dt;
                } else if (key_back && !key_forward) {
                    if (current_anim_ != "step_back") {
                        auto it = clips_.find("step_back");
                        if (it != clips_.end()) {
                            player_.play(&it->second, true);
                            current_anim_ = "step_back";
                            current_move_ = "StepBack";
                            move_state_ = 1;
                        }
                    }
                    loc_.player_x -= step_dir * 300.0f * dt;
                } else if (!attack_anim_playing && !punch_just_pressed_ && !kick_just_pressed_ &&
                           !key_up && !key_down && !key_forward && !key_back) {
                    // No direction pressed → idle stance
                    if (current_anim_ != "fists1_stance_idle" && current_anim_ != "stance_2") {
                        auto it = clips_.find("fists1_stance_idle");
                        if (it != clips_.end()) {
                            player_.play(&it->second, true);
                            current_anim_ = "fists1_stance_idle";
                            current_move_.clear();
                            move_state_ = 0;
                        }
                    }
                }
            }
            prev_up_state_ = key_up;

            // === HIT DETECTION (player attack hitting enemy) ===
            if (attack_anim_playing && !player_hit_this_frame_) {
                auto mit = moves_.find(current_move_);
                if (mit != moves_.end()) {
                    auto& move = mit->second;
                    int cur_frame = (int)(player_.time() * anim_speed_);
                    bool in_attack_window = (cur_frame >= move.attack_start && cur_frame <= move.attack_end);
                    if (in_attack_window && move.attack_start >= 0) {
                        // Simple distance-based hit check (simplified segment-segment)
                        float dx = loc_.player_x - loc_.enemy_x;
                        float dy = loc_.player_y - loc_.enemy_y;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist < 160.0f) {
                            enemy_health_ = std::max(0.0f, enemy_health_ - move.damage * 100.0f);
                            player_hit_this_frame_ = true;
                            shake_amp_ = 6.0f; shake_dur_ = 0.2f;
                            if (!enemy_is_uninterrupt_) {
                                // Interrupt enemy: hit reaction + clear their attack state
                                // (prevents phantom hits from their interrupted attack)
                                enemy_hitstun_ = 0.3f;
                                auto hit = clips_.find("high_hit_short");
                                if (hit != clips_.end()) enemy_player_.play(&hit->second, false);
                                enemy_move_state_ = 0;
                                enemy_current_move_.clear();
                                enemy_hit_this_frame_ = false;
                            }
                            std::printf("[hit] player hits! dmg=%.0f enemy HP=%.0f\n",
                                        move.damage * 100, enemy_health_);
                        }
                    }
                }
            }

            // === ENEMY HIT DETECTION ===
            {
                bool enemy_attack_playing = enemy_move_state_ == 10 && enemy_player_.is_playing();
                if (enemy_attack_playing && !enemy_hit_this_frame_) {
                    auto mit = moves_.find(enemy_current_move_);
                    if (mit != moves_.end()) {
                        auto& move = mit->second;
                        int cur_frame = (int)(enemy_player_.time() * anim_speed_);
                        bool in_attack_window = (cur_frame >= move.attack_start && cur_frame <= move.attack_end);
                        if (in_attack_window && move.attack_start >= 0) {
                            float dx = loc_.player_x - loc_.enemy_x;
                            float dy = loc_.player_y - loc_.enemy_y;
                            float dist = std::sqrt(dx * dx + dy * dy);
                            if (dist < 160.0f) {
                                player_health_ = std::max(0.0f, player_health_ - move.damage * 100.0f);
                                enemy_hit_this_frame_ = true;
                                shake_amp_ = 6.0f; shake_dur_ = 0.2f;
                                if (!is_uninterrupt_) {
                                    // Interrupt player: hit reaction + clear attack state
                                    player_hitstun_ = 0.3f;
                                    auto hit = clips_.find("high_hit_short");
                                    if (hit != clips_.end()) player_.play(&hit->second, false);
                                    move_state_ = 0;
                                    current_move_.clear();
                                    player_hit_this_frame_ = false;
                                }
                                std::printf("[hit] enemy hits! dmg=%.0f player HP=%.0f\n",
                                            move.damage * 100, player_health_);
                            }
                        }
                    }
                }
                // Auto-reset hit flag when enemy animation finishes
                if (!enemy_attack_playing) enemy_hit_this_frame_ = false;
            }

            // === ENEMY AI ===
            // Only act when idle — never interrupt own attack/hitstun
            if (enemy_hitstun_ > 0 || enemy_move_state_ != 0) {
                // Busy: in hitstun or performing a move — skip AI
            } else {
                enemy_ai_timer_ -= dt;
                if (enemy_ai_timer_ <= 0) {
                    enemy_ai_timer_ = 2.0f + (rand() % 3001) / 1000.0f;
                    int a = rand() % 7;
                    if (a == 0) {
                        // Step toward player (with anim)
                        auto it = clips_.find("step_forward");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, true);
                            enemy_current_anim_ = "step_forward";
                            enemy_current_move_ = "StepForward";
                            enemy_move_state_ = 2;
                        }
                    } else if (a == 1) {
                        auto it = clips_.find("heavy_punch");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, false);
                            enemy_current_move_ = "HeavyPunch";
                            enemy_current_anim_ = "heavy_punch";
                            enemy_move_state_ = 10;
                            enemy_hit_this_frame_ = false;
                        }
                    } else if (a == 2) {
                        auto it = clips_.find("front_kick");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, false);
                            enemy_current_move_ = "FrontKick";
                            enemy_current_anim_ = "front_kick";
                            enemy_move_state_ = 10;
                            enemy_hit_this_frame_ = false;
                        }
                    } else if (a == 3) {
                        // Step away from player (with anim)
                        auto it = clips_.find("step_back");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, true);
                            enemy_current_anim_ = "step_back";
                            enemy_current_move_ = "StepBack";
                            enemy_move_state_ = 1;
                        }
                    } else if (a == 4 && enemy_jump_y_ == 0) {
                        enemy_vel_y_ = -500.0f;
                        enemy_jump_y_ = -1.0f;
                        auto it = clips_.find("jump");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, false);
                            enemy_current_move_ = "Jump";
                            enemy_current_anim_ = "jump";
                            enemy_move_state_ = 10;
                        }
                    } else if (a == 5) {
                        auto it = clips_.find("high_punch");
                        if (it != clips_.end()) {
                            enemy_player_.play(&it->second, false);
                            enemy_current_move_ = "HighPunch";
                            enemy_current_anim_ = "high_punch";
                            enemy_move_state_ = 10;
                            enemy_hit_this_frame_ = false;
                        }
                    }
                    // a==6: idle
                    if (loc_.enemy_x < bound_left_) loc_.enemy_x = bound_left_;
                    if (loc_.enemy_x > bound_right_) loc_.enemy_x = bound_right_;
                }
            }

            // Enemy step movement: apply displacement + auto-stop after a while
            if (enemy_move_state_ == 2) {
                float edir = (loc_.enemy_x < loc_.player_x) ? 1.0f : -1.0f;
                loc_.enemy_x += edir * 250.0f * dt;
                enemy_step_timer_ += dt;
                if (enemy_step_timer_ > 0.5f) {
                    enemy_step_timer_ = 0;
                    enemy_move_state_ = 0;
                    enemy_current_move_.clear();
                    auto it = clips_.find("fists1_stance_idle");
                    if (it != clips_.end()) { enemy_player_.play(&it->second, true); enemy_current_anim_ = "fists1_stance_idle"; }
                }
            } else if (enemy_move_state_ == 1) {
                float edir = (loc_.enemy_x < loc_.player_x) ? 1.0f : -1.0f;
                loc_.enemy_x -= edir * 250.0f * dt;
                enemy_step_timer_ += dt;
                if (enemy_step_timer_ > 0.5f) {
                    enemy_step_timer_ = 0;
                    enemy_move_state_ = 0;
                    enemy_current_move_.clear();
                    auto it = clips_.find("fists1_stance_idle");
                    if (it != clips_.end()) { enemy_player_.play(&it->second, true); enemy_current_anim_ = "fists1_stance_idle"; }
                }
            } else {
                enemy_step_timer_ = 0;
            }

            // Enemy: any finished non-looping anim (attack, hit reaction, jump) → idle
            if (enemy_player_.is_playing() && enemy_player_.is_finished()) {
                enemy_move_state_ = 0;
                enemy_current_move_.clear();
                enemy_current_anim_ = "fists1_stance_idle";
                auto it = clips_.find("fists1_stance_idle");
                if (it != clips_.end()) enemy_player_.play(&it->second, true);
            }

            // === PUSHER ===
            {
                float min_gap = 80.0f;
                if (loc_.player_x + min_gap > loc_.enemy_x) {
                    float mid = (loc_.player_x + loc_.enemy_x) * 0.5f;
                    loc_.player_x = std::max(bound_left_, mid - min_gap * 0.5f);
                    loc_.enemy_x = std::min(bound_right_, mid + min_gap * 0.5f);
                }
            }

            // === KO CHECK ===
            if (player_health_ <= 0 || enemy_health_ <= 0) {
                ko_ = true;
                ko_timer_ = 0;
                std::printf("[fight] KO!\n");
            }

            // === BOUNDS ===
            if (loc_.player_x < bound_left_) loc_.player_x = bound_left_;
            if (loc_.player_x > bound_right_) loc_.player_x = bound_right_;
            if (loc_.enemy_x < bound_left_) loc_.enemy_x = bound_left_;
            if (loc_.enemy_x > bound_right_) loc_.enemy_x = bound_right_;

            // Clear just-pressed flags for next frame
            punch_just_pressed_ = false;
            kick_just_pressed_ = false;
            } // end if(!ko_)
            // KO state: wait then reset
            if (ko_) {
                ko_timer_ += dt;
                if (ko_timer_ > 2.5f) {
                    player_health_ = 100;
                    enemy_health_ = 100;
                    ko_ = false;
                    ko_timer_ = 0;
                    round_timer_ = 99.0f;
                    loc_.player_x = loc_.player_x;
                    enemy_ai_timer_ = 2.0f;
                    move_state_ = 0; current_move_.clear();
                    enemy_move_state_ = 0; enemy_current_move_.clear();
                    player_hit_this_frame_ = false;
                    enemy_hit_this_frame_ = false;
                    auto it = clips_.find("fists1_stance_idle");
                    if (it != clips_.end()) { player_.play(&it->second, true); current_anim_ = "fists1_stance_idle"; }
                    auto eit = clips_.find("fists1_stance_idle");
                    if (eit != clips_.end()) { enemy_player_.play(&eit->second, true); enemy_current_anim_ = "fists1_stance_idle"; }
                    std::printf("[fight] round reset\n");
                }
            }
        }

        if (loaded_) return;
        auto& root = root_;

        auto& dz = resf2::dz::DzRegistry::instance();
        if (fs::exists(root / "files.dz"))
            dz.open_archive((root / "files.dz").string());
        if (fs::exists(root / "animations.dz"))
            dz.open_archive((root / "animations.dz").string());

        auto loc_path = root / "locations" / location_name_ / "params.xml";
        if (fs::exists(loc_path)) {
            resf2::format::LocationParser p;
            p.load_file(loc_path.string(), loc_);
            std::printf("[%s] location: %s, %zu layers\n",
                        location_name_.c_str(), loc_.color.c_str(), loc_.layers.size());
        }

        if (!loc_.color.empty()) {
            auto hx = loc_.color;
            if (hx.starts_with("0x") || hx.starts_with("0X")) hx = hx.substr(2);
            if (hx.size() >= 6) {
                auto v = std::stoul(hx, nullptr, 16);
                bg_r_ = ((v >> 16) & 0xFF) / 255.0f;
                bg_g_ = ((v >> 8) & 0xFF) / 255.0f;
                bg_b_ = (v & 0xFF) / 255.0f;
            }
        }

        // Find and load the combined JSON atlas for this location (sf2_pc format)
        // The atlas file is in the same directory as the location XML with a hashed name: {location}.{hash}.json
        std::string location_atlas_path = "";
        
        // Check sf2_pc path first
        fs::path pc_location_dir = fs::path("E:/reSF2/sf2_pc/www/res/locations") / location_name_;
        if (fs::exists(pc_location_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(pc_location_dir)) {
                if (entry.path().extension() == ".json") {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind(location_name_ + ".", 0) == 0) {
                        location_atlas_path = entry.path().string();
                        break;
                    }
                }
            }
        }
        
        // Fallback to mobile path
        if (location_atlas_path.empty()) {
            for (const auto& entry : std::filesystem::directory_iterator(root / "1536" / "locations" / location_name_)) {
                if (entry.path().extension() == ".json") {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind(location_name_ + ".", 0) == 0) {
                        location_atlas_path = entry.path().string();
                        break;
                    }
                }
            }
        }
        if (location_atlas_path.empty()) {
            // Also check mobile assets path
            for (const auto& entry : std::filesystem::directory_iterator(root / "locations" / location_name_)) {
                if (entry.path().extension() == ".json") {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind(location_name_ + ".", 0) == 0) {
                        location_atlas_path = entry.path().string();
                        break;
                    }
                }
            }
        }

        LoadedAtlas location_atlas;
        if (!location_atlas_path.empty()) {
            location_atlas = load_pc_atlas(location_atlas_path.substr(0, location_atlas_path.size() - 5)); // remove .json
            std::printf("[atlas] Loaded location atlas: %s (%zu frames)\n", location_atlas_path.c_str(), location_atlas.frames.size());
        }

        for (auto& l : loc_.layers) {
            // sf2_pc format: no per-layer atlas, all frames in single location atlas
            // Just use the shared location_atlas
            if (!location_atlas.frames.empty()) {
                atlases_.push_back(location_atlas); // Copy for each layer (or we could share via ptr)
            }
        }

        // Load skeleton
        auto skel_path = root / "models" / "skeleton.xml";
        load_skeleton(skel_path);

        // Load multiple animations.
        // NOTE: power_fists_* anims use a 209-node skeleton — incompatible with
        // our 67-node skeleton.xml. Do NOT load them (they render scrambled).
        auto anim_dir = root / "animations" / "binary";
        std::vector<std::string> anim_names = {
            "stance_1", "stance_2", "fists1_stance_idle", "fists2_stance_idle",
            "heavy_punch", "high_punch", "double_punch",
            "high_kick", "front_kick",
            "jump", "step_forward", "step_back",
            "high_block",
            "high_hit_short", "middle_hit_short", "stun",
            "sweep_hit_short", "sweep",
        };
        for (auto& an : anim_names) {
            auto bin_path = (anim_dir / (an + ".bin")).string();
            auto data = read_file_bytes(bin_path);
            if (data.empty()) {
                std::printf("[anim] %s: not found\n", an.c_str());
                continue;
            }
            auto& clip = clips_[an];
            if (clip.load_from_bin(
                    reinterpret_cast<const uint8_t*>(data.data()),
                    data.size(), an)) {
                std::printf("[anim] %s: %zu nodes, %.2fs\n",
                            an.c_str(), clip.nodes.size(), clip.duration);
            }
        }

        // Start with fists1_stance_idle
        auto it = clips_.find("fists1_stance_idle");
        if (it != clips_.end()) {
            player_.play(&it->second, true);
            char_loaded_ = true;
        }

        // Enemy: use fists1_stance_idle
        auto eit = clips_.find("fists1_stance_idle");
        if (eit != clips_.end())
            enemy_player_.play(&eit->second, true);

        // Load moves from moves.xml
        auto moves_xml_text = read_file_text((root / "animations" / "moves.xml").string());
        if (!moves_xml_text.empty()) {
            moves_ = load_moves_from_xml(moves_xml_text);
            std::printf("[moves] loaded %zu moves from moves.xml\n", moves_.size());
        } else {
            std::printf("[moves] moves.xml not found\n");
        }

        std::printf("[%s] ready: %zu atlases, %zu layers, loaded %zu anims\n",
                    location_name_.c_str(), atlases_.size(), loc_.layers.size(), clips_.size());
        camera_x_ = (loc_.player_x - loc_.width * 0.5f) + screen_w_ * 0.25f;
        round_timer_ = 99.0f;
        player_base_y_ = loc_.player_y;
        enemy_base_y_ = loc_.enemy_y;
        player_jump_y_ = 0; player_vel_y_ = 0;
        enemy_jump_y_ = 0; enemy_vel_y_ = 0;
        std::printf("[%s] camera_x_ init: %.0f (player_x=%.0f)\n",
                    location_name_.c_str(), camera_x_, loc_.player_x);
        // Play area boundaries in left-origin
        bound_left_ = loc_.wall;
        bound_right_ = loc_.width - loc_.wall;
        std::printf("[%s] bounds: [%.0f, %.0f]\n",
                    location_name_.c_str(), bound_left_, bound_right_);
        loaded_ = true;
    }

    void render(core::Renderer2D& r) override {
        r.clear({bg_r_, bg_g_, bg_b_, 1});
        auto& adapter = static_cast<Ren2DAdapter&>(r);

        // Camera follows player horizontally with fixed Y
        {
            float player_cx = loc_.player_x - loc_.width * 0.5f;
            float cam_center_x = player_cx + screen_w_ * 0.25f;
            camera_x_ = cam_center_x; // for debug display
            float cam_y = -(static_cast<float>(loc_.floor)) + 30.0f; // fixed Y: floor surface ~30px below center
            adapter.r_.camera().set_target(cam_center_x, cam_y);
            adapter.r_.camera().update(100000);
        }

        // Screen shake via renderer camera
        if (shake_dur_ > 0) {
            adapter.r_.camera().shake(shake_amp_, static_cast<std::uint32_t>(shake_dur_ * 1000));
            shake_dur_ = 0;
        }

        // Render layers back-to-front with parallax (matching original formula)
        for (auto& layer : loc_.layers) {
            if (layer.images.empty()) continue;

            float parallax_factor = layer.factor;
            if (parallax_factor <= 0.0f) parallax_factor = 1.0f;
            float parallax_shift = (1.0f - parallax_factor) * camera_x_;

            for (auto& img : layer.images) {
                float world_x = img.x - parallax_shift;
                float world_y = -img.y;

                if (img.class_name == "pixel_1") continue;

                auto* frame = find_frame(img.class_name);
                if (!frame || !frame->texture) continue;

                float w = frame->w;
                float h = frame->h;
                float px = world_x - w / 2;
                float py = world_y - h / 2;

                // Parallax layers: tile horizontally to fill the visible area
                if (parallax_factor < 0.99f) {
                    float vis_left = camera_x_ - screen_w_ * 0.5f;
                    float vis_right = camera_x_ + screen_w_ * 0.5f;
                    float tile_w = w;
                    float start_x = px;
                    while (start_x + tile_w > vis_left) start_x -= tile_w;
                    while (start_x < vis_left) start_x += tile_w;
                    start_x -= tile_w;
                    for (float tx = start_x; tx < vis_right; tx += tile_w) {
                        adapter.r_.draw_textured_quad(
                            *frame->texture, tx, py, w, h,
                            frame->u0, frame->v0, frame->u1, frame->v1);
                    }
                } else {
                    adapter.r_.draw_textured_quad(
                        *frame->texture, px, py, w, h,
                        frame->u0, frame->v0, frame->u1, frame->v1);
                }
            }
        }

        // Render characters on top
        render_characters(adapter.r_);
        render_weapon(adapter.r_);
        render_ui(adapter.r_);

        static int dbg_frame = 0;
        if (++dbg_frame % 30 == 0) {
            std::printf("\r[%s] FPS: %.0f  Camera: %.0f  Char: %s",
                        location_name_.c_str(), fps_, camera_x_, char_loaded_ ? "PLAY" : "NA");
        }
        std::fflush(stdout);
    }

    std::string name() const override { return location_name_; }
};

// ========== Main ==========
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    fs::path assets_root;
    std::string location_name = "dojo";
    for (int i = 1; i < argc; i++) {
        if (i + 1 < argc && strcmp(argv[i], "--assets") == 0) {
            assets_root = argv[++i];
        } else if (i + 1 < argc && (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--location") == 0)) {
            location_name = argv[++i];
        } else if (argv[i][0] != '-') {
            assets_root = argv[i];
        }
    }
    if (assets_root.empty()) {
        auto cwd = fs::current_path();
        for (auto d = cwd; d.has_root_path(); d = d.parent_path()) {
            auto candidate = d / "assets";
            if (fs::is_directory(candidate / "locations")) {
                assets_root = candidate;
                break;
            }
        }
        if (assets_root.empty())
            assets_root = cwd / "assets";
    }

    std::printf("reSF2 Port — Phase 4\n");
    std::printf("Assets root: %s\n", assets_root.string().c_str());

    auto platform = std::make_unique<plat::GlfwPlatform>();
    plat::WindowConfig cfg;
    cfg.title = "reSF2 Port — " + location_name + " (arrows: scroll)";
    cfg.width = 800; cfg.height = 640; cfg.vsync = false;

    if (!platform->init(cfg)) { std::fprintf(stderr, "Platform init failed\n"); return 1; }
    if (!platform->make_gl_current()) { std::fprintf(stderr, "GL context failed\n"); return 1; }

    auto renderer = std::make_unique<ren::Renderer>();
    if (!renderer->init(platform->window_width(), platform->window_height())) return 1;
    renderer->set_clear_color(0, 0, 0, 1);

    auto rend2d = std::make_unique<Ren2DAdapter>(*renderer);
    core::StateStack states;
    auto scene = std::make_unique<LocationScene>();
    scene->set_assets_root(assets_root);
    scene->set_screen_size(cfg.width, cfg.height);
    scene->set_location_name(location_name);
    auto* scene_ptr = scene.get();
    states.push(std::move(scene));

    auto last_ms = platform->now_ms();
    while (true) {
        if (!platform->poll_events()) break;
        if (platform->should_quit()) break;
        if (platform->input().keys_just_pressed[(size_t)plat::Key::Escape]) break;

        auto& inp = platform->input();
        scene_ptr->set_input_state(inp.keys_down.data());

        auto now = platform->now_ms();
        auto dt = (std::min)((uint32_t)(now - last_ms), 50u);
        last_ms = now;

        states.update(dt / 1000.0f);
        if (states.empty()) break;

        renderer->set_clear_color(0, 0, 0, 1);
        renderer->begin_frame();
        states.render(*rend2d);
        renderer->end_frame();

        platform->swap_buffers();
        platform->sleep_ms(16);
    }

    renderer->shutdown();
    platform->shutdown();
    return 0;
}
