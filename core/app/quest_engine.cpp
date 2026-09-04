// Quest engine core — see quest_engine.hpp for the spec/notes.
//
// Data paths (existing patterns, read-only): tutorial chain at
// `reference/extracted/xml/res/quest_extensions/tutorial_quests.xml` (like
// list.xml in screens.cpp); battle→zone index from
// `reference/extracted/xml/res/stages.xml` (like load_zone_map).

#include "app/quest_engine.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

#include "app/app.hpp"
#include "app/save_system.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr int kMaxActivateDepth = 4;
constexpr int kMaxActionDepth = 6;

int parse_int_or(const std::string& s, int fallback) {
    try {
        std::size_t pos = 0;
        const int v = std::stoi(s, &pos);
        if (pos != s.size()) return fallback;
        return v;
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string read_file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Parses one <Conditions> element into cond (AND of Equal/Operator kids).
void parse_conds(const pugi::xml_node& node, QuestCond& cond) {
    cond.kind = "And";
    for (pugi::xml_node ch = node.first_child(); ch; ch = ch.next_sibling()) {
        const std::string tag = ch.name();
        if (tag == "Equal" || tag == "GreaterEqual") {
            QuestCond leaf;
            leaf.kind = tag;
            leaf.value1 = ch.attribute("Value1").value();
            leaf.value2 = ch.attribute("Value2").value();
            leaf.invert = std::string(ch.attribute("Not").value()) == "1";
            cond.children.push_back(std::move(leaf));
        } else if (tag == "Operator") {
            QuestCond op;
            const std::string type = ch.attribute("Type").value();
            op.kind = (type == "Or") ? "Or" : "And";
            op.invert = std::string(ch.attribute("Not").value()) == "1";
            for (pugi::xml_node g = ch.first_child(); g; g = g.next_sibling()) {
                if (g.type() != pugi::node_element) continue;
                const std::string gtag = g.name();
                if (gtag == "Equal" || gtag == "GreaterEqual") {
                    QuestCond leaf;
                    leaf.kind = gtag;
                    leaf.value1 = g.attribute("Value1").value();
                    leaf.value2 = g.attribute("Value2").value();
                    leaf.invert = std::string(g.attribute("Not").value()) == "1";
                    op.children.push_back(std::move(leaf));
                } else if (gtag == "Operator") {
                    QuestCond sub;
                    const std::string st = g.attribute("Type").value();
                    sub.kind = (st == "Or") ? "Or" : "And";
                    sub.invert = std::string(g.attribute("Not").value()) == "1";
                    parse_conds(g, sub);
                    QuestCond fixed;
                    fixed.kind = sub.kind;
                    fixed.invert = sub.invert;
                    fixed.children = std::move(sub.children);
                    op.children.push_back(std::move(fixed));
                }
            }
            cond.children.push_back(std::move(op));
        }
    }
}

// Parses one action element (If/Dialog keep structured children).
void parse_action(const pugi::xml_node& node, QuestAction& act) {
    act.tag = node.name();
    for (pugi::xml_attribute a = node.first_attribute(); a; a = a.next_attribute()) {
        act.attrs[a.name()] = a.value();
    }
    if (act.tag == "If") {
        for (pugi::xml_node ch = node.first_child(); ch; ch = ch.next_sibling()) {
            const std::string tag = ch.name();
            if (tag == "Conditions") {
                parse_conds(ch, act.if_cond);
            } else if (tag == "Then" || tag == "Else") {
                for (pugi::xml_node g = ch.first_child(); g; g = g.next_sibling()) {
                    if (g.type() != pugi::node_element) continue;
                    QuestAction sub;
                    parse_action(g, sub);
                    if (tag == "Then") act.if_then.push_back(std::move(sub));
                    else act.if_else.push_back(std::move(sub));
                }
            }
        }
    } else if (act.tag == "Dialog") {
        for (pugi::xml_node ch = node.first_child(); ch; ch = ch.next_sibling()) {
            if (ch.type() != pugi::node_element) continue;
            QuestAction sub;
            parse_action(ch, sub);
            act.children.push_back(std::move(sub));
        }
    }
}

std::string attr_or(const std::map<std::string, std::string>& attrs, const char* key,
                    const char* fallback = "") {
    const auto it = attrs.find(key);
    return it != attrs.end() ? it->second : fallback;
}

} // namespace

bool QuestEngine::ensure_loaded(App& app) {
    if (loaded_) return true;
    // Battle→zone index (stages.xml Zone/Battle names).
    try {
        const std::string xml = read_file_text("reference/extracted/xml/res/stages.xml");
        if (!xml.empty()) {
            sf2::data::xml_doc doc;
            doc.parse(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
            const pugi::xml_node root = doc.root().first_child();
            if (root && std::string(root.name()) == "Stages") {
                for (pugi::xml_node zone : root.child("Zones").children("Zone")) {
                    const std::string zname = zone.attribute("Name").value();
                    if (zname.empty()) continue;
                    for (pugi::xml_node b : zone.children("Battle")) {
                        const std::string bname = b.attribute("Name").value();
                        if (!bname.empty() && battle_zone_.find(bname) == battle_zone_.end()) {
                            battle_zone_[bname] = zname;
                        }
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }
    // Tutorial chain (Sjb-equivalent: included while step != END).
    try {
        std::string step;
        try {
            step = app.save().load().story_step();
        } catch (const std::exception&) {
        }
        if (step == "END") {
            loaded_ = true;
            return true;
        }
        const std::string xml = read_file_text(
            "reference/extracted/xml/res/quest_extensions/tutorial_quests.xml");
        if (xml.empty()) return false;
        sf2::data::xml_doc doc;
        doc.parse(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root || std::string(root.name()) != "Quests") return false;
        for (pugi::xml_node q = root.child("Quest"); q; q = q.next_sibling("Quest")) {
            QuestDef def;
            def.name = q.attribute("Name").value();
            if (def.name.empty()) continue;
            def.priority = parse_int_or(q.attribute("Priority").value(), 0);
            def.unresumable =
                std::string(q.attribute("Unresumable").value()) == "1";
            const pugi::xml_node events = q.child("Events");
            if (events) {
                for (pugi::xml_node e = events.first_child(); e; e = e.next_sibling()) {
                    if (e.type() != pugi::node_element) continue;
                    def.events.push_back(e.name());
                }
            }
            const pugi::xml_node conds = q.child("Conditions");
            if (conds) parse_conds(conds, def.root);
            const pugi::xml_node acts = q.child("Actions");
            if (acts) {
                for (pugi::xml_node a = acts.first_child(); a; a = a.next_sibling()) {
                    if (a.type() != pugi::node_element) continue;
                    QuestAction act;
                    parse_action(a, act);
                    def.actions.push_back(std::move(act));
                }
            }
            quests_.push_back(std::move(def));
        }
        loaded_ = true;
        std::fprintf(stdout, "[quest] engine loaded: %zu quests, %zu battle zones\n",
                     quests_.size(), battle_zone_.size());
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[quest] engine load failed: %s\n", e.what());
        return false;
    }
    return true;
}

std::string QuestEngine::battle_zone(const std::string& battle) const {
    // "Zone|Battle|n" form first (JS Fight names), else the stages index.
    const std::size_t bar = battle.find('|');
    if (bar != std::string::npos && bar > 0) return battle.substr(0, bar);
    const auto it = battle_zone_.find(battle);
    return it != battle_zone_.end() ? it->second : std::string();
}

std::string QuestEngine::resolve_token(const std::string& token,
                                       const QuestJournal& journal,
                                       const std::string& story_step, int level) const {
    if (token == "_$StoryTutorialStep") return story_step;
    if (token == "_$SceneTo") return journal.scene_to;
    if (token == "_$SceneFrom") return journal.scene_from;
    if (token == "_$Fight") return journal.fight;
    if (token == "_$FightResult") return journal.fight_result;
    if (token == "_$ActionID") return journal.action_id;
    if (token == "?Fight[_$Fight].Zone") return journal.fight_zone;
    if (token == "?Player[].Level") return std::to_string(level);
    if (token == "?SysInfo[].Switch" || token == "?SysInfo[].Steam" ||
        token == "?SysInfo[].Paid" || token == "?SysInfo[].AnyF2P") {
        return "0";  // desktop shell: no Switch/Steam/paid flags
    }
    if (token == "?Purchase[WEAPON_KNIVES].Type") return "Weapon";
    // `?Concat[...]`/unknown queries + plain literals pass through (literals
    // compare verbatim; unresolved queries never equal a bare literal).
    return token;
}

bool QuestEngine::conditions_hold(const QuestCond& cond, const QuestJournal& journal,
                                  const std::string& story_step, int level) const {
    if (cond.kind == "Equal" || cond.kind == "GreaterEqual") {
        const std::string a = resolve_token(cond.value1, journal, story_step, level);
        const std::string b = resolve_token(cond.value2, journal, story_step, level);
        bool ok = false;
        if (cond.kind == "GreaterEqual") {
            ok = parse_int_or(a, -1) >= parse_int_or(b, 0);
        } else {
            ok = (a == b);
        }
        return cond.invert ? !ok : ok;
    }
    // And (default) / Or over children.
    if (cond.kind == "Or") {
        bool ok = false;
        for (const QuestCond& c : cond.children) {
            if (conditions_hold(c, journal, story_step, level)) {
                ok = true;
                break;
            }
        }
        return cond.invert ? !ok : ok;
    }
    for (const QuestCond& c : cond.children) {
        if (!conditions_hold(c, journal, story_step, level)) {
            return cond.invert ? true : false;
        }
    }
    return cond.invert ? false : true;
}

void QuestEngine::run_actions(App& app, const std::vector<QuestAction>& acts,
                              const QuestJournal& journal, QuestSideEffects& fx,
                              int depth) {
    if (depth > kMaxActionDepth) return;
    (void)app;
    for (const QuestAction& a : acts) {
        const std::string& t = a.tag;
        if (t == "If") {
            // If needs the live step/level for its Conditions.
            std::string step;
            int level = journal.player_level;
            try {
                const WarriorSave w = app.save().load();
                step = w.story_step();
                level = w.level;
            } catch (const std::exception&) {
            }
            const bool take = conditions_hold(a.if_cond, journal, step, level);
            run_actions(app, take ? a.if_then : a.if_else, journal, fx, depth + 1);
        } else if (t == "ChangeScene") {
            std::string dst = attr_or(a.attrs, "Destination");
            if (dst == "_$SceneTo") dst = journal.scene_to;
            fx.scene_requests.push_back(dst);
        } else if (t == "Fight") {
            fx.fight_requests.push_back(attr_or(a.attrs, "Name"));
        } else if (t == "FightEnd") {
            fx.unknown.push_back("FightEnd (needs ca.Ka().kD scene hook)");
        } else if (t == "OpenShop") {
            std::string tab = attr_or(a.attrs, "Tab");
            if (tab == "?Purchase[WEAPON_KNIVES].Type") tab = "Weapon";
            fx.scene_requests.push_back("Shop:" + tab + ":" + attr_or(a.attrs, "Item"));
        } else if (t == "Dialog") {
            std::string lines;
            for (const QuestAction& c : a.children) {
                if (c.tag == "Line") {
                    if (!lines.empty()) lines += " | ";
                    lines += attr_or(c.attrs, "Text");
                }
            }
            fx.dialogs.push_back(attr_or(a.attrs, "Type") + ":" +
                                 attr_or(a.attrs, "Title") + ": " + lines);
            // Nested Button/Line/If actions (SetStoryTutorialStep + Fight
            // live inside Welcome's dialog Button) — same allowlist.
            run_actions(app, a.children, journal, fx, depth + 1);
        } else if (t == "SetStoryTutorialStep") {
            fx.has_story_step = true;
            fx.story_step = attr_or(a.attrs, "Value");
        } else if (t == "SetMapFocus") {
            fx.has_map_focus = true;
            fx.map_focus = attr_or(a.attrs, "Battle");
        } else if (t == "SetCurrentZone") {
            fx.has_current_zone = true;
            const std::string name = attr_or(a.attrs, "Name");
            fx.current_zone = name.empty() ? attr_or(a.attrs, "Value") : name;
        } else if (t == "SetVariable") {
            if (attr_or(a.attrs, "Scope") == "Global") {
                fx.set_vars[attr_or(a.attrs, "Name")] = attr_or(a.attrs, "Value");
            }
            // Local/NotificationText vars feed Dialog lines only — recorded
            // via the Dialog summary above.
        } else if (t == "ClickButton") {
            fx.clicks.push_back(attr_or(a.attrs, "Target"));
        } else if (t == "ClearQuestQueue") {
            fx.clears.push_back(attr_or(a.attrs, "Name"));
        } else if (t == "Activate") {
            // Chained Activate (StoryTutorialOpenScene): fired by fire().
            fx.unknown.push_back("Activate:" + attr_or(a.attrs, "ActionID"));
        } else if (t == "Wait") {
            // Collapsed (synchronous runs) — recorded for traceability.
            fx.unknown.push_back("Wait:" + attr_or(a.attrs, "Frames") + "f");
        } else if (t == "StoryTutorialMove" || t == "StoryTutorialPunchbag" ||
                   t == "StoryTutorialBuyItem" || t == "StoryTutorialLearnPerk" ||
                   t == "StoryTutorialDoubleSweep" || t == "StoryTutorialShowBlock") {
            fx.minigames.push_back(t + " (needs fight hooks)");
        } else if (t == "Line" || t == "Button" || t == "Then" || t == "Else" ||
                   t == "Conditions") {
            run_actions(app, a.children, journal, fx, depth + 1);
        } else {
            fx.unknown.push_back(t);
        }
    }
}

void QuestEngine::apply_effects(App& app, const QuestSideEffects& fx) {
    try {
        WarriorSave w = app.save().load();
        bool dirty = false;
        if (fx.has_story_step && w.story_step() != fx.story_step) {
            w.set_story_step(fx.story_step);
            dirty = true;
        }
        if (fx.has_map_focus && w.map_focus != fx.map_focus) {
            w.map_focus = fx.map_focus;
            dirty = true;
        }
        if (fx.has_current_zone && w.current_zone != fx.current_zone) {
            w.current_zone = fx.current_zone;
            dirty = true;
        }
        for (const auto& kv : fx.set_vars) {
            if (kv.first.empty()) continue;
            auto it = w.variables.find(kv.first);
            if (it == w.variables.end() || it->second != kv.second) {
                w.variables[kv.first] = kv.second;
                dirty = true;
            }
        }
        if (dirty) {
            app.save().save(w);
            std::fprintf(stdout,
                         "[quest] save applied (step=%s focus=%s zone=%s vars=%zu)\n",
                         fx.has_story_step ? fx.story_step.c_str() : "-",
                         fx.has_map_focus ? fx.map_focus.c_str() : "-",
                         fx.has_current_zone ? fx.current_zone.c_str() : "-",
                         fx.set_vars.size());
            std::fflush(stdout);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[quest] save apply failed: %s\n", e.what());
    }
}

void QuestEngine::fire_inner(App& app, const std::string& event,
                             const QuestJournal& journal,
                             std::vector<std::string>& fired, int depth) {
    if (depth > kMaxActivateDepth) return;
    std::string step;
    int level = journal.player_level;
    try {
        const WarriorSave w = app.save().load();
        step = w.story_step();
        level = w.level;
    } catch (const std::exception&) {
    }
    for (const QuestDef& q : quests_) {
        bool listens = false;
        for (const std::string& e : q.events) {
            if (e == event) {
                listens = true;
                break;
            }
        }
        if (!listens) continue;
        if (q.unresumable) {
            bool seen = false;
            for (const std::string& f : fired_) {
                if (f == q.name) {
                    seen = true;
                    break;
                }
            }
            if (seen) continue;
        }
        if (!conditions_hold(q.root, journal, step, level)) continue;
        QuestSideEffects fx;
        run_actions(app, q.actions, journal, fx, 0);
        apply_effects(app, fx);
        if (q.unresumable) fired_.push_back(q.name);
        fired.push_back(q.name);
        std::fprintf(stdout, "[quest] FIRED %s on %s (step=%s scene=%s->%s)\n", q.name.c_str(),
                     event.c_str(), step.c_str(), journal.scene_from.c_str(),
                     journal.scene_to.c_str());
        if (!fx.dialogs.empty()) {
            for (const std::string& d : fx.dialogs) {
                std::fprintf(stdout, "[quest]   dialog: %s\n", d.c_str());
            }
        }
        if (!fx.scene_requests.empty()) {
            for (const std::string& s : fx.scene_requests) {
                std::fprintf(stdout, "[quest]   scene request (record only): %s\n", s.c_str());
            }
        }
        if (!fx.fight_requests.empty()) {
            for (const std::string& s : fx.fight_requests) {
                std::fprintf(stdout, "[quest]   fight request (record only): %s\n", s.c_str());
            }
        }
        if (!fx.minigames.empty()) {
            for (const std::string& s : fx.minigames) {
                std::fprintf(stdout, "[quest]   minigame (record only): %s\n", s.c_str());
            }
        }
        if (!fx.clicks.empty()) {
            for (const std::string& s : fx.clicks) {
                std::fprintf(stdout, "[quest]   click (record only): %s\n", s.c_str());
            }
        }
        std::fflush(stdout);
        // Chained Activate (StoryTutorialOpenScene): re-fire synchronously.
        for (const std::string& u : fx.unknown) {
            if (u.rfind("Activate:", 0) == 0) {
                QuestJournal j2 = journal;
                j2.action_id = u.substr(9);
                fire_inner(app, "Activate", j2, fired, depth + 1);
            }
        }
        // Queue clears (Mn): latch the named quest as done.
        for (const std::string& c : fx.clears) {
            bool seen = false;
            for (const std::string& f : fired_) {
                if (f == c) {
                    seen = true;
                    break;
                }
            }
            if (!seen) fired_.push_back(c);
        }
        // Refresh the step for later quests in this same firing.
        try {
            step = app.save().load().story_step();
        } catch (const std::exception&) {
        }
    }
}

void QuestEngine::note_fight(const std::string& name, const std::string& result) {
    last_fight_ = name;
    last_result_ = result;
}

std::vector<std::string> QuestEngine::fire(App& app, const std::string& event,
                                           const QuestJournal& journal) {
    std::vector<std::string> fired;
    if (!ensure_loaded(app)) return fired;
    QuestJournal j = journal;
    if (j.fight.empty()) {
        j.fight = last_fight_;
        j.fight_result = last_result_;
    }
    if (j.fight_zone.empty() && !j.fight.empty()) j.fight_zone = battle_zone(j.fight);
    if (j.player_level <= 0) j.player_level = 1;
    fire_inner(app, event, j, fired, 0);
    return fired;
}

QuestEngine& App::quest_engine() {
    if (!quest_engine_) quest_engine_ = std::make_unique<QuestEngine>();
    return *quest_engine_;
}

} // namespace sf2::app
