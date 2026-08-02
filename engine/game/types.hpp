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
#include "../format/effect_curve.hpp"
#include "../renderer/renderer.hpp"
#include "../reverse/plist_atlas.hpp"

#include "attributes.hpp"

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
    // [ORIGINAL] <SimpleEffect> animates its own alpha through a <Transparency>
    // curve; a plain <Image> leaves this empty and draws fully opaque. See
    // engine/format/effect_curve.hpp for the law and the binary addresses.
    resf2::format::EffectCurve transparency;
};

struct LocationLayer {
    int type = 0;
    float factor = 1.0f;
    std::string atlas_name;

    // [ORIGINAL] <Layer Path="locations/spaceship/"> — the atlas lives in
    // ANOTHER location's directory. 26 layers in the shipped params.xml files
    // use this (flying_rocks, waterfall, ruins_village, spaceship). Without it
    // the atlas is looked up under the current location and the layer renders
    // nothing at all. Read by the original at game+0x3E40D0.
    std::string path;

    // [ORIGINAL] <Layer Scaling="1">, set on 62 foreground layers; the original
    // keeps it as a flag on the layer object (+0x154/+0x155).
    bool scaling = false;

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
    // [ORIGINAL] The nested <Damage Type="UnarmedDamage" Shift="-10"/> inside
    // <Damage Value=".."> — the attribute this attack reads (DamageAttribute)
    // and the shift added to it before the f3 difference (game+0x60DF98,
    // "DamageAttribute(+Shift) -> DefenseAttribute"). LIVE_GAME_EVIDENCE Q3:
    // real HighPunch ships UnarmedDamage Shift=-10. Empty attr = default
    // (WeaponDamage for weapon hits, UnarmedDamage for fists).
    std::string damage_attr;
    int damage_attr_shift = 0;

    int block_start = -1;
    int uninterrupt_start = -1;
    int uninterrupt_end = -1;
    // [ORIGINAL] SemiUninterrupt: animation can be interrupted by attacks
    // but not by movement. From IntervalAttack::getFactors @ 0x10115910.
    // moves.xml: 81 moves declare it (e.g. DoubleStepForward End=2).
    int semi_uninterrupt_start = 0;
    int semi_uninterrupt_end = -1;
    // [ORIGINAL] SelfUninterrupt: animation can only be interrupted by itself
    // (combo chains). moves.xml: 4 moves declare it (e.g. DoubleStepForward 10..12).
    int self_uninterrupt_start = -1;
    int self_uninterrupt_end = -1;
    std::vector<std::string> key_types;
    // [ORIGINAL] <Key Type="Punch" PressType="Tap|Hold"/>
    // Parallel to key_types. 419 bindings are Tap and 212 are Hold, and the two
    // select DIFFERENT moves from the same key -- dropping it made every hold
    // behave like a tap, so charged/held attacks were unreachable.
    std::vector<std::string> key_press_types;
    // True when any binding of this move requires a held key.
    bool needs_hold = false;

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

    // [ORIGINAL] <Align> — how the animation is anchored to the fighter.
    //   <Align Axis="X|Z" ShiftModelNode="NPivot">
    //     <Pivot Object="Nodes" Part="NHeel_2"/>
    //     <Position Player="Me" Object="Pivot" ShiftX="70"/>
    //   </Align>
    // The named node of the ANIMATION is placed at the fighter's position
    // (plus the shift); the axes listed are the ones the alignment controls.
    // 718 moves align on X|Z and 51 on X|Y|Z, i.e. the vertical usually comes
    // straight from the animation — which matches the floor-space finding in
    // PORT_PLAN 3.1. The most common anchors are the heels (NHeel_2 437x,
    // NHeel_1 176x), because a fighting animation pivots on the planted foot.
    //
    // [ORIGINAL] `MoveInfo::parseAlign` @ 0x1017e140 stores each `Object`
    // attribute as an enum, resolved against the string table:
    //   1 = "Nodes"      (0x105b25f8)   a named node
    //   2 = "Wall"       (0x105b028c)   the location's left/right wall
    //   3 = "Animation"  (0x10379eb0)   the animation's own origin
    //   4 = "Pivot"      (0x105b3c40)   the model's current node
    // Anything else (including a missing <Align>) leaves the ctor default 0,
    // which `Model::alignAnimation` treats as "no alignment at all" — see
    // AlignObject::None below.
    enum class AlignObject : int {
        None = 0, Nodes = 1, Wall = 2, Animation = 3, Pivot = 4
    };
    std::string moveinside_pivot_node;   // <Pivot Part="...">, the anchor node
    bool moveinside_is_animation = false;  // <Pivot Object="Animation">
    AlignObject align_pivot_object = AlignObject::None;
    AlignObject align_position_object = AlignObject::None;
    std::string align_position_node;     // <Position Part="...">
    std::string align_axis;              // "X|Z", "X|Y|Z", ...
    bool align_x = false;
    bool align_y = false;
    bool align_z = false;
    float align_shift_x = 0.0f;          // <Position ShiftX="...">
    float align_shift_y = 0.0f;
    std::string align_shift_model_node;  // <Align ShiftModelNode="...">
    bool has_align = false;

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
        int damage = 0;         // flat Damage="" attribute form
        float damage_value = 0; // nested <Damage Value=""/> form, which is what
                                // moves.xml uses
        std::string damage_attr;   // nested <Damage Type=".."/> (e.g. UnarmedDamage)
        int damage_attr_shift = 0; // nested <Damage Shift=".."/>
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
    
    // [ORIGINAL] IntervalAttack flags from 0x10115d80
    bool ignores_block = false;        // +0x75: IgnoresBlock attribute
    bool no_effect = false;            // +0x74: NoEffect attribute
    std::vector<std::string> attacking_parts;  // +0xac: AttackingParts vector (skeleton edge names)
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

// ---------- Damage settings (from internalSettings.xml) ----------
//
// [ORIGINAL] Parsed from internalSettings.xml at load time.
// Binary ref: internalSettings parsing at 0x10291370
// These are character-attribute scaling factors. For a fresh character
// with 0 attribute points, the effective value equals the Base.
struct DamageSettings {
    // <DamageFactor Base="0.0001" Attribute="DamageFactor"/>
    // Per-point multiplier for the character's DamageFactor attribute.
    // attribute_multiplier = 1.0 + damage_factor_base * character_damage_factor_attr
    float damage_factor_base = 0.0001f;

    // <BlockDamageFactor Base="0.0001" Attribute="BlockDamageFactor" />
    // Per-point multiplier for the character's BlockDamageFactor attribute.
    // Reduces chip damage when blocking.
    float block_damage_factor_base = 0.0001f;

    // <AverageBaseDamage Value="0.1" />
    // Fallback base damage when a move has no explicit <Damage Value>.
    float average_base_damage = 0.1f;

    // <CriticalHit><Probability Base="0.0001" Attribute="CriticalChance" />
    // Per-point crit chance multiplier.
    float crit_probability_base = 0.0001f;

    // <CriticalHit><Damage Base="0.0001" Attribute="CriticalDamage" />
    // Per-point crit damage multiplier.
    float crit_damage_base = 0.0001f;

    // Base block factor (damage fraction that gets through when blocking).
    // [ORIGINAL] BlockDamage.Value = 0.5 from binary @ 0x101598c0
    // Not from XML — hardcoded in the binary.
    float base_block_factor = 0.5f;
};

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

    // [ORIGINAL] The fighter's attribute map — the name-keyed int container at
    // model+0x1C4 that Model::getParameter (game+0x6275F4) reads. Runtime-only:
    // rebuilt from equipped items / the AlignTargetAttributes baseline, never
    // serialised.
    AttributeSet attributes;
};

struct HitSpark {
    float x, y;
    float age = 0;
    float lifetime = 0.3f;
    float scale = 1.0f;
};

} // namespace resf2::game
