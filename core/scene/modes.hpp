#pragma once

// Tournament/survival fight setup (stages.xml Battles + JS series flow).
//
// Static sources: `reference/extracted/xml/res/stages.xml` (Battle/Fight/
// Warrior/Rules/Rewards/Groups/Templates) + sf2.502f0946.js.
//   - Tournament: N sequential Fights; each has Warriors (Template/Items/
//     Tactic/stats/Perks), Rules (Attributes DamageFactor ladders per
//     side, Eclipse, NoBulletsReplenishment), Rewards[participation, win].
//   - Survival: ONE Fight with per-wave Rewards + Warriors with Number
//     (wave count; `z6a` = sum over Xs) + Groups (random pools,
//     `efa` expansion with Random/NoDoubles).
// Series flow (`Da.sR`: `Xs.length>1 ? pT>1 : false`; `Rk` = series index,
// `ng` = rounds won per warrior reset per battle, `Wka` per-copy init):
//   - `ModeSeries` tracks (battle type, fight index, wave, used templates)
//     + `advance()` (tournament next-fight / survival next-wave).
//   - `resolve_*` maps (battle, index/wave, rng) to a `ModeFight`.
// Fight application: `FightController::apply_mode_setup` (rounds/time/
// recovery/rules/enemy config). Enemy Template stat merge (`ur`) and
// AttributesAlign application are OPEN (parsed + stored); intros (`hCa`),
// PF lists (`vJa`), area counters (`lGa`), quest counters (`Fsb`), entry
// energy (`qZa`/`uZa`), repeat caps (`Replays`, map-side YL) are map/UI
// side — noted, not fight scope.

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <pugixml.hpp>

namespace sf2::scene {

// One Fight <Warrior> (or <Template> member / group member).
struct StageWarrior {
    std::string template_name;  // Template= ("" = inline/base)
    std::string tactic;  // Tactic= (Beginner/Standard/...)
    std::map<std::string, std::string> attrs;  // all other attrs (stats)
    std::vector<std::string> items;  // <Items><Item Name>
    std::vector<std::string> perks;  // <Perks><Perk Name>
    struct GroupRef {
        std::string name;
        bool random = false;  // Random="1" (eb)
        bool no_doubles = false;  // NoDoubles="1" (fK)
    };
    std::vector<GroupRef> groups;
    int number = 1;  // Number= (wave count; `z6a` sums over Xs)
    struct Delta {
        double factor = 0.0, shift = 0.0;
        int priority = 0;
        // `OP` (JS `Ci.OP`, L191): absent Eclipse -> 2, Eclipse="0" -> 1,
        // Eclipse="1" -> 0. `v.eNa` (L1204) filters the damage `IY` rows by
        // this and the eclipse flag.
        int eclipse_op = 2;
    };
    std::vector<Delta> align;  // <AttributesAlign> (application OPEN)
};

// One Fight <Rules> child.
struct StageRule {
    std::string tag;  // Attributes / NoBulletsReplenishment / ...
    std::map<std::string, std::string> attrs;
    // Child `<Animation Name="..."/>` names (JS `Ce.c4a` L848 -> `ra.yz`):
    // the `EM` animation list every animation-scoped rule (`Ce`: HotGround,
    // LoseFall, DamageFactor) matches against (`Lba`, L848). Empty -> no
    // animations (LoseFall then never arms; HotGround `Voa` stays false).
    std::vector<std::string> animations;
    // Child `<Node Name=".." Axis=".." Min=".." Max=".."/>` zones (JS
    // `en.Mia` L860 -> `fv` L861-862): HotGround's per-body-part zones. The
    // `fv` ctor seeds N/J = +/-3.4e38 (X) and W/P = +/-3.4e38 (Y); `of`
    // (L16) reads Min -> first, Max -> second with those defaults, and only
    // the axis named by `Axis` fills its pair. Stored raw here; `fight.hpp`
    // maps Min/Max onto the X or Y bounds.
    struct Zone {
        std::string name;  // Node Name (`fv.name`)
        std::string axis;  // Axis attr ("X"/"Y"; anything else -> no fill)
        float min = -3.4028234663852886e38f;  // `of` Min default (`c`)
        float max = 3.4028234663852886e38f;   // `of` Max default (`d`)
    };
    std::vector<Zone> zones;
    // `<Level Min Max>` wrapper range (JS `bb.Ajb` L894 via `Zf(a,0,MAX)` —
    // a Min/Max ATTR read): the power range `[xFa,wFa]` the rule gates on
    // (`Lb.Ti`/`c_a` L846). No wrapper -> [0, INT_MAX] -> always true.
    int power_min = 0;
    int power_max = 2147483647;
    // JS `pn` (`ERuleRandom`, L879): when this leaf was nested under a
    // `<RandomRule>` wrapper, the wrapper's group id and the wrapper's DIRECT
    // child index it came from (`pn.Ae` order). `pn.M4` picks ONE choice per
    // group via `Da.pg.jf()` and only that choice's rules activate. `-1` =
    // not random-wrapped. `random_no_doubles` = the wrapper `NoDoubles`
    // (`pn.aVa` via `u.ka`) and `random_each_round` = `Refresh=="EachRound"`
    // (`pn.Zsa`; anything else -> 1 = per fight).
    int random_group = -1;
    int random_choice = -1;
    bool random_no_doubles = false;
    bool random_each_round = false;
};

// One <Reward> row.
struct StageReward {
    int money = 0, exp = 0, bonus = 0;
    int prize_base = 0;
};

struct StageFight {
    std::string name;
    int power = 0;
    int rounds = 2;
    int round_time = 99;
    int replays = 1;  // Replays= (entry repeat cap data, map-side)
    double health_recovery = 1.0;  // `qDa` (default 1)
    std::vector<StageReward> rewards;
    std::vector<StageWarrior> warriors;
    std::vector<StageRule> rules;
};

struct StageBattle {
    std::string name, type, location, music;
    std::vector<StageFight> fights;
};

struct TemplateDef {
    std::string name, base, first_name;
    std::vector<std::string> items;
};

struct GroupDef {
    std::string name;
    std::vector<StageWarrior> members;  // Template refs (+overrides)
};

// A resolved enemy (one wave / one tournament fight).
struct ResolvedWarrior {
    std::string template_name;
    std::string first_name;
    std::string tactic;
    std::map<std::string, std::string> attrs;
    std::vector<std::string> items;
    std::vector<std::string> perks;
};

// One resolved fight to run.
struct ModeFight {
    int rounds = 2;
    int round_time = 99;
    double health_recovery = 1.0;
    StageReward reward;  // selected row (participation vs win/wave)
    ResolvedWarrior enemy;
    std::vector<StageRule> rules;
    std::string location, music;
};

// Series cursor (`Rk` + wave/ng state).
struct ModeSeries {
    std::string battle_type;  // TOURNAMENT / SURVIVAL / ...
    int fight_index = 0;  // tournament fight (Rk analog)
    int wave = 0;  // survival wave
    std::vector<std::string> used_templates;  // NoDoubles memory
};

// ---- stages.xml parse --------------------------------------------------
namespace modes_detail {

inline int xml_int(const pugi::xml_node& n, const char* attr, int def) {
    const pugi::xml_attribute a = n.attribute(attr);
    if (!a) return def;
    try {
        return std::stoi(a.value());
    } catch (...) {
        return def;
    }
}

inline double xml_num(const pugi::xml_node& n, const char* attr, double def) {
    const pugi::xml_attribute a = n.attribute(attr);
    if (!a) return def;
    try {
        std::size_t pos = 0;
        const double d = std::stod(a.value(), &pos);
        if (pos == std::string(a.value()).size()) return d;
    } catch (...) {
    }
    return def;
}

// One `<AttributesAlign><Delta Factor Shift Priority Eclipse>` child -> a
// `StageWarrior::Delta` (JS `Ci` ctor L800 + the parse at L191). Shared by the
// warrior parse and the inherited-align resolver so the two cannot drift.
inline void collect_align(const pugi::xml_node& node,
                          std::vector<StageWarrior::Delta>& out) {
    if (!node) return;
    for (const pugi::xml_node d : node.children("Delta")) {
        StageWarrior::Delta dt;
        dt.factor = xml_num(d, "Factor", 0.0);
        dt.shift = xml_num(d, "Shift", 0.0);
        dt.priority = xml_int(d, "Priority", 0);
        // JS L191: `Eclipse==null ? OP=2 : (I(Eclipse)==0 ? 1 : 0)`.
        const pugi::xml_attribute ecl = d.attribute("Eclipse");
        dt.eclipse_op = !ecl ? 2 : (ecl.as_int() == 0 ? 1 : 0);
        out.push_back(dt);
    }
}

// The `<Template Name="Default">` align rows — the player/avatar `IY` (the
// only template in stages.xml that carries an `<AttributesAlign>`).
inline std::vector<StageWarrior::Delta> default_align(
    const pugi::xml_node& templates) {
    std::vector<StageWarrior::Delta> out;
    if (!templates) return out;
    for (const pugi::xml_node t : templates.children("Template")) {
        const char* nm = t.attribute("Name").value();
        if (nm != nullptr && std::string(nm) == "Default") {
            collect_align(t.child("AttributesAlign"), out);
            break;
        }
    }
    return out;
}

// JS `pGa` (L198) + `xc` parse (L191): the EFFECTIVE `<AttributesAlign>` rows
// of a stage warrior. `pGa` appends the derived node's `<AttributesAlign>`
// child to the base clone, so inherited rows come FIRST and the warrior's own
// rows last; the whole `Templates` section carries exactly ONE such block
// (`<Template Name="Default">`, the player/avatar base), so the resolved list
// is `Default`'s rows + the warrior's own. `Ci.a5a` (L800) then keeps only the
// max-`Priority` rows — which is how a per-warrior set overrides `Default`'s
// (e.g. Dojo_Disciple's two `Priority="1"` rows).
inline std::vector<StageWarrior::Delta> stage_warrior_align(
    const pugi::xml_node& warrior, const pugi::xml_node& templates) {
    std::vector<StageWarrior::Delta> out;
    if (!warrior) return out;
    // Only a warrior that inherits (has a `Template` attr) picks up the
    // `Default` rows; a standalone warrior (Punchbag) uses only its own.
    if (warrior.attribute("Template")) {
        out = default_align(templates);
    }
    collect_align(warrior.child("AttributesAlign"), out);
    return out;
}

inline StageWarrior parse_warrior(const pugi::xml_node& w) {
    StageWarrior out;
    for (const pugi::xml_attribute a : w.attributes()) {
        const std::string k = a.name();
        if (k == "Template") {
            out.template_name = a.value();
        } else if (k == "Tactic") {
            out.tactic = a.value();
        } else if (k == "Number") {
            try {
                out.number = std::max(1, std::stoi(a.value()));
            } catch (...) {
                out.number = 1;
            }
        } else {
            out.attrs[k] = a.value();
        }
    }
    const pugi::xml_node items = w.child("Items");
    if (items) {
        for (const pugi::xml_node i : items.children("Item")) {
            if (i.attribute("Name")) out.items.push_back(i.attribute("Name").value());
        }
    }
    const pugi::xml_node perks = w.child("Perks");
    if (perks) {
        for (const pugi::xml_node p : perks.children("Perk")) {
            if (p.attribute("Name")) out.perks.push_back(p.attribute("Name").value());
        }
    }
    const pugi::xml_node groups = w.child("Groups");
    if (groups) {
        for (const pugi::xml_node g : groups.children("Group")) {
            StageWarrior::GroupRef r;
            if (g.attribute("Name")) r.name = g.attribute("Name").value();
            r.random = g.attribute("Random") && std::string(g.attribute("Random").value()) == "1";
            r.no_doubles =
                g.attribute("NoDoubles") && std::string(g.attribute("NoDoubles").value()) == "1";
            out.groups.push_back(std::move(r));
        }
    }
    collect_align(w.child("AttributesAlign"), out.align);
    return out;
}

// JS `Ce.c4a` (L848) + `en.Mia` (L860): the child elements a rule consumes.
// `c4a` reads `<Animation Name>` direct children into the `EM` list; `Mia`
// reads `<Node Name Axis Min Max>` direct children into the `Va` zone list
// (`hp(name)` = matching direct children, same as `children(name)`). Shared
// by `parse_stages` and the battle rule feeder (`app::battle_fight_rules`)
// so the two parsers cannot drift.
inline void parse_rule_children(const pugi::xml_node& el, StageRule& rule) {
    for (const pugi::xml_node an : el.children("Animation")) {
        if (an.attribute("Name"))
            rule.animations.push_back(an.attribute("Name").value());
    }
    for (const pugi::xml_node zn : el.children("Node")) {
        StageRule::Zone z;
        if (zn.attribute("Name")) z.name = zn.attribute("Name").value();
        if (zn.attribute("Axis")) z.axis = zn.attribute("Axis").value();
        z.min = static_cast<float>(xml_num(zn, "Min", z.min));
        z.max = static_cast<float>(xml_num(zn, "Max", z.max));
        rule.zones.push_back(std::move(z));
    }
}

// JS `bb.OE`/`bb.M3` (L887-894) + `nh.parse` (L853) + `pn.parse` (L879):
// expand one `<Rules>` child into the flat StageRule list. Every node-name
// resolution walks DIRECT children only (JS element `hp(a)`: `e.name==a`
// over `this.children`) — never a descendant/global search, so a wrapper's
// own tag (`ComplexRule`/`RandomRule`) cannot shadow the effect rules
// nested inside it.
//   - `<Level Min Max>` (`bb.Ajb` L894: `Zf(a,0,MAX)` + `bb.M3` per child):
//     stamp the range on the subtree.
//   - `<ComplexRule>` (`nh.parse` L853: `a=a.children` then `bb.M3`):
//     flatten its direct children into the parent, same group/choice (the
//     whole ComplexRule is one random choice; `nh.jh` returns `Ae`).
//   - `<RandomRule>` (`pn.parse` L879: `bb.OE(a,this.Ae)`): flatten its
//     direct children as CHOICES of a new group; `NoDoubles`/`Refresh`
//     (`pn.aVa`/`pn.Zsa`) come from the wrapper attrs. The actual pick is
//     deferred to `FightController::rules_begin_round` (`pn.M4` ->
//     `Da.pg.jf()`), because only ONE choice may activate.
//   - anything else: one leaf StageRule (tag + attrs + the `Ce.c4a` L848 /
//     `en.Mia` L860 direct children via `parse_rule_children`).
inline void append_rule_element(const pugi::xml_node& el,
                                std::vector<StageRule>& out, int power_lo,
                                int power_hi, int& next_group, int random_group,
                                int random_choice, bool no_doubles,
                                bool each_round) {
    const std::string tag = el.name();
    if (tag == "Level") {
        // `bb.Ajb` (L894): Min/Max attrs -> the subtree's power range.
        const int lo = xml_int(el, "Min", 0);
        const int hi = xml_int(el, "Max", 2147483647);
        for (const pugi::xml_node c : el.children())
            append_rule_element(c, out, lo, hi, next_group, random_group,
                                random_choice, no_doubles, each_round);
        return;
    }
    if (tag == "ComplexRule") {
        // `nh.parse` (L853): direct children (`a.children`); the ComplexRule
        // is a single unit, so every leaf keeps the parent group/choice.
        for (const pugi::xml_node c : el.children())
            append_rule_element(c, out, power_lo, power_hi, next_group,
                                random_group, random_choice, no_doubles,
                                each_round);
        return;
    }
    if (tag == "RandomRule") {
        // `pn.parse` (L879): `bb.OE` fills `Ae` with the direct children
        // (each is one choice). `pn.M4` draws `Da.pg.jf()` at activation.
        const int gid = next_group++;
        const pugi::xml_attribute nd = el.attribute("NoDoubles");
        const bool no_dbl = nd && std::string(nd.value()) == "1";  // `u.ka`
        const pugi::xml_attribute rf = el.attribute("Refresh");
        const bool each = rf && std::string(rf.value()) == "EachRound";  // `Zsa`
        int choice = 0;
        for (const pugi::xml_node c : el.children())
            append_rule_element(c, out, power_lo, power_hi, next_group, gid,
                                choice++, no_dbl, each);
        return;
    }
    StageRule rule;
    rule.tag = tag;
    for (const pugi::xml_attribute a : el.attributes())
        rule.attrs[a.name()] = a.value();
    // `Ce.c4a` (L848) + `en.Mia` (L860): direct `<Animation>`/`<Node>`.
    parse_rule_children(el, rule);
    rule.power_min = power_lo;
    rule.power_max = power_hi;
    rule.random_group = random_group;
    rule.random_choice = random_choice;
    rule.random_no_doubles = no_doubles;
    rule.random_each_round = each_round;
    out.push_back(std::move(rule));
}

}  // namespace modes_detail

// Parses the stages document into battles + templates + groups. Returns
// false on malformed XML (empty out).
inline bool parse_stages(const std::string& xml_text, std::vector<StageBattle>& battles,
                         std::map<std::string, TemplateDef>& templates,
                         std::map<std::string, GroupDef>& groups) {
    using namespace modes_detail;
    battles.clear();
    templates.clear();
    groups.clear();
    pugi::xml_document doc;
    if (!doc.load_string(xml_text.c_str())) return false;
    const pugi::xml_node root = doc.root().first_child();
    const pugi::xml_node zones = root.child("Zones");
    if (zones) {
        for (const pugi::xml_node z : zones.children()) {
            for (const pugi::xml_node b : z.children("Battle")) {
                StageBattle battle;
                if (b.attribute("Name")) battle.name = b.attribute("Name").value();
                if (b.attribute("Type")) battle.type = b.attribute("Type").value();
                if (b.attribute("Location")) battle.location = b.attribute("Location").value();
                if (b.attribute("Music")) battle.music = b.attribute("Music").value();
                for (const pugi::xml_node f : b.children("Fight")) {
                    StageFight fight;
                    if (f.attribute("Name")) fight.name = f.attribute("Name").value();
                    fight.power = xml_int(f, "Power", 0);
                    fight.rounds = xml_int(f, "Rounds", 2);
                    fight.round_time = xml_int(f, "RoundTime", 99);
                    fight.replays = xml_int(f, "Replays", 1);
                    fight.health_recovery = xml_num(f, "HealthRecovery", 1.0);
                    const pugi::xml_node rewards = f.child("Rewards");
                    if (rewards) {
                        for (const pugi::xml_node r : rewards.children("Reward")) {
                            StageReward rw;
                            rw.money = xml_int(r, "Money", 0);
                            rw.exp = xml_int(r, "Exp", 0);
                            rw.bonus = xml_int(r, "Bonus", 0);
                            rw.prize_base = xml_int(r, "PrizeBase", 0);
                            fight.rewards.push_back(rw);
                        }
                    }
                    const pugi::xml_node warriors = f.child("Warriors");
                    if (warriors) {
                        for (const pugi::xml_node w : warriors.children("Warrior")) {
                            fight.warriors.push_back(parse_warrior(w));
                        }
                    }
                    const pugi::xml_node rules = f.child("Rules");
                    if (rules) {
                        // JS `bb.OE` (L887-888): walk the DIRECT `<Rules>`
                        // children (`hp` semantics) and expand `<Level>`
                        // (`bb.Ajb` L894), `<ComplexRule>` (`nh.parse` L853)
                        // and `<RandomRule>` (`pn.parse` L879) into flat
                        // StageRules. `next_group` numbers the RandomRule
                        // wrappers (`pn` groups) for the activation pick.
                        int next_group = 0;
                        for (const pugi::xml_node r : rules.children())
                            append_rule_element(r, fight.rules, 0, 2147483647,
                                                next_group, -1, -1, false,
                                                false);
                    }
                    battle.fights.push_back(std::move(fight));
                }
                battles.push_back(std::move(battle));
            }
        }
    }
    const pugi::xml_node wsec = root.child("Warriors");
    if (wsec) {
        const pugi::xml_node tsec = wsec.child("Templates");
        if (tsec) {
            for (const pugi::xml_node t : tsec.children("Template")) {
                TemplateDef def;
                if (t.attribute("Name")) def.name = t.attribute("Name").value();
                if (t.attribute("Template")) def.base = t.attribute("Template").value();
                if (t.attribute("FirstName")) def.first_name = t.attribute("FirstName").value();
                const pugi::xml_node items = t.child("Items");
                if (items) {
                    for (const pugi::xml_node i : items.children("Item")) {
                        if (i.attribute("Name")) def.items.push_back(i.attribute("Name").value());
                    }
                }
                if (!def.name.empty()) templates[def.name] = std::move(def);
            }
        }
        const pugi::xml_node gsec = wsec.child("WarriorGroups");
        if (gsec) {
            for (const pugi::xml_node g : gsec.children("WarriorGroup")) {
                GroupDef def;
                if (g.attribute("Name")) def.name = g.attribute("Name").value();
                for (const pugi::xml_node w : g.children("Warrior")) {
                    def.members.push_back(parse_warrior(w));
                }
                if (!def.name.empty()) groups[def.name] = std::move(def);
            }
        }
    }
    return true;
}

// Template chain items (base-first, override appends; dedup by Name —
// the `ur` stat-merge detail is OPEN).
inline std::vector<std::string> template_items(
    const std::string& name, const std::map<std::string, TemplateDef>& templates) {
    std::vector<std::string> out;
    std::string cur = name;
    std::vector<std::string> chain;
    while (!cur.empty()) {
        const auto it = templates.find(cur);
        if (it == templates.end()) break;
        chain.push_back(cur);
        cur = it->second.base;
        if (chain.size() > 8) break;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (const std::string& item : templates.at(*it).items) {
            bool dup = false;
            for (const std::string& have : out) {
                if (have == item) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out.push_back(item);
        }
    }
    return out;
}

inline std::string template_first_name(
    const std::string& name, const std::map<std::string, TemplateDef>& templates) {
    std::string cur = name, best;
    int depth = 0;
    while (!cur.empty() && depth++ < 8) {
        const auto it = templates.find(cur);
        if (it == templates.end()) break;
        if (!it->second.first_name.empty() && best.empty()) best = it->second.first_name;
        cur = it->second.base;
    }
    return best;
}

// `z6a`: total survival waves = sum of Warrior Number over Xs.
inline int survival_waves(const StageFight& fight) {
    int total = 0;
    for (const StageWarrior& w : fight.warriors) total += w.number;
    return total;
}

// Resolve one survival wave warrior (`efa` at the name level): walk the
// Number counts, then pick from the wave's Groups (Random via draw01 with
// NoDoubles memory, else sequential Ix[f%len]). Returns false when the
// wave is out of range.
inline bool resolve_survival_warrior(
    const StageFight& fight, int wave,
    const std::map<std::string, TemplateDef>& templates,
    const std::map<std::string, GroupDef>& groups, std::function<double()> draw01,
    std::vector<std::string>& used_templates, ResolvedWarrior& out) {
    int w = wave;
    const StageWarrior* slot = nullptr;
    for (const StageWarrior& cand : fight.warriors) {
        if (w < cand.number) {
            slot = &cand;
            break;
        }
        w -= cand.number;
    }
    if (slot == nullptr) return false;
    out = ResolvedWarrior();
    out.tactic = slot->tactic;
    out.attrs = slot->attrs;
    out.perks = slot->perks;
    if (slot->groups.empty()) {
        out.template_name = slot->template_name;
    } else {
        // `efa` group walk: first group with a resolvable pool wins (the
        // multi-group merge `ur`/`$Wa` is OPEN — single pick implemented).
        for (const StageWarrior::GroupRef& g : slot->groups) {
            const auto it = groups.find(g.name);
            if (it == groups.end() || it->second.members.empty()) continue;
            const std::vector<StageWarrior>& pool = it->second.members;
            std::size_t pick = 0;
            if (g.random && draw01) {
                const double r = draw01();
                pick = static_cast<std::size_t>(r * static_cast<double>(pool.size())) %
                       pool.size();
                if (g.no_doubles) {
                    for (std::size_t tries = 0; tries < pool.size(); ++tries) {
                        const std::string& nm = pool[pick].template_name;
                        bool used = false;
                        for (const std::string& u : used_templates) {
                            if (u == nm) {
                                used = true;
                                break;
                            }
                        }
                        if (!used) break;
                        pick = (pick + 1) % pool.size();
                    }
                }
            } else {
                pick = static_cast<std::size_t>(w) % pool.size();
            }
            const StageWarrior& m = pool[pick];
            out.template_name = m.template_name.empty() ? slot->template_name : m.template_name;
            if (!m.tactic.empty()) out.tactic = m.tactic;
            for (const auto& kv : m.attrs) out.attrs[kv.first] = kv.second;
            if (!m.items.empty()) out.items = m.items;
            if (!m.perks.empty()) out.perks = m.perks;
            break;
        }
    }
    if (!out.template_name.empty()) {
        out.items = template_items(out.template_name, templates);
        // Fight-warrior <Items> override the template list when present.
        if (!slot->items.empty()) out.items = slot->items;
        out.first_name = template_first_name(out.template_name, templates);
    } else {
        out.items = slot->items;
    }
    used_templates.push_back(out.template_name);
    return true;
}

// Resolve one tournament fight (warriors expand trivially — no groups on
// tournament rows; first warrior is the enemy).
inline bool resolve_tournament_fight(const StageBattle& battle, int fight_index,
                                     const std::map<std::string, TemplateDef>& templates,
                                     const std::map<std::string, GroupDef>& groups,
                                     ModeFight& out) {
    (void)groups;
    if (fight_index < 0 ||
        fight_index >= static_cast<int>(battle.fights.size())) {
        return false;
    }
    const StageFight& f = battle.fights[fight_index];
    out = ModeFight();
    out.rounds = f.rounds;
    out.round_time = f.round_time;
    out.health_recovery = f.health_recovery;
    out.rules = f.rules;
    out.location = battle.location;
    out.music = battle.music;
    if (!f.rewards.empty()) {
        out.reward = f.rewards.size() > 1 ? f.rewards[1] : f.rewards[0];
    }
    if (!f.warriors.empty()) {
        const StageWarrior& w = f.warriors[0];
        out.enemy.template_name = w.template_name;
        out.enemy.tactic = w.tactic;
        out.enemy.attrs = w.attrs;
        out.enemy.perks = w.perks;
        if (!w.template_name.empty()) {
            out.enemy.items = template_items(w.template_name, templates);
            out.enemy.first_name = template_first_name(w.template_name, templates);
        }
        if (!w.items.empty()) out.enemy.items = w.items;
    }
    return true;
}

// Reward row select: tournament participation row [0] vs win row [1];
// survival per-wave row (clamped).
inline StageReward reward_for(const std::string& battle_type, const StageFight& fight, int wave,
                              bool won) {
    if (fight.rewards.empty()) return StageReward();
    if (battle_type == "SURVIVAL") {
        std::size_t i = static_cast<std::size_t>(wave < 0 ? 0 : wave);
        if (i >= fight.rewards.size()) i = fight.rewards.size() - 1;
        return fight.rewards[i];
    }
    if (won && fight.rewards.size() > 1) return fight.rewards[1];
    return fight.rewards[0];
}

// Series advance (`Da.sR` shape): tournament next fight while fights
// remain (ng>=eL modeled by the caller via `won`); survival next wave
// while wave < z6a sum. Returns false at series end.
inline bool advance_series(const StageBattle& battle, ModeSeries& s, bool won) {
    if (battle.type == "SURVIVAL") {
        if (battle.fights.empty()) return false;
        const int total = survival_waves(battle.fights[0]);
        if (s.wave + 1 < total) {
            ++s.wave;
            return true;
        }
        return false;
    }
    if (won && s.fight_index + 1 < static_cast<int>(battle.fights.size())) {
        ++s.fight_index;
        return true;
    }
    return false;
}

// Enemy config for fight init (resolved from a ResolvedWarrior).
struct ModeEnemy {
    std::string tactic;  // "" = keep the init tactic
    // Locks items for the enemy move list (type, subtype) pairs.
    std::vector<std::pair<std::string, std::string>> owned;
    std::map<std::string, double> attrs;  // numeric stat overrides
    std::vector<std::string> perk_names;  // Warrior <Perks> (enemy_refs)
};

// Whole-fight setup applied post-init (`FightController::apply_mode_setup`).
struct ModeSetup {
    int rounds = 2;
    int round_time = 99;
    double health_recovery = 1.0;
    StageReward reward;
    ModeEnemy enemy;
    double player_damage_factor = 0.0;  // Rules, ApplyTo=Player
    double enemy_damage_factor = 0.0;  // Rules, ApplyTo=Bot
    bool no_bullets = false;  // NoBulletsReplenishment (ApplyTo=Player)
};

// Build a ModeSetup from a resolved ModeFight. `enemy_owned` (type,
// subtype) pairs come from the app layer (item catalog); numeric attrs
// parse from the warrior strings (non-numeric skipped). Rules mapping:
// `<Attributes DamageFactor ApplyTo=Player/Bot>` → side adds;
// `<NoBulletsReplenishment ApplyTo=Player>` → flag; Eclipse → noted
// OPEN (no Cea setter found; bp stays 1.0).
inline ModeSetup mode_setup_from_fight(
    const ModeFight& mf,
    const std::vector<std::pair<std::string, std::string>>& enemy_owned) {
    ModeSetup out;
    out.rounds = mf.rounds;
    out.round_time = mf.round_time;
    out.health_recovery = mf.health_recovery;
    out.reward = mf.reward;
    out.enemy.tactic = mf.enemy.tactic;
    out.enemy.owned = enemy_owned;
    for (const auto& kv : mf.enemy.attrs) {
        try {
            std::size_t pos = 0;
            const double d = std::stod(kv.second, &pos);
            if (pos == kv.second.size()) out.enemy.attrs[kv.first] = d;
        } catch (...) {
        }
    }
    out.enemy.perk_names = mf.enemy.perks;
    for (const StageRule& r : mf.rules) {
        if (r.tag == "Attributes") {
            const auto di = r.attrs.find("DamageFactor");
            const auto ai = r.attrs.find("ApplyTo");
            if (di != r.attrs.end() && ai != r.attrs.end()) {
                try {
                    const double d = std::stod(di->second);
                    if (ai->second == "Player") {
                        out.player_damage_factor += d;
                    } else if (ai->second == "Bot") {
                        out.enemy_damage_factor += d;
                    }
                } catch (...) {
                }
            }
        } else if (r.tag == "NoBulletsReplenishment") {
            const auto ai = r.attrs.find("ApplyTo");
            if (ai != r.attrs.end() && ai->second == "Player") out.no_bullets = true;
        }
    }
    return out;
}

}  // namespace sf2::scene
