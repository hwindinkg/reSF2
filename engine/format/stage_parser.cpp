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

// [ORIGINAL] Read one <Warrior>, including its nested variant warriors.
//
// Warriors nest: 1227 <Warrior> elements exist in stages.xml but only 699 are
// direct children of a <Warriors> block, so reading a single level loses a
// third of the roster (and with it their equipment and attributes).
static StageWarrior parse_warrior(XmlNode& wr) {
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

    // Attributes the damage model reads. Note the default is 0, not 1: an
    // absent RangedDamage means "no ranged capability", whereas an absent
    // WeaponDamage above means "unscaled" and so defaults to 1.
    w.ranged_damage = to_float(wr.attr("RangedDamage"));
    w.magic_damage = to_float(wr.attr("MagicDamage"));
    w.ranged_quantity = to_float(wr.attr("RangedQuantity"));
    w.enchantment_resistance = to_float(wr.attr("EnchantmentResistance"));
    w.magic_initial_charge = to_float(wr.attr("MagicInitialCharge"));
    w.magic_damage_recharge = to_float(wr.attr("MagicDamageRecharge"));
    w.magic_pain_recharge = to_float(wr.attr("MagicPainRecharge"));
    w.rating_correction = to_float(wr.attr("RatingCorrection"));
    w.player_rating = to_float(wr.attr("PlayerRating"));
    w.enemy_rating = to_float(wr.attr("EnemyRating"));
    w.number = to_int(wr.attr("Number"));

    // Equipment: <Items><Item Name=..> names weapon / armour / helmet and the
    // skeleton to wear them on. Some warriors list <Item> directly, so accept
    // both shapes but never both for the same node.
    if (auto* items = wr.first_child("Items")) {
        for (auto& in : items->children) {
            if (in.name != "Item") continue;
            auto name = in.attr("Name");
            if (!name.empty()) w.items.push_back(std::move(name));
        }
    } else {
        for (auto& in : wr.children) {
            if (in.name != "Item") continue;
            auto name = in.attr("Name");
            if (!name.empty()) w.items.push_back(std::move(name));
        }
    }

    if (auto* perks = wr.first_child("Perks")) {
        for (auto& pn : perks->children) {
            if (pn.name != "Perk") continue;
            auto name = pn.attr("Name");
            if (!name.empty()) w.perks.push_back(std::move(name));
        }
    } else {
        for (auto& pn : wr.children) {
            if (pn.name != "Perk") continue;
            auto name = pn.attr("Name");
            if (!name.empty()) w.perks.push_back(std::move(name));
        }
    }

    if (auto* align = wr.first_child("AttributesAlign")) {
        for (auto& dn : align->children) {
            if (dn.name != "Delta") continue;
            StageAttributeDelta d;
            d.factor = to_float(dn.attr("Factor"), 1.0f);
            d.shift = to_float(dn.attr("Shift"));
            w.attributes_align.push_back(d);
        }
    }

    // Nested variants.
    for (auto& child : wr.children) {
        if (child.name == "Warrior") {
            w.variants.push_back(parse_warrior(child));
        } else if (child.name == "WarriorGroup") {
            for (auto& gw : child.children) {
                if (gw.name == "Warrior") w.variants.push_back(parse_warrior(gw));
            }
        }
    }
    return w;
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

                // [ORIGINAL] <Rules> holds one element per rule, where the
                // element name is the rule kind and Name= its argument.
                if (auto* rules = fn.first_child("Rules")) {
                    for (auto& rn : rules->children) {
                        StageRule rule;
                        rule.kind = rn.name;
                        rule.name = rn.attr("Name");
                        fight.rules.push_back(std::move(rule));
                    }
                }

                auto* warriors = fn.first_child("Warriors");
                if (warriors) {
                    for (auto& wr : warriors->children) {
                        if (wr.name == "WarriorGroup") {
                            // An inline pool: treat its members as warriors of
                            // this fight.
                            for (auto& gw : wr.children) {
                                if (gw.name == "Warrior")
                                    fight.warriors.push_back(parse_warrior(gw));
                            }
                            continue;
                        }
                        if (wr.name != "Warrior") continue;
                        fight.warriors.push_back(parse_warrior(wr));
                    }
                }

                battle.fights.push_back(std::move(fight));
            }

            zone.battles.push_back(std::move(battle));
        }

        out.zones.push_back(std::move(zone));
    }

    // [ORIGINAL] <Stages><Warriors> holds the shared roster data: <Templates>
    // (the named builds every <Warrior Template=".."> refers to) and
    // <WarriorGroups> (the random pools SURVIVAL/TOURNAMENT draw from). Both
    // sit one level deeper than the per-fight <Warriors> blocks, which is why
    // they were previously missed entirely.
    XmlNode* roster = nullptr;
    for (auto& child : stages->children) {
        if (child.name != "Warriors") continue;
        if (child.first_child("Templates") || child.first_child("WarriorGroups")) {
            roster = &child;
            break;
        }
    }

    if (roster) {
        if (auto* templates = roster->first_child("Templates")) {
            for (auto& tn : templates->children) {
                if (tn.name != "Template") continue;
                StageTemplate t;
                t.name = tn.attr("Name");
                t.parent = tn.attr("Template");
                t.first_name = tn.attr("FirstName");
                t.avatar = tn.attr("Avatar");
                t.voice = tn.attr("Voice");
                // [ORIGINAL] <Template CriticalChance=.. CriticalDamage=..>
                // — the hero's crit attributes (stages.xml Default:
                // CriticalChance="1000"); absent -> 0 (CriticalDamage=0
                // means the 2^(base*attr) crit multiplier is 1.0).
                t.critical_chance = (int)to_float(tn.attr("CriticalChance"));
                t.critical_damage = (int)to_float(tn.attr("CriticalDamage"));
                if (auto* items = tn.first_child("Items")) {
                    for (auto& in : items->children) {
                        if (in.name != "Item") continue;
                        auto nm = in.attr("Name");
                        if (!nm.empty()) t.items.push_back(std::move(nm));
                    }
                }
                if (auto* perks = tn.first_child("Perks")) {
                    for (auto& pn : perks->children) {
                        if (pn.name != "Perk") continue;
                        auto nm = pn.attr("Name");
                        if (!nm.empty()) t.perks.push_back(std::move(nm));
                    }
                }
                out.templates.push_back(std::move(t));
            }
        }
    }

    XmlNode* group_owner = roster ? roster : stages;
    if (auto* groups = group_owner->first_child("WarriorGroups")) {
        for (auto& gn : groups->children) {
            if (gn.name != "WarriorGroup") continue;
            StageWarriorGroup g;
            g.name = gn.attr("Name");
            for (auto& wn : gn.children) {
                if (wn.name == "Warrior") g.warriors.push_back(parse_warrior(wn));
            }
            out.warrior_groups.push_back(std::move(g));
        }
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
