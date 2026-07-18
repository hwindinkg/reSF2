#include "stage_parser.hpp"
#include "xml_doc.hpp"
#include <cstdio>
#include <fstream>
#include <charconv>

namespace resf2::format {

static int to_int(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    int v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

static float to_float(const std::string& s, float def = 0) {
    if (s.empty()) return def;
    float v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

static bool to_bool(const std::string& s) {
    return s == "1" || s == "true" || s == "True";
}

bool StageParser::parse(const std::string& xml, StageData& out) {
    XmlDocument doc;
    if (!doc.parse(xml)) {
        error_ = doc.error();
        return false;
    }

    auto* stages = doc.root()->first_child("Stages");
    if (!stages) { error_ = "No <Stages>"; return false; }

    auto* zones = stages->first_child("Zones");
    if (!zones) { error_ = "No <Zones>"; return false; }

    for (auto& zn : zones->children) {
        if (zn.name != "Zone") continue;

        StageZone zone;
        zone.name = zn.attr("Name");
        zone.start = to_int(zn.attr("Start"));
        zone.filename = zn.attr("FileName");

        for (auto& bn : zn.children) {
            if (bn.name != "Battle") continue;

            StageBattle battle;
            battle.name = bn.attr("Name");
            battle.alias = bn.attr("Alias");
            battle.title = bn.attr("Title");
            battle.icon = bn.attr("Icon");
            battle.preview = bn.attr("Preview");
            battle.type = bn.attr("Type");
            battle.x = to_float(bn.attr("X"));
            battle.y = to_float(bn.attr("Y"));
            battle.location = bn.attr("Location");
            battle.music = bn.attr("Music");

            for (auto& fn : bn.children) {
                if (fn.name != "Fight") continue;

                StageFight fight;
                fight.name = fn.attr("Name");
                fight.power = to_int(fn.attr("Power"));
                fight.rounds = to_int(fn.attr("Rounds"), 1);
                fight.round_time = to_int(fn.attr("RoundTime"), 99);
                fight.replays = to_int(fn.attr("Replays"));

                auto* rewards = fn.first_child("Rewards");
                if (rewards) {
                    auto* rw = rewards->first_child("Reward");
                    if (rw) {
                        fight.reward.money = to_int(rw->attr("Money"));
                        fight.reward.exp = to_int(rw->attr("Exp"));
                        fight.reward.prize_base = to_int(rw->attr("PrizeBase"));
                    }
                }

                auto* warriors = fn.first_child("Warriors");
                if (warriors) {
                    for (auto& wr : warriors->children) {
                        if (wr.name != "Warrior") continue;
                        StageWarrior w;
                        w.template_name = wr.attr("Template");
                        w.tactic = wr.attr("Tactic");
                        w.beginner_cheat = wr.attr("BeginnerCheat");
                        w.first_name = wr.attr("FirstName");
                        w.weapon_damage = to_float(wr.attr("WeaponDamage"), 1);
                        w.unarmed_damage = to_float(wr.attr("UnarmedDamage"), 1);
                        w.body_defense = to_float(wr.attr("BodyDefense"), 1);
                        w.head_defense = to_float(wr.attr("HeadDefense"), 1);
                        w.critical_chance = to_float(wr.attr("CriticalChance"));
                        w.warrior_power = to_float(wr.attr("WarriorPower"));
                        w.not_ai = to_bool(wr.attr("NotAI"));
                        w.not_animation = to_bool(wr.attr("NotAnimation"));
                        fight.warriors.push_back(std::move(w));
                    }
                }

                battle.fights.push_back(std::move(fight));
            }

            zone.battles.push_back(std::move(battle));
        }

        out.zones.push_back(std::move(zone));
    }

    return true;
}

bool StageParser::load_file(const std::string& path, StageData& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { error_ = "Cannot open: " + path; return false; }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return parse(data, out);
}

} // namespace resf2::format
