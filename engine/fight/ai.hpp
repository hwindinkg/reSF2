#pragma once

#include <cstdint>
#include <string>
#include "../core/math.hpp"
#include "fighter.hpp"

namespace resf2::fight {

// AI opponent using tactic tables from .atf files
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

private:
    float react_timer_ = 0;
    int last_decision_ = 8; // Central
    float decision_cooldown_ = 0;

    // Tactic data (decompressed .atf binary records)
    std::vector<std::byte> tactic_data_;
    bool tactic_loaded_ = false;

    // Evaluate distances and pick a move
    void decide(Fighter& self, const Fighter& opponent);
};

} // namespace resf2::fight
