#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../core/math.hpp"
#include "../renderer/renderer.hpp"
#include "../reverse/plist_atlas.hpp"

namespace resf2::game {

namespace ren = resf2::renderer;
namespace plist = resf2::reverse::plist;

// ---------- Asset types ----------

struct AtlasRef {
    std::unique_ptr<ren::Texture2D> texture;
    std::shared_ptr<plist::ParsedAtlas> atlas;
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
    float wall = 0;
    float floor = 0;
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
    float margin1 = 0, margin2 = 0;
};

// ---------- Move definition (from moves.xml) ----------

struct MoveDef {
    std::string name;
    std::string filename;
    std::string template_name;
    int first_frame = -1;   // -1 = not specified
    int end_frame = 0;
    int priority = 0;

    int attack_start = -1;
    int attack_end = -1;
    std::vector<std::string> attack_edges;
    float damage = 0.0f;
    float impulse_x = 0.0f;
    float impulse_y = 0.0f;

    int block_start = -1;
    int uninterrupt_start = -1;
    int uninterrupt_end = -1;
    std::vector<std::string> key_types;

    int key_count = 0;
    std::string direction;
    std::string move_type;
    std::string weapon_filter;
    bool is_unarmed = false;
    bool is_jump = false;
    bool is_short_attack = false;
    bool is_retreat = false;
    bool is_step = false;
    bool is_double_step = false;
    bool is_block = false;
    bool is_stance = false;
    bool is_idle = false;
    bool is_not_titan = false;
    std::string tactic_weapon;

    float distance_min = 0.0f;
    float distance_max = 0.0f;
    bool has_distance_cond = false;

    std::string required_perk;
    std::string required_weapon_subtype;

    std::string moveinside_pivot_node;
    bool moveinside_is_animation = false;

    std::string required_current_animation;

    bool is_attack = false;
    int mid_frames = 2;

    // Sound events (from <Actions><Sound .../> in moves.xml)
    struct SoundEvent {
        float time = 0;
        std::string sound;
    };
    std::vector<SoundEvent> sound_events;

    struct Interval {
        std::string type;
        std::string name;
        float start = 0;
        float end = 0;
        int damage = 0;
        float impulse_x = 0;
        float impulse_y = 0;
        std::string hit_type;
        std::vector<std::string> edges;
        std::string condition_anim;
    };
    std::vector<Interval> intervals;

    // Invulnerable intervals (target-side: animation frames where the fighter cannot be hit)
    struct InvulnerableInterval {
        float start = 0;
        float end = 0;
        std::string name; // "Evade", "Recovery", "Boss"
    };
    std::vector<InvulnerableInterval> invulnerable_intervals;

    // IgnoresInvulnerable — this attack can pierce target invulnerability
    std::string ignores_invulnerable;
};

// ---------- Animation ----------

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
            nodes.reserve(nc);
            for (uint32_t i = 0; i < nc; ++i) {
                if (offset + 12 > sz) break;
                float fx, fy, fneg_z;
                memcpy(&fx, &data[offset], 4);
                memcpy(&fy, &data[offset + 4], 4);
                memcpy(&fneg_z, &data[offset + 8], 4);
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

// ---------- Body / Verlet ----------

struct BodyNode {
    std::string name;
    float x = 0, y = 0, z = 0;
    float mass = 1.0f;
    bool fixed = false;
    float attenuation = 0.02f;
    bool cloth = false;
};

struct BodyEdge {
    std::string name;
    std::string end1, end2;
    float length = 0;
    float radius = 0;
    bool collisible = false;
};

struct VerletNode {
    float x = 0, y = 0;
    float px = 0, py = 0;
    float mass = 1.0f;
    float inv_mass = 1.0f;
    bool fixed = false;
    float attenuation = 0.02f;
};

struct VerletConstraint {
    std::string n1, n2;
    float length = 0;
    float stiffness = 1.0f;
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

enum class Overlay { None, Menu, Dialog };

// ---------- Fighter state ----------

struct FighterState {
    float health = 100.0f;
    float max_health = 100.0f;
    float energy = 0.0f;
    float max_energy = 100.0f;
    bool is_blocking = false;
    bool is_hit = false;
    float hit_stun_time = 0.0f;
    float invuln_time = 0.0f;
    int hits_landed = 0;
    int hits_taken = 0;
    bool is_dead = false;
};

struct HitSpark {
    float x, y;
    float age = 0;
    float lifetime = 0.3f;
    float scale = 1.0f;
};

} // namespace resf2::game
