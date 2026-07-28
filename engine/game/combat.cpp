// engine/game/combat.cpp
//
// Combat system implementation — combat timers, enemy AI, hit detection.

#include "combat.hpp"
#include "asset_manager.hpp"
#include "tactic_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace resf2::game {

// ---------- Combat timer decay ----------

void Combat::tick_combat_timers(float dt_sec) {
    if (player_hit_flash_ > 0)
        player_hit_flash_ = std::max(0.0f, player_hit_flash_ - dt_sec);
    if (enemy_hit_flash_ > 0)
        enemy_hit_flash_ = std::max(0.0f, enemy_hit_flash_ - dt_sec);
    if (player_fighter_.hit_stun_time > 0)
        player_fighter_.hit_stun_time = std::max(0.0f, player_fighter_.hit_stun_time - dt_sec);
    if (enemy_fighter_.hit_stun_time > 0)
        enemy_fighter_.hit_stun_time = std::max(0.0f, enemy_fighter_.hit_stun_time - dt_sec);
    if (player_fighter_.invuln_time > 0)
        player_fighter_.invuln_time = std::max(0.0f, player_fighter_.invuln_time - dt_sec);
    if (enemy_fighter_.invuln_time > 0)
        enemy_fighter_.invuln_time = std::max(0.0f, enemy_fighter_.invuln_time - dt_sec);

    // [ORIGINAL] Combo timer: reset combo if no hit for 1.5 seconds (90 frames at 60Hz)
    // Combo.Time = 90 from InternalSettings
    if (combo_timer_ > 0) {
        combo_timer_ -= dt_sec;
        if (combo_timer_ <= 0) {
            // [ORIGINAL] Combo.MinHits = 3 — combo only counts if >= 3 hits within the window
            if (player_fighter_.hits_landed >= 3) {
                std::printf("[COMBAT] Combo ended: %d hits (valid combo)\n", player_fighter_.hits_landed);
            } else if (player_fighter_.hits_landed > 0) {
                std::printf("[COMBAT] Combo ended: %d hits (below MinHits=3)\n", player_fighter_.hits_landed);
            }
            player_fighter_.hits_landed = 0;
            enemy_fighter_.hits_landed = 0;
        }
    }
}

// ---------- Enemy AI ----------

void Combat::update_enemy_ai(
    float dt_sec,
    float player_pos_x,
    const std::string& /*player_anim*/,
    float /*anim_time*/,
    float /*anim_fps*/,
    const std::string& /*current_move*/,
    bool& play_sound_out,
    std::string& sound_name_out,
    float& sound_vol_out
) {
    // [ORIGINAL] Enemy AI: simple state machine.
    // States: 0=idle, 1=approach, 2=attack, 3=retreat, 4=block
    if (enemy_fighter_.is_dead || player_fighter_.is_dead) return;

    enemy_ai_timer_ += dt_sec;
    enemy_attack_cooldown_ = std::max(0.0f, enemy_attack_cooldown_ - dt_sec);

    if (enemy_fighter_.hit_stun_time > 0) {
        // Stunned — can't act
        enemy_anim_ = "fists_hit";
    } else if (enemy_ai_timer_ >= enemy_ai_decision_interval_) {
        enemy_ai_timer_ = 0;
        float dist = std::abs(enemy_pos_x_ - player_pos_x);
        
        // [ORIGINAL] AI tactic roulette from FUN_10171d80
        // Use weighted selection from tacticSettings.xml if available
        if (tactic_settings_ && tactic_settings_->loaded()) {
            // Build candidate list with tactic names
            std::vector<std::string> candidates = {"ForwardStep", "ShortAttack", "BackStep", "Duck"};
            
            // Build context for weight evaluation
            TacticContext ctx;
            ctx.distance = dist;
            ctx.health = enemy_fighter_.health / std::max(1.0f, enemy_fighter_.max_health);
            ctx.enemy_health = player_fighter_.health / std::max(1.0f, player_fighter_.max_health);
            ctx.hits = enemy_fighter_.hits_landed;
            
            // Find enemy's tactic (fallback to "Default" or first available)
            const TacticDef* tactic = tactic_settings_->tactic("Default");
            if (!tactic && tactic_settings_->count() > 0) {
                // Get first available tactic
                for (size_t i = 0; i < tactic_settings_->count(); ++i) {
                    // This is a workaround since we don't have direct iteration
                    // In practice, "Default" or "Basic" should exist
                    break;
                }
            }
            
            if (tactic) {
                int selected_idx = tactic_settings_->choose(*tactic, candidates, ctx);
                if (selected_idx >= 0) {
                    const std::string& selected = candidates[selected_idx];
                    
                    // Map tactic labels to AI states
                    if (selected == "ForwardStep") {
                        enemy_ai_state_ = 1;  // approach
                    } else if (selected == "ShortAttack") {
                        enemy_ai_state_ = 2;  // attack
                    } else if (selected == "BackStep" || selected == "Retreat") {
                        enemy_ai_state_ = 3;  // retreat
                    } else if (selected == "Duck") {
                        enemy_ai_state_ = 4;  // block
                    } else {
                        enemy_ai_state_ = 0;  // idle
                    }
                    
                    std::printf("[COMBAT] AI tactic: dist=%.1f health=%.2f enemy=%.2f -> %s (state=%d)\n",
                                dist, ctx.health, ctx.enemy_health, selected.c_str(), enemy_ai_state_);
                } else {
                    // All weights zero, fallback to simple logic
                    enemy_ai_state_ = (dist > 200) ? 1 : 0;
                    std::printf("[COMBAT] AI tactic: all weights zero, fallback state=%d\n", enemy_ai_state_);
                }
            } else {
                // No tactic found, use fallback
                enemy_ai_state_ = (dist > 200) ? 1 : 0;
                std::printf("[COMBAT] AI tactic: no tactic def, fallback state=%d\n", enemy_ai_state_);
            }
        } else {
            // [HEURISTIC-TODO] Fallback AI without tacticSettings
            int r = std::rand() % 100;

            // [ORIGINAL] AI behavior tuned for engaging combat:
            if (dist > 250) {
                enemy_ai_state_ = 1;  // approach
            } else if (dist > 120) {
                if (r < 50) enemy_ai_state_ = 2;
                else if (r < 70) enemy_ai_state_ = 1;
                else if (r < 80) enemy_ai_state_ = 4;
                else enemy_ai_state_ = 0;
            } else {
                if (r < 35) enemy_ai_state_ = 2;
                else if (r < 55) enemy_ai_state_ = 3;
                else if (r < 75) enemy_ai_state_ = 4;
                else enemy_ai_state_ = 0;
            }

            // Aggression: if player is low health, attack more
            if (player_fighter_.health < 30 && r < 50) {
                enemy_ai_state_ = 2;
            }
            // Self-preservation: if enemy low health, retreat/block more
            if (enemy_fighter_.health < 30 && r < 60) {
                enemy_ai_state_ = (r < 30) ? 4 : 3;
            }
            
            std::printf("[COMBAT] AI fallback: dist=%.1f r=%d state=%d\n", dist, r, enemy_ai_state_);
        }
    }

    // Execute current AI state
    enemy_fighter_.is_blocking = (enemy_ai_state_ == 4);
    float enemy_speed = 90.0f;

    if (enemy_ai_state_ == 1) {  // approach
        if (enemy_pos_x_ > player_pos_x)
            enemy_pos_x_ -= enemy_speed * dt_sec;
        else
            enemy_pos_x_ += enemy_speed * dt_sec;
        enemy_anim_ = "step_forward";
        enemy_facing_right_ = (player_pos_x > enemy_pos_x_);
    } else if (enemy_ai_state_ == 3) {  // retreat
        if (enemy_pos_x_ < player_pos_x)
            enemy_pos_x_ -= enemy_speed * dt_sec;
        else
            enemy_pos_x_ += enemy_speed * dt_sec;
        enemy_anim_ = "step_back";
    } else if (enemy_ai_state_ == 2 && enemy_attack_cooldown_ <= 0) {  // attack
        enemy_anim_ = "high_punch";
        enemy_attacking_ = true;
        enemy_attack_duration_ = 0.4f;
        enemy_attack_cooldown_ = 1.5f;
        play_sound_out = true;
        sound_name_out = "f_pl_attack2";
        sound_vol_out = 0.4f;
        // [ORIGINAL] Dojo is TRAINING — enemy attacks don't deal damage.
    } else if (enemy_ai_state_ == 4) {  // block
        enemy_anim_ = "fists_block";
    } else {  // idle
        enemy_anim_ = "fists_idle";
    }

    if (enemy_attacking_) {
        enemy_attack_duration_ -= dt_sec;
        if (enemy_attack_duration_ <= 0) enemy_attacking_ = false;
    }
    enemy_anim_time_ += dt_sec;

    // Face the player
    enemy_facing_right_ = (player_pos_x > enemy_pos_x_);
}

// ---------- Hit detection ----------

void check_hit_detection(
    Combat& combat,
    AssetManager& assets,
    const HitDetectionInput& input,
    std::function<void(const std::string&, float)> play_sound,
    std::function<void(const std::string&, float, float)> apply_bag_impulse
) {
    if (combat.hit_anim() == 0 || !assets.bag_model()) return;

    auto anim_it = assets.animations().find(*input.current_anim);
    if (anim_it == assets.animations().end()) return;

    int fc = anim_it->second.frame_count;
    int current_frame = (int)(input.anim_time * input.anim_fps);
    auto move_it = assets.moves().find(combat.current_move());

    if (move_it == assets.moves().end() || move_it->second.attack_start <= 0) return;

    // Debug logging for hit check
    {
        std::string expected_anim;
        bool anim_match = false;
        if (move_it != assets.moves().end()) {
            expected_anim = move_it->second.filename;
            if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size() - 4) == ".bin")
                expected_anim = expected_anim.substr(0, expected_anim.size() - 4);
            anim_match = (expected_anim == *input.current_anim);
        }
        std::printf("[HIT_CHECK] f=%llu move='%s' anim='%s' exp_anim='%s' match=%d frame=%d/%d hit_anim=%u atk=%d-%d bag_hit=%d\n",
                    (unsigned long long)input.total_frame_count,
                    combat.current_move().c_str(), input.current_anim->c_str(),
                    expected_anim.c_str(), (int)anim_match,
                    current_frame, fc, combat.hit_anim(),
                    move_it->second.attack_start,
                    move_it->second.attack_end,
                    (int)combat.hit_this_interval());
    }

    int attack_start = move_it->second.attack_start;
    int attack_end = move_it->second.attack_end > 0 ? move_it->second.attack_end : attack_start;
    int frame_start = attack_start - 1;
    int frame_end = attack_end - 1;
    bool in_attack_interval = (current_frame >= frame_start && current_frame <= frame_end);

    if (!in_attack_interval) {
        combat.mutable_hit_this_interval() = false;
    }

    if (in_attack_interval && !combat.hit_this_interval()) {
        // Enemy hit (distance-based fallback)
        if (combat.show_enemy() && combat.enemy_fighter().invuln_time <= 0) {
            float dist_to_enemy = std::abs(combat.mutable_enemy_pos_x() - input.player_pos_x);
            if (dist_to_enemy < 180.0f) {
                combat.enemy_fighter().invuln_time = 0.4f;
                combat.mutable_enemy_hit_flash() = 0.25f;
                int snd_idx = (current_frame + (int)combat.current_move()[0]) % 4 + 1;
                play_sound("f_pl_attack" + std::to_string(snd_idx), 0.7f);
                play_sound("armor", 0.5f);
                combat.mutable_hit_this_interval() = true;
                if (input.hit_sparks) {
                    HitSpark spark;
                    spark.x = combat.mutable_enemy_pos_x() + ((float)(std::rand() % 20) - 10.0f);
                    spark.y = (combat.mutable_enemy_pos_y() - 40) + ((float)(std::rand() % 20) - 10.0f);
                    spark.age = 0;
                    spark.lifetime = 0.36f;
                    spark.scale = 0.8f + (float)(std::rand() % 40) / 100.0f;
                    input.hit_sparks->push_back(spark);
                }
                std::printf("[HIT] f=%llu move='%s' hit enemy dist=%.1f\n",
                    (unsigned long long)input.total_frame_count, combat.current_move().c_str(), dist_to_enemy);
            }
        }

        // Segment-vs-segment collision with bag
        bool hit_registered = false;
        for (auto& edge_name : move_it->second.attack_edges) {
            if (edge_name.empty()) continue;
            auto skel_edge = assets.skeleton_edges().find(edge_name);
            std::string node1, node2;
            if (skel_edge != assets.skeleton_edges().end()) {
                node1 = skel_edge->second.end1;
                node2 = skel_edge->second.end2;
            } else {
                if (edge_name.find("Foot") != std::string::npos ||
                    edge_name.find("Calf") != std::string::npos ||
                    edge_name.find("Leg") != std::string::npos) {
                    node1 = "NToe_1"; node2 = "NAnkle_1";
                } else {
                    node1 = "NWrist_1"; node2 = "NKnuckles_1";
                }
            }

            for (int endpoint = 0; endpoint < 2; endpoint++) {
                std::string& limb_node = (endpoint == 0) ? node1 : node2;
                if (limb_node.empty()) continue;
                auto ait = input.anim_node_pos->find(limb_node);
                if (ait == input.anim_node_pos->end()) continue;

                float limb_lx = ait->second.first;
                float limb_ly = ait->second.second;
                auto pivot_it = assets.skeleton_nodes().find("NPivot");
                float pivot_ly = pivot_it != assets.skeleton_nodes().end() ? pivot_it->second.y : input.stance_npivot_y;
                float limb_wx = input.player_pos_x + (input.facing_right ? limb_lx : -limb_lx);
                float limb_wy = input.player_pos_y + input.y_adjust_smoothed + (limb_ly - pivot_ly);

                float atk_radius = 0;
                auto skel_it = assets.skeleton_edges().find(edge_name);
                if (skel_it != assets.skeleton_edges().end()) {
                    atk_radius = skel_it->second.radius;
                }

                auto ait2 = input.anim_node_pos->find(node2);
                if (ait2 == input.anim_node_pos->end()) continue;

                float limb2_lx = ait2->second.first;
                float limb2_ly = ait2->second.second;
                float limb2_wx = input.player_pos_x + (input.facing_right ? limb2_lx : -limb2_lx);
                float limb2_wy = input.player_pos_y + input.y_adjust_smoothed + (limb2_ly - pivot_ly);

                bool hit_this_interval_this_frame = false;
                for (auto& be : assets.bag_model()->edges) {
                    if (!be.collisible) continue;
                    float bag_r = be.radius;
                    if (bag_r <= 0) continue;
                    if (be.end1.empty() || be.end2.empty()) continue;

                    auto bv1 = input.bag_verlet->find(be.end1);
                    auto bv2 = input.bag_verlet->find(be.end2);
                    if (bv1 == input.bag_verlet->end() || bv2 == input.bag_verlet->end()) continue;

                    float be1x = bv1->second.x, be1y = bv1->second.y;
                    float be2x = bv2->second.x, be2y = bv2->second.y;

                    float ex = limb2_wx - limb_wx, ey = limb2_wy - limb_wy;
                    float fx = be2x - be1x, fy = be2y - be1y;
                    float gx = limb_wx - be1x, gy = limb_wy - be1y;
                    float a = ex * ex + ey * ey;
                    float b = ex * fx + ey * fy;
                    float c = fx * fx + fy * fy;
                    float d = ex * gx + ey * gy;
                    float e = fx * gx + fy * gy;
                    float det = a * c - b * b;
                    float s, t;
                    if (det < 1e-12f) {
                        s = 0.0f;
                        t = (b > c) ? d / b : e / c;
                        t = std::max(0.0f, std::min(1.0f, t));
                    } else {
                        s = (b * e - c * d) / det;
                        t = (a * e - b * d) / det;
                        if (s < 0) { s = 0; t = e / c; t = std::max(0.0f, std::min(1.0f, t)); }
                        else if (s > 1) { s = 1; t = (b + e) / c; t = std::max(0.0f, std::min(1.0f, t)); }
                        else if (t < 0) { t = 0; s = -d / a; s = std::max(0.0f, std::min(1.0f, s)); }
                        else if (t > 1) { t = 1; s = (b - d) / a; s = std::max(0.0f, std::min(1.0f, s)); }
                    }
                    float px = limb_wx + s * ex, py = limb_wy + s * ey;
                    float qx = be1x + t * fx, qy = be1y + t * fy;
                    float rx = px - qx, ry = py - qy;
                    float sq_dist = rx * rx + ry * ry;
                    float threshold = atk_radius + bag_r;
                    if (sq_dist < threshold * threshold) {
                        std::printf("[COMBAT] HIT! move=%s frame=%d/%d [%d-%d] atk_edge=%s bag_edge=%s sq_dist=%.1f thresh=%.1f (atk_r=%.1f bag_r=%.1f)\n",
                                    combat.current_move().c_str(), current_frame, fc,
                                    frame_start, frame_end,
                                    edge_name.c_str(), be.name.c_str(),
                                    sq_dist, threshold * threshold, atk_radius, bag_r);
                        float imp_x = move_it->second.impulse_x;
                        float imp_y = move_it->second.impulse_y;
                        if (imp_x != 0 || imp_y != 0) {
                            float dir = input.facing_right ? 1.0f : -1.0f;
                            float hit_ratio = std::max(0.0f, std::min(1.0f, t));
                            float dist1 = 1.0f - hit_ratio;
                            float dist2 = hit_ratio;
                            apply_bag_impulse(be.end1, dir * imp_x * dist1, imp_y * dist1);
                            apply_bag_impulse(be.end2, dir * imp_x * dist2, imp_y * dist2);
                        }
                        combat.mutable_hit_this_interval() = true;
                        hit_this_interval_this_frame = true;
                        hit_registered = true;
                        int snd_idx = (current_frame + (int)combat.current_move()[0]) % 4 + 1;
                        play_sound("f_pl_attack" + std::to_string(snd_idx), 0.8f);
                        play_sound("armor", 0.6f);
                        if (combat.enemy_fighter().invuln_time <= 0) {
                            combat.enemy_fighter().invuln_time = 0.2f;
                            combat.mutable_enemy_hit_flash() = 0.2f;
                            combat.player_fighter().hits_landed++;
                            // [ORIGINAL] Combo.Time = 90 frames = 1.5s at 60Hz (from InternalSettings)
                            combat.mutable_combo_timer() = 1.5f;
                            std::printf("[COMBAT] Combo: hits=%d timer=1.5s\n", combat.player_fighter().hits_landed);
                            int hit_snd_idx = (current_frame + (int)combat.current_move()[0]) % 4 + 1;
                            play_sound("f_pl_attack" + std::to_string(hit_snd_idx), 0.7f);
                            play_sound("armor", 0.5f);
                            if (input.hit_sparks) {
                                HitSpark spark;
                                spark.x = combat.mutable_enemy_pos_x() + ((float)(std::rand() % 20) - 10.0f);
                                spark.y = (combat.mutable_enemy_pos_y() - 40) + ((float)(std::rand() % 20) - 10.0f);
                                spark.age = 0;
                                spark.lifetime = 0.36f;
                                spark.scale = 0.8f + (float)(std::rand() % 40) / 100.0f;
                                input.hit_sparks->push_back(spark);
                            }
                        }
                        break;
                    }
                }
                if (hit_this_interval_this_frame) break;
            }
            if (hit_registered) break;
        }
    }

    // Cleanup: clear current_move when animation has finished
    if (combat.hit_anim() == 0 && combat.move_state() == 0) {
        combat.mutable_need_switch_to_idle() = true;
        combat.mutable_current_move().clear();
        combat.mutable_hit_this_interval() = false;
    }
    if (combat.hit_anim() == 0 && combat.move_state() != 0 && !combat.current_move().empty()) {
        combat.mutable_current_move().clear();
        combat.mutable_hit_this_interval() = false;
    }
}

} // namespace resf2::game
