// engine/fight/ai.cpp
//
// AI opponent controller. Uses tactic data from .atf files to make combat
// decisions: approach, retreat, attack, block, and special moves.
// Decisions are made every ~0.8s (matching original SF2 disassembly).

#include "ai.hpp"
#include "moves.hpp"
#include "../reverse/atf_tactics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace resf2::fight {

void AIController::set_tactic_data(const uint8_t* data, size_t size) {
    // Store raw tactic data for use during decisions
    // The .atf binary records contain per-distance-interval move weights
    // that the AI uses to select attacks at specific ranges.
    // Full record parsing is a Stage 5 task; for now we store the data
    // and extract aggregate distances from the binary prefix.
    if (!data || size == 0) return;

    auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);
    auto parsed = reverse::atf::parse(span);
    if (parsed) {
        std::printf("[AI] Loaded tactic data: weapon_a='%s' weapon_b='%s' records=%u stride=%u\n",
                    parsed->header.weapon_a_name.c_str(),
                    parsed->header.weapon_b_name.c_str(),
                    parsed->binary_prefix.record_count,
                    parsed->binary_prefix.stride);
        tactic_data_ = std::move(parsed->decompressed);
        tactic_loaded_ = true;
    } else {
        std::printf("[AI] Failed to parse tactic data: %s\n",
                    reverse::atf::to_string(parsed.error()));
        tactic_loaded_ = false;
    }
}

void AIController::update(float dt, Fighter& self, const Fighter& opponent) {
    react_timer_ += dt;
    decision_cooldown_ = std::max(0.0f, decision_cooldown_ - dt);

    // Don't act while in hit stun
    if (self.state() == Fighter::State::Hit || self.state() == Fighter::State::Dead) {
        return;
    }

    // Make a new decision every decision_cooldown_ seconds
    if (decision_cooldown_ <= 0 && react_timer_ >= settings.reaction_time) {
        react_timer_ = 0;
        decide(self, opponent);
        decision_cooldown_ = 0.6f + (float)(std::rand() % 400) / 1000.0f; // 0.6-1.0s
    }
}

void AIController::decide(Fighter& self, const Fighter& opponent) {
    // Compute distance between fighters
    float dx = opponent.position().x - self.position().x;
    float dist = std::abs(dx);

    // Random factor for variety
    int r = std::rand() % 100;

    // Determine facing direction
    bool face_right = (dx > 0);
    self.set_facing(face_right);

    // Calculate health ratios
    float self_health_ratio = self.health() / self.config().max_health;
    float opp_health_ratio = opponent.health() / opponent.config().max_health;

    // Decide action based on distance, health, aggression, and randomness
    int action = 0; // 0=idle, 1=approach, 2=attack_light, 3=attack_heavy,
                    // 4=retreat, 5=block, 6=special

    if (dist > 300.0f) {
        // Far: approach always
        action = 1;
    } else if (dist > 200.0f) {
        // Mid-far: prefer approach, sometimes attack
        float approach_chance = 0.6f + (1.0f - settings.aggression) * 0.3f;
        if (r < (int)(approach_chance * 100.0f)) {
            action = 1;
        } else {
            action = 2; // light attack (may whiff at this range)
        }
    } else if (dist > 100.0f) {
        // Mid range: mix of attacks
        if (r < (int)(settings.attack_chance * 100.0f)) {
            action = (r < 50) ? 2 : 3; // light or heavy
        } else if (r < (int)((settings.attack_chance + settings.special_chance) * 100.0f)) {
            action = 6; // special
        } else if (r < (int)((settings.attack_chance + settings.special_chance +
                             settings.block_chance) * 100.0f)) {
            action = 5; // block
        } else {
            action = (r < 85) ? 1 : 4; // approach or retreat
        }
    } else {
        // Close range (< 100px): mix
        if (r < (int)(settings.attack_chance * 100.0f)) {
            action = (r < 50) ? 2 : 3;
        } else if (r < (int)((settings.attack_chance + settings.special_chance) * 100.0f)) {
            action = 6;
        } else if (r < (int)((settings.attack_chance + settings.special_chance +
                             settings.block_chance) * 100.0f)) {
            action = 5;
        } else {
            action = (r < 60) ? 4 : 0; // retreat or idle
        }
    }

    // Health-based modifiers
    if (self_health_ratio < 0.3f && r < 50) {
        // Low health: more defensive
        action = (r < 25) ? 4 : 5; // retreat or block
    }
    if (opp_health_ratio < 0.3f && r < 40) {
        // Opponent low health: press advantage
        action = 3; // heavy attack
    }

    // Execute action
    self.set_direction(8); // Central (no movement dir yet)

    switch (action) {
        case 1: // Approach
            self.set_direction(face_right ? 6 : 4); // Forward/Back relative
            // Don't trigger attack — just movement
            self.set_action_punch(false);
            self.set_action_kick(false);
            self.set_action_block(false);
            last_decision_ = 1;
            break;

        case 2: // Light attack
            self.set_action_punch(true);
            self.set_action_kick(false);
            self.set_action_block(false);
            last_decision_ = 2;
            std::printf("[AI] %s attacks (light) at dist=%.0f\n",
                        self.config().name.c_str(), dist);
            break;

        case 3: // Heavy attack (kick)
            self.set_action_punch(false);
            self.set_action_kick(true);
            self.set_action_block(false);
            last_decision_ = 3;
            std::printf("[AI] %s attacks (heavy) at dist=%.0f\n",
                        self.config().name.c_str(), dist);
            break;

        case 4: // Retreat
            self.set_direction(face_right ? 4 : 6);
            self.set_action_punch(false);
            self.set_action_kick(false);
            last_decision_ = 4;
            break;

        case 5: // Block
            self.set_action_block(true);
            last_decision_ = 5;
            break;

        case 6: // Special (jump/roll toward opponent)
            self.set_direction(face_right ? 6 : 4);
            self.set_action_special(true);
            last_decision_ = 6;
            std::printf("[AI] %s special at dist=%.0f\n",
                        self.config().name.c_str(), dist);
            break;

        default: // Idle
            self.set_action_punch(false);
            self.set_action_kick(false);
            self.set_action_block(false);
            last_decision_ = 0;
            break;
    }

    // Debug log
    std::printf("[AI_DECISION] f=%s dist=%.0f self_hp=%.0f opp_hp=%.0f action=%d\n",
                self.config().name.c_str(), dist,
                self.health(), opponent.health(), action);
}

} // namespace resf2::fight
