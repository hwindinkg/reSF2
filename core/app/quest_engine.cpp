// Quest engine core — see quest_engine.hpp for the spec/notes.
//
// Data paths (existing patterns, read-only): the real quest tree root at
// `reference/extracted/xml/res/quests.xml` (the same extracted-res source
// stages.xml/list.xml resolve from in screens.cpp); battle→zone index from
// `reference/extracted/xml/res/stages.xml` (like load_zone_map).

#include "app/quest_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "app/app.hpp"
#include "app/lang_table.hpp"
#include "app/save_system.hpp"
#include "app/screens.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr int kMaxActivateDepth = 4;
constexpr int kMaxActionDepth = 6;

constexpr const char* kQuestResRoot = "reference/extracted/xml/res/";

// D1 `He.S` L1047-1051: the `<Dialog Type>` values whose JS body ASSIGNS the
// widget `C`. Only those reach the final `C!=null?...` branch, so only they
// skip `(Ib.RP=!1, this.sa())` and PARK the serialized `Yb` (L954) until
// `He.gf` L1062. Every other Type falls through to `this.sa()`:
//   - `Notification`  -> `Ib.F().Qhb(...)` bar post (L1050); the final guard
//     `this.type=="Notification"&&x` only parks it when `WaitNotificationClose`
//     (`XVa`, L1043 defaults "0") is set, so it is fire-and-forget by default.
//   - `Native`        -> L1051 short-circuits (`this.type=="Native"`).
//   - `Scroll`/`MultiLineScroll`/`ThreeButtons`/`ItemSetDialog`/
//     `MultilineTMP`/`Simple`/`Probability` -> a bare `debugger;` (or the
//     `Probability` guard) leaves `C` null -> advance. This matches the port's
//     own `dialog_kind()==kNone` renderer split (screens.cpp L5743-5750).
bool dialog_builds_widget(const std::string& type) {
    // `He` L1043: an absent `Type` is "Regular" (the port keeps "" — the
    // renderer maps "" and "Regular" to the same 280 `od` widget).
    return type.empty() || type == "Regular" || type == "Stranger" ||
           type == "NoAvatar" || type == "Multiline" || type == "MultilineBig" ||
           type == "ShowLoot";
}

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

// `ge.ZGa` (L1278): `u.H(<NotificationDlgDefaultReadTime Value>)` from
// internal_settings.xml (<Basic>), statically 0 (L2481). The shipped file has
// `Value="1.0"`, so an absent `ReadTime` attr on a Notification dismisses
// after 1 s (L1043 `this.SK=u.H(ReadTime, ge.ZGa)`).
float default_notification_read_time() {
    static bool cached = false;
    static float value = 0.0f;
    if (cached) return value;
    cached = true;
    try {
        sf2::data::xml_doc doc;
        std::ifstream in(std::string(kQuestResRoot) + "internal_settings.xml",
                         std::ios::binary);
        if (in) {
            std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
            const pugi::xml_node root = doc.root().first_child();
            const pugi::xml_node basic = root ? root.child("GUI").child("Basic") : pugi::xml_node();
            const pugi::xml_node node = basic ? basic.child("NotificationDlgDefaultReadTime")
                                              : pugi::xml_node();
            if (node) value = static_cast<float>(std::atof(node.attribute("Value").as_string("")));
        }
    } catch (const std::exception&) {
    }
    return value;
}

// `Xy` L1044: the `TextOffset` vector literal ("x;y") -> an `H`. An absent
// attr is `null` (the `He` ctor default `new H(0,0,0,1)`).
void parse_text_offset(const std::string& v, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (v.empty()) return;
    const std::size_t semi = v.find(';');
    if (semi == std::string::npos) {
        x = static_cast<float>(std::atof(v.c_str()));
        return;
    }
    x = static_cast<float>(std::atof(v.substr(0, semi).c_str()));
    y = static_cast<float>(std::atof(v.substr(semi + 1).c_str()));
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

// `xn.jOa(a)` (L1168): scene NAME -> screen id (11 = unknown, the JS default).
// `xn.iOa(a)` (L1167) is the reverse; only the ids the shell ports exist.
int scene_id_for_name(const std::string& name) {
    if (name == "Preloader") return 0;
    if (name == "Loader") return 2;
    if (name == "Dojo") return 3;
    if (name == "Shop") return 4;
    if (name == "Map") return 5;
    if (name == "Fight") return 6;
    if (name == "Profile") return 7;
    if (name == "GeneralMenu") return 8;
    if (name == "Pvp") return 9;
    return 11;  // JS default (no such screen in the shell)
}

// `xn.iOa(a)` (L1167): screen id -> scene name (the quest journal's scene).
std::string scene_name_for_id(int id) {
    switch (id) {
        case 0: return "Preloader";
        case 2: return "Loader";
        case 3: return "Dojo";
        case 4: return "Shop";
        case 5: return "Map";
        case 6: return "Fight";
        case 7: return "Profile";
        case 8: return "GeneralMenu";
        case 9: return "Pvp";
        default: return std::string();
    }
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
        // JS `be.Gib` (L1007): `this.k7 = be.ifa(Place!=null ? Place : "Map")`
        // — the scene the SET belongs to. Only an explicit `Place` gates in
        // the port (the JS reads `k7` as the checkpoint scene `Faa`, never as
        // a fire gate), so an absent attribute leaves `place` at 0 (ungated).
        const std::string place_name = acts.attribute("Place").value();
        if (!place_name.empty()) def.place = scene_id_for_name(place_name);
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

QuestEngine::ActionRest QuestEngine::run_actions(
    App& app, const std::vector<QuestAction>& acts, const QuestJournal& journal,
    QuestSideEffects& fx, std::map<std::string, std::string>& locals,
    const std::string& quest, int depth) {
    ActionRest result;
    if (depth > kMaxActionDepth) return result;
    for (std::size_t i = 0; i < acts.size(); ++i) {
        const QuestAction& a = acts[i];
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
            ActionRest sub = run_actions(app, take ? a.if_then : a.if_else, journal, fx,
                                         locals, quest, depth + 1);
            // `co` (L1038) runs the chosen `Yb`; a `Wait` inside it suspends
            // the whole walk, so the outer tail is appended to the remainder.
            if (sub.suspended) {
                sub.rest.insert(sub.rest.end(), acts.begin() + i + 1, acts.end());
                return sub;
            }
        } else if (t == "ChangeScene") {
            std::string dst = attr_or(a.attrs, "Destination");
            if (dst == "_$SceneTo") dst = journal.scene_to;
            fx.scene_requests.push_back(dst);
            // `Gn.S` (L1032): `qIa(xn.jOa(ba.Pc(a, Destination)))` — the live
            // request the engine performs in `tick`.
            QuestSceneRequest req;
            req.destination = dst;
            req.reopen = attr_bool01(attr_or(a.attrs, "ReopenScene"));
            fx.navigate.push_back(std::move(req));
        } else if (t == "Fight") {
            fx.fight_requests.push_back(attr_or(a.attrs, "Name"));
        } else if (t == "FightEnd") {
            fx.unknown.push_back("FightEnd (needs ca.Ka().kD scene hook)");
        } else if (t == "OpenShop") {
            std::string tab = attr_or(a.attrs, "Tab");
            // `vj.E0(ba.Pc(a, Tab))` (L1093): the tab arg is resolved through
            // the expression engine. `?Purchase[X].Type` (tutorial_quests.xml
            // L107) reads the purchased item's catalog Type (`Pa`), the only
            // `?`-form the shipped shop quests use.
            const std::string kPurchase = "?Purchase[";
            const std::string kTypeTail = "].Type";
            if (tab.compare(0, kPurchase.size(), kPurchase) == 0 &&
                tab.size() > kPurchase.size() + kTypeTail.size() &&
                tab.compare(tab.size() - kTypeTail.size(), kTypeTail.size(), kTypeTail) == 0) {
                const std::string item = tab.substr(
                    kPurchase.size(),
                    tab.size() - kPurchase.size() - kTypeTail.size());
                const std::string resolved = catalog_item_type(app, item);
                if (!resolved.empty()) tab = resolved;
            }
            const std::string item = attr_or(a.attrs, "Item");
            fx.scene_requests.push_back("Shop:" + tab + ":" + item);
            // `go.S` (L1092): `this.qO = vj.E0(...)`, `this.ah = ba.Pc(a,Item)`
            // -> `mp(4, new Gj(qO, ib))` + `Oa.uLa(qO, ah)`.
            QuestShopOpen open;
            open.tab = tab;
            open.item = item;
            fx.shop_opens.push_back(std::move(open));
        } else if (t == "Dialog") {
            // D1: the widget-building Types park the chain at this dialog
            // (`He.S` L1051) — see the park branch below.
            const std::string dlg_type = attr_or(a.attrs, "Type");
            bool dlg_queued = false;
            std::size_t dlg_queue_index = 0;
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
                // D9 `He.S` L1046: `f=ba.Pc(a,this.title); f=ba.Fz(f,a); r=
                // ba.Pc(a,this.image)` — the `Title`/`Image` attrs run the SAME
                // quest-variable resolution as the `Line` text (xml quests.xml
                // L313 `Title="_Title_Assistant" Image="_Avatar_Assistant_1"`).
                dlg.title = quest_var(app, locals, attr_or(a.attrs, "Title"));
                dlg.image = quest_var(app, locals, attr_or(a.attrs, "Image"));
                // D10 `He` L1043-1045 parse (see the appliers at `He.S` L1051).
                dlg.mirrored = attr_bool01(attr_or(a.attrs, "Mirrored"));
                // `He.S` L1047: `this.n4a&&(r+="|Flip")` — the mirror rides the
                // image string that `v.RIa` L1222 parses.
                if (dlg.mirrored) dlg.image += "|Flip";
                dlg.image_scale = static_cast<float>(
                    std::atof(attr_or(a.attrs, "ImageScale", "1").c_str()));
                dlg.content_offset_x = static_cast<float>(
                    std::atof(attr_or(a.attrs, "ContentOffsetX").c_str()));
                dlg.image_offset_x = static_cast<float>(
                    std::atof(attr_or(a.attrs, "ImageOffsetX").c_str()));
                dlg.image_offset_y = static_cast<float>(
                    std::atof(attr_or(a.attrs, "ImageOffsetY").c_str()));
                parse_text_offset(attr_or(a.attrs, "TextOffset"), dlg.text_offset_x,
                                  dlg.text_offset_y);
                dlg.text_pos_x_by_image =
                    attr_bool01(attr_or(a.attrs, "TextPosXByImage", "1"));
                dlg.block_raycast = attr_bool01(attr_or(a.attrs, "BlockRaycast", "1"));
                dlg.disable_notifications_buttons =
                    attr_bool01(attr_or(a.attrs, "DisableNotificationsButtons"));
                // D8 `He.SK` L1043: `u.H(ReadTime, ge.ZGa)`.
                {
                    const std::string rt = attr_or(a.attrs, "ReadTime");
                    dlg.read_time = rt.empty()
                                        ? default_notification_read_time()
                                        : static_cast<float>(std::atof(rt.c_str()));
                }
                // `He.ah` L1045: the `Item` attr the portrait prefers (L1046).
                dlg.item = attr_or(a.attrs, "Item");
                // `He.L` L1044 `Loot` (`ShowLoot` -> `Xc.Uhb` L929 splits it on
                // `|`) and `MinContentHeight` (`Od.cv`, the `Md` floor).
                dlg.min_content_height = static_cast<float>(
                    std::atof(attr_or(a.attrs, "MinContentHeight").c_str()));
                {
                    const std::string loot = attr_or(a.attrs, "Loot");
                    for (std::size_t p = 0; p < loot.size();) {
                        const std::size_t bar = loot.find('|', p);
                        const std::size_t end = bar == std::string::npos ? loot.size() : bar;
                        const std::string one = loot.substr(p, end - p);
                        if (!one.empty()) dlg.loot.push_back(one);
                        p = end + 1;
                    }
                }
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
                        // JS `He.Rib` L1057-1058: `Type` selects the slot —
                        // `Left`→`Ng`, `Middle`→`Nh`, `Right`→`rh`,
                        // `Close`→`Hj` — each keeping its own `Text`/`Color`/
                        // `Flashing` (`vh`, L1063). An absent `Type` is the
                        // Right slot (the historical single-button path).
                        const std::string btype = attr_or(c.attrs, "Type");
                        const std::string bt = attr_or(c.attrs, "Text");
                        const std::string bc = attr_or(c.attrs, "Color");
                        const bool hint = attr_or(c.attrs, "Flashing") == "1";
                        if (btype.empty() || btype == "Right") {
                            // `He.Rib` L1057-1058: the `<Button>` creates the
                            // `rh` slot regardless of nested actions.
                            dlg.has_right_button = true;
                            // Defer the nested actions (`dhb(1)` L1061).
                            for (const QuestAction& sub : c.children) {
                                dlg.button_actions.push_back(sub);
                            }
                            // An explicit `Button Text` (sensei_arc.xml L59
                            // `dlgStoryBtnFight`) overrides the last caption.
                            if (!bt.empty()) dlg.button_text = bt;
                            if (!bc.empty()) dlg.button_color = bc;
                        } else if (btype == "Left" || btype == "Middle" ||
                                   btype == "Close") {
                            EngineDialogButton& slot = btype == "Left"      ? dlg.left_
                                                      : btype == "Middle" ? dlg.middle_
                                                                          : dlg.close_;
                            for (const QuestAction& sub : c.children) {
                                slot.actions.push_back(sub);
                            }
                            if (!bt.empty()) slot.text = bt;
                            slot.color = bc;
                            slot.hint = hint;
                        }
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
                    dlg_queued = true;
                    dlg_queue_index = dialogs_.size() - 1;
                }
                // D1 `He.S` L1051: a Type that builds a widget (`C != null`)
                // never reaches `(Ib.RP=!1, this.sa())`, so the serialized
                // `Yb` (L954) parks here and only `He.gf` L1062 (on dismissal)
                // resumes the tail. Interactive only — like `Wait`, a headless
                // run collapses the modal gate (screens.cpp drains the queue
                // synchronously). A lineless widget dialog is not queued by the
                // port, so parking would have no dismissal to resume it; it
                // keeps the eager walk (see the D1 report).
                if (!app.headless() && dlg_queued &&
                    dialog_builds_widget(dlg_type)) {
                    std::fprintf(stdout,
                                 "[quest] chain parked at dialog (%s/%s, %zu tail actions)\n",
                                 dlg_type.c_str(), attr_or(a.attrs, "Title").c_str(),
                                 acts.size() - i - 1);
                    std::fflush(stdout);
                    ActionRest parked;
                    parked.suspended = true;
                    parked.frames = 0;
                    parked.dialog_parked = true;
                    parked.dialog_index = dlg_queue_index;
                    parked.rest.assign(acts.begin() + i + 1, acts.end());
                    return parked;
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
            const std::string target = attr_or(a.attrs, "Target");
            fx.clicks.push_back(target);
            // `Nn` `UseFlashing="1"` (L1114): highlight the target plate. The
            // tutorial pairs it with `IgnoreCallback="1"` so the map FIGHT
            // button is focused/flashed but NEVER launched by the quest
            // (tutorial_quests.xml L156).
            const bool use_flash = attr_bool01(attr_or(a.attrs, "UseFlashing"));
            if (use_flash) fx.flash_targets.push_back(target);
            // `Nn.S` (L1114): `IgnoreCallback` saves + clears the target's own
            // click listeners (`this.I$ = this.xk.pa.ni.slice();
            // this.xk.pa.clear()`), so a press only completes the quest step;
            // WITHOUT it the target's callback stays live and the PLAYER's
            // press dispatches it. The JS never auto-presses (`xk.pa
            // .addListener(Qg)` waits for the click) — the engine only arms.
            if (!attr_bool01(attr_or(a.attrs, "IgnoreCallback"))) {
                fx.click_arm.push_back(target);
            }
        } else if (t == "MenuBtnFlashing") {
            // Desktop navigation guidance (FLOW_STATIC L140-142): the web/
            // else branch of `StoryTutorialOpenScene` shows the notification
            // and flashes the `_NextScene` nav button; the shell never
            // navigates (tutorial_quests.xml L361). `_NextScene` resolves to
            // the global `NextScene` (Shop/Map/Dojo/Profile).
            fx.menu_flashes.push_back(
                quest_var(app, locals, attr_or(a.attrs, "BtnName")));
            // `eo.parse`/`N3a` (L1117): `BtnName` + `za.instance.sxa()` ->
            // `scroll.collapse(0)` (L2001) — the flash first collapses the
            // `za` scroll so the collapsed header carries the pulse; the row
            // flash lands once the player expands it.
            fx.collapse_nav = true;
        } else if (t == "ClickHint") {
            // `Fe.S0a` (L947-953) has NO `EClickHint` case, so `Fe.Ij`
            // (`Nz.hi` L953 matches "ClickHint") falls back to the base `S`
            // (L945 `S.S` = no-op) — the shipped build does NOT render an
            // arrow for it. Recorded (never invents an arrow the JS lacks).
            fx.click_hints.push_back(attr_or(a.attrs, "Target"));
        } else if (t == "SceneMenuScroll") {
            // Same as `ClickHint`: no `ESceneMenuScroll` case in `Fe.S0a`
            // (L947-953) -> base `S` no-op. Recorded only.
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
            // `Ro.S` (L1119): `this.Oqa = ba.UBa(a, Frames)` then a frame
            // listener fires `stop()` after that many frames. `Yb` (L954)
            // serializes the list, so the actions AFTER the Wait must not run
            // until the delay elapses. Interactive: suspend here and let
            // `tick` resume the tail. Headless (no scheduler armed) collapses
            // in order — the tail still runs after this point, never before.
            const std::string raw = attr_or(a.attrs, "Frames", "0");
            const int frames = parse_int_or(quest_var(app, locals, raw), 0);
            if (!app.headless() && frames > 0) {
                ActionRest suspended;
                suspended.suspended = true;
                suspended.frames = frames;
                suspended.rest.assign(acts.begin() + i + 1, acts.end());
                return suspended;
            }
            fx.unknown.push_back("Wait:" + raw + "f (collapsed)");
        } else if (t == "GiveItem") {
            // `Yn.S` (L1238): `Pa.W$a(name, level, qty, putOn, packItem)`.
            // The port's WarriorSave has no `Pa.W$a` inventory-write path and
            // the Name is usually a `?Concat[...]` the query engine does not
            // model -> record-only.
            fx.unknown.push_back("GiveItem (needs Pa.W$a; ?Concat unresolved): " +
                                 attr_or(a.attrs, "Name"));
        } else if (t == "ShowNews") {
            // `xo` (L1246): `S(a){super.S(a); this.sa()}` — the shipped build
            // is a NO-OP. Nothing to execute.
        } else if (t == "ForceExecution") {
            // `Wn.S` (L1205): `ha.F().AD(Name)` + `ha.F().Qaa(q, true)` —
            // re-queue the named quest so it can run again. The port's latch
            // is `fired_`; un-latch the name (a following `Activate`/event
            // pass re-runs it).
            const std::string name = attr_or(a.attrs, "Name");
            if (!name.empty()) {
                std::vector<std::string> keep;
                for (const std::string& f : fired_) {
                    if (f != name) keep.push_back(f);
                }
                fired_.swap(keep);
                std::fprintf(stdout, "[quest] ForceExecution unlatched %s\n", name.c_str());
                std::fflush(stdout);
            }
        } else if (t == "StoryTutorialMove" || t == "StoryTutorialPunchbag" ||
                   t == "StoryTutorialBuyItem" || t == "StoryTutorialLearnPerk" ||
                   t == "StoryTutorialDoubleSweep" || t == "StoryTutorialShowBlock") {
            // `Do`/`Eo`/`Ao`/`Co`/`Bo`/`Fo` (L1242-1247) all forward to the
            // story-tutorial scene hooks (`Oa.ska`, fight move hooks) the
            // shell drives itself; record-only.
            fx.minigames.push_back(t + " (needs fight hooks)");
            // The two lessons `StoryTutorialWelcome` rides on (`Do`/`Eo`, the
            // xml L30/L35 actions BETWEEN the three dialogs) SERIALIZE the
            // chain: the JS arms `Re(Cm, TutorialStepTimeout)` and `Cm` ->
            // `this.sa()` resumes the tail, so the next `Dialog` (the next bar
            // beat / the `Regular` modal) is not created yet. Park the tail on
            // the app clock (see `tutorial_gate_beat`). Only the fresh-profile
            // tutorial path gates; every other harness keeps the eager walk.
            // Depth 0 only: both lessons sit at the top level of
            // `StoryTutorialWelcome`'s `<Actions>`, so the parked tail is
            // always complete (no outer remainder to re-attach).
            if (depth == 0 && app.fresh_tutorial() && !tutorial_gate_.active &&
                (t == "StoryTutorialMove" || t == "StoryTutorialPunchbag")) {
                tutorial_gate_.active = true;
                tutorial_gate_.beat = (t == "StoryTutorialMove") ? 1 : 2;
                tutorial_gate_.remaining = kTutorialStepTimeoutSec;
                tutorial_gate_.rest.assign(acts.begin() + i + 1, acts.end());
                tutorial_gate_.journal = journal;
                tutorial_gate_.locals = locals;
                tutorial_gate_.quest = quest;
                std::fprintf(stdout,
                             "[quest] %s: chain parked %.1fs (beat %d, %zu tail actions)\n",
                             t.c_str(), kTutorialStepTimeoutSec, tutorial_gate_.beat,
                             tutorial_gate_.rest.size());
                std::fflush(stdout);
                ActionRest parked;
                parked.suspended = true;  // the gate clock owns the resume, not `tick`
                parked.frames = 0;
                return parked;
            }
        } else if (t == "Line" || t == "Button" || t == "Then" || t == "Else" ||
                   t == "Conditions") {
            ActionRest sub = run_actions(app, a.children, journal, fx, locals, quest,
                                         depth + 1);
            if (sub.suspended) {
                sub.rest.insert(sub.rest.end(), acts.begin() + i + 1, acts.end());
                return sub;
            }
        } else {
            fx.unknown.push_back(t);
        }
    }
    return result;
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

// --- live action execution ------------------------------------------------
// The JS action classes do their work in `S(a)`; the port collects the
// executable ones in `QuestSideEffects` and performs the navigations here.
// Interactive only — a headless run keeps the record-only behaviour and stays
// deterministic (the task's headless-safe rule).
void QuestEngine::enqueue_effects(App& app, const QuestSideEffects& fx,
                                  const QuestJournal& journal,
                                  const std::map<std::string, std::string>& locals,
                                  const std::string& quest) {
    (void)journal;
    (void)locals;
    (void)quest;
    if (app.headless()) return;
    for (const QuestSceneRequest& s : fx.navigate) nav_queue_.push_back(s);
    for (const QuestShopOpen& s : fx.shop_opens) shop_queue_.push_back(s);
    for (const std::string& t : fx.click_arm) {
        armed_clicks_.push_back(t);
        std::fprintf(stdout, "[quest] ClickButton armed: %s (callback live)\n", t.c_str());
    }
    if (fx.collapse_nav) collapse_nav_pending_ = true;
    std::fflush(stdout);
}

void QuestEngine::resume_run(App& app, PendingRun& run) {
    QuestSideEffects fx;
    const ActionRest rest =
        run_actions(app, run.actions, run.journal, fx, run.locals, run.quest, 0);
    apply_effects(app, fx);
    enqueue_effects(app, fx, run.journal, run.locals, run.quest);
    if (rest.dialog_parked) {
        // D1: the `Wait` tail parked on a widget dialog; the dismissal owns
        // the resume now.
        attach_dialog_park(rest, run.locals);
        run.actions.clear();
    } else if (rest.suspended) {
        run.actions = rest.rest;
        run.frames = rest.frames;
    } else {
        run.actions.clear();
    }
}

// The StoryTutorial lesson gate (see `tutorial_gate_beat` in the header): the
// JS `Do`/`Eo` `Cm` (`sf2.502f0946.js` L1242/L1243) resumes the serialized
// chain (`Yb` L954) once the `TutorialStepTimeout` budget elapses. Runs the
// stashed tail; a second lesson re-parks it (the PunchBag beat).
bool QuestEngine::resume_tutorial_gate(App& app) {
    TutorialGate gate = std::move(tutorial_gate_);
    tutorial_gate_ = TutorialGate{};  // cleared first: the tail may re-park
    QuestSideEffects fx;
    ActionRest rest =
        run_actions(app, gate.rest, gate.journal, fx, gate.locals, gate.quest, 0);
    // D1: the resumed tail may itself park on a widget dialog.
    if (rest.dialog_parked) attach_dialog_park(rest, gate.locals);
    (void)rest;  // a re-arm leaves the parked tail in `tutorial_gate_`
    apply_effects(app, fx);
    enqueue_effects(app, fx, gate.journal, gate.locals, gate.quest);
    return !tutorial_gate_.active;
}

bool QuestEngine::tutorial_gate_tick(App& app, float dt) {
    if (!tutorial_gate_.active) return false;
    if (dt <= 0.0f) return false;
    tutorial_gate_.remaining -= dt;
    if (tutorial_gate_.remaining > 0.0f) return false;
    std::fprintf(stdout, "[quest] tutorial lesson beat %d done -> chain resumes\n",
                 tutorial_gate_.beat);
    std::fflush(stdout);
    return resume_tutorial_gate(app);
}

// `Gn.qIa` (L1032): `mp(a,null,null,CallEvents)` — push the target scene. The
// push is skipped when the target already is the current scene (unless
// `ReopenScene`), and `Fight`(6) routes to the Dojo(3) (`mp(3)`).
void QuestEngine::do_navigate(App& app, const QuestSceneRequest& req) {
    ++scene_actions_;  // `Gn` executed (test hook; not a record)
    std::string name = req.destination;
    if (name.empty()) name = "Dojo";   // `a==null||a=="" ? qIa(3)`
    int id = scene_id_for_name(name);  // `xn.jOa` L1168
    if (id == 11) {
        std::fprintf(stdout, "[quest] ChangeScene '%s': xn.jOa -> 11 (no shell screen)\n",
                     name.c_str());
        std::fflush(stdout);
        return;
    }
    const int cur = app.screens().current_id();
    if (id == 6) id = 3;  // `a==6 ? wa.F().mp(3,...)`
    if (id == cur && !req.reopen) {
        std::fprintf(stdout, "[quest] ChangeScene %s: already current (skip)\n", name.c_str());
        std::fflush(stdout);
        return;
    }
    std::unique_ptr<Screen> scr = make_screen(app.screens(), static_cast<ScreenId>(id));
    if (scr == nullptr) {
        std::fprintf(stdout, "[quest] ChangeScene %s (id %d): no shell screen\n",
                     name.c_str(), id);
        std::fflush(stdout);
        return;
    }
    std::fprintf(stdout, "[quest] ChangeScene %s -> push (id %d)\n", name.c_str(), id);
    std::fflush(stdout);
    armed_clicks_.clear();
    app.screens().push(std::move(scr));
}

// `go.Thb` (L1092) + `Oa.uLa` (L1181866): open the Shop at the `vj.E0` tab and
// select the item (the shell helper owns the shop tab table + catalog).
void QuestEngine::do_open_shop(App& app, const QuestShopOpen& open) {
    ++shop_actions_;  // `go` executed (test hook; not a record)
    std::fprintf(stdout, "[quest] OpenShop tab=%s item=%s -> shop\n", open.tab.c_str(),
                 open.item.c_str());
    std::fflush(stdout);
    armed_clicks_.clear();
    shop_open_at(app, open.tab, open.item);
}

void QuestEngine::tick(App& app) {
    if (app.headless()) {
        // The driver paths never auto-run: drop anything queued so a stale
        // request can never fire later (defensive; enqueue is gated too).
        pending_.clear();
        nav_queue_.clear();
        shop_queue_.clear();
        collapse_nav_pending_ = false;
        return;
    }
    if (!loaded_) return;
    // `Ro` (L1119): resume every run whose frame delay elapsed.
    std::vector<PendingRun> due;
    for (std::size_t i = 0; i < pending_.size();) {
        if (--pending_[i].frames <= 0) {
            due.push_back(std::move(pending_[i]));
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (PendingRun& run : due) resume_run(app, run);
    // `Gn`/`go`/`eo`: perform the queued navigation. Bounded drain — a push
    // fires ChangeTab/SceneLoaded, which may enqueue more work.
    for (int pass = 0; pass < 16; ++pass) {
        if (nav_queue_.empty() && shop_queue_.empty() && !collapse_nav_pending_) break;
        std::vector<QuestSceneRequest> navs;
        navs.swap(nav_queue_);
        std::vector<QuestShopOpen> shops;
        shops.swap(shop_queue_);
        const bool collapse = collapse_nav_pending_;
        collapse_nav_pending_ = false;
        if (collapse) set_za_nav_open(false);  // `eo` L1117 -> `za.sxa()`
        for (const QuestSceneRequest& n : navs) do_navigate(app, n);
        for (const QuestShopOpen& s : shops) do_open_shop(app, s);
    }
}

// JS `ha.GEa` (L521470): is a quest with this name still in the active queue
// (`Dh`)? The port has no explicit `Dh`; the equivalent live state is a queued
// dialog from the quest, a deferred `Wait` run, or the parked StoryTutorial
// gate — all three carry the firing quest name. `Mt` (L521470) removes the
// instance from `Dh` when its run completes, so once none of these reference
// the name the quest is eligible again (exactly the JS re-fire rule).
bool QuestEngine::quest_active(const std::string& name) const {
    if (name.empty()) return false;
    for (const EngineDialog& d : dialogs_) {
        if (d.quest == name) return true;
    }
    for (const PendingRun& r : pending_) {
        if (r.quest == name) return true;
    }
    if (tutorial_gate_.active && tutorial_gate_.quest == name) return true;
    return false;
}

void QuestEngine::fire_inner(App& app, const std::string& event,
                             const QuestJournal& journal,
                             std::vector<std::string>& fired, int depth,
                             const std::string* only) {
    if (depth > kMaxActivateDepth) return;
    // The screen currently mounted (JS `wa.F().Td.Tf`): the Place gate below
    // compares a set's authored scene against it.
    const int cur = app.screens().current_id();
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
        if (only != nullptr && q.name != *only) continue;
        bool listens = false;
        for (const std::string& e : q.events) {
            if (e == event) {
                listens = true;
                break;
            }
        }
        if (!listens) continue;
        if (q.unresumable) {
            // `ha.GEa` (L521470): skip ONLY while this quest is still in the
            // active queue. `Unresumable` (`be.cyb`, L518544) gates the RESUME
            // path (`REa()`), not the fire gate — so the Lynx boss dialog
            // re-fires on the next `SceneLoaded`/Map after a loss (step stays
            // MAP), and is stopped only when `FirstGuardBeaten` writes
            // LEARN_PERK (quests.xml L260-263). A `ClearQuestQueue` name
            // (`fired_`) stays latched.
            if (quest_active(q.name)) continue;
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
        // Place gate (JS `be.Gib` L1007): a set authored for another screen
        // must NOT run here. Park it (JS `Dh` queue) and replay it once that
        // scene is entered (`retry_place_pending`). Only an EXPLICIT Place
        // gates (`q.place != 0`), so an unauthored set keeps the old timing.
        if (q.place != 0 && q.place != cur) {
            bool dup = false;
            for (const PlacePending& p : place_pending_) {
                if (p.quest_index == i) {
                    dup = true;
                    break;
                }
            }
            if (!dup) place_pending_.push_back(PlacePending{i, event, journal});
            std::fprintf(stdout,
                         "[quest] DEFER %s on %s: Place id=%d, current scene id=%d "
                         "(set parked until that screen is entered)\n",
                         q.name.c_str(), event.c_str(), q.place, cur);
            std::fflush(stdout);
            continue;
        }
        // A parked set is replayed by the retry pass, never twice here.
        if (only == nullptr && place_pending_has(i)) continue;
        QuestSideEffects fx;
        std::map<std::string, std::string> locals;  // run-local vars
        const ActionRest rest =
            run_actions(app, q.actions, journal, fx, locals, q.name, 0);
        apply_effects(app, fx);
        // Live actions (`Gn`/`go`/`Nn`/`eo`) + a suspended `Wait` tail.
        enqueue_effects(app, fx, journal, locals, q.name);
        if (rest.dialog_parked) {
            // D1: the chain parked at a widget dialog; `He.gf` L1062 (the
            // dismissal) owns the resume, NOT `tick`.
            attach_dialog_park(rest, locals);
        } else if (rest.suspended) {
            PendingRun run;
            run.actions = rest.rest;
            run.journal = journal;
            run.locals = locals;
            run.quest = q.name;
            run.frames = rest.frames;
            std::fprintf(stdout, "[quest] Wait %d frames -> deferred %zu actions\n",
                         rest.frames, run.actions.size());
            std::fflush(stdout);
            pending_.push_back(std::move(run));
        }
        // Live UI guidance (draw-only, last value wins): the shell reads
        // these each frame instead of the quest auto-acting.
        if (!fx.flash_targets.empty()) flash_target_ = fx.flash_targets.back();
        if (!fx.menu_flashes.empty()) nav_flash_ = fx.menu_flashes.back();
        if (fx.has_map_focus) last_map_focus_ = fx.map_focus;
        // `Unresumable` is NOT a session latch (see the fire gate above):
        // the instance leaves `Dh` when its run ends, so `fired_` now tracks
        // only `ClearQuestQueue` names.
        fired.push_back(q.name);
        std::fprintf(stdout, "[quest] FIRED %s on %s (step=%s scene=%s->%s cur=%d)\n",
                     q.name.c_str(), event.c_str(), ctx.story_step.c_str(),
                     journal.scene_from.c_str(), journal.scene_to.c_str(), cur);
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
    // Observability: the exact triple/result the next `FightEnd` condition
    // reads (`_$Fight`/`_$FightResult`). `FirstGuardBeaten` keys on
    // `ZONE_1|BOSS_LYNX|1` + Win (quests.xml L260-263) -> LEARN_PERK.
    std::fprintf(stdout, "[story] fight recorded: %s -> %s\n", name.c_str(),
                 result.c_str());
    std::fflush(stdout);
}

// `He` pager (L1042-1062). The head dialog's current page caption. `Od.EF`
// L1946 / `Od.X2` L1950 set the primary (`Right`) caption per page:
//   - NON-last page: the current row's `ButtonText` (`ai[oo].KC`, via `DLa`);
//   - LAST page: the explicit right `Text` (`qy`) WINS —
//     `this.Ql=this.qy==""?this.ai[this.oo].KC:this.qy` — and only falls back
//     to the row caption when `qy` is empty.
// The port returned the row caption on EVERY page (the precedence inverted).
std::string QuestEngine::dialog_button_text() const {
    const EngineDialog* dp = modal_top();
    if (dp == nullptr) return std::string();
    const EngineDialog& d = *dp;
    const std::size_t n = d.lines.size();
    // `uj.sqb()` L1953: `Multiline`/`MultilineBig` have no pager, so the plate
    // carries the LAST row's caption (or the authored right `Text`) at once.
    const bool scroll_all = d.type == "Multiline" || d.type == "MultilineBig";
    const std::size_t page =
        n == 0 ? 0 : (scroll_all ? n - 1 : (d.page < n ? d.page : n - 1));
    const bool last = n == 0 || page + 1 >= n;
    if (!last && page < d.line_buttons.size() && !d.line_buttons[page].empty()) {
        return d.line_buttons[page];  // `DLa(ai[oo].KC)`
    }
    // LAST page: `qy` (the authored right `Text`) wins, else the row caption.
    // `button_text` already carries the authored text or the last row's
    // `ButtonText` (the parse-time fallback), so this matches
    // `qy==""?ai[oo].KC:qy`. A non-last page whose row caption is empty keeps
    // the fallback so the pager plate stays visible (the port draws the plate
    // only when the caption is non-empty).
    if (!d.button_text.empty()) return d.button_text;
    if (page < d.line_buttons.size()) return d.line_buttons[page];
    return std::string();
}

bool QuestEngine::dialog_has_next_page() const {
    const EngineDialog* d = modal_top();
    if (d == nullptr) return false;
    // `uj.sqb()` L1953 (`Multiline`/`MultilineBig`) lays out EVERY `<Line>` as
    // one scrollable body, so there is NO pager: `Od.EF`/`Od.X2` L1946/L1950
    // never run and the plate carries the last-page caption straight away.
    if (d->type == "Multiline" || d->type == "MultilineBig") return false;
    return d->page + 1 < d->lines.size();
}

void QuestEngine::advance_dialog_page() {
    const std::size_t mi = modal_index();
    if (mi >= dialogs_.size()) return;
    EngineDialog& d = dialogs_[mi];
    if (d.page + 1 >= d.lines.size()) return;
    ++d.page;
    std::fprintf(stdout, "[quest] dialog page -> %zu/%zu (%s, more=%d)\n", d.page + 1,
                 d.lines.size(), dialog_button_text().c_str(),
                 dialog_has_next_page() ? 1 : 0);
    std::fflush(stdout);
}

// D1 `He.gf` L1062 (`gf(){Ib.RP=!1;this.sa()}`): resume the chain parked at a
// dismissed dialog (`dlg.continuation`, `Yb` L954).
void QuestEngine::resume_dialog_chain(App& app, EngineDialog& dlg,
                                      std::vector<std::string>* fights_out) {
    if (dlg.continuation.empty()) return;
    std::map<std::string, std::string> locals = dlg.continuation_locals;
    std::fprintf(stdout, "[quest] dialog dismissed -> resume chain (%zu actions)\n",
                 dlg.continuation.size());
    std::fflush(stdout);
    run_chain_effects(app, dlg.continuation, dlg.journal, locals, dlg.quest,
                      std::vector<QuestAction>(), fights_out);
}

void QuestEngine::dismiss_dialog(App& app) {
    // `He.dhb(0)` with no `Ng` slot (L1061) / `He.gf` L1062: pop the top modal
    // and resume its parked chain.
    const std::size_t mi = modal_index();
    if (mi >= dialogs_.size()) return;
    EngineDialog dlg = std::move(dialogs_[mi]);
    dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(mi));
    resume_dialog_chain(app, dlg, nullptr);
}

void QuestEngine::attach_dialog_park(
    const ActionRest& rest, const std::map<std::string, std::string>& locals) {
    if (!rest.dialog_parked || rest.dialog_index >= dialogs_.size()) return;
    dialogs_[rest.dialog_index].continuation = rest.rest;
    dialogs_[rest.dialog_index].continuation_locals = locals;
    std::fprintf(stdout, "[quest] chain parked -> dialog owns %zu tail actions\n",
                 rest.rest.size());
    std::fflush(stdout);
}

QuestEngine::ActionRest QuestEngine::run_chain_effects(
    App& app, const std::vector<QuestAction>& acts, const QuestJournal& journal,
    std::map<std::string, std::string>& locals, const std::string& quest,
    const std::vector<QuestAction>& outer, std::vector<std::string>* fights_out) {
    QuestSideEffects fx;
    ActionRest rest = run_actions(app, acts, journal, fx, locals, quest, 0);
    // `Yb` (L954) composition: a park stores `rest ++ outer` (finish this
    // list, then resume the ENCLOSING one); a `Wait` defers the same
    // composition to `tick`.
    if (rest.dialog_parked || rest.suspended) {
        if (!outer.empty()) {
            rest.rest.insert(rest.rest.end(), outer.begin(), outer.end());
        }
        if (rest.dialog_parked) {
            attach_dialog_park(rest, locals);
        } else {
            PendingRun run;
            run.actions = rest.rest;
            run.journal = journal;
            run.locals = locals;
            run.quest = quest;
            run.frames = rest.frames;
            std::fprintf(stdout, "[quest] Wait %d frames -> deferred %zu actions\n",
                         run.frames, run.actions.size());
            std::fflush(stdout);
            pending_.push_back(std::move(run));
        }
    }
    apply_effects(app, fx);
    enqueue_effects(app, fx, journal, locals, quest);
    // Live UI guidance (draw-only, last value wins) — the same signals
    // `fire_inner` publishes for a chain that runs there.
    if (!fx.flash_targets.empty()) flash_target_ = fx.flash_targets.back();
    if (!fx.menu_flashes.empty()) nav_flash_ = fx.menu_flashes.back();
    if (fx.has_map_focus) last_map_focus_ = fx.map_focus;
    if (fights_out != nullptr) {
        for (const std::string& f : fx.fight_requests) fights_out->push_back(f);
    }
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
    if (!fx.attach_files.empty()) {
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
        load_ctx_ = c;
        for (const std::string& f : fx.attach_files) load_quest_file(app, f);
    }
    // Chained `Activate` (`Ge` L1024): re-fire with `Ge.MZ` = ActionID.
    for (const std::string& u : fx.activate_requests) {
        QuestJournal j2 = journal;
        j2.action_id = u;
        fire(app, "Activate", j2);
    }
    return rest;
}

std::vector<std::string> QuestEngine::press_dialog(App& app, int button_index) {
    std::vector<std::string> fights;
    // `Wb` pops its TOP dialog (`Xc`/`dhb`); a bar Notification is not in that
    // queue (`He.S` L1050 -> `Ib.F().Qhb`), so pop the first non-Notification.
    const std::size_t mi = modal_index();
    if (mi >= dialogs_.size()) return fights;
    EngineDialog dlg = std::move(dialogs_[mi]);
    dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(mi));
    // `He.dhb(a)` L1061: 0=Left(`Ng`), 1=Right(`rh`), 2=Middle(`Nh`),
    // 100=Close(`Hj`). Anything else fires nothing (`dhb` falls through).
    const std::vector<QuestAction>* chosen = &dlg.button_actions;
    const char* slot = "Right";
    if (button_index == 0) {
        chosen = &dlg.left_.actions;
        slot = "Left";
    } else if (button_index == 2) {
        chosen = &dlg.middle_.actions;
        slot = "Middle";
    } else if (button_index == 100) {
        chosen = &dlg.close_.actions;
        slot = "Close";
    }
    std::fprintf(stdout, "[quest] dialog button pressed: %s (%s, %s)\n", dlg.title.c_str(),
                 dlg.button_text.c_str(), slot);
    std::fflush(stdout);
    std::map<std::string, std::string> locals;
    // JS `Yb` (L954) runs the outer quest chain STRICTLY SEQUENTIALLY and
    // SUSPENDS it at a widget dialog (`He.S` L1051 -> `Wb.Xob` L927); a
    // button's nested actions are a SEPARATE sub-`Yb` (`He.Rib` L1057-1058
    // installs `g.actions`, fired by `dhb(0)` L1061 as `this.Ng.actions.S`).
    // `He.gf` L1062 (`this.sa()`) resumes the OUTER chain only once the nested
    // sub-`Yb` COMPLETES. `dlg.continuation` is that parked outer chain.
    const std::size_t queue_before = dialogs_.size();
    {
        const ActionRest rest = run_chain_effects(
            app, *chosen, dlg.journal, locals, dlg.quest, dlg.continuation,
            &fights);
        // The nested list completed (no park, no `Wait`): `He.gf` L1062 ->
        // `this.sa()` -> the outer chain resumes now.
        if (!rest.suspended && !dlg.continuation.empty()) {
            run_chain_effects(app, dlg.continuation, dlg.journal,
                              dlg.continuation_locals, dlg.quest,
                              std::vector<QuestAction>(), &fights);
        }
    }
    // Splice the dialogs this press queued (the nested list's, then the
    // resumed tail's) back to the FRONT of the `Wb` queue, where the pressed
    // dialog's slot opened.
    // Cite: quests.xml L859-872 (`FirstGuardBeaten`) — hello -> (refuse)
    // `tutorial_girl_please` -> `tutorial_girl_end` -> tournament.
    if (dialogs_.size() > queue_before) {
        std::vector<EngineDialog> nested;
        nested.reserve(dialogs_.size() - queue_before);
        for (std::size_t i = queue_before; i < dialogs_.size(); ++i) {
            nested.push_back(std::move(dialogs_[i]));
        }
        dialogs_.resize(queue_before);
        const std::size_t at = std::min(mi, dialogs_.size());
        dialogs_.insert(dialogs_.begin() + static_cast<std::ptrdiff_t>(at),
                        std::make_move_iterator(nested.begin()),
                        std::make_move_iterator(nested.end()));
    }
    if (!fights.empty()) {
        // Observability for the story/fight handshake: the dialog plate that
        // carries `<Fight>` (the Lynx `StoryTutorialBossFight`, tutorial_
        // quests.xml L147-151) started a real fight.
        std::fprintf(stdout, "[story] dialog '%s' (%s) -> launch fight '%s'\n",
                     dlg.title.c_str(), dlg.quest.c_str(), fights.front().c_str());
        std::fflush(stdout);
    }
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
    // Parked Place sets whose screen is now mounted (JS `RA`/`qT` pump).
    retry_place_pending(app, fired);
    return fired;
}

// JS `be.Gib` (L1007) gate retry: replay every parked set whose authored
// scene is the CURRENT screen (`wa.F().Td.Tf`), with the journal captured at
// the original match (the JS `Dh` pump runs `this.ta` as of `RA`). Runs after
// the normal pass, so a quest that fires normally never double-fires (it is
// skipped by `place_pending_has` while parked).
void QuestEngine::retry_place_pending(App& app, std::vector<std::string>& fired) {
    if (place_pending_.empty()) return;
    const int cur = app.screens().current_id();
    for (std::size_t i = 0; i < place_pending_.size();) {
        if (place_pending_[i].quest_index >= quests_.size() ||
            quests_[place_pending_[i].quest_index].place != cur) {
            ++i;
            continue;
        }
        const PlacePending p = place_pending_[i];
        place_pending_.erase(place_pending_.begin() + static_cast<std::ptrdiff_t>(i));
        const std::string only = quests_[p.quest_index].name;
        fire_inner(app, p.event, p.journal, fired, 0, &only);
    }
}

bool QuestEngine::place_pending_has(std::size_t quest_index) const {
    for (const PlacePending& p : place_pending_) {
        if (p.quest_index == quest_index) return true;
    }
    return false;
}

QuestEngine& App::quest_engine() {
    if (!quest_engine_) quest_engine_ = std::make_unique<QuestEngine>();
    return *quest_engine_;
}

// --- `--dialog-verify` slot census (see the header) ------------------------
namespace {

void census_walk(const pugi::xml_node& node, QuestButtonCensus& out) {
    for (pugi::xml_node ch = node.first_child(); ch; ch = ch.next_sibling()) {
        if (ch.type() == pugi::node_element) {
            const std::string tag = ch.name();
            if (tag == "Dialog") {
                ++out.dialogs;
                // `He` L1043: an absent `Type` defaults to "Regular".
                const std::string ty = ch.attribute("Type").value();
                ++out.types[ty.empty() ? "Regular" : ty];
            } else if (tag == "Button") {
                const std::string ty = ch.attribute("Type").value();
                if (ty.empty() || ty == "Right") {
                    ++out.right;
                } else if (ty == "Left") {
                    ++out.left;
                } else if (ty == "Middle") {
                    ++out.middle;
                } else if (ty == "Close") {
                    ++out.close;
                }
            }
        }
        census_walk(ch, out);
    }
}

void census_file(const std::filesystem::path& p, QuestButtonCensus& out) {
    const std::string xml = read_file_text(p.string());
    if (xml.empty()) return;
    sf2::data::xml_doc doc;
    doc.parse(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
    const pugi::xml_node root = doc.root();
    if (!root) return;
    QuestButtonCensus local;
    census_walk(root, local);
    // Raw text census (INCLUDING commented-out XML) — the naive inventory.
    for (std::size_t at = 0; (at = xml.find("<Button", at)) != std::string::npos;) {
        const std::size_t gt = xml.find('>', at);
        const std::size_t end = gt == std::string::npos ? xml.size() : gt;
        const std::string tag = xml.substr(at, end - at);
        std::string ty;
        const std::size_t tp = tag.find("Type=\"");
        if (tp != std::string::npos) {
            const std::size_t s = tp + 6;
            const std::size_t q = tag.find('"', s);
            if (q != std::string::npos) ty = tag.substr(s, q - s);
        }
        if (ty.empty() || ty == "Right") {
            ++local.text_right;
        } else if (ty == "Left") {
            ++local.text_left;
        } else if (ty == "Middle") {
            ++local.text_middle;
        } else if (ty == "Close") {
            ++local.text_close;
        }
        at = end;
    }
    for (std::size_t at = 0; (at = xml.find("<Dialog", at)) != std::string::npos;) {
        const std::size_t nx = at + 7;  // exact element name (not <Dialogs>)
        if (nx < xml.size() && xml[nx] != '>' && xml[nx] != ' ' && xml[nx] != '/') {
            at = nx;
            continue;
        }
        ++local.text_dialogs;
        at = nx;
    }
    std::fprintf(stdout,
                 "[dlgverify] census %-40s dlg=%-4zu R=%-4zu L=%-4zu M=%-3zu C=%zu (bytes=%zu)\n",
                 (p.parent_path().filename() / p.filename()).string().c_str(), local.dialogs,
                 local.right, local.left, local.middle, local.close, xml.size());
    std::fflush(stdout);
    ++out.files;
    out.dialogs += local.dialogs;
    out.right += local.right;
    out.left += local.left;
    out.middle += local.middle;
    out.close += local.close;
    out.text_dialogs += local.text_dialogs;
    out.text_right += local.text_right;
    out.text_left += local.text_left;
    out.text_middle += local.text_middle;
    out.text_close += local.text_close;
    for (const auto& kv : local.types) out.types[kv.first] += kv.second;
}

} // namespace

QuestButtonCensus census_quest_tree() {
    QuestButtonCensus out;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(kQuestResRoot);
    const fs::path q = root / "quests.xml";
    if (fs::exists(q, ec)) census_file(q, out);
    const fs::path ext = root / "quest_extensions";
    if (fs::is_directory(ext, ec)) {
        for (fs::recursive_directory_iterator it(ext, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".xml") continue;
            census_file(it->path(), out);
        }
    }
    // The `<Dialog Type=...>` distribution (`He.S` L1045-1051). Sorted by
    // descending count so the report reads like the routing table.
    std::vector<std::pair<std::string, std::size_t>> dist(out.types.begin(),
                                                          out.types.end());
    std::sort(dist.begin(), dist.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    std::fprintf(stdout, "[dlgverify] dialog Type distribution (%zu parsed elements):\n",
                 dist.empty() ? 0 : out.dialogs);
    for (const auto& kv : dist) {
        // The `He.S` routing for each Type (see screens.cpp `dialog_kind`).
        const char* route = "";
        if (kv.first == "Regular") route = "-> Xc.Xhb -> 280 Od";
        else if (kv.first == "Stranger") route = "-> Xc.Bia -> 290 uj";
        else if (kv.first == "Multiline") route = "-> Xc.Bia -> 290 uj (all lines)";
        else if (kv.first == "MultilineBig") route = "-> Xc.Nhb -> 290 uj (all lines)";
        else if (kv.first == "NoAvatar") route = "-> Xc.rIa -> 340 Ve";
        else if (kv.first == "ShowLoot") route = "-> Xc.Uhb -> 370 vn";
        else if (kv.first == "Notification") route = "-> Ib bar";
        else route = "-> JS `debugger`: NO renderer (auto-advance)";
        std::fprintf(stdout, "[dlgverify]   %-16s %4zu  %s\n", kv.first.c_str(), kv.second,
                     route);
    }
    std::fflush(stdout);
    return out;
}

} // namespace sf2::app
