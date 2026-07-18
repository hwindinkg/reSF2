#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "../core/math.hpp"

namespace resf2::fight {

using core::Vec2;

// A single move definition from moves.xml
struct MoveDef {
    std::string name;
    std::string filename;         // .bin animation filename
    std::string template_name;    // e.g. "Punch", "Kick", "Hit", "Jump"

    // Key state (from Template analysis)
    int key_count = 1;            // 1key, 2key, 3key
    std::string direction;        // Central, Forward, Back, Up, Down, UpForward, etc.
    std::string move_type;        // Punch, Kick, Block, Hit, Jump, Move, Idle

    // Weapon
    std::string tactic_weapon;    // "Fists", "Swords", etc.

    // Conditions
    std::string required_current_animation; // for chain combos

    // Distance condition (AI/player distance)
    struct DistanceCondition {
        float min_dist = 0;
        float max_dist = 99999;
        bool active = false;
    };
    DistanceCondition distance;

    // Intervals (attack frames)
    struct AttackInterval {
        float start = 0;
        float end = 0;
        int damage = 0;
        std::string hit_type;     // "Light", "Heavy", "Knockdown"
        Vec2 impulse;             // hit impulse direction
    };
    std::vector<AttackInterval> attack_intervals;

    // Sound events
    struct SoundEvent {
        float time = 0;
        std::string sound;
    };
    std::vector<SoundEvent> sound_events;

    // Uninterrupt interval
    struct UninterruptInterval {
        float start = 0;
        float end = 0;
    };
    std::vector<UninterruptInterval> uninterrupt_intervals;

    // MoveInside (root motion pivot adjustment)
    struct MoveInside {
        Vec2 pivot;
        float consumption = 0; // how much of the displacement is consumed
    };
    MoveInside move_inside;

    // Display info
    std::string anim_name;       // readable name
    float anim_speed = 1.0f;
    bool loop = false;

    // Locks (weapon/perk requirements)
    std::string lock_weapon;
    std::string lock_perk;
};

// Moves database - loaded from moves.xml
class MoveDatabase {
public:
    bool load_from_xml(const std::string& xml_content);
    bool load_from_file(const std::string& path);

    const MoveDef* find(const std::string& name) const;
    const MoveDef* find_by_filename(const std::string& filename) const;

    // Find matching moves for given input state
    struct MoveQuery {
        std::string direction;      // Central, Forward, Back, Up, etc.
        std::string move_type;      // Punch, Kick
        int key_count = 1;
        std::string current_animation;
        std::string tactic_weapon;
        float distance_to_enemy = 1000;
        bool is_unarmed = true;
        bool in_uninterrupt = false;
    };

    std::vector<const MoveDef*> query(const MoveQuery& q) const;

    size_t size() const { return moves_.size(); }
    const auto& all_moves() const { return moves_; }

private:
    std::unordered_map<std::string, MoveDef> moves_;

    // Parse direction from the Template string
    // Templates are like "1keyForwardPunch", "2keyDownKick", "3keyUpPunch"
    void parse_template(MoveDef& move, const std::string& tmpl);
};

} // namespace resf2::fight
