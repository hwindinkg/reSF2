#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../core/math.hpp"
#include "animation.hpp"
#include "moves.hpp"

namespace resf2::fight {

// Fighter state machine — matches JS fighter behavior
class Fighter {
public:
    enum class State {
        Idle,
        Walk,   // WalkForward, WalkBack
        Attack, // 1key/2key/3key attack
        Hit,    // taking damage
        Block,
        Jump,     // neutral jump
        JumpForward,
        JumpBack,
        Crouch,
        RollForward,
        RollBack,
        AirAttack,
        Knockdown,
        Getup,
        Dead,
        Special
    };

    struct Config {
        std::string name = "Fighter";
        Vec2 position{400, 300};
        float scale = 1.0f;
        bool facing_right = true;
        float health = 100;
        float max_health = 100;
        float energy = 100;
        float max_energy = 100;
        float speed = 200.0f;           // walk speed px/s
        float push_distance = 40.0f;     // pushback when hit
        std::string tactic_weapon = "Fists";
    };

    Fighter() = default;
    explicit Fighter(const Config& cfg) : config_(cfg) {}

    void reset();
    void update(float dt);
    void render(class core::Renderer2D& r); // debug rendering

    // Input
    void set_direction(int dir) { input_dir_ = dir; }
    void set_action_punch(bool v) { input_punch_ = v; }
    void set_action_kick(bool v) { input_kick_ = v; }
    void set_action_block(bool v) { input_block_ = v; }
    void set_action_special(bool v) { input_special_ = v; }

    // Combat
    void take_damage(float amount, Vec2 impulse);
    void apply_hit(const MoveDef::AttackInterval& interval);

    // State
    State state() const { return state_; }
    const Config& config() const { return config_; }
    Config& config() { return config_; }
    Vec2 position() const { return config_.position; }
    void set_position(Vec2 p) { config_.position = p; }
    bool facing_right() const { return config_.facing_right; }
    void set_facing(bool r) { config_.facing_right = r; }
    float health() const { return config_.health; }
    float energy() const { return config_.energy; }

    // Animation
    AnimationPlayer& anim_player() { return anim_player_; }
    const AnimationPlayer& anim_player() const { return anim_player_; }
    const MoveDef* current_move() const { return current_move_; }
    bool is_attacking() const;
    bool is_in_uninterrupt() const;

    // Collision box (feet-level)
    Rect collision_box() const;

    // Combo state
    int combo_count() const { return combo_count_; }
    void set_combo_count(int c) { combo_count_ = c; }

private:
    Config config_;
    State state_ = State::Idle;

    // Input state
    int input_dir_ = 8; // Central
    bool input_punch_ = false;
    bool input_kick_ = false;
    bool input_block_ = false;
    bool input_special_ = false;

    // Animation
    AnimationPlayer anim_player_;
    const MoveDef* current_move_ = nullptr;

    // Combat state
    float hit_stun_timer_ = 0;
    float attack_timer_ = 0;
    int combo_count_ = 0;
    bool just_attacked_ = false;

    // State timers
    float state_timer_ = 0;

    // Root motion accumulation
    Vec2 rm_accum_;

    void transition_to(State s);
    void update_idle(float dt);
    void update_walk(float dt);
    void update_attack(float dt);
    void update_hit(float dt);
    void update_block(float dt);
    void try_attack();
    void select_move();
};

} // namespace resf2::fight
