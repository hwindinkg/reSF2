#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.hpp"
#include "tactic_memory.hpp"

namespace resf2::game {

// Forward declarations
class AssetManager;
class Combat;

struct VerletNode;  // already in types.hpp
struct HitSpark;    // already in types.hpp

// ---------- Hit detection input ----------
// Parameters from Game/AnimationPlayer that Combat needs for hit detection.

struct HitDetectionInput {
    // Animation state (from AnimationPlayer)
    const std::unordered_map<std::string, std::pair<float, float>>* anim_node_pos = nullptr;
    float y_adjust_smoothed = 0.0f;
    float stance_npivot_y = 106.0f;
    uint64_t total_frame_count = 0;
    const std::string* current_anim = nullptr;
    float anim_time = 0.0f;
    float anim_fps = 20.0f;

    // Player state
    float player_pos_x = 0.0f;
    float player_pos_y = 0.0f;
    bool facing_right = true;

    // Bag verlet physics (modified on hit impulse)
    std::unordered_map<std::string, VerletNode>* bag_verlet = nullptr;

    // Hit sparks output (new entries are appended)
    std::vector<HitSpark>* hit_sparks = nullptr;
};

// Check hit detection for the current move/frame.
// Modifies combat state (hit_this_interval_, fighter states, combo, etc.)
// and calls play_sound / apply_bag_impulse callbacks for side effects.
void check_hit_detection(
    Combat& combat,
    AssetManager& assets,
    const HitDetectionInput& input,
    std::function<void(const std::string&, float)> play_sound,
    std::function<void(const std::string&, float, float)> apply_bag_impulse
);

// ---------- Combat system ----------
//
// Encapsulates combat logic: move selection, hit detection,
// damage application, enemy AI, and fighter state management.

class Combat {
public:
    Combat() = default;

    // Access fighter states (mutable — direct field access works)
    FighterState& player_fighter() { return player_fighter_; }
    const FighterState& player_fighter() const { return player_fighter_; }
    FighterState& enemy_fighter() { return enemy_fighter_; }
    const FighterState& enemy_fighter() const { return enemy_fighter_; }

    // --- Mutable accessors (for Game reference aliasing) ---
    std::string& mutable_current_move() { return current_move_; }
    bool& mutable_hit_this_interval() { return hit_this_interval_; }
    int& mutable_move_state() { return move_state_; }
    bool& mutable_is_uninterrupt() { return is_uninterrupt_; }
    uint32_t& mutable_hit_anim() { return hit_anim_; }
    float& mutable_player_hit_flash() { return player_hit_flash_; }
    float& mutable_enemy_hit_flash() { return enemy_hit_flash_; }
    float& mutable_combo_timer() { return combo_timer_; }
    int& mutable_no_key_frames() { return no_key_frames_; }
    bool& mutable_start_stance_playing() { return start_stance_playing_; }
    bool& mutable_need_switch_to_idle() { return need_switch_to_idle_; }
    uint32_t& mutable_step_cooldown() { return step_cooldown_; }
    bool& mutable_step_active() { return step_active_; }
    uint32_t& mutable_step_duration() { return step_duration_; }
    float& mutable_step_start_x() { return step_start_x_; }
    float& mutable_step_displacement() { return step_displacement_; }
    int& mutable_bag_swing() { return bag_swing_; }
    float& mutable_bag_swing_dir() { return bag_swing_dir_; }
    float& mutable_bag_angle() { return bag_angle_; }
    float& mutable_bag_angle_vel() { return bag_angle_vel_; }
    float& mutable_enemy_ai_timer() { return enemy_ai_timer_; }
    float& mutable_enemy_ai_decision_interval() { return enemy_ai_decision_interval_; }
    int& mutable_enemy_ai_state() { return enemy_ai_state_; }
    float& mutable_enemy_attack_cooldown() { return enemy_attack_cooldown_; }
    float& mutable_enemy_pos_x() { return enemy_pos_x_; }
    float& mutable_enemy_pos_y() { return enemy_pos_y_; }
    std::string& mutable_enemy_anim() { return enemy_anim_; }
    float& mutable_enemy_anim_time() { return enemy_anim_time_; }
    bool& mutable_enemy_facing_right() { return enemy_facing_right_; }
    float& mutable_enemy_y_adjust() { return enemy_y_adjust_; }
    bool& mutable_enemy_attacking() { return enemy_attacking_; }
    float& mutable_enemy_attack_duration() { return enemy_attack_duration_; }
    bool& mutable_show_enemy() { return show_enemy_; }
    bool& mutable_is_battle_mode() { return is_battle_mode_; }
    // [D3] The enemy-AI fight memory the TacticDecisionPipeline reads and
    // decays (ADR-005 D8); Game ticks it per AI frame.
    TacticMemory& mutable_enemy_tactic_memory() { return enemy_tactic_memory_; }

    // --- Block decision state (player AI) ---
    // [ORIGINAL] FUN_10171d80 runs every 0.6-1.0s, roulette over tactic weights.
    // "Duck" in the animation weights maps to block action.
    float& mutable_block_decision_cooldown() { return block_decision_cooldown_; }
    bool& mutable_block_decision_pending() { return block_decision_pending_; }
    float& mutable_recent_damage_taken() { return recent_damage_taken_; }
    int& mutable_enemy_hits_on_player() { return enemy_hits_on_player_; }

    // --- Const accessors (for read-only access) ---
    const std::string& current_move() const { return current_move_; }
    bool hit_this_interval() const { return hit_this_interval_; }
    void reset_hit_this_interval() { hit_this_interval_ = false; }
    int move_state() const { return move_state_; }
    bool is_uninterrupt() const { return is_uninterrupt_; }
    uint32_t hit_anim() const { return hit_anim_; }
    void dec_hit_anim(uint32_t dt) { if (hit_anim_ > dt) hit_anim_ -= dt; else hit_anim_ = 0; }
    float player_hit_flash() const { return player_hit_flash_; }
    float enemy_hit_flash() const { return enemy_hit_flash_; }
    float combo_timer() const { return combo_timer_; }
    int no_key_frames() const { return no_key_frames_; }
    bool start_stance_playing() const { return start_stance_playing_; }
    bool need_switch_to_idle() const { return need_switch_to_idle_; }
    bool show_enemy() const { return show_enemy_; }
    bool is_battle_mode() const { return is_battle_mode_; }

    // Tick combat timers (hit flash, stun, invuln, combo)
    void tick_combat_timers(float dt_sec);

    // [ORIGINAL] Combo.MinHits = 3 (from InternalSettings)
    // Returns true if the current hit count qualifies as a combo (>= 3 hits)
    [[nodiscard]] bool has_valid_combo() const {
        return player_fighter_.hits_landed >= 3 && combo_timer_ > 0;
    }

    // Set tactic settings for AI decision making
    void set_tactic_settings(const class TacticSettings* settings) { tactic_settings_ = settings; }

    // Enemy AI update
    void update_enemy_ai(
        float dt_sec,
        float player_pos_x,
        const std::string& player_anim,
        float anim_time,
        float anim_fps,
        const std::string& current_move,
        bool& play_sound_out,
        std::string& sound_name_out,
        float& sound_vol_out
    );

private:
    // Fighter states
    FighterState player_fighter_;
    FighterState enemy_fighter_;

    // Combat state
    std::string current_move_;
    bool hit_this_interval_ = false;
    int move_state_ = 0;
    bool is_uninterrupt_ = false;
    uint32_t hit_anim_ = 0;
    float player_hit_flash_ = 0.0f;
    float enemy_hit_flash_ = 0.0f;
    float combo_timer_ = 0.0f;
    int no_key_frames_ = 0;

    // Step state
    uint32_t step_cooldown_ = 0;
    bool step_active_ = false;
    uint32_t step_duration_ = 0;
    float step_start_x_ = 0;
    float step_displacement_ = 0;
    bool start_stance_playing_ = false;
    bool need_switch_to_idle_ = false;

    // Bag physics
    int bag_swing_ = 0;
    float bag_swing_dir_ = 1.0f;
    float bag_angle_ = 0.0f;
    float bag_angle_vel_ = 0.0f;

    // AI state
    float enemy_ai_timer_ = 0.0f;
    float enemy_ai_decision_interval_ = 0.8f;
    int enemy_ai_state_ = 0;
    float enemy_attack_cooldown_ = 0.0f;

    // Enemy state (used by AI and rendering)
    std::string enemy_anim_ = "fists_idle";
    float enemy_anim_time_ = 0.0f;
    float enemy_pos_x_ = 0.0f;
    float enemy_pos_y_ = 0.0f;
    bool enemy_facing_right_ = false;
    float enemy_y_adjust_ = 0.0f;
    bool enemy_attacking_ = false;
    float enemy_attack_duration_ = 0.0f;
    // [D3] Enemy-AI fight memory (ADR-005 D8): per-animation decayed records
    // consumed by the TacticDecisionPipeline; ticked once per AI frame.
    TacticMemory enemy_tactic_memory_;
    bool show_enemy_ = false;
    bool is_battle_mode_ = false;
    
    // [ORIGINAL] AI tactic system from tacticSettings.xml
    const class TacticSettings* tactic_settings_ = nullptr;

    // --- Player block decision state (FUN_10171d80) ---
    // [ORIGINAL] Block is NOT automatic — it's a weighted roulette decision
    // every 0.6-1.0s. BlockChance factors from tacticSettings.xml determine
    // the weight of "Duck" (block) candidate.
    float block_decision_cooldown_ = 0.0f;   // seconds until next decision
    bool block_decision_pending_ = false;     // true when decision needs evaluation
    float recent_damage_taken_ = 0.0f;        // damage recently taken (decays)
    int enemy_hits_on_player_ = 0;            // enemy hits landed on player
};

} // namespace resf2::game
