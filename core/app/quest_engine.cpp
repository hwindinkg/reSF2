// Quest engine core — see quest_engine.hpp for the spec/notes.
//
// Data paths (existing patterns, read-only): the real quest tree root at
// `reference/extracted/xml/res/quests.xml` (the same extracted-res source
// stages.xml/list.xml resolve from in screens.cpp); battle→zone index from
// `reference/extracted/xml/res/stages.xml` (like load_zone_map).

#include "app/quest_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "app/app.hpp"
#include "app/lang_table.hpp"
#include "app/save_system.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr int kMaxActivateDepth = 4;
constexpr int kMaxActionDepth = 6;

constexpr const char* kQuestResRoot = "reference/extracted/xml/res/";

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

// JS `ki` (the numeric test behind `yb.Uha` L966): a whole string that
// parses as a number takes the numeric compare; anything else is a string
// (`yb.PNa` = exact equality).
bool is_numeric(const std::string& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != nullptr && *end == '\0' && end != s.c_str();
}

double to_number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

bool truthy01(const std::string& s) {
    return s == "1" || s == "true" || (is_numeric(s) && to_number(s) != 0.0);
}

// Splits a `hb` triple (`Me|Re|Lq`, `hb.toString` L1416) field index.
std::string triple_field(const std::string& s, int index) {
    std::size_t start = 0;
    for (int i = 0; i <= index; ++i) {
        if (start > s.size()) return std::string();
        const std::size_t bar = s.find('|', start);
        const std::string part =
            s.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (i == index) return part;
        if (bar == std::string::npos) return std::string();
        start = bar + 1;
    }
    return std::string();
}

std::string read_file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// One condition element (JS `yb.parse` L958 / `yb.tD` L994): the leaf tag
// table is Equal|Greater|GreaterEqual|Less|LessEqual (all carry
// Value1/Value2 + optional Not="1"), and `<Operator Type="And|Or" Not>`
// nests further elements. A `<Conditions>` block is an implicit AND of its
// element children (`be.GS` L1010).
void parse_condition_element(const pugi::xml_node& ch, QuestCond& parent) {
    const std::string tag = ch.name();
    const std::string not_attr = ch.attribute("Not").value();
    if (tag == "Equal" || tag == "Greater" || tag == "GreaterEqual" || tag == "Less" ||
        tag == "LessEqual" || tag == "Contains" || tag == "Starts" || tag == "Ends") {
        QuestCond leaf;
        leaf.kind = tag;
        leaf.value1 = ch.attribute("Value1").value();
        leaf.value2 = ch.attribute("Value2").value();
        leaf.invert = not_attr == "1";
        parent.children.push_back(std::move(leaf));
        return;
    }
    if (tag == "Operator" || tag == "And" || tag == "Or") {
        QuestCond op;
        const std::string type = ch.attribute("Type").value();
        op.kind = (type == "Or" || tag == "Or") ? "Or" : "And";
        op.invert = not_attr == "1";
        for (pugi::xml_node g = ch.first_child(); g; g = g.next_sibling()) {
            if (g.type() != pugi::node_element) continue;
            parse_condition_element(g, op);
        }
        parent.children.push_back(std::move(op));
        return;
    }
    // Unknown condition element: kept so it evaluates UNKNOWN (never fires).
    QuestCond leaf;
    leaf.kind = tag;
    leaf.value1 = ch.attribute("Value1").value();
    leaf.value2 = ch.attribute("Value2").value();
    leaf.invert = not_attr == "1";
    parent.children.push_back(std::move(leaf));
}

// Parses one <Conditions> element into cond (AND of its element children).
void parse_conds(const pugi::xml_node& node, QuestCond& cond) {
    cond.kind = "And";
    for (pugi::xml_node ch = node.first_child(); ch; ch = ch.next_sibling()) {
        if (ch.type() != pugi::node_element) continue;
        parse_condition_element(ch, cond);
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
    } else if (act.tag == "Button") {
        // JS `He.Rib` L1057: the button's nested `Yb` actions
        // (`SetStoryTutorialStep`/`Fight`/...) run on press (`dhb` L1061),
        // not at parse time — keep them structured for `press_dialog`.
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

// Splits an `hb` triple (`Me|Re|Lq`, `hb.toString` L1416) into zone/name.
void split_battle_triple(const std::string& s, std::string& zone, std::string& name) {
    const std::size_t p1 = s.find('|');
    if (p1 == std::string::npos) {
        zone.clear();
        name = s;
        return;
    }
    zone = s.substr(0, p1);
    const std::size_t p2 = s.find('|', p1 + 1);
    name = s.substr(p1 + 1,
                    p2 == std::string::npos ? std::string::npos : p2 - (p1 + 1));
}

// `u.ka(a,b)` L2455: "1"/"true" -> true.
bool attr_bool01(const std::string& v) { return v == "1" || v == "true"; }

// `_Name` quest-variable reference (JS `to` + the `_`-prefixed resolver).
// `StoryTutorialOpenScene` passes its arguments through refs — `Dialog`
// `Line Text="_SenseiDialogText"` (tutorial_quests.xml L328) and
// `MenuBtnFlashing BtnName="_NextScene"` (L361) — while the producer quests
// write them with `SetVariable Scope="Global"` (L115-116, L137-138), i.e.
// into the save's quest variables. A run-local (`NotificationTextMove`,
// L18) wins; an unknown name is the empty string (so `!= ""` reads false).
std::string quest_var(App& app, const std::map<std::string, std::string>& locals,
                      const std::string& token) {
    if (token.size() < 2 || token[0] != '_' || token[1] == '$') return token;
    const std::string name = token.substr(1);
    const auto it = locals.find(name);
    if (it != locals.end()) return it->second;
    try {
        const WarriorSave w = app.save().load();
        const auto gv = w.variables.find(name);
        if (gv != w.variables.end()) return gv->second;
    } catch (const std::exception&) {
    }
    return std::string();
}

// Resolves a `ShowBattle`/`HideBattle`/... `Name` attr into the `hb`
// zone/name pair (a triple keeps its zone; a bare name uses the stages index).
QuestBattleWrite battle_write_from(const std::string& triple, const std::string& zone_hint) {
    QuestBattleWrite bw;
    std::string zone;
    split_battle_triple(triple, zone, bw.name);
    if (bw.name.empty()) {
        bw.name = triple;
        zone.clear();
    }
    bw.zone = zone.empty() ? zone_hint : zone;
    return bw;
}

} // namespace

const WarriorSave& QuestEngine::EvalCtx::live(App& app) const {
    if (!save_loaded) {
        try {
            save = app.save().load();
        } catch (const std::exception&) {
            save = WarriorSave{};
        }
        save_loaded = true;
    }
    return save;
}

// JS `L3(a,b)` (L184): if the file exists (`Ixb` gate), read it and walk the
// ROOT element's children — `Quest` registers (`WO(new be(e,a))`), `Include`
// recurses through `Sjb`. The port appends to `quests_` in load order.
void QuestEngine::load_quest_file(App& app, const std::string& rel) {
    if (rel.empty()) return;
    // The shipped tree names the same file from more than one place
    // (AttachScripts_Zone1 L110-111 and FirstGuardBeaten L284-285 both
    // attach zone_1); registering it twice would double-fire every quest
    // in it, so each file loads once (JS `WO` has no such guard — noted).
    for (const std::string& f : loaded_files_) {
        if (f == rel) return;
    }
    const std::string path = std::string(kQuestResRoot) + rel;
    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            std::fprintf(stdout, "[quest] include/attach missing (skipped): %s\n", rel.c_str());
            return;  // JS `Ixb(a)` file-exists gate
        }
    }
    const std::string xml = read_file_text(path);
    if (xml.empty()) return;
    sf2::data::xml_doc doc;
    doc.parse(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
    const pugi::xml_node root = doc.root().first_child();
    if (!root) return;
    loaded_files_.push_back(rel);  // before walking: self-includes terminate
    for (pugi::xml_node ch = root.first_child(); ch; ch = ch.next_sibling()) {
        if (ch.type() != pugi::node_element) continue;
        const std::string tag = ch.name();
        if (tag == "Quest") {
            parse_quest_node(app, ch, rel);
        } else if (tag == "Include") {
            load_include(app, ch);
        }
    }
}

// JS `Sjb(a,b)` (L184): `be.GS(a.A("Conditions"), c)` over the Include's own
// `<Conditions>`; if ANY condition fails the include is skipped. Otherwise
// `File` is split on `|` and every alternative that exists is loaded in
// order (`for(c=0;c<a.length;) this.L3(a[c++], b+"--")`).
void QuestEngine::load_include(App& app, const pugi::xml_node& node) {
    QuestCond conds;
    const pugi::xml_node cs = node.child("Conditions");
    if (cs) parse_conds(cs, conds);
    if (!conds.children.empty() && !conditions_hold(app, conds, load_ctx_)) {
        std::fprintf(stdout, "[quest] include gated off: %s\n",
                     node.attribute("File").value());
        return;
    }
    const std::string file = node.attribute("File").value();
    std::size_t start = 0;
    while (start <= file.size()) {
        const std::size_t bar = file.find('|', start);
        const std::string alt = file.substr(
            start, bar == std::string::npos ? std::string::npos : bar - start);
        if (!alt.empty()) load_quest_file(app, alt);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
}

// JS `WO`/`be` ctor (L1006): one `<Quest>` node -> QuestDef.
void QuestEngine::parse_quest_node(App& app, const pugi::xml_node& q,
                                   const std::string& file) {
    (void)app;
    (void)file;
    QuestDef def;
    def.name = q.attribute("Name").value();
    if (def.name.empty()) return;
    def.priority = parse_int_or(q.attribute("Priority").value(), 0);
    def.unresumable = std::string(q.attribute("Unresumable").value()) == "1";
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

bool QuestEngine::ensure_loaded(App& app) {
    if (loaded_) return true;
    // Battle→zone index (stages.xml Zone/Battle names).
    try {
        const std::string xml = read_file_text(std::string(kQuestResRoot) + "stages.xml");
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
    // The real tree (JS `p.F().L3("quests.xml")`): the root's inline
    // quests plus every `<Include>` whose `<Conditions>` hold. The
    // tutorial chain is one of those includes, gated on
    // `_$StoryTutorialStep != END` (quests.xml L10-14), so the loader no
    // longer short-circuits on an END step.
    try {
        load_ctx_ = EvalCtx{};
        try {
            const WarriorSave w = app.save().load();
            load_ctx_.story_step = w.story_step();
            load_ctx_.level = w.level;
            load_ctx_.save = w;
            load_ctx_.save_loaded = true;
        } catch (const std::exception&) {
        }
        load_quest_file(app, "quests.xml");
        loaded_ = true;
        std::fprintf(stdout,
                     "[quest] engine loaded: %zu quests from %zu files, %zu battle zones, "
                     "%zu unanswerable queries\n",
                     quests_.size(), loaded_files_.size(), battle_zone_.size(),
                     logged_queries_.size());
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

void QuestEngine::note_unanswerable(const std::string& token) {
    if (logged_queries_.insert(token).second) {
        std::fprintf(stdout,
                     "[quest] unanswerable query (condition UNKNOWN, no fire): %s\n",
                     token.c_str());
        std::fflush(stdout);
    }
}

// `?Method[arg].Field` queries the shell models. Mirrors the JS `sg.gAa`
// dispatch (L959) for the methods the shipped quests actually read through
// conditions; everything else is UNKNOWN (logged) rather than invented.
bool QuestEngine::resolve_query(App& app, const std::string& token, const EvalCtx& ctx,
                                std::string& out) {
    const std::size_t lb = token.find('[');
    const std::size_t rb = (lb == std::string::npos) ? std::string::npos : token.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) {
        note_unanswerable(token);
        return false;
    }
    const std::string method = token.substr(1, lb - 1);
    std::string arg = token.substr(lb + 1, rb - lb - 1);
    std::string field = token.substr(rb + 1);
    if (!field.empty() && field[0] == '.') field = field.substr(1);
    // A nested `_$`/`_` reference inside the brackets (`?Fight[_$Fight].Zone`).
    if (!arg.empty() && arg[0] == '_') {
        std::string inner;
        if (!resolve_token(app, arg, ctx, inner)) return false;
        arg = inner;
    }
    if (method == "SysInfo") {
        // JS `$wb` (L983-987) for the shipped desktop/web build.
        static const char* const kSys[][2] = {
            {"Paid", "1"},          {"AnyPaid", "1"},          {"AnyF2P", "0"},
            {"ChinaF2P", "0"},      {"FacebookLoginSupport", "0"}, {"IsDebug", "0"},
            {"Steam", "0"},         {"Switch", "0"},           {"UserObserved", "0"},
            {"F2P", "0"},           {"DailyOffersAvailable", "0"}, {"AdvertisingSupport", "0"},
            {"AndroidAPILevel", "0"}, {"Editor", "0"},         {"GamingServiceHasAccount", "0"},
            {"HuaweiF2P", "0"},     {"IsbnF2P", "0"},          {"LowGraphicsSupport", "0"},
            {"MyGamezF2P", "0"},    {"RaidsSupport", "0"},     {"SamsungF2P", "0"},
            {"QualityCondition", "HIGH"}, {"CurrentPlatformName", "PAID"},
            {"DeviceType", "Desktop"}, {"HasPayments", "0"},
        };
        for (const auto& row : kSys) {
            if (field == row[0]) {
                out = row[1];
                return true;
            }
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Player") {
        const WarriorSave& w = ctx.live(app);
        if (field == "Level") {
            out = std::to_string(w.level);
            return true;
        }
        if (field == "Money") {
            out = std::to_string(w.money);
            return true;
        }
        if (field == "Weapon") {
            out = w.weapon;
            return true;
        }
        if (field == "Armor") {
            out = w.armor;
            return true;
        }
        if (field == "Helm") {
            out = w.helm;
            return true;
        }
        if (field == "Ranged") {
            out = w.ranged;
            return true;
        }
        if (field == "Magic") {
            out = w.magic;
            return true;
        }
        if (field == "MapFocus") {
            out = w.map_focus;
            return true;
        }
        if (field == "HasPayments") {
            out = "0";
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Fight") {
        // JS `X3a` (L970): the live fight controller. The port's journal
        // carries the `hb` triple at FightEnd (`_$Fight`) and the
        // save carries the win counts (`<Fights>`).
        const std::string triple = arg.empty() ? ctx.journal.fight : arg;
        if (field == "Zone") {
            out = triple_field(triple, 0);
            return true;
        }
        if (field == "Battle") {
            out = triple_field(triple, 1);
            return true;
        }
        if (field == "Fight") {
            out = triple_field(triple, 2);
            return true;
        }
        if (field == "Name") {
            out = triple;
            return true;
        }
        if (field == "WinCount") {
            const WarriorSave& w = ctx.live(app);
            int wins = 0;
            for (const WarriorSave::FightWins& f : w.fights) {
                if (f.name == triple) wins = f.wins;
            }
            out = std::to_string(wins);
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Purchase") {
        // Only the shipped tutorial lookup is modelled (JS `IJa` on the
        // weapons purchase the chain performs).
        if (field == "Type" && arg == "WEAPON_KNIVES") {
            out = "Weapon";
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    note_unanswerable(token);
    return false;
}

bool QuestEngine::resolve_token(App& app, const std::string& token, const EvalCtx& ctx,
                                std::string& out) {
    out.clear();
    if (token.empty()) return true;
    // A top-level boolean expression in ONE Value1 (the shipped
    // `?SysInfo[].Switch Or ?SysInfo[].Steam` form, tutorial_quests.xml
    // L15/L234/L311): JS evaluates the expression string; on the desktop
    // build every operand is "0", so the result is "0".
    {
        const std::string or_sep = " Or ";
        const std::string and_sep = " And ";
        const std::size_t op = token.find(or_sep);
        if (op != std::string::npos) {
            std::string a, b;
            if (!resolve_token(app, token.substr(0, op), ctx, a)) return false;
            if (!resolve_token(app, token.substr(op + or_sep.size()), ctx, b)) return false;
            out = (truthy01(a) || truthy01(b)) ? "1" : "0";
            return true;
        }
        const std::size_t ap = token.find(and_sep);
        if (ap != std::string::npos) {
            std::string a, b;
            if (!resolve_token(app, token.substr(0, ap), ctx, a)) return false;
            if (!resolve_token(app, token.substr(ap + and_sep.size()), ctx, b)) return false;
            out = (truthy01(a) && truthy01(b)) ? "1" : "0";
            return true;
        }
    }
    if (token.rfind("_$", 0) == 0) {
        if (token == "_$StoryTutorialStep") {
            // JS `p.o.zi.HH`: the live step defaults to `kU[0]` =
            // "NotStarted" for a fresh profile (the stock
            // `<Warrior Tutorial="MOVE">` is not a valid step name). The
            // port keeps the step in the save's quest variables, so an
            // absent value reads as NotStarted ONLY on the armed
            // fresh-tutorial path — the seeded post-tutorial saves stay
            // chain-silent.
            if (ctx.story_step.empty() && fresh_tutorial_) {
                out = "NotStarted";
                return true;
            }
            out = ctx.story_step;
            return true;
        }
        if (token == "_$SceneTo") {
            out = ctx.journal.scene_to;
            return true;
        }
        if (token == "_$SceneFrom") {
            out = ctx.journal.scene_from;
            return true;
        }
        if (token == "_$Fight") {
            out = ctx.journal.fight;
            return true;
        }
        if (token == "_$FightResult") {
            out = ctx.journal.fight_result;
            return true;
        }
        if (token == "_$ActionID") {
            out = ctx.journal.action_id;
            return true;
        }
        // Other `Bj` journal fields (`_$CurrentScene`, `_$Iterator`, ...):
        // the shell does not model them -> UNKNOWN.
        note_unanswerable(token);
        return false;
    }
    if (token[0] == '_') {
        // `_Name` quest/session variable (JS `p.o.f5a`: the variable's
        // value, default "0"). The save's `<Variables>` store holds the
        // `SetVariable Scope="Global"` writes.
        const std::string name = token.substr(1);
        const WarriorSave& w = ctx.live(app);
        const auto it = w.variables.find(name);
        out = (it != w.variables.end()) ? it->second : "0";
        return true;
    }
    if (token[0] == '?') return resolve_query(app, token, ctx, out);
    out = token;  // literal
    return true;
}

QuestEngine::Tri QuestEngine::eval_cond(App& app, const QuestCond& cond, const EvalCtx& ctx) {
    if (cond.kind == "And" || cond.kind == "Or") {
        const bool is_or = cond.kind == "Or";
        bool acc = !is_or;      // And starts true, Or starts false
        bool unknown = false;
        for (const QuestCond& c : cond.children) {
            const Tri r = eval_cond(app, c, ctx);
            if (r == Tri::Unknown) {
                unknown = true;
                continue;
            }
            if (is_or && r == Tri::True) {
                acc = true;
                unknown = false;
                break;
            }
            if (!is_or && r == Tri::False) {
                acc = false;
                unknown = false;
                break;
            }
        }
        Tri result = unknown ? Tri::Unknown : (acc ? Tri::True : Tri::False);
        if (cond.invert) {
            if (result == Tri::True) result = Tri::False;
            else if (result == Tri::False) result = Tri::True;
        }
        return result;
    }
    // Leaf comparison (JS `yb.tga`/`w0a`/`Uha` L965-966).
    std::string a, b;
    if (!resolve_token(app, cond.value1, ctx, a)) return Tri::Unknown;
    if (!resolve_token(app, cond.value2, ctx, b)) return Tri::Unknown;
    bool ok = false;
    if (cond.kind == "Equal" || cond.kind == "Contains" || cond.kind == "Starts" ||
        cond.kind == "Ends") {
        // Both numeric -> numeric compare; otherwise exact string compare
        // (`w0a` falls back to `PNa`).
        ok = (is_numeric(a) && is_numeric(b)) ? (to_number(a) == to_number(b)) : (a == b);
    } else if (cond.kind == "Greater" || cond.kind == "GreaterEqual" ||
               cond.kind == "Less" || cond.kind == "LessEqual") {
        if (is_numeric(a) && is_numeric(b)) {
            const double va = to_number(a);
            const double vb = to_number(b);
            if (cond.kind == "Greater") ok = va > vb;
            else if (cond.kind == "GreaterEqual") ok = va >= vb;
            else if (cond.kind == "Less") ok = va < vb;
            else ok = va <= vb;
        } else {
            ok = (a == b);  // JS `w0a` -> `PNa`
        }
    } else {
        return Tri::Unknown;  // unsupported leaf tag
    }
    if (cond.invert) ok = !ok;
    return ok ? Tri::True : Tri::False;
}

bool QuestEngine::conditions_hold(App& app, const QuestCond& cond, const EvalCtx& ctx) {
    return eval_cond(app, cond, ctx) == Tri::True;
}

void QuestEngine::run_actions(App& app, const std::vector<QuestAction>& acts,
                              const QuestJournal& journal, QuestSideEffects& fx,
                              std::map<std::string, std::string>& locals,
                              const std::string& quest, int depth) {
    if (depth > kMaxActionDepth) return;
    for (const QuestAction& a : acts) {
        const std::string& t = a.tag;
        if (t == "If") {
            // `co.S` (L1038): the If's own `<Conditions>` against the live
            // journal/save; an UNKNOWN operand takes the Else branch.
            EvalCtx c;
            c.journal = journal;
            c.level = journal.player_level;
            try {
                const WarriorSave w = app.save().load();
                c.story_step = w.story_step();
                c.level = w.level;
                c.save = w;
                c.save_loaded = true;
            } catch (const std::exception&) {
            }
            const bool take = conditions_hold(app, a.if_cond, c);
            run_actions(app, take ? a.if_then : a.if_else, journal, fx, locals, quest,
                        depth + 1);
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
            // Structured record for the Sensei modal (screens.cpp displays).
            // Line refs: `_Local` resolves via the run's locals (If/Else set
            // NotificationTextMove/PunchBag just above); the result is kept
            // as a runtime lang key and resolved at DRAW time (the lang table
            // is not loaded when the quest fires at boot).
            {
                EngineDialog dlg;
                dlg.type = attr_or(a.attrs, "Type");
                dlg.title = attr_or(a.attrs, "Title");
                dlg.image = attr_or(a.attrs, "Image");
                dlg.quest = quest;
                dlg.journal = journal;
                for (const QuestAction& c : a.children) {
                    if (c.tag == "Line") {
                        std::string text = attr_or(c.attrs, "Text");
                        // `_`-refs: run-locals then the global quest variables
                        // (see `quest_var`). An unresolved ref drops the row.
                        if (!text.empty() && text[0] == '_') {
                            text = quest_var(app, locals, text);
                        }
                        if (!text.empty()) dlg.lines.push_back(text);
                        // `He.jkb` (L1042): the row keeps its own caption.
                        dlg.line_buttons.push_back(attr_or(c.attrs, "ButtonText"));
                    } else if (c.tag == "Button") {
                        // JS `He.Rib` L1057: the nested actions run on press
                        // (`dhb` L1061) — defer them (do NOT run eagerly).
                        for (const QuestAction& sub : c.children) {
                            dlg.button_actions.push_back(sub);
                        }
                        // An explicit `Button Text` (sensei_arc.xml L59
                        // `dlgStoryBtnFight`) overrides the last row's caption.
                        const std::string bt = attr_or(c.attrs, "Text");
                        if (!bt.empty()) dlg.button_text = bt;
                    }
                }
                // No authored `Button Text`: the LAST row's `ButtonText`
                // labels the action plate (tutorial_quests.xml L160
                // `dlgStoryBtnFight`) — the earlier rows are the pager's
                // intermediate captions (`dlgStoryBtnMore`, L159).
                if (dlg.button_text.empty()) {
                    for (auto it = dlg.line_buttons.rbegin();
                         it != dlg.line_buttons.rend(); ++it) {
                        if (!it->empty()) {
                            dlg.button_text = *it;
                            break;
                        }
                    }
                }
                if (!dlg.lines.empty()) {
                    if (dialogs_.size() >= 8) {
                        std::fprintf(stdout, "[quest] dialog queue full, dropping oldest\n");
                        dialogs_.erase(dialogs_.begin());
                    }
                    std::fprintf(stdout, "[quest] dialog queued (%s/%s): %zu lines\n",
                                 dlg.type.c_str(), dlg.title.c_str(), dlg.lines.size());
                    dialogs_.push_back(std::move(dlg));
                }
            }
            // Dialog children (Line/Button) are NOT run here: Line is a data
            // row and Button's actions are deferred to `press_dialog`.
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
        } else if (t == "ShowBattle" || t == "HideBattle") {
            // `Aj` L1108/L1109 (`EShowBattle`/`EHideBattle`): `Name` is the
            // `hb` triple; `ShowBattle` -> `Iaa(hb,true,true,Locked,Hidden,
            // ReplayCount)`, `HideBattle` -> `Aj(false)` -> `Iaa` `c=false`
            // -> `Eja` (record dropped).
            const std::string nm = attr_or(a.attrs, "Name");
            QuestBattleWrite bw = battle_write_from(nm, battle_zone(nm));
            if (!bw.name.empty()) {
                bw.remove = (t == "HideBattle");
                bw.locked = attr_bool01(attr_or(a.attrs, "Locked"));
                bw.hidden = attr_bool01(attr_or(a.attrs, "Hidden"));
                bw.replay_count = parse_int_or(attr_or(a.attrs, "ReplayCount"), 0);
                fx.battle_writes.push_back(std::move(bw));
            }
        } else if (t == "SetBattleVisibility") {
            // `no` L1095-1096 (`ESetBattleVisibility`): `IsVisible` default
            // "0"; hidden = `!(IsVisible>0)` (`b = !ba.Zv(a,AN)`), written
            // via `Iaa(hb,false,true,false,hidden)` (ensure + set Hidden).
            const std::string nm = attr_or(a.attrs, "Name");
            QuestBattleWrite bw = battle_write_from(nm, battle_zone(nm));
            if (!bw.name.empty()) {
                bw.hidden = !attr_bool01(attr_or(a.attrs, "IsVisible", "0"));
                fx.battle_writes.push_back(std::move(bw));
            }
        } else if (t == "ToggleBattle") {
            // `Ho` L1106 (`EToggleBattle`): `Toggle` on/On -> flip Hidden on
            // the EXISTING record (`hl.gx(!li)`), no create.
            const std::string nm = attr_or(a.attrs, "Name");
            QuestBattleWrite bw = battle_write_from(nm, battle_zone(nm));
            if (!bw.name.empty()) {
                bw.toggle_hidden = true;
                fx.battle_writes.push_back(std::move(bw));
            }
        } else if (t == "SetVariable") {
            if (attr_or(a.attrs, "Scope") == "Global") {
                fx.set_vars[attr_or(a.attrs, "Name")] = attr_or(a.attrs, "Value");
            } else {
                // Local run vars (NotificationTextMove/PunchBag) feed Dialog
                // line refs below; `?`-expressions stay unresolved (noted).
                const std::string v = attr_or(a.attrs, "Value");
                if (!attr_or(a.attrs, "Name").empty() &&
                    v.find('?') == std::string::npos) {
                    locals[attr_or(a.attrs, "Name")] = v;
                }
            }
        } else if (t == "ClickButton") {
            fx.clicks.push_back(attr_or(a.attrs, "Target"));
            // `Nn` `UseFlashing="1"` (L1115): highlight the target plate. The
            // tutorial pairs it with `IgnoreCallback="1"` so the map FIGHT
            // button is focused/flashed but NEVER launched by the quest
            // (tutorial_quests.xml L156).
            if (attr_bool01(attr_or(a.attrs, "UseFlashing"))) {
                fx.flash_targets.push_back(attr_or(a.attrs, "Target"));
            }
        } else if (t == "MenuBtnFlashing") {
            // Desktop navigation guidance (FLOW_STATIC L140-142): the web/
            // else branch of `StoryTutorialOpenScene` shows the notification
            // and flashes the `_NextScene` nav button; the shell never
            // navigates (tutorial_quests.xml L361). `_NextScene` resolves to
            // the global `NextScene` (Shop/Map/Dojo/Profile).
            fx.menu_flashes.push_back(
                quest_var(app, locals, attr_or(a.attrs, "BtnName")));
        } else if (t == "ClickHint") {
            // `ClickHint` (the Switch/Steam branch, tutorial_quests.xml L338):
            // recorded only — the desktop shell drives navigation through the
            // nav flash instead of an auto-click.
            fx.click_hints.push_back(attr_or(a.attrs, "Target"));
        } else if (t == "SceneMenuScroll") {
            // `SceneMenuScroll Action` (the Switch/Steam branch): recorded.
            fx.scene_menu_scroll.push_back(attr_or(a.attrs, "Action"));
        } else if (t == "ClearQuestQueue") {
            fx.clears.push_back(attr_or(a.attrs, "Name"));
        } else if (t == "AttachQuestFile") {
            // JS `Bn.S` (L1025): `p.F().L3(this.filename)` — load the file
            // at this point in the run. Deferred to the end of the fire
            // pass (the JS `RA` loop captured its list length, so the
            // newly loaded quests only run on FUTURE events).
            fx.attach_files.push_back(attr_or(a.attrs, "File"));
        } else if (t == "Activate") {
            // JS `Ge.S` (L1023): `ha.F().RA("QUEST_EVENT_ACTIVATE")` with
            // `Ge.MZ` = the ActionID; re-fired by fire_inner.
            fx.activate_requests.push_back(attr_or(a.attrs, "ActionID"));
        } else if (t == "Wait") {
            // Collapsed (synchronous runs) — recorded for traceability.
            fx.unknown.push_back("Wait:" + attr_or(a.attrs, "Frames") + "f");
        } else if (t == "StoryTutorialMove" || t == "StoryTutorialPunchbag" ||
                   t == "StoryTutorialBuyItem" || t == "StoryTutorialLearnPerk" ||
                   t == "StoryTutorialDoubleSweep" || t == "StoryTutorialShowBlock") {
            fx.minigames.push_back(t + " (needs fight hooks)");
        } else if (t == "Line" || t == "Button" || t == "Then" || t == "Else" ||
                   t == "Conditions") {
            run_actions(app, a.children, journal, fx, locals, quest, depth + 1);
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
        // `hl` battle-record writes (JS `J1a` L259 / `Iaa` L260-261 /
        // `Eja` L261 / `Ho` L1106) — the `WDa` unlock bit `Qr.lla` reads.
        for (const QuestBattleWrite& bw : fx.battle_writes) {
            if (bw.name.empty()) continue;
            if (bw.remove) {
                w.battle_remove(bw.zone, bw.name);  // HideBattle -> `Eja`
                std::fprintf(stdout, "[quest] battle record remove %s|%s\n",
                             bw.zone.c_str(), bw.name.c_str());
                dirty = true;
                continue;
            }
            if (bw.toggle_hidden) {
                WarriorSave::BattleRecord* rec = w.battle_record(bw.zone, bw.name);
                if (rec != nullptr) {  // `Ho` L1106: flip only, never create
                    rec->hidden = !rec->hidden;
                    dirty = true;
                }
                std::fprintf(stdout, "[quest] battle hidden toggle %s|%s\n",
                             bw.zone.c_str(), bw.name.c_str());
                continue;
            }
            w.battle_set_visibility(bw.zone, bw.name, bw.locked, bw.hidden,
                                    bw.replay_count);
            std::fprintf(stdout, "[quest] battle record %s|%s locked=%d hidden=%d\n",
                         bw.zone.c_str(), bw.name.c_str(), bw.locked ? 1 : 0,
                         bw.hidden ? 1 : 0);
            dirty = true;
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
    EvalCtx ctx;
    ctx.journal = journal;
    ctx.level = journal.player_level;
    try {
        const WarriorSave w = app.save().load();
        ctx.story_step = w.story_step();
        ctx.level = w.level;
        ctx.save = w;
        ctx.save_loaded = true;
    } catch (const std::exception&) {
    }
    std::vector<std::string> attaches;  // AttachQuestFile, applied post-pass
    // JS `ha.RA` (L1018) captures the event list length before iterating:
    // quests registered by an attach during this pass run on FUTURE events.
    const std::size_t quest_total = quests_.size();
    for (std::size_t i = 0; i < quest_total; ++i) {
        const QuestDef& q = quests_[i];
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
        if (!conditions_hold(app, q.root, ctx)) continue;
        QuestSideEffects fx;
        std::map<std::string, std::string> locals;  // run-local vars
        run_actions(app, q.actions, journal, fx, locals, q.name, 0);
        apply_effects(app, fx);
        // Live UI guidance (draw-only, last value wins): the shell reads
        // these each frame instead of the quest auto-acting.
        if (!fx.flash_targets.empty()) flash_target_ = fx.flash_targets.back();
        if (!fx.menu_flashes.empty()) nav_flash_ = fx.menu_flashes.back();
        if (fx.has_map_focus) last_map_focus_ = fx.map_focus;
        if (q.unresumable) fired_.push_back(q.name);
        fired.push_back(q.name);
        std::fprintf(stdout, "[quest] FIRED %s on %s (step=%s scene=%s->%s)\n", q.name.c_str(),
                     event.c_str(), ctx.story_step.c_str(), journal.scene_from.c_str(),
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
        if (!fx.click_hints.empty()) {
            for (const std::string& s : fx.click_hints) {
                std::fprintf(stdout, "[quest]   click hint (record only): %s\n", s.c_str());
            }
        }
        if (!fx.menu_flashes.empty()) {
            for (const std::string& s : fx.menu_flashes) {
                std::fprintf(stdout, "[quest]   nav flash: %s\n", s.c_str());
            }
        }
        if (!fx.attach_files.empty()) {
            for (const std::string& s : fx.attach_files) {
                std::fprintf(stdout, "[quest]   attach quest file: %s\n", s.c_str());
            }
        }
        if (!fx.unknown.empty()) {
            for (const std::string& s : fx.unknown) {
                std::fprintf(stdout, "[quest]   action (record only): %s\n", s.c_str());
            }
        }
        std::fflush(stdout);
        // Queue clears (Mn `Yba` L1019): latch the named quest as done.
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
        for (const std::string& f : fx.attach_files) attaches.push_back(f);
        // Refresh the step/save snapshot for later quests in this firing.
        try {
            const WarriorSave w = app.save().load();
            ctx.story_step = w.story_step();
            ctx.level = w.level;
            ctx.save = w;
            ctx.save_loaded = true;
        } catch (const std::exception&) {
        }
        // Chained Activate (`Ge` L1024): re-fire synchronously with
        // `Ge.MZ` = ActionID. Done last so an attach inside it has already
        // been collected (`attaches` above) and never touches `q`.
        for (const std::string& u : fx.activate_requests) {
            QuestJournal j2 = journal;
            j2.action_id = u;
            fire_inner(app, "Activate", j2, fired, depth + 1);
        }
    }
    // `AttachQuestFile` (`Bn.S` L1025 -> `L3`): load after the pass, so the
    // loaded quests register for FUTURE events only (JS `RA` semantics).
    for (const std::string& f : attaches) {
        load_ctx_ = ctx;
        load_quest_file(app, f);
    }
    if (!attaches.empty()) {
        std::fprintf(stdout, "[quest] attach applied: %zu quests from %zu files\n",
                     quests_.size(), loaded_files_.size());
        std::fflush(stdout);
    }
}

void QuestEngine::note_fight(const std::string& name, const std::string& result) {
    last_fight_ = name;
    last_result_ = result;
}

// `He` pager (L1042-1062). The head dialog's current page caption: the row's
// `ButtonText` (`He.jkb`), falling back to the action plate's caption.
std::string QuestEngine::dialog_button_text() const {
    if (dialogs_.empty()) return std::string();
    const EngineDialog& d = dialogs_.front();
    const std::size_t page =
        d.lines.empty() ? 0 : (d.page < d.lines.size() ? d.page : d.lines.size() - 1);
    if (page < d.line_buttons.size() && !d.line_buttons[page].empty()) {
        return d.line_buttons[page];
    }
    return d.button_text;
}

bool QuestEngine::dialog_has_next_page() const {
    if (dialogs_.empty()) return false;
    return dialogs_.front().page + 1 < dialogs_.front().lines.size();
}

void QuestEngine::advance_dialog_page() {
    if (dialogs_.empty()) return;
    EngineDialog& d = dialogs_.front();
    if (d.page + 1 >= d.lines.size()) return;
    ++d.page;
    std::fprintf(stdout, "[quest] dialog page -> %zu/%zu (%s, more=%d)\n", d.page + 1,
                 d.lines.size(), dialog_button_text().c_str(),
                 dialog_has_next_page() ? 1 : 0);
    std::fflush(stdout);
}

std::vector<std::string> QuestEngine::press_dialog(App& app) {
    std::vector<std::string> fights;
    if (dialogs_.empty()) return fights;
    EngineDialog dlg = dialogs_.front();
    dialogs_.erase(dialogs_.begin());
    std::fprintf(stdout, "[quest] dialog button pressed: %s (%s)\n", dlg.title.c_str(),
                 dlg.button_text.c_str());
    std::fflush(stdout);
    QuestSideEffects fx;
    std::map<std::string, std::string> locals;
    run_actions(app, dlg.button_actions, dlg.journal, fx, locals, dlg.quest, 0);
    apply_effects(app, fx);
    for (const std::string& f : fx.fight_requests) fights.push_back(f);
    return fights;
}

std::vector<std::string> QuestEngine::fire(App& app, const std::string& event,
                                           const QuestJournal& journal) {
    std::vector<std::string> fired;
    fresh_tutorial_ = app.fresh_tutorial();
    // Scene-scoped UI guidance resets on the navigation edge: a flash target
    // belongs to the screen that requested it (the map's FIGHT plate), and a
    // nav highlight clears once the player reaches its named screen (`Mn`/
    // `Yba` clear semantics, FLOW_STATIC L82/L133).
    if (event == "SceneLoaded") {
        flash_target_.clear();
        if (journal.scene_to == nav_flash_) nav_flash_.clear();
    }
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
