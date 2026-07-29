// engine/fight/ai.cpp
//
// AI opponent controller. Uses tactic data from .atf files and
// tacticSettings.xml weighted roulette to make combat decisions.
// Decisions are made every ~0.8s (matching original SF2 disassembly).
//
// [ORIGINAL] The decision loop mirrors FUN_10171d80: build a TacticContext
// from the current fight state, look up the fighter's TacticDef, run the
// jL roulette-wheel pick over candidate animations, then map the winner
// back to a fighter action (approach / attack / block / retreat / idle).

#include "ai.hpp"
#include "moves.hpp"
#include "../reverse/atf_tactics.hpp"
#include "../game/tactic_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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
        std::printf("[AI] Loaded tactic data: weapon_a='%s' weapon_b='%s' stride=%u names=%zu\n",
                    parsed->header.weapon_a_name.c_str(),
                    parsed->header.weapon_b_name.c_str(),
                    parsed->binary_prefix.stride,
                    parsed->animation_names.size());
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

    // Determine facing direction
    bool face_right = (dx > 0);
    self.set_facing(face_right);

    // [ORIGINAL] Try tactic-driven decision first (tacticSettings.xml roulette).
    // This replaces the hardcoded distance thresholds with the weighted
    // selection from the original game (FUN_10171d80 / jL / cc).
    if (decide_by_tactic(self, opponent, dist)) {
        return;
    }

    // Fallback: hardcoded decision tree (no tacticSettings loaded)
    int r = std::rand() % 100;

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

// [ORIGINAL] Tactic-driven decision making — matches FUN_10171d80 / jL / cc.
//
// Builds a TacticContext from the current fight state (distance, health,
// damage, hits), looks up the fighter's TacticDef from tacticSettings.xml,
// builds a candidate list of action labels, runs the weighted roulette
// (jL), and maps the chosen label back to a fighter action.
//
// Returns true if a tactic decision was made (even if it chose idle),
// false if tactic_settings_ is not available (caller should fallback).
bool AIController::decide_by_tactic(Fighter& self, const Fighter& opponent,
                                    float dist) {
    if (!tactic_settings_ || !tactic_settings_->loaded()) {
        return false;
    }

    // Resolve the TacticDef for this AI fighter.
    // Try the configured name first, then "Default", then first available.
    const game::TacticDef* tactic = nullptr;
    if (!tactic_name_.empty()) {
        tactic = tactic_settings_->tactic(tactic_name_);
    }
    if (!tactic) {
        tactic = tactic_settings_->tactic("Default");
    }
    if (!tactic && tactic_settings_->count() > 0) {
        // No named match — iterate via known tactic names.
        // For now we can't iterate TacticSettings directly, so bail out.
        return false;
    }
    if (!tactic) {
        return false;
    }

    // Build TacticContext from fight state — mirrors Gb() terms.
    // [ORIGINAL] sf2_beautified.js:20096
    game::TacticContext ctx;
    ctx.distance = dist;
    ctx.health = self.health() / std::max(1.0f, self.config().max_health);
    ctx.enemy_health = opponent.health() / std::max(1.0f, opponent.config().max_health);
    ctx.hits = static_cast<float>(self.combo_count());

    // Candidate action labels — these match the <Animation Name="...">
    // entries in tacticSettings.xml. The weight_for() lookup finds the
    // matching TacticWeight, with unnamed catch-all as fallback.
    // [ORIGINAL] sf2_beautified.js:19930 (iCa)
    static const std::vector<std::string> candidates = {
        "ForwardStep",    // approach toward opponent
        "ShortAttack",    // light/quick attack
        "HeavyAttack",    // heavy/slow attack
        "BackStep",       // retreat from opponent
        "Duck",           // block / duck
        "Idle"            // do nothing (catch-all weight)
    };

    // jL roulette pick — returns index into candidates, or -1 if all zero.
    int chosen_idx = tactic_settings_->choose(*tactic, candidates, ctx);

    bool face_right = (opponent.position().x - self.position().x) > 0;
    self.set_facing(face_right);
    self.set_direction(8); // Central

    // Map chosen label to fighter actions
    if (chosen_idx < 0) {
        // All weights zero — no decision. Stay idle.
        // [ORIGINAL] jL returns -1, caller does nothing this decision.
        self.set_action_punch(false);
        self.set_action_kick(false);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 0;
        std::printf("[AI_TACTIC] %s: all weights zero -> idle (dist=%.0f)\n",
                    self.config().name.c_str(), dist);
        return true;
    }

    const std::string& chosen = candidates[static_cast<size_t>(chosen_idx)];

    if (chosen == "ForwardStep") {
        self.set_direction(face_right ? 6 : 4);
        self.set_action_punch(false);
        self.set_action_kick(false);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 1;
    } else if (chosen == "ShortAttack") {
        self.set_action_punch(true);
        self.set_action_kick(false);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 2;
    } else if (chosen == "HeavyAttack") {
        self.set_action_punch(false);
        self.set_action_kick(true);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 3;
    } else if (chosen == "BackStep") {
        self.set_direction(face_right ? 4 : 6);
        self.set_action_punch(false);
        self.set_action_kick(false);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 4;
    } else if (chosen == "Duck") {
        self.set_action_punch(false);
        self.set_action_kick(false);
        self.set_action_block(true);
        self.set_action_special(false);
        last_decision_ = 5;
    } else {
        // Idle or unknown label
        self.set_action_punch(false);
        self.set_action_kick(false);
        self.set_action_block(false);
        self.set_action_special(false);
        last_decision_ = 0;
    }

    std::printf("[AI_TACTIC] %s: tactic='%s' dist=%.0f hp=%.2f ehp=%.2f -> %s (decision=%d)\n",
                self.config().name.c_str(),
                tactic->name.c_str(),
                dist, ctx.health, ctx.enemy_health,
                chosen.c_str(), last_decision_);
    return true;
}

} // namespace resf2::fight
