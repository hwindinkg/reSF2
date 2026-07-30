#pragma once

#include <string>
#include <vector>

namespace resf2::format {

struct StageReward {
    int money = 0;
    int exp = 0;
    int prize_base = 0;
};

// [ORIGINAL] <Warrior><AttributesAlign><Delta Factor="1" Shift="0"/>
// The per-opponent alignment pair that feeds the damage formula's
// "Delta.Factor / Shift" stage (see the tracer output documented in
// reverse/analysis/PORT_GAPS.md, and Model::getTotalDamage @ game+0x4527B4).
struct StageAttributeDelta {
    float factor = 1.0f;
    float shift = 0.0f;
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

    // [ORIGINAL] Attributes the damage model reads that were previously
    // dropped. RangedDamage / MagicDamage / EnchantmentResistance are three of
    // the seven names Model::getParameter (game+0x6275F4) resolves; without
    // them ranged and magic opponents cannot be simulated at all.
    float ranged_damage = 0.0f;
    float magic_damage = 0.0f;
    float ranged_quantity = 0.0f;
    float enchantment_resistance = 0.0f;
    float magic_initial_charge = 0.0f;
    float magic_damage_recharge = 0.0f;
    float magic_pain_recharge = 0.0f;
    float rating_correction = 0.0f;
    float player_rating = 0.0f;
    float enemy_rating = 0.0f;
    int number = 0;

    // [ORIGINAL] <Items><Item Name="..."/> — the opponent's equipment: weapon,
    // armour, helmet, and the skeleton/model to wear them on. 2200 of these
    // appear in stages.xml. Dropping them left every enemy bare-handed.
    std::vector<std::string> items;

    // [ORIGINAL] <Perks><Perk Name="..."/> — the opponent's active perks.
    std::vector<std::string> perks;

    // [ORIGINAL] <AttributesAlign><Delta .../>
    std::vector<StageAttributeDelta> attributes_align;

    // [ORIGINAL] <Warrior> nests: a wave/variant of an opponent is expressed as
    // a child <Warrior> that inherits the parent's fields. 1227 <Warrior>
    // elements exist but only 699 are direct children of a <Warriors> block, so
    // a parser that reads one level deep loses a third of the roster.
    std::vector<StageWarrior> variants;
};

// [ORIGINAL] <Fight><Rules><NoPerks Name="EndStanceClear"/></Rules>
// Per-fight modifiers. The element NAME is the rule kind and Name= is its
// argument, so both have to be kept.
struct StageRule {
    std::string kind;   // element name, e.g. "NoPerks"
    std::string name;   // Name attribute, e.g. "EndStanceClear"
};

struct StageFight {
    std::string name;
    int power = 0;
    int rounds = 1;
    int round_time = 99;
    int replays = 0;

    // [ORIGINAL] Per-fight rules; previously discarded.
    std::vector<StageRule> rules;
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

// [ORIGINAL] <Stages><Warriors><Templates><Template Name="..." Template="...">
//
// The named opponent builds every <Warrior Template="X"> refers to. 191 of
// them, each carrying the Avatar, Voice, FirstName and the <Items> list that
// determines what the opponent actually LOOKS like and fights with. The parser
// previously ignored <Templates> entirely, which is why opponents had no
// equipment and no avatar: 722 warriors reference a Template and the template
// itself was never read.
//
// `parent` is the Template attribute, i.e. templates inherit from one another
// (190 of the 191 do); "Default" is the root build.
struct StageTemplate {
    std::string name;
    std::string parent;      // Template="..." -- inherit from this build
    std::string first_name;  // localization key for the displayed name
    std::string avatar;      // avatar texture name
    std::string voice;       // "Male" / "Female" / ...
    std::vector<std::string> items;
    std::vector<std::string> perks;
};

// [ORIGINAL] <WarriorGroups><WarriorGroup Name="Generic_1"><Warrior Template=..>
// A named pool the game draws random opponents from -- used by SURVIVAL and
// TOURNAMENT battles, which otherwise have no roster at all.
struct StageWarriorGroup {
    std::string name;
    std::vector<StageWarrior> warriors;
};

struct StageZone {
    std::string name;
    int start = 0;
    std::string filename;
    std::vector<StageBattle> battles;
};

struct StageData {
    std::vector<StageZone> zones;

    // [ORIGINAL] Opponent pools, referenced by name from battles.
    std::vector<StageWarriorGroup> warrior_groups;

    // [ORIGINAL] Named opponent builds, referenced by <Warrior Template="..">.
    std::vector<StageTemplate> templates;
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
