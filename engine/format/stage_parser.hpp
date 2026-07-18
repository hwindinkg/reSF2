#pragma once

#include <string>
#include <vector>

namespace resf2::format {

struct StageReward {
    int money = 0;
    int exp = 0;
    int prize_base = 0;
};

struct StageWarrior {
    std::string template_name;
    std::string tactic;
    std::string beginner_cheat;
    std::string first_name;
    float weapon_damage = 1.0f;
    float unarmed_damage = 1.0f;
    float body_defense = 1.0f;
    float head_defense = 1.0f;
    float critical_chance = 0.0f;
    float warrior_power = 0.0f;
    bool not_ai = false;
    bool not_animation = false;
};

struct StageFight {
    std::string name;
    int power = 0;
    int rounds = 1;
    int round_time = 99;
    int replays = 0;
    StageReward reward;
    std::vector<StageWarrior> warriors;
};

struct StageBattle {
    std::string name;
    std::string alias;
    std::string title;
    std::string icon;
    std::string preview;
    std::string type;
    float x = 0, y = 0;
    std::string location;
    std::string music;
    std::vector<StageFight> fights;
};

struct StageZone {
    std::string name;
    int start = 0;
    std::string filename;
    std::vector<StageBattle> battles;
};

struct StageData {
    std::vector<StageZone> zones;
};

class StageParser {
public:
    bool parse(const std::string& xml, StageData& out);
    bool load_file(const std::string& path, StageData& out);
    const std::string& error() const { return error_; }

private:
    std::string error_;
};

} // namespace resf2::format
