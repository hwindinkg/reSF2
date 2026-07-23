// engine/fight/fighter.cpp
//
// Fighter state machine — handles state transitions, damage, invulnerability,
// combo tracking, and animation integration for each combatant in a battle.

#include "fighter.hpp"
#include "moves.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace resf2::fight {

void Fighter::reset() {
    state_ = State::Idle;
    config_.health = config_.max_health;
    config_.energy = 0;
    hit_stun_timer_ = 0;
    attack_timer_ = 0;
    combo_count_ = 0;
    state_timer_ = 0;
    current_move_ = nullptr;
    just_attacked_ = false;
    anim_player_.stop();
}

void Fighter::update(float dt) {
    state_timer_ += dt;

    // Decay timers
    if (hit_stun_timer_ > 0) {
        hit_stun_timer_ = std::max(0.0f, hit_stun_timer_ - dt);
    }
    if (attack_timer_ > 0) {
        attack_timer_ = std::max(0.0f, attack_timer_ - dt);
    }

    // Update animation
    anim_player_.update(dt);

    switch (state_) {
        case State::Idle:
            update_idle(dt);
            break;
        case State::Walk:
            update_walk(dt);
            break;
        case State::Attack:
            update_attack(dt);
            break;
        case State::Hit:
            update_hit(dt);
            break;
        case State::Block:
            update_block(dt);
            break;
        default:
            break;
    }
}

bool Fighter::is_attacking() const {
    return state_ == State::Attack && attack_timer_ > 0;
}

bool Fighter::is_in_uninterrupt() const {
    if (!current_move_ || state_ != State::Attack) return false;
    if (!anim_player_.is_playing()) return false;

    // Intervals are given in frame numbers. Convert to time using
    // the move's mid_frames: time_s = frame * (mid_frames + 1) / 60.0f
    float frame_to_time = (float)(current_move_->mid_frames + 1) / 60.0f;
    float anim_t = anim_player_.time();  // absolute time in seconds

    for (auto& ui : current_move_->uninterrupt_intervals) {
        float un_start = ui.start * frame_to_time;
        float un_end = (ui.end > 0) ? ui.end * frame_to_time : 999.0f;
        if (anim_t >= un_start && anim_t <= un_end) return true;
    }
    // Fallback: use first attack interval as uninterrupt window
    if (current_move_->uninterrupt_intervals.empty() &&
        !current_move_->attack_intervals.empty()) {
        float as = current_move_->attack_intervals[0].start * frame_to_time;
        float ae = current_move_->attack_intervals[0].end * frame_to_time;
        return anim_t >= as && anim_t <= ae;
    }
    return false;
}

// Check if the fighter is in an Invulnerable interval of their current animation.
// If so, incoming attacks should miss.
bool Fighter::is_invulnerable() const {
    if (!current_move_ || state_ == State::Idle) return false;
    if (!anim_player_.is_playing()) return false;

    float dur = anim_player_.duration();
    if (dur <= 0) return false;

    float anim_t = anim_player_.time();  // absolute time in seconds
    for (auto& invi : current_move_->invulnerable_intervals) {
        // Intervals are frame-based; convert to time: frame / fps (using 30fps as default)
        float inv_start = invi.start / 30.0f;
        float inv_end = invi.end > 0 ? invi.end / 30.0f : dur;  // no End = entire rest of anim
        if (anim_t >= inv_start && anim_t <= inv_end) {
            return true;
        }
    }
    return false;
}

void Fighter::take_damage(float amount, Vec2 impulse) {
    if (config_.health <= 0) return;

    float old_health = config_.health;
    config_.health = std::max(0.0f, config_.health - amount);

    // Gain energy on hit
    config_.energy = std::min(config_.max_energy, config_.energy + amount * 0.5f);

    combo_count_++;

    // Transition to hit state
    if (config_.health <= 0) {
        transition_to(State::Dead);
    } else {
        transition_to(State::Hit);
        hit_stun_timer_ = 0.3f; // 300ms hit stun
    }

    // Apply velocity impulse (pushback)
    // Root motion handles this later
    (void)impulse;

    std::printf("[FIGHTER] '%s' took %.0f damage (%.0f -> %.0f), combo=%d\n",
                config_.name.c_str(), amount, old_health, config_.health, combo_count_);
}

void Fighter::apply_hit(const MoveDef::AttackInterval& interval) {
    take_damage((float)interval.damage, interval.impulse);
}

void Fighter::transition_to(State s) {
    if (s == state_) return;
    state_ = s;
    state_timer_ = 0;
}

void Fighter::update_idle(float) {
    // Idle: wait for input or AI decision
    if (input_punch_ || input_kick_) {
        try_attack();
    }
}

void Fighter::update_walk(float) {
    // Movement handled externally (position update by Game/AI)
}

void Fighter::update_attack(float dt) {
    if (attack_timer_ <= 0) {
        // Attack finished
        transition_to(State::Idle);
    }
}

void Fighter::update_hit(float dt) {
    if (hit_stun_timer_ <= 0) {
        transition_to(State::Idle);
    }
}

void Fighter::update_block(float) {
    // Block state handled externally
}

void Fighter::try_attack() {
    // Start attack animation
    just_attacked_ = true;
    transition_to(State::Attack);
}

void Fighter::select_move() {
    // Move selection is handled by Game using MoveDatabase
}

Rect Fighter::collision_box() const {
    return {
        config_.position.x - 30,
        config_.position.y - 80,
        60,
        100
    };
}

} // namespace resf2::fight
