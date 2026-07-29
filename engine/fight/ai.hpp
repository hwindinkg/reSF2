#pragma once

#include <cstdint>
#include <string>
#include "../core/math.hpp"
#include "fighter.hpp"

// [ORIGINAL] Forward-declare the tactic system from engine/game so the
// fight-level AI controller can use weighted roulette selection without
// pulling the full game header into ai.hpp's public surface.
namespace resf2::game {
class TacticSettings;
struct TacticDef;
struct TacticContext;
}  // namespace resf2::game

namespace resf2::fight {

// AI opponent using tactic tables from .atf files and tacticSettings.xml
class AIController {
public:
    void set_tactic_data(const uint8_t* data, size_t size);

    // Called each frame to decide AI actions
    void update(float dt, Fighter& self, const Fighter& opponent);

    // Difficulty settings (from ComputerSettings.xml)
    struct Settings {
        float reaction_time = 0.2f;    // seconds before AI responds
        float aggression = 0.5f;        // 0=defensive, 1=aggressive
        float accuracy = 0.7f;          // chance to pick optimal move
        float block_chance = 0.3f;
        float attack_chance = 0.4f;
        float special_chance = 0.1f;
    };
    Settings settings;

    void set_settings(const Settings& s) { settings = s; }
    bool has_tactic_data() const { return tactic_loaded_; }
    int last_decision() const { return last_decision_; }

    // [ORIGINAL] Tactic-driven decision making (from tacticSettings.xml).
    // When set, the AI uses weighted roulette selection instead of the
    // hardcoded distance/health thresholds. Matches FUN_10171d80 / jL / cc.
    void set_tactic_settings(const resf2::game::TacticSettings* ts) {
        tactic_settings_ = ts;
    }

    // Which <Tactic> entry to use for this AI fighter.
    // Falls back to "Default" if not set or not found.
    void set_tactic_name(const std::string& name) { tactic_name_ = name; }

private:
    float react_timer_ = 0;
    int last_decision_ = 8; // Central
    float decision_cooldown_ = 0;

    // Tactic data (decompressed .atf binary records)
    std::vector<std::byte> tactic_data_;
    bool tactic_loaded_ = false;

    // [ORIGINAL] Tactic-driven AI (tacticSettings.xml roulette)
    const resf2::game::TacticSettings* tactic_settings_ = nullptr;
    std::string tactic_name_;

    // Evaluate distances and pick a move
    void decide(Fighter& self, const Fighter& opponent);

    // [ORIGINAL] Tactic-driven decide — uses TacticSettings::choose() roulette
    // Returns true if a tactic decision was made, false if fallback is needed.
    bool decide_by_tactic(Fighter& self, const Fighter& opponent, float dist);
};

} // namespace resf2::fight
