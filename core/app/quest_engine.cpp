// Quest engine core — see quest_engine.hpp for the spec/notes.
//
// Data paths (existing patterns, read-only): the real quest tree root at
// `reference/extracted/xml/res/quests.xml` (the same extracted-res source
// stages.xml/list.xml resolve from in screens.cpp); battle→zone index from
// `reference/extracted/xml/res/stages.xml` (like load_zone_map).

#include "app/quest_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "app/app.hpp"
#include "app/lang_table.hpp"
#include "app/save_system.hpp"
#include "app/screens.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

namespace {

constexpr int kMaxActivateDepth = 4;
constexpr int kMaxActionDepth = 6;

// `bb.OE`/`bb.M3` (L887-894) expand a `<Rules>` child into leaves for the
// `?Fight.*` equipment/currency queries (`cJ`/`bJ` L727592/L727725,
// `dEa`/`I3a` L727855/L626221). Mirrors `scene::modes_detail::
// append_rule_element` (Level stamp, ComplexRule flatten, RandomRule group)
// but stores only the fields those handlers read.
void collect_fight_rule(const pugi::xml_node& el, int power_lo, int power_hi,
                        int& next_group, int random_group,
                        std::vector<FightRule>& out) {
    const std::string tag = el.name();
    if (tag == "Level") {
        const int lo =
            el.attribute("Min") ? std::atoi(el.attribute("Min").value()) : 0;
        const int hi = el.attribute("Max")
                           ? std::atoi(el.attribute("Max").value())
                           : 2147483647;
        for (const pugi::xml_node c : el.children())
            collect_fight_rule(c, lo, hi, next_group, random_group, out);
        return;
    }
    if (tag == "ComplexRule") {
        for (const pugi::xml_node c : el.children())
            collect_fight_rule(c, power_lo, power_hi, next_group, random_group, out);
        return;
    }
    if (tag == "RandomRule") {
        const int gid = next_group++;
        for (const pugi::xml_node c : el.children())
            collect_fight_rule(c, power_lo, power_hi, next_group, gid, out);
        return;
    }
    FightRule rule;
    rule.tag = tag;
    for (const pugi::xml_attribute a : el.attributes())
        rule.attrs[a.name()] = a.value();
    rule.power_min = power_lo;
    rule.power_max = power_hi;
    rule.random_group = random_group;
    out.push_back(std::move(rule));
}

constexpr const char* kQuestResRoot = "reference/extracted/xml/res/";

// --- RealPrice markup (JS `aa` L1217616-1217700) -------------------------
// `aa.Z6` L1280957 = "+-*^(){}|/&#%=! "; `aa.pM` L1280988 = `Z6+"?.$,:;"`.
// `Ela(a,b)` = `yxa(Dub(a),b)`; `Dub` escapes every `pM` char with `@`;
// `yxa` collapses each `@X` (X in `pM`) into ONE codepoint `10240+g`
// (`n0a` L1217655 = `String.fromCodePoint(10240+a)`). Chars outside `pM`
// (and already-`@`-escaped pairs) pass through. The JS `b.Cq+=b.G` glyph
// count drives layout only; the query result is the string alone.
constexpr int kMarkupGlyphBase = 10240;
const char* const kMarkupZ6 = "+-*^(){}|/&#%=! ";

const std::string& markup_pm() {
    static const std::string pm = std::string(kMarkupZ6) + "?.$,:;";
    return pm;
}

// `Kg` L3847: true when any char is a digit; called on `charAt(0)`.
bool js_any_digit(const std::string& s) {
    for (char c : s) {
        if (c > 47 && c < 58) return true;
    }
    return false;
}

// `aa.Dub` L1217628.
std::string markup_dub(const std::string& a) {
    const std::string& pm = markup_pm();
    std::string r;
    r.reserve(a.size() * 2);
    char prev = '\\0';
    bool have_prev = false;
    for (char e : a) {
        const bool skip = (e == '@') || (have_prev && prev == '@') ||
                          (pm.find(e) == std::string::npos);
        if (!skip) r.push_back('@');
        r.push_back(e);
        prev = e;
        have_prev = true;
    }
    return r;
}

// `aa.yxa` L1217655.
std::string markup_yxa(const std::string& a) {
    if (a.find('@') == std::string::npos) return a;
    const std::string& pm = markup_pm();
    std::string r;
    std::size_t d = 0;
    while (d + 1 < a.size()) {
        const char e = a[d];
        const char f = a[d + 1];
        if (e == '@') {
            const std::size_t g = pm.find(f);
            if (g != std::string::npos) {
                const unsigned cp = unsigned(kMarkupGlyphBase + g);
                r.push_back(char(0xE0 | (cp >> 12)));
                r.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                r.push_back(char(0x80 | (cp & 0x3F)));
            } else {
                r.push_back(e);
                r.push_back(f);
            }
            d += 2;
            if (d == a.size() - 1) {
                r.push_back(a[d]);
                break;
            }
        } else {
            r.push_back(e);
            ++d;
            if (d == a.size() - 1) {
                r.push_back(f);
                break;
            }
        }
    }
    return r;
}

// `aa.Ela` L1217616 plus the shared RealPrice body (`cdb` L978 / `wfb` L992).
std::string real_price_text(const std::string& xr) {
    std::string a = xr;
    if (!a.empty() && js_any_digit(a.substr(0, 1))) a = " " + a;
    return markup_yxa(markup_dub(a));
}

// `p.F().Df` (L90916 `Wab`, 21 rows) + `b0`/`rAa` (L90727). The stages
// `<Battle Type>` is the KEY; `pkb` L719570 sets `type = b0(key)`
// (key -> FightXxx) and `?Fight.Type`/`?Battle.Type` read it back with
// `rAa(type)` (value -> key). The round trip is the key when it is one of
// the 21 shipped rows, else DUMMY (`b0` defaults to FightNone, `rAa` to
// DUMMY).
const char* fight_type_label(const std::string& key) {
    static const char* const kKeys[] = {
        "DUMMY", "TUTORIAL", "CHALLENGE", "BOSSES", "TOURNAMENT", "STORY",
        "SURVIVAL", "TACTICS", "AUTO", "AI", "HIDDEN", "FAKE", "PVP",
        "PERIODIC", "FINAL_BATTLE", "FINAL_BATTLE_REPLAYABLE",
        "BOSSES_INTERMISSION", "REPLAYABLE", "BOSSES_REPLAYABLE",
        "FINAL_BATTLE_TITAN", "RAID"};
    for (const char* k : kKeys) {
        if (key == k) return k;
    }
    return "DUMMY";
}


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
    } else {
        // JS `S.parse` keeps the whole action subtree (`$n`/EGivePerk clones
        // the node via `a.st().clone()`, L555926 `parse`). The port only
        // structured If/Dialog/Button before, so a data-bearing action such as
        // `<GivePerk><Perk Name Level UpgradeLevel/></GivePerk>` lost its
        // children. Capture the remaining element children generically; only
        // the Line/Button/Then/Else/Conditions dispatch in `run_actions`
        // executes `children`, so this adds data without changing others.
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

// `_Name` quest-variable reference (JS `to` + the `_`-prefixed resolver) is
// now a `QuestEngine` member (`quest_var`), so it can read the session map
// `global_vars_` (JS `p.o.AG`) with the JS `f5a` precedence.

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

// `vj.E0` (L1168): tab NAME -> index (0 = "Default", the JS default).
int QuestEngine::tab_index_for_name(const std::string& name) {
    if (name == "Weapon") return 1;
    if (name == "Armor") return 2;
    if (name == "Helm") return 3;
    if (name == "Ranged") return 4;
    if (name == "Magic") return 5;
    if (name == "Ruby") return 6;
    if (name == "Free") return 7;
    if (name == "RaidConsumable") return 8;
    if (name == "Cheat") return 9;
    if (name == "Perks") return 10;
    if (name == "Moves") return 11;
    if (name == "Achievements") return 12;
    if (name == "QuestItems") return 13;
    if (name == "Count") return 14;
    if (name == "BattlePass") return 15;
    if (name == "StoryMapStage") return 16;
    if (name == "RaidMapStage") return 17;
    return 0;
}

// `uh.getName` (L1169): index -> tab NAME ("Default" when unknown).
std::string QuestEngine::tab_name_for_index(int index) {
    switch (index) {
        case 1: return "Weapon";
        case 2: return "Armor";
        case 3: return "Helm";
        case 4: return "Ranged";
        case 5: return "Magic";
        case 6: return "Ruby";
        case 7: return "Free";
        case 8: return "RaidConsumable";
        case 9: return "Cheat";
        case 10: return "Perks";
        case 11: return "Moves";
        case 12: return "Achievements";
        case 13: return "QuestItems";
        case 14: return "Count";
        case 15: return "BattlePass";
        case 16: return "StoryMapStage";
        case 17: return "RaidMapStage";
        default: return "Default";
    }
}

// `vj.ifa` (L1169): tab index -> owning screen id (11 = the JS default).
int QuestEngine::tab_screen_for_index(int index) {
    switch (index) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: return 4;
        case 10: case 11: case 12: case 13: case 15: return 7;
        case 16: case 17: return 5;
        default: return 11;
    }
}

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
    QuestDef def;
    def.name = q.attribute("Name").value();
    def.file = file;  // JS `be.fileName`; `Ln.iLa` L531194 writes it as `K_`.
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
                            // `pkb` L719570: `this.type = p.F().b0(<Battle
                            // Type>)`; the query maps it back via `rAa`.
                            battle_type_[bname] = b.attribute("Type").value();
                            // `IIa` L98652: `a.Nn = u.I(get("ReplayInterval"))`
                            // per `<Fight>` (JS default 0 — absent attr ->
                            // `parseInt(null)` -> 0). Keyed by the `hb` triple
                            // `Zone|Battle|Fight-Name` for `?Fight.*.TimeLeft`.
                            for (pugi::xml_node f : b.children("Fight")) {
                                const std::string fname = f.attribute("Name").value();
                                if (fname.empty()) continue;
                                const int nn =
                                    f.attribute("ReplayInterval")
                                        ? std::atoi(f.attribute("ReplayInterval").value())
                                        : 0;
                                fight_replay_interval_[zname + "|" + bname + "|" + fname] = nn;
                                // `IIa` L98652 `a.d4=u.I(get("Power"),1)`.
                                const int pw = f.attribute("Power")
                                                   ? std::atoi(f.attribute("Power").value())
                                                   : 1;
                                fight_power_[zname + "|" + bname + "|" + fname] = pw;
                                // The fight's `<Rules>` + `<Rewards>` for the
                                // `?Fight.*` equipment (`cJ`/`bJ`) and
                                // currency (`dEa`/`I3a`) queries, and the
                                // `?Fight.Money`/`?Fight.Bonus` reward rows
                                // (`wi`, `Wjb` L105391).
                                {
                                    const std::string ftriple =
                                        zname + "|" + bname + "|" + fname;
                                    int next_group = 0;
                                    for (const pugi::xml_node r :
                                         f.child("Rules").children())
                                        collect_fight_rule(r, 0, 2147483647,
                                                           next_group, -1,
                                                           fight_rules_[ftriple]);
                                    for (const pugi::xml_node rr :
                                         f.child("Rewards").children("Reward")) {
                                        FightReward fr;
                                        fr.money =
                                            rr.attribute("Money")
                                                ? std::atoi(rr.attribute("Money").value())
                                                : 0;
                                        fr.bonus =
                                            rr.attribute("Bonus")
                                                ? std::atoi(rr.attribute("Bonus").value())
                                                : 0;
                                        fight_rewards_[ftriple].push_back(fr);
                                    }
                                }
                                // `?Fight.Description`: `GD()` L727376 =
                                // `g8!=null&&g8!=""?g8:Sb`. `Sb` = the
                                // `<Fight Description>` attr (`IIa` L98560
                                // `jla`). `g8` = `o7a()` (L731629), the first
                                // level-gated top-level rule whose `o0()`
                                // (L731530) text is non-empty. Statically only
                                // a direct `<Description Alias>` child resolves
                                // (`Cg` = the Alias attr, L437628); a
                                // `<RandomRule>` resolves through its RUNTIME
                                // pick (`ERuleRandom.CB`), absent from the
                                // static document.
                                std::string desc;
                                if (f.attribute("Description"))
                                    desc = f.attribute("Description").value();
                                for (pugi::xml_node r : f.child("Rules").children()) {
                                    if (std::string(r.name()) != "Description") continue;
                                    const char* al =
                                        r.attribute("Alias") ? r.attribute("Alias").value() : "";
                                    if (*al) {
                                        desc = al;
                                        break;
                                    }
                                }
                                fight_description_[zname + "|" + bname + "|" + fname] = desc;
                            }
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

// `p.items.$b(name)` (the list.xml catalog, JS `it` g="5A"): cached because
// the catalog is static for the process. Returns null when the name is absent.
const CatalogItem* QuestEngine::catalog_find(App& app, const std::string& name) const {
    if (name.empty()) return nullptr;
    if (!catalog_ready_) {
        try {
            catalog_cache_ = load_full_catalog(app);
        } catch (const std::exception&) {
            catalog_cache_.clear();
        }
        catalog_ready_ = true;
    }
    for (const CatalogItem& ci : catalog_cache_) {
        if (ci.name == name) return &ci;
    }
    return nullptr;
}

// `p.iMa` L112419 -> `p.items.Jrb`/`hnb` L167. `Jrb(a,b)` walks every catalog
// item whose `lock` (`pL` L322 = PackLabel) equals `a` and equips it (`eMa`
// L167 -> `Ir(true)` L322 `this.yj=a`); `hnb(a)` unequips the same set
// (`Ir(false)`). The port stores the equip as the type slot (weapon/armor/
// helm/ranged/magic) plus the `OwnedItem::equipped` flag — the identical write
// the shop's `Pa.iwa` + `$o` (`ZYa` L2251) path performs, so the shop, profile
// and loadout read it back.
void QuestEngine::apply_toggle_items(App& app, const std::string& label, bool on) {
    if (label.empty()) return;
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return;
    }
    // `p.iMa(a,b)` L112419: `if (b ? p.o.vq(a,!0) : p.o.tnb(a)) b ?
    // p.items.Jrb(a,p.o.bb()) : p.items.hnb(a)`. The FIRST half is the shop
    // lock write — `vq` L267 (`<Shop><Lock Name=a>` + `R$.add`) or `tnb` L267
    // (remove the `<Lock>` + `R$` entry). When the label is ALREADY in that
    // state the toggle is false and the equip/unequip half is SKIPPED
    // (JS-exact: `iMa` only calls `Jrb`/`hnb` on a successful toggle).
    const bool toggled = on ? w.shop_lock_add(label) : w.shop_lock_remove(label);
    std::size_t n = 0;
    if (toggled) {
        for (WarriorSave::OwnedItem& oi : w.items) {
            const CatalogItem* ci = catalog_find(app, oi.name);
            if (ci == nullptr || ci->pack_label != label) continue;
            if (!on) {
                oi.equipped = false;
                ++n;
                continue;
            }
            if (ci->type == "Armor") w.armor = ci->name;
            else if (ci->type == "Helm") w.helm = ci->name;
            else if (ci->type == "Ranged") w.ranged = ci->name;
            else if (ci->type == "Magic") w.magic = ci->name;
            else if (ci->type == "Weapon") w.weapon = ci->name;
            oi.equipped = true;
            ++n;
        }
    }
    if (toggled || n > 0) {
        try {
            app.save().save(w);
        } catch (const std::exception&) {
        }
    }
    std::fprintf(stdout,
                 "[quest] ToggleItems %s=%s -> lock %s, %zu owned item(s) %s\n",
                 label.c_str(), on ? "on" : "off",
                 toggled ? (on ? "granted" : "cleared") : "unchanged", n,
                 on ? "equipped (Jrb)" : "unequipped (hnb)");
    std::fflush(stdout);
}

// `Pn.S` L1064: `l = base.Ofa() * ((100 - e.G) / 100)`; `g=new yf(n,
// "#internalQuest#", a, g.G, k.G); g.KA=l; g.TP=K.T(e)` then
// `p.o.xa.<item>.Gp = g` + `p.o.xa.vu()` (L301). `Toggle="0"` takes the
// `f.G<0 -> b.G.E4()` branch (clear the offer).
void QuestEngine::apply_discount(App& app, const std::string& item, int percent,
                                 bool on, long long period, bool sale, int count,
                                 int new_amount, const std::string& new_price) {
    if (item.empty()) return;
    // `Pn.S` L1065: `if(c.G)` (the resolved `Toggle` > 0) gates the whole
    // write — `Percent` only gates the `KA` override (`e.G>0 &&`), never the
    // offer's creation. The `Toggle="0"` tail is
    // `f.G<0 ? b.G.E4() : b.G.ynb(f.G)`: `E4` clears the item's `Gp`; `ynb`
    // (L170xxx `X.remove(this.lB, a)`) removes the UPGRADE offer at `count`.
    if (!on) {
        if (count >= 0) {
            const auto it = upgrade_offers_.find(item);
            if (it != upgrade_offers_.end()) {
                it->second.erase(count);
                if (it->second.empty()) upgrade_offers_.erase(it);
            }
            std::fprintf(stdout,
                         "[quest] Discount %s|%d toggle=0 -> upgrade offer removed (ynb)\n",
                         item.c_str(), count);
            std::fflush(stdout);
            return;
        }
        offers_.erase(item);
        std::fprintf(stdout, "[quest] Discount %s toggle=0 -> offer cleared (E4)\n",
                     item.c_str());
        std::fflush(stdout);
        return;
    }
    const CatalogItem* ci = catalog_find(app, item);
    const std::int64_t base = ci != nullptr ? ci->price : 0;
    EngineItemOffer o;
    o.item = item;
    o.percent = percent;
    // `f.V4 = d.G` (the resolved `Sale` > 0) and `a = h.G>0 ? p.Dc + h.G + tz
    // : 0` (the `yf.yn` end time; `tz` = `trunc(ed.getTimezoneOffset())` = 0,
    // L2204).
    o.sale = sale;
    o.end_time = period > 0 ? static_cast<long long>(now_seconds()) + period : 0;
    o.active = true;  // `yf.fE` (written `h.G>0`, never read in the bundle)
    // `new yf(og, Q2a, yn, g.G, k.G)` (L1065/1066): `yf.Aw = NewAmount`,
    // `yf.KA` initial = `"" + NewPrice`.
    o.new_amount = new_amount;
    o.new_price = new_price;
    // `Pn.S` L1065 `|count` UPGRADE branch:
    //   `if(X.Xa(b.G.lB,f.G) && b.G.JQ(f.G)!=null){ ... b.G.lB.set(f.G,g) }`
    // `X.Xa` = `Map.has` (class `X` L52808 `static Xa(a,b){return a.has(b)}`).
    // `lB` is written ONLY by this branch, so on a fresh item it is always
    // empty -> the branch is a NO-OP (JS-exact; the shipped
    // `dynamic_discounts.xml` L261 `Item="_DiscountItem|100*Level"` therefore
    // never sets an upgrade offer — `UpgradeDiscountWrapper` L248 has no
    // effect). The port reproduces the guard verbatim rather than inventing a
    // positive path. No base offer is created for a `|count` form (the JS
    // never touches `Gp` here).
    if (count >= 0) {
        const auto it = upgrade_offers_.find(item);
        const bool has = it != upgrade_offers_.end() && it->second.count(count) != 0;
        if (has) {
            // `l = b.G.JQ(f.G).Ofa() * ((100-e.G)/100)` — the upgrade entry's
            // own `Ofa()` (the item's price at that level).
            const std::int64_t ub = it->second[count].price;
            EngineItemOffer u = o;
            u.price = percent > 0
                          ? static_cast<std::int64_t>(std::trunc(
                                static_cast<double>(ub) * (100.0 - percent) / 100.0))
                          : ub;
            upgrade_offers_[item][count] = u;
            std::fprintf(stdout,
                         "[quest] Discount %s|%d percent=%d -> upgrade offer price %lld "
                         "(lB.set)\n",
                         item.c_str(), count, percent, static_cast<long long>(u.price));
        } else {
            std::fprintf(stdout,
                         "[quest] Discount %s|%d percent=%d -> no-op (lB lacks level)\n",
                         item.c_str(), count, percent);
        }
        std::fflush(stdout);
        return;
    }
    // `KA` is set ONLY when `e.G>0`; a `Percent="0"` offer keeps the base.
    o.price = percent > 0
                  ? static_cast<std::int64_t>(std::trunc(
                        static_cast<double>(base) *
                        (100.0 - static_cast<double>(percent)) / 100.0))
                  : base;
    offers_[item] = o;
    std::fprintf(stdout,
                 "[quest] Discount %s percent=%d period=%lld sale=%d -> price %lld end=%lld "
                 "(base %lld, vu)\n",
                 item.c_str(), percent, period, sale ? 1 : 0,
                 static_cast<long long>(o.price), o.end_time, static_cast<long long>(base));
    std::fflush(stdout);
}

const EngineItemOffer* QuestEngine::upgrade_offer_for(const std::string& item,
                                                      int level) const {
    const auto it = upgrade_offers_.find(item);
    if (it == upgrade_offers_.end()) return nullptr;
    const auto jt = it->second.find(level);
    return jt == it->second.end() ? nullptr : &jt->second;
}

std::size_t QuestEngine::upgrade_offer_count() const {
    std::size_t n = 0;
    for (const auto& kv : upgrade_offers_) n += kv.second.size();
    return n;
}

const EngineItemOffer* QuestEngine::offer_for(const std::string& item) const {
    const auto it = offers_.find(item);
    return it == offers_.end() ? nullptr : &it->second;
}

std::int64_t QuestEngine::offer_price(const std::string& item, std::int64_t base) const {
    const EngineItemOffer* o = offer_for(item);
    if (o == nullptr || !o->active) return base;
    return o->price;
}


// The list.xml `BonusPrice` (the JS item `od`). `CatalogItem` does not carry
// it, so it is read direct from the catalog file and cached.
int QuestEngine::catalog_bonus_price(App& app, const std::string& name) const {
    (void)app;
    if (!bonus_price_ready_) {
        try {
            const std::string xml =
                read_file_text(std::string(kQuestResRoot) + "list.xml");
            if (!xml.empty()) {
                sf2::data::xml_doc doc;
                doc.parse(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
                const pugi::xml_node root = doc.root().first_child();
                if (root && std::string(root.name()) == "List") {
                    for (pugi::xml_node item : root.child("Items").children("Item")) {
                        const std::string n = item.attribute("Name").value();
                        if (n.empty()) continue;
                        bonus_price_cache_[n] =
                            sf2::data::xml_attr_int(item, "BonusPrice", 0);
                    }
                }
            }
        } catch (const std::exception&) {
        }
        bonus_price_ready_ = true;
    }
    const auto it = bonus_price_cache_.find(name);
    return it != bonus_price_cache_.end() ? it->second : 0;
}

void QuestEngine::note_unanswerable(const std::string& token) {
    if (logged_queries_.insert(token).second) {
        std::fprintf(stdout,
                     "[quest] unanswerable query (condition UNKNOWN, no fire): %s\n",
                     token.c_str());
        std::fflush(stdout);
    }
}

// `?Method[args].Field` queries the shell models. Mirrors the JS `sg.gAa`
// dispatch (L967-969) for the methods the shipped quests actually read
// through conditions/actions; everything else is UNKNOWN (logged) rather
// than invented.
namespace {

// The index of the closer matching the opener at `open` (nested counted).
// `closer` is `]` for the `?M[..]` form or `)` for the `?M(..)` form (both
// legal in the JS: `vNa` L965 strips `(`/`)`; the shipped quests mix them).
std::size_t query_match_bracket(const std::string& s, std::size_t open,
                                char closer = ']') {
    const char opener = (closer == ')') ? '(' : '[';
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == opener) ++depth;
        else if (s[i] == closer && --depth == 0) return i;
    }
    return std::string::npos;
}

// Split at TOP-LEVEL commas (depth 0): the JS query parser keeps each
// argument node separate (`sg` L955-957 `a.zb`), so nested `?Q[...]` args
// (`?Sum[?Multi[100,?Player[].Level],30]`) survive intact. Both bracket
// styles count: `?Sub(?Multi(100,?Player().Level),?Purchase(_$Purchase)..)`
// (quests.xml L9829) nests parens.
std::vector<std::string> query_split_args(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char ch : s) {
        if (ch == '[' || ch == '(') ++depth;
        else if ((ch == ']' || ch == ')') && depth > 0) --depth;
        if (ch == ',' && depth == 0) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    out.push_back(cur);
    return out;
}

// JS `""+f` for the arithmetic folds: an integral value prints without a
// decimal point (`?Multi[100,3]` -> "300", never "300.0").
std::string query_num_to_string(double v) {
    const double t = std::trunc(v);
    if (t == v && v >= -9007199254740992.0 && v <= 9007199254740992.0) {
        return std::to_string(static_cast<long long>(t));
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// JS `Da.pg.jf()` (Math.random) for `?UniformIntRandom[a,b]`. Deterministic
// seed: no shipped CONDITION uses the op (it appears only in action values),
// so a stable stream is safe and keeps the port's runs reproducible.
double query_rng01() {
    static std::mt19937 gen(0x5f2f2f31u);
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

// JS `p.Dc` (L178 `Math.round(Hb.instance.getTime())`, L1218 `v.f0()`): the
// game clock in SECONDS. `Hb.getTime()` = `Math.trunc(now().getTime()/1E3)`
// with `now()` = `ed.getDate(N$+(L.K.time-baa))`, `N$` = `ed.rfa()` =
// `Math.round(ed.axb+Date.now()/1E3)` and `ed.axb=0` — an ABSOLUTE epoch
// clock advanced by the app tick (`L.K.time`), NOT a per-process zero. The
// port's `WarriorSave::live_clock()` is that clock (persisted in the save,
// advanced in `App::update_fixed`), so a saved `?Fight.TimeLeft`/`Timer`
// deadline is reachable after the clock passes it.
double quest_now() { return WarriorSave::live_clock(); }

// JS `K.parseInt` for the `Slice` bounds (`Nwb` L957): leading whitespace, an
// optional sign, then decimal digits; no digits -> NaN, which makes every
// `Nwb` guard false and `J.substr(str, NaN, NaN)` yield "".
bool query_parse_int(const std::string& s, long long& out) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = s[i] == '-';
        ++i;
    }
    const std::size_t first = i;
    long long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
    if (i == first) return false;  // NaN
    out = neg ? -v : v;
    return true;
}

}  // namespace

// JS `p.Dc` (L178): the game clock in SECONDS — the SAME `quest_now()` the
// offer deadlines use. Public so the shop cell's `b.yn > p.Dc` sale test
// (`ns.j5` L2308-2309) reads the clock `apply_discount` writes into `yf.yn`.
double QuestEngine::now_seconds() { return quest_now(); }

// --- shop-offer controller (`nt` g="5B", `p.Cw`; `hh`/`pl` model) ----------

// The `p.Cw.It` list is built in `A1a` (L180xxx) from `p.items.gHa` — every
// list.xml item whose `SubType` is "Offer"/"DailyOffer" (`mt.Mga` L175260,
// pushed into `gHa` by `Lia` L86475). Order = list.xml order.
const std::vector<CatalogItem>& QuestEngine::offer_defs(App& app) {
    if (!offer_defs_ready_) {
        offer_defs_.clear();
        try {
            const std::vector<CatalogItem> all = load_full_catalog(app);
            for (const CatalogItem& ci : all) {
                if (ci.is_offer) offer_defs_.push_back(ci);
            }
        } catch (const std::exception&) {
            offer_defs_.clear();
        }
        offer_defs_ready_ = true;
    }
    return offer_defs_;
}

const CatalogItem* QuestEngine::offer_def(App& app, const std::string& name) {
    if (name.empty()) return nullptr;
    for (const CatalogItem& ci : offer_defs(app)) {
        if (ci.name == name) return &ci;
    }
    return nullptr;
}

// `fz.Wn` L180945: the state ordinal the `a_a` sort keys on.
int QuestEngine::offer_state_rank(const std::string& state) {
    if (state == "Active") return 3;
    if (state == "End") return 6;
    if (state == "JustStarted") return 2;
    if (state == "LastChance") return 5;
    if (state == "NotStarted") return 1;
    if (state == "Purchased") return 4;
    if (state == "Unknown") return 0;
    return -1;
}

// `p.o.P7a(name)` L130088: the live `rc`; a fresh one is NotStarted.
const EngineOfferState& QuestEngine::offer_state(const std::string& name) const {
    const auto it = offer_states_.find(name);
    if (it != offer_states_.end()) return it->second;
    static const EngineOfferState kDefault;
    return kDefault;
}

// `QEa()` L180xxx: `let a=this.o.yl.gJ(this.oJ()); return a!=null ?
// a.Nv - p.Dc > 0 : false`. `oJ()` = `Ai.fHa + name`; `Ai.fHa="OfferTimer_"`
// (L1272450). The timer lives in the SAME `p.o.yl` store as `timers_`.
bool QuestEngine::offer_timer_active(const std::string& name) const {
    const auto it = timers_.find("OfferTimer_" + name);
    return it != timers_.end() && it->second - quest_now() > 0.0;
}

// `isActive()` L180xxx: `QEa() || state=="JustStarted" || state=="Active" ?
// true : state=="LastChance"`.
bool QuestEngine::offer_is_active(const std::string& name) const {
    if (offer_timer_active(name)) return true;
    const std::string st = offer_state(name).state;
    if (st == "JustStarted" || st == "Active") return true;
    return st == "LastChance";
}

// `Nga()` (`hh` L180xxx + `pl` override): `p.o.xa.Jga(name)` = the inventory
// entry exists with count>0 (`Jga` L151772). `pl` ORs every `item.Ht` name.
bool QuestEngine::offer_owned(App& app, const CatalogItem& ci) const {
    WarriorSave w;
    try {
        w = app.save().load();
    } catch (const std::exception&) {
        return false;
    }
    if (w.has_item(ci.name)) return true;
    if (ci.offer_kind == "DailyOffer") {
        for (const std::string& n : ci.offer_items) {
            if (w.has_item(n)) return true;
        }
    }
    return false;
}

// `Ti(player)` L180xxx: `if(this.item.CE==null) return true; for(c : CE)
// if(!c.compare(a)) return false; return true;`. `CE` is never null (empty
// array -> true). Each leaf evaluates through the shared condition engine.
bool QuestEngine::offer_conditions_hold(App& app, const CatalogItem& ci) {
    if (ci.offer_conditions.empty()) return true;
    QuestCond cond;
    cond.kind = "And";
    for (const OfferCondition& oc : ci.offer_conditions) {
        QuestCond leaf;
        leaf.kind = oc.kind;
        leaf.value1 = oc.value1;
        leaf.value2 = oc.value2;
        leaf.invert = oc.invert;
        cond.children.push_back(std::move(leaf));
    }
    EvalCtx c;
    try {
        const WarriorSave w = app.save().load();
        c.story_step = w.story_step();
        c.level = w.level;
    } catch (const std::exception&) {
    }
    c.journal.player_level = c.level;
    return conditions_hold(app, cond, c);
}

// `Qba(a)` + `pwb(a)` L180xxx: `!Nga() && (BCa()<=0 || BCa()<p.Dc) &&
// (N0()<=0 || N0()>p.Dc) && Ti(player)` then `qwb(); YWa();
// rc.state="JustStarted"; rc.ox++`. `BCa`/`N0` come from DateStart/DateEnd
// (absent in the shipped list.xml -> both 0 -> pass).
bool QuestEngine::offer_try_start(App& app, const CatalogItem& ci) {
    EngineOfferState& st = offer_states_[ci.name];
    if (st.name.empty()) st.name = ci.name;
    if (st.state.empty()) st.state = "NotStarted";
    if (offer_owned(app, ci)) return false;             // !Nga()
    if (!offer_conditions_hold(app, ci)) return false;  // Ti(player)
    // `qwb()`: `N0()>0 ? N0() : p.Dc + F9a()`.
    const double deadline = quest_now() + static_cast<double>(ci.offer_duration);
    timer_activate("OfferTimer_" + ci.name, deadline);
    st.state = "JustStarted";
    ++st.ox;
    std::fprintf(stdout,
                 "[quest] CheckOffersStart: %s -> JustStarted (timer %.0fs, ox=%d)\n",
                 ci.name.c_str(), static_cast<double>(ci.offer_duration), st.ox);
    std::fflush(stdout);
    return true;
}

// `a_a()` L180xxx: start the eligible offers. `hHa` (lp()==0, "Offer") starts
// every NotStarted one; `Vha` (lp()==1, "DailyOffer") starts at most one,
// only when none is already active, after sorting by `fz.Wn(state)` then `ox`.
void QuestEngine::offer_check_start(App& app) {
    const std::vector<CatalogItem>& defs = offer_defs(app);
    for (const CatalogItem& ci : defs) {
        if (ci.offer_kind != "Offer") continue;
        EngineOfferState& st = offer_states_[ci.name];
        if (st.name.empty()) st.name = ci.name;
        if (st.state.empty()) st.state = "NotStarted";
        if (st.state == "NotStarted") offer_try_start(app, ci);
    }
    for (const CatalogItem& ci : defs) {
        if (ci.offer_kind != "DailyOffer") continue;
        if (offer_is_active(ci.name)) return;
    }
    // `m.rj(Vha, state!="NotStarted" ? state=="End" : true)`.
    std::vector<const CatalogItem*> vip;
    for (const CatalogItem& ci : defs) {
        if (ci.offer_kind != "DailyOffer") continue;
        const std::string st = offer_state(ci.name).state;
        if (st == "NotStarted" || st == "End") vip.push_back(&ci);
    }
    std::stable_sort(vip.begin(), vip.end(),
                     [this](const CatalogItem* x, const CatalogItem* y) {
                         return offer_state_rank(offer_state(x->name).state) <
                                offer_state_rank(offer_state(y->name).state);
                     });
    std::stable_sort(vip.begin(), vip.end(),
                     [this](const CatalogItem* x, const CatalogItem* y) {
                         return offer_state(x->name).ox < offer_state(y->name).ox;
                     });
    for (const CatalogItem* ci : vip) {
        if (offer_try_start(app, *ci)) break;
    }
}

// `En.S` L528761 (`EChangeOfferState`): resolve Name/Value; `if(a!="Unknown")
// { c=find(p.Cw.It, c.ab()==name); c!=null && c.rc.state!=a && (c.rc.state=a,
// p.o.save()) }`.
void QuestEngine::offer_change_state(App& app, const std::string& name,
                                     const std::string& state) {
    if (state == "Unknown") return;
    if (offer_def(app, name) == nullptr) return;
    EngineOfferState& st = offer_states_[name];
    if (st.name.empty()) st.name = name;
    if (st.state.empty()) st.state = "NotStarted";
    if (st.state == state) return;
    st.state = state;
    std::fprintf(stdout, "[quest] ChangeOfferState %s -> %s\n", name.c_str(),
                 state.c_str());
    std::fflush(stdout);
}

// `$Za` L180xxx -> `C3a`: on the `OfferTimer_<name>` expiry,
// `if(rc.state!="Purchased"){ a.nKa(); rc.state = item.dU ? "LastChance" : "End" }`.
void QuestEngine::offer_timer_expired(App& app, const std::string& name) {
    const CatalogItem* ci = offer_def(app, name);
    EngineOfferState& st = offer_states_[name];
    if (st.name.empty()) st.name = name;
    if (st.state.empty()) st.state = "NotStarted";
    if (st.state == "Purchased") return;
    const bool last_chance = ci != nullptr && ci->offer_show_last_chance;
    st.state = last_chance ? "LastChance" : "End";
    std::fprintf(stdout, "[quest] offer timer %s expired -> %s\n",
                 name.c_str(), st.state.c_str());
    std::fflush(stdout);
}

// `tlb(a)` L180xxx: `a.lp()!=1 && a.D3a(); a.nKa(); a.rc.n4=p.Dc;
// a.rc.state="Purchased"; save; a.Wwa(a)`.
void QuestEngine::offer_purchase(App& app, const std::string& name) {
    (void)app;
    const CatalogItem* ci = offer_def(app, name);
    if (ci == nullptr) return;
    EngineOfferState& st = offer_states_[name];
    if (st.name.empty()) st.name = name;
    if (ci->offer_kind != "DailyOffer") timer_end("OfferTimer_" + name);  // D3a
    st.n4 = static_cast<long long>(std::llround(quest_now()));
    st.state = "Purchased";
    std::fprintf(stdout, "[quest] offer %s purchased (n4=%lld)\n", name.c_str(),
                 st.n4);
    std::fflush(stdout);
}

// `TZa`/`Wwa` L180xxx: for every Purchased offer with `!rc.UH`, grant the
// not-yet-received items and set `rc.UH` when all are in. The port has no
// store grant path, so this marks `UH` only when the player already owns the
// whole offer content (the shipped `CheckItemsFromPurchasedOffers` restore).
void QuestEngine::offer_check_purchased(App& app) {
    for (const CatalogItem& ci : offer_defs(app)) {
        EngineOfferState& st = offer_states_[ci.name];
        if (st.state != "Purchased" || st.UH) continue;
        if (ci.offer_items.empty()) continue;
        WarriorSave w;
        try {
            w = app.save().load();
        } catch (const std::exception&) {
            continue;
        }
        bool all = true;
        for (const std::string& n : ci.offer_items) {
            if (!w.has_item(n)) {
                all = false;
                break;
            }
        }
        if (all) {
            st.UH = true;
            std::fprintf(stdout,
                         "[quest] CheckItemsFromPurchasedOffers: %s UH=1\n",
                         ci.name.c_str());
            std::fflush(stdout);
        }
    }
}
bool QuestEngine::resolve_query(App& app, const std::string& token, const EvalCtx& ctx,
                                std::string& out) {
    const std::size_t lb_b = token.find('[');
    const std::size_t lb_p = token.find('(');
    const std::size_t lb =
        (lb_p != std::string::npos && (lb_b == std::string::npos || lb_p < lb_b))
            ? lb_p
            : lb_b;
    const char closer =
        (lb != std::string::npos && token[lb] == '(') ? ')' : ']';
    const std::size_t rb =
        (lb == std::string::npos) ? std::string::npos
                                  : query_match_bracket(token, lb, closer);
    if (lb == std::string::npos || rb == std::string::npos) {
        note_unanswerable(token);
        return false;
    }
    const std::string method = token.substr(1, lb - 1);
    const std::string inner = token.substr(lb + 1, rb - lb - 1);
    std::string field = token.substr(rb + 1);
    if (!field.empty() && field[0] == '.') field = field.substr(1);

    // Resolve every top-level argument through the token evaluator: JS
    // evaluates the argument nodes before the method body (each `a.zb[i].body`
    // is a resolved value). A nested `?Q[...]` or a `_$`/`_` reference
    // resolves; a literal passes through; an unanswerable nested expression
    // makes the whole query UNKNOWN (the JS parser would have thrown).
    std::vector<std::string> args;
    bool args_ok = true;
    for (const std::string& raw : query_split_args(inner)) {
        std::string v;
        if (!raw.empty() && (raw.find('?') != std::string::npos || raw[0] == '_')) {
            if (!resolve_token(app, raw, ctx, v)) {
                args_ok = false;
                v.clear();
            }
        } else {
            v = raw;
        }
        args.push_back(v);
    }
    const std::string arg = args.empty() ? std::string() : args[0];

    // `sg.yE` (L955-957): the arithmetic fold ops. `Sum`/`Sub`/`Multi` fold
    // left-to-right (first arg sets, the rest apply); `NDiv`/`Mod` take two;
    // `UniformIntRandom(a,b)` = a + trunc(((b+1)|0 - a) * random()).
    if (method == "Sum" || method == "Sub" || method == "Multi" || method == "NDiv" ||
        method == "Mod" || method == "UniformIntRandom") {
        if (!args_ok) {
            note_unanswerable(token);
            return false;
        }
        const auto num = [](const std::string& s) {
            return is_numeric(s) ? to_number(s) : 0.0;
        };
        if (method == "Sum" || method == "Sub" || method == "Multi") {
            double f = 0.0;
            int e = 0;
            for (const std::string& a : args) {
                const double d = num(a);
                if (e == 0) f = d;
                else if (method == "Sum") f += d;
                else if (method == "Sub") f -= d;
                else f *= d;
                ++e;
            }
            out = query_num_to_string(f);
            return true;
        }
        if (args.size() != 2) {
            note_unanswerable(token);
            return false;
        }
        if (method == "NDiv") {
            // `K.T(parseInt(a)/parseInt(b))` — the JS `K.T` truncates.
            const long long a = static_cast<long long>(to_number(args[0]));
            const long long b = static_cast<long long>(to_number(args[1]));
            if (b == 0) {  // JS would produce Infinity; never a wrong value.
                note_unanswerable(token);
                return false;
            }
            out = std::to_string(a / b);
            return true;
        }
        if (method == "Mod") {
            const long long a = static_cast<long long>(to_number(args[0]));
            const long long b = static_cast<long long>(to_number(args[1]));
            if (b == 0) {
                note_unanswerable(token);
                return false;
            }
            out = std::to_string(a % b);
            return true;
        }
        const double lo = static_cast<double>(static_cast<int>(to_number(args[0])));
        const double hi1 = static_cast<double>(static_cast<int>(to_number(args[1]) + 1.0));
        const double v = lo + (hi1 - lo) * query_rng01();
        out = std::to_string(static_cast<long long>(std::trunc(v)));
        return true;
    }

    // `QNa` (L956-957) — the ONE handler behind both `Concat` (dispatch `fAa`
    // L967: `case "Concat":this.QNa(b,a,1)`) and `Slice` (L969:
    // `case "Slice":this.QNa(b,a,2)`). The loop concatenates every evaluated
    // argument when c==1; for c==2 only the first is kept, and with 3+
    // arguments `Nwb(f[0],parseInt(f[1]),parseInt(f[2]))` (L957) carves an
    // INCLUSIVE `[start,end]` range: `d = 1 + end - start`, clamped to the
    // tail, `""` when `start>len || end<start || start<0 || end<0`.
    if (method == "Concat" || method == "Slice") {
        if (!args_ok) {
            note_unanswerable(token);
            return false;
        }
        if (method == "Concat") {
            std::string s;
            for (const std::string& a : args) s += a;
            out = s;
            return true;
        }
        if (args.size() < 3) {
            out = args.empty() ? std::string() : args[0];
            return true;
        }
        long long start = 0;
        long long end = 0;
        const long long len = static_cast<long long>(args[0].size());
        if (!query_parse_int(args[1], start) || !query_parse_int(args[2], end) ||
            start > len || end < start || start < 0 || end < 0) {
            out.clear();
            return true;
        }
        long long d = 1 + end - start;
        if (1 + end > len) d = len - start;
        out = args[0].substr(static_cast<std::size_t>(start),
                             static_cast<std::size_t>(d));
        return true;
    }

    if (method == "DataVersion" || method == "VersionController") {
        // `yzb` (L982) reads `tg.fz` for `?DataVersion`; `Czb` (L983) reads
        // `tg.version` for `?VersionController`. Both are set to the same
        // `new Dh(1,0,13,0)` (`Wdb` L594899, also static-init L1280835). `Dh`
        // ctor L1210840: (WS=Production, wE=Major, zE=Minor, fz); `toString`
        // L1210846 = "WS.wE.zE" (the `.fz` suffix only when its arg is true).
        // Fields read: DataVersion->`.fz`, Major->`.wE`, Minor->`.zE`,
        // Production->`.WS`, Full/Version->`toString()`; any other leaves the
        // JS `b.result` unset -> UNKNOWN.
        if (field == "DataVersion") {
            out = "0";
        } else if (field == "Major") {
            out = "0";
        } else if (field == "Minor") {
            out = "13";
        } else if (field == "Production") {
            out = "1";
        } else if (field == "Full" || field == "Version") {
            out = "1.0.13";
        } else {
            note_unanswerable(token);
            return false;
        }
        return true;
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
            // `$wb` L983-987 extras. `NBO` = `L.K.Wt==null||L.K.Wt.length==0
            // ? "1":"0"` — `Wt` is the store SKU list, `[]` in a non-store
            // build (init L32668) -> "1". `OsName` = `L.K.R7a()` (L26795): the
            // UA test yields "Windows" on this platform. `StarterPacksAvailable`
            // = a==7?"1":"0" over 7 named SKUs; none present -> "0".
            {"NBO", "1"}, {"OsName", "Windows"}, {"StarterPacksAvailable", "0"},
        };
        for (const auto& row : kSys) {
            if (field == row[0]) {
                out = row[1];
                return true;
            }
        }
        if (field == "Time") {
            // `$wb` L987: `case "Time":b.result=K.T(p.Dc)`. `p.Dc` is the game
            // clock, set at `Edb` (L89485): `p.Dc=Math.round(Hb.instance.getTime())`
            // — the `Hb` seconds the port maps onto `quest_now()`.
            out = std::to_string(static_cast<long long>(std::llround(quest_now())));
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Timer") {
        // `uxb` L988: `c=a.Xl(); ... (a=d.yl.gJ(c), b.result=a!=null ?
        // K.T(v.ZI(a.Nv)) : "")`. Only the `.Value` field is handled; `v.ZI`
        // (L1218) = `a>b ? a-b : 0` — the REMAINING seconds. An absent timer
        // yields "" (not UNKNOWN): the shipped `Not="1"` (`<=0`) gates on it.
        if (field != "Value") {
            note_unanswerable(token);
            return false;
        }
        const auto it = timers_.find(arg);
        if (it == timers_.end()) {
            out = "";
            return true;
        }
        const double now = quest_now();
        out = query_num_to_string(it->second > now ? it->second - now : 0.0);
        return true;
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
        // `blb` (L973-975): `Bonus`->`K.T(c.fd)` (the player's Bonus, default 0
        // in the JS ctor L124164), `Power`->`c.dk==null?"null":""+c.dk`
        // (`this.dk` = the `Power` attr, L124993), `CoinIcon`->`c.Vf`
        // (`this.Vf=b!=null?b:Z.Hna`, L124569; `Z.Hna="gold"` L1274515).
        if (field == "Bonus") {
            out = std::to_string(w.bonus);
            return true;
        }
        if (field == "Power") {
            out = std::to_string(w.power);
            return true;
        }
        if (field == "CoinIcon") {
            out = "gold";
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
        if (field == "Type") {
            // `X3a` L972: `case "Type":a=p.F().rAa(c.type)`. The fight
            // controller `dl` L726825 inherits the owning battle type
            // (`IIa(c, fight, a.type, ...)` L98334 with `pkb` L719570),
            // so the label is that battle stages `Type` key, else DUMMY.
            const std::string battle = triple_field(triple, 1);
            out = "DUMMY";
            if (!battle.empty()) {
                const auto it = battle_type_.find(battle);
                if (it != battle_type_.end()) out = fight_type_label(it->second);
            }
            return true;
        }
        // `X3a` L498367: the data fields read the live fight record `c.Wc`
        // (`il`), defaulting to "0" when absent. `Level`=`bb()` (L143548),
        // `LossCount`=`FW`, `WinCount`=`no`, `Timestamp`=`Pz()` (the `TimeLeft`
        // attr). `TimeLeft`=`f9a()` (L727530) = `max(Nn - Wc.WQ(), 0)` with
        // `Nn` = the `<Fight ReplayInterval>` (`IIa` L98652) and `Wc.WQ()`
        // = the record clock `Qe` (`il` ctor L141504 `Qe=-1`; `ZA` L143481
        // `Qe = Gs<=0 ? -1 : clock-Gs`, `Gs` = the record `TimeLeft` attr).
        // The sibling `n1a` (L625394) computes the same remaining as
        // `Pz() + Nn - p.Dc` = `Nn - (p.Dc - Gs)`, i.e. the clock is the
        // GAME clock `p.Dc` (the port's `quest_now()`). `G3a` (L93340) passes
        // `p.o.e4` (the energy-regen countdown `aPa` L138972) — the port has
        // no energy-regen timer, so `p.Dc`/`quest_now()` is used.
        const WarriorSave& w = ctx.live(app);
        const WarriorSave::FightWins* rec = nullptr;
        for (const WarriorSave::FightWins& f : w.fights) {
            if (f.name == triple) rec = &f;
        }
        if (field == "WinCount") {
            out = std::to_string(rec != nullptr ? rec->wins : 0);
            return true;
        }
        if (field == "Level") {
            out = std::to_string(rec != nullptr ? rec->level : 0);
            return true;
        }
        if (field == "LossCount") {
            out = std::to_string(rec != nullptr ? rec->losses : 0);
            return true;
        }
        if (field == "Timestamp") {
            out = std::to_string(rec != nullptr ? rec->time_left : 0);
            return true;
        }
        if (field == "Power") {
            // `X3a` L498367 `case "Power":a=c.d4==null?"null":""+c.d4;`.
            // `c.d4` = `IIa` L98652 `u.I(get("Power"),1)` — an int for any
            // parsed fight, so the answer is its decimal string.
            const auto it = fight_power_.find(triple);
            out = it != fight_power_.end() ? std::to_string(it->second) : "0";
            return true;
        }
        if (field == "Description") {
            // `X3a` L498367 `case "Description":a=c.GD();`; `GD()` L727376
            // = `g8!=null&&g8!=""?g8:Sb`.
            const auto it = fight_description_.find(triple);
            out = it != fight_description_.end() ? it->second : std::string();
            return true;
        }
        if (field == "Difficulty") {
            // `X3a` L498367 `case "Difficulty":a=Wc.NAa(v.Gz(c));
            // a=K.T(Wc.gD.indexOf(a));`. `v.Gz` resolves the fight record and
            // `Wc.NAa` picks the LAST `Wc.gD` row whose `RatingRatioTreshold`
            // is `<` the rating ratio; `indexOf` is the 0-based level index.
            // The port resolves the SAME rating the Map's `Wc` bar uses
            // (`map_fight_difficulty_level` -> `map_battle_rating_cached` ->
            // `map_difficulty_level`); a malformed triple is the JS `p.Wv`
            // miss -> `-1`.
            out = std::to_string(map_fight_difficulty_level(app, triple));
            return true;
        }
        if (field == "TimeLeft") {
            const auto nit = fight_replay_interval_.find(triple);
            const long long nn = nit != fight_replay_interval_.end() ? nit->second : 0;
            const long long gs = rec != nullptr ? rec->time_left : 0;
            const double qe = gs <= 0 ? -1.0 : (quest_now() - static_cast<double>(gs));
            long long v = static_cast<long long>(
                std::llround(static_cast<double>(nn) - qe));
            if (v < 0) v = 0;  // `a<0&&(a=0)`
            out = std::to_string(v);
            return true;
        }
        // `X3a` L498044: `Armor`/`Helm`/`Weapon`/`Ranged`/`Magic`/
        // `RaidCharge` -> `c.cJ(type)` (L727592); the `*Level` variants ->
        // `K.T(c.bJ(type))` (L727725). `cJ`/`bJ` walk `zR` in order: skip a
        // rule failing its `<Level Min Max>` gate `Ti()` (L846), skip a
        // removed rule (`NV`), then match the resolved item's `ib.type`.
        // `<EquipItem>` (`hn` L441150) starts `NV=!0` -> skipped;
        // `<RequireItem Type="T" MinLevel="L">` (`Ff` L440697, `NV=!1`)
        // resolves to an item with `type=T` and an empty `Name` (`I.Qd`/
        // `pL` L163388 read `Type`, no `Name`), so `cJ(T)=""` and
        // `bJ(T)=L` (`zf.init` L644001 `Np(MinLevel)` -> `Ce`).
        // `<RandomAquiredItem>` resolves a runtime-random owned item
        // (`on.wJa` L446578 `m9a`), not derivable here.
        static const char* const kEquipTypes[] = {
            "Weapon", "Armor", "Helm", "Ranged", "Magic", "RaidCharge"};
        for (const char* et : kEquipTypes) {
            const std::size_t tlen = std::string(et).size();
            const bool is_level = field.size() == tlen + 5 &&
                                  field.compare(0, tlen, et) == 0 &&
                                  field.compare(field.size() - 5, 5, "Level") == 0;
            if (field != et && !is_level) continue;
            std::string name_out;
            int level_out = 0;
            const auto rit = fight_rules_.find(triple);
            if (rit != fight_rules_.end()) {
                for (const FightRule& r : rit->second) {
                    // `Ti()`: the rule's level gate.
                    if (r.power_min > w.level || w.level > r.power_max) continue;
                    // `EquipItem` `NV=!0`; `RandomAquiredItem` is runtime-random.
                    if (r.tag != "RequireItem") continue;
                    const auto ti = r.attrs.find("Type");
                    if (ti == r.attrs.end() || ti->second != et) continue;
                    int ml = 0;
                    const auto mi = r.attrs.find("MinLevel");
                    if (mi != r.attrs.end()) {
                        try {
                            ml = std::stoi(mi->second);
                        } catch (...) {
                        }
                    }
                    name_out = std::string();  // `ab()` = `Ba` = the (empty) Name
                    level_out = ml;            // `Ce` = `Np(MinLevel)`
                    break;
                }
            }
            out = is_level ? std::to_string(level_out) : name_out;
            return true;
        }
        // `X3a` L498044: `Money` -> `K.T(c.h4)`, `Bonus` -> `K.T(c.g4)`.
        // `$L(level)` (L730846) sets `h4`/`g4` from the LAST `wi` row's
        // `bm(level)`: `h4`=`Tb` (Money), `g4`=`Uo` (Bonus) (`tt.parse`
        // L121771). The shipped `<Reward>` rows are flat, so the row is its
        // own level value.
        if (field == "Money" || field == "Bonus") {
            int v = 0;
            const auto rit = fight_rewards_.find(triple);
            if (rit != fight_rewards_.end() && !rit->second.empty()) {
                v = field == "Money" ? rit->second.back().money
                                     : rit->second.back().bonus;
            }
            out = std::to_string(v);
            return true;
        }
        // `X3a` L498044: `CheckCurrency` -> `K.T(c.dEa())` (L727855),
        // `EnoughCurrency` -> `K.T(v.I3a(c))` (L626221). `dEa` is true when
        // any `e0()` (`ERuleCurrencyCost`, `oh` L435626: `Kj`=Name default
        // "", `an`=Value clamped `>=0`) has a non-empty `Name` and `an > 0`.
        // `I3a` L626221: for each rule's `Name` (`c`) it sums the `an` of
        // every rule carrying that `Name` (`e`) and fails when the player
        // balance `d = p.o.uD(c)` is below it (`if(d<e)return!1`). `uD(a)`
        // (L138798) = `rea(a).count`, `rea` (L137813) `m.find(this.Ll,
        // b.currency.name==a)` — the `<Currencies Name=...>` count seeded by
        // `xf.Jia` L139448 (`u.I(a.attributes.get(d.name))`, absent -> 0).
        if (field == "CheckCurrency" || field == "EnoughCurrency") {
            std::vector<std::pair<std::string, long long>> costs;
            const auto rit = fight_rules_.find(triple);
            if (rit != fight_rules_.end()) {
                for (const FightRule& r : rit->second) {
                    if (r.tag != "CurrencyCost") continue;
                    const auto ni = r.attrs.find("Name");
                    if (ni == r.attrs.end() || ni->second.empty()) continue;
                    long long val = 0;
                    const auto vi = r.attrs.find("Value");
                    if (vi != r.attrs.end()) {
                        try {
                            val = std::stoll(vi->second);
                        } catch (...) {
                        }
                    }
                    if (val < 0) val = 0;  // `oh.parse` L435626: `an<0 -> 0`
                    costs.emplace_back(ni->second, val);
                }
            }
            if (field == "CheckCurrency") {
                bool any = false;
                for (const auto& c : costs) {
                    if (c.second > 0) any = true;
                }
                out = any ? "1" : "0";
                return true;
            }
            // `I3a` L626221: `d = p.o.uD(c)` (the `<Currencies>` count for the
            // name; absent -> 0) vs the summed `an` (`e`) of every rule with
            // that `Name`; `d < e` fails.
            const WarriorSave& w = ctx.live(app);
            bool enough = true;
            for (const auto& c : costs) {
                long long need = 0;
                for (const auto& d : costs) {
                    if (d.first == c.first) need += d.second;
                }
                const auto bit = w.currencies.find(c.first);
                const long long balance =
                    bit != w.currencies.end() ? bit->second : 0;
                if (balance < need) {  // `p.o.uD(c) < e`
                    enough = false;
                    break;
                }
            }
            out = enough ? "1" : "0";
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Purchase") {
        // `IJa` (L980): arg = "name|failure". `e=p.rf(name)` (the owned
        // instance via `p.o.xa.Qj`), `d = e!=null ? e.ib : p.items.$b(name)`.
        std::string name = arg;
        std::string failure;
        const std::size_t bar = name.find('|');
        if (bar != std::string::npos) {
            failure = name.substr(bar + 1);
            name = name.substr(0, bar);
        }
        if (!args_ok) {
            note_unanswerable(token);
            return false;
        }
        if (field == "Failure") {
            out = failure;
            return true;
        }
        const WarriorSave& w = ctx.live(app);
        const WarriorSave::OwnedItem* owned = nullptr;
        for (const WarriorSave::OwnedItem& it : w.items) {
            if (it.name == name) {
                owned = &it;
                break;
            }
        }
        const CatalogItem* ci = catalog_find(app, name);
        if (field == "Type") {
            out = ci != nullptr ? ci->type : std::string();
            return true;
        }
        if (field == "Name") {
            out = ci != nullptr ? ci->name : std::string();
            return true;
        }
        if (field == "UpgradeLevel") {
            out = std::to_string(owned != nullptr ? owned->upgrade_level : 0);
            return true;
        }
        if (field == "PaidItem") {
            // `IJa` L980: `case "PaidItem":b.result=d!=null?d.D3:""`. `D3` =
            // the list.xml `PaidItem` attr, default "None" (item ctor
            // L1266096); `d` is the catalog item (owned or list).
            out = ci != nullptr ? ci->paid_item : std::string();
            return true;
        }
        if (field == "Timeout") {
            // `IJa` L980: `case "Timeout":a=e!=null?e.Bh:0;d=v.f0();
            // b.result=K.T(a>d?a-d:0)`. `e.Bh` = the owned item's DeliveryTime
            // (`zF` L643170), `v.f0()` = `p.Dc`. The port keys pending
            // deliveries by item name in `WarriorSave::timers` (wall clock), so
            // the remaining seconds are the same countdown (0 when absent).
            const WarriorSave& w = ctx.live(app);
            const auto it = w.timers.find(name);
            const std::int64_t now = WarriorSave::wall_now();
            const std::int64_t left =
                (it != w.timers.end() && it->second > now) ? it->second - now : 0;
            out = std::to_string(left);
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Item") {
        // `cdb` (L976-978): `c = p.items.$b(name)`. A name not in the catalog
        // leaves the JS `b.result` unset -> "" (authoritative: the catalog IS
        // `p.items`, so this is a real empty value, never UNKNOWN).
        const CatalogItem* ci = catalog_find(app, arg);
        if (ci == nullptr) {
            out.clear();
            return true;
        }
        if (field == "Quantity") {
            // `c = p.o.xa.te(c)` (the inventory entry); `c!=null ? c.pd() : "0"`.
            int q = 0;
            for (const WarriorSave::OwnedItem& it : ctx.live(app).items) {
                if (it.name == arg) {
                    q = it.count;
                    break;
                }
            }
            out = std::to_string(q);
            return true;
        }
        if (field == "SubType") {
            out = ci->subtype;
            return true;
        }
        if (field == "Type") {
            out = ci->type;
            return true;
        }
        if (field == "Name") {
            out = ci->name;
            return true;
        }
        if (field == "Price") {
            out = std::to_string(ci->price);
            return true;
        }
        if (field == "RealPrice") {
            // `cdb` L978: `a=c.xr; a!=null&&a!=""&&Kg(c.xr.charAt(0))&&
            // (a=" "+a); c=new Ia; b.result=aa.Ela(a,c)`. `c.xr` = the
            // list.xml RealPrice (`CatalogItem::offer_real_price`).
            out = real_price_text(ci->offer_real_price);
            return true;
        }
        if (field == "Level") {
            // `cdb` L977: `case "Level":b.result=c.xf==null?"null":""+c.xf`.
            // A Level-less list.xml row answers the STRING "null".
            out = ci->has_level ? std::to_string(ci->level) : std::string("null");
            return true;
        }
        if (field == "BonusPrice") {
            // `c.od` = the list.xml `BonusPrice` (see `catalog_bonus_price`).
            out = std::to_string(catalog_bonus_price(app, arg));
            return true;
        }
        if (field == "RecieveGold") {
            // `cdb` L978: `case "RecieveGold":b.result=K.T(c.Mn)`.
            out = std::to_string(ci->recieve_gold);
            return true;
        }
        if (field == "RecieveBonus") {
            // `cdb` L978: `case "RecieveBonus":b.result=K.T(c.Ip)`.
            out = std::to_string(ci->recieve_bonus);
            return true;
        }
        if (field == "Availability") {
            // `LCa()` (L1272275): `!li() && isActive && HJ(lock)`. `li()` =
            // `this.hidden` (item `li()` L1271725) and `isActive` = `!ShopHide`
            // (item ctor L1266096: `this.isActive=!b; this.eW=!b`). `HJ(lock)`
            // is a zone gate keyed on the item's `lock`; a lock-less shipped
            // item returns true, so the sourceable form is `!Hidden && !ShopHide`.
            out = (ci->hidden || ci->shop_hide) ? "0" : "1";
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "ItemsOfType") {
        // `edb` (L981): `c=p.o.xa.hJ(arg); a.Sd=="Quantity"&&(b.result=K.T(c.length))`.
        // `hJ` (L151584) keeps the inventory entries whose catalog type equals
        // `arg` (subtype "" = any) and which are `isActive` (its `c` flag
        // defaults true; `isActive` = `!ShopHide`, item ctor L1266096).
        const WarriorSave& w = ctx.live(app);
        std::size_t n = 0;
        for (const WarriorSave::OwnedItem& it : w.items) {
            const CatalogItem* ci = catalog_find(app, it.name);
            if (ci != nullptr && ci->type == arg && !ci->shop_hide) ++n;
        }
        if (field == "Quantity") {
            out = std::to_string(n);
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Offer") {
        // `wfb` L508754 (dispatch L497065 `case "Offer":this.wfb(b,a)`):
        // `c=a.Xl(); d=m.dn(p.Cw.It, e=>e.ab()==c); if(Sd=="Exists") result=
        // d!=null?"1":"0"; else if(d!=null) switch(Sd){...}`. An absent offer
        // leaves `b.result` at the builder default "".
        if (!args_ok) {
            note_unanswerable(token);
            return false;
        }
        const CatalogItem* d = offer_def(app, arg);
        if (field == "Exists") {
            out = d != nullptr ? "1" : "0";
            return true;
        }
        if (d == nullptr) {
            out.clear();
            return true;
        }
        if (field == "AllItemsRecieved") {
            out = offer_state(arg).UH ? "1" : "0";
            return true;
        }
        if (field == "Description") {
            out = d->offer_description;
            return true;
        }
        if (field == "FocusOnBuy") {
            out = d->offer_focus_on_buy;
            return true;
        }
        if (field == "Image") {
            // `case "Image":b.result=d.item.fileName` — `fileName` = the
            // list.xml `Image` attr (L163660), NOT ButtonImage; the shipped
            // offers carry only ButtonImage -> "".
            out = d->image;
            return true;
        }
        if (field == "ProfitImage") {
            out = d->offer_profit_image;
            return true;
        }
        if (field == "RealPrice") {
            // `case "RealPrice":a=d.item.xr; a!=null&&a!=""&&Kg(a.charAt(0))
            // &&(a=" "+a); b.result=aa.Ela(a,d)`. `Kg` L3847 = "any char is a
            // digit"; `charAt(0)` -> the first char. `Ela` L1217616 is the
            // `Ela` L1217616 = `yxa(Dub(a))` (see `real_price_text`).
            out = real_price_text(d->offer_real_price);
            return true;
        }
        if (field == "ShowLastChance") {
            out = d->offer_show_last_chance ? "1" : "0";
            return true;
        }
        if (field == "State") {
            out = offer_state(arg).state;
            return true;
        }
        if (field == "TimerActive") {
            out = offer_timer_active(arg) ? "1" : "0";
            return true;
        }
        if (field == "TimerName") {
            out = "OfferTimer_" + arg;  // `d.oJ()` = `Ai.fHa + ab()`
            return true;
        }
        if (field == "Title") {
            out = d->offer_text;
            return true;
        }
        if (field == "Type") {
            out = d->offer_kind == "DailyOffer" ? "1" : "0";  // `K.T(d.lp())`
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Offers") {
        // `yfb` L509394: the LIST query over `p.Cw.It`. Each `First*` finds
        // the first offer in that state (`m.dn`) and yields `a.ab()`; the JS
        // writes `null` when none matches (the shipped gate accepts "" or "0").
        if (!args_ok) {
            note_unanswerable(token);
            return false;
        }
        const std::vector<CatalogItem>& defs = offer_defs(app);
        auto first_with = [&](const char* st) -> const CatalogItem* {
            for (const CatalogItem& ci : defs) {
                if (offer_state(ci.name).state == st) return &ci;
            }
            return nullptr;
        };
        const CatalogItem* m = nullptr;
        bool known = true;
        if (field == "First") {
            m = defs.empty() ? nullptr : &defs.front();
        } else if (field == "FirstActive") {
            m = first_with("Active");
        } else if (field == "FirstEnd") {
            m = first_with("End");
        } else if (field == "FirstJustStarted") {
            m = first_with("JustStarted");
        } else if (field == "FirstLastChance") {
            m = first_with("LastChance");
        } else if (field == "FirstNotStarted") {
            m = first_with("NotStarted");
        } else if (field == "FirstPurchased") {
            m = first_with("Purchased");
        } else {
            known = false;
        }
        if (!known) {
            note_unanswerable(token);
            return false;
        }
        out = m != nullptr ? m->name : "0";
        return true;
    }
    if (method == "Pack") {
        // `zib` (L979): `IsAvailable -> Mc.F().T1(c)?"1":"0"`. `T1` L478996:
        // `c=m.find(this.wq, d=>d.name==arg); return c==null?!1:...` — `wq` is
        // the downloaded-pack registry; the port has no pack downloader, so the
        // registry is empty, `c==null` -> false -> "0".
        if (field == "IsAvailable") {
            out = "0";
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Enchantment") {
        // `z3a` (L967): the raw arg is split on "|"; fewer than 2 parts leaves
        // the JS `b.result` unset -> UNKNOWN. `Item`=parts[0], `Recipe`=parts[1],
        // `DeliveryTime`=`Math.trunc(parts[2])` (0 when only 2 parts).
        std::vector<std::string> parts;
        std::size_t p0 = 0;
        for (;;) {
            const std::size_t bar = arg.find('|', p0);
            if (bar == std::string::npos) {
                parts.push_back(arg.substr(p0));
                break;
            }
            parts.push_back(arg.substr(p0, bar - p0));
            p0 = bar + 1;
        }
        if (parts.size() < 2) {
            note_unanswerable(token);
            return false;
        }
        if (field == "Item") {
            out = parts[0];
            return true;
        }
        if (field == "Recipe") {
            out = parts[1];
            return true;
        }
        if (field == "DeliveryTime") {
            long long h = 0;
            if (parts.size() == 3) query_parse_int(parts[2], h);
            out = std::to_string(h);
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    if (method == "Battle") {
        // `nYa` (L981-982): `d=new hb; d.kj(arg)` (the "zone|name|" triple or
        // a bare battle name); `a=p.Uk(d)` = the stages battle def.
        std::string zone;
        std::string name = arg;
        const std::size_t bar = arg.find('|');
        if (bar != std::string::npos) {
            zone = arg.substr(0, bar);
            const std::size_t bar2 = arg.find('|', bar + 1);
            name = arg.substr(bar + 1, bar2 == std::string::npos ? std::string::npos
                                                                 : bar2 - bar - 1);
        } else {
            const auto it = battle_zone_.find(arg);
            if (it != battle_zone_.end()) zone = it->second;
        }
        const bool def_exists = battle_zone_.find(name) != battle_zone_.end();
        const WarriorSave& w = ctx.live(app);
        const WarriorSave::BattleRecord* rec = w.find_battle(zone, name);
        if (field == "Available") {
            // `p.Uk(d)!=null ? (p.o.WDa(d)?"1":"0") : "0"`; `WDa(a) =
            // this.iF.get(a)!=null` (L129828) = the save carries the record.
            out = (def_exists && w.has_battle(name)) ? "1" : "0";
            return true;
        }
        if (field == "Name") {
            out = def_exists ? name : std::string();
            return true;
        }
        if (field == "Zone") {
            out = def_exists ? zone : std::string();
            return true;
        }
        if (field == "Hidden") {
            // `a!=null && a.ob!=null ? (a.ob.li()?"1":"0") : "-1"`.
            out = (def_exists && rec != nullptr) ? (rec->hidden ? "1" : "0") : "-1";
            return true;
        }
        if (field == "Locked") {
            // `c=!0; a!=null&&(c=a.ob==null||a.ob.tt())` — locked when the
            // def OR the record is missing, or the record carries Locked.
            const bool locked = !def_exists || rec == nullptr || rec->locked;
            out = locked ? "1" : "0";
            return true;
        }
        if (field == "IsOpened") {
            // `b.result="0"; a!=null&&(a.ob!=null&&(b.result=a.ob.tt()||a.ob.li()?"0":"1"), ...)`.
            out = (def_exists && rec != nullptr && !rec->locked && !rec->hidden)
                      ? "1"
                      : "0";
            return true;
        }
        if (field == "Type") {
            // `nYa` L982: `c="FightDummy"; a!=null&&(c=a.type); b.result=
            // p.F().rAa(c)`. `a.type = p.F().b0(<Battle Type>)` (`pkb`
            // L719570); the `b0`/`rAa` round trip is the stages `Type`
            // key when known, else DUMMY (both defaults).
            out = "DUMMY";
            if (def_exists) {
                const auto it = battle_type_.find(name);
                if (it != battle_type_.end()) out = fight_type_label(it->second);
            }
            return true;
        }
        note_unanswerable(token);
        return false;
    }
    note_unanswerable(token);
    return false;
}

// `v.su.kU` — the story-step universe the JS validates against. `nw.parse`
// (`sf2.502f0946.js` idx 614997: `this.kU.push(c.attributes.get("Name"))`)
// fills it from `internal_settings.xml` L116-126 `<StepsNames>`; `nw.Ucb(a)`
// is `kU.includes(a)`. `zt.parse` (`zi.g="81"`, bundle idx 157285) reads the
// save's `Tutorial` attribute and sets `this.HH = v.su.Ucb(a) ? a :
// v.su.kU[0]`, so an absent OR invalid step normalizes to `kU[0]` =
// "NotStarted". The shipped `users_default.b7da2019.xml` carries
// `Tutorial="MOVE"` — not a member — i.e. a FRESH tutorial profile.
static const char* const kStorySteps[] = {
    "NotStarted", "FIGHT",  "STEP_BUY_ITEM", "STEP_BUY_ITEM_FINISH", "MAP",
    "LEARN_PERK", "SHOW_DOUBLE_SWEEP",     "SHOW_BLOCK",           "END"};
static bool valid_story_step(const std::string& s) {
    for (const char* step : kStorySteps) {
        if (s == step) return true;
    }
    return false;
}

// `_Name` quest-variable reference. JS `p.o.f5a` (L133027) resolves in
// precedence order: `ha.F().q0(a)` (Local/CH2) -> `this.AG.get(a)`
// (Global/CH1) -> `this.rv.get(a)` (Users/CH0). The JS `rv`/`AG`/`aH` keys
// carry the leading `_` (`wkb` L132880 `c = "_" + Name`); the port's maps use
// the PUBLIC `Name` (`variable_key_for` bridges at the save boundary), so the
// `_` is stripped here. An unknown name is the empty string, so a `!= ""`
// condition reads false (the pre-fix behaviour and the JS null default).
std::string QuestEngine::quest_var(
    App& app, const std::map<std::string, std::string>& locals,
    const std::string& token) {
    if (token.size() < 2 || token[0] != '_' || token[1] == '$') return token;
    const std::string name = token.substr(1);
    const auto it = locals.find(name);            // Local (CH2)
    if (it != locals.end()) return it->second;
    const auto gv = global_vars_.find(name);      // Global (CH1)
    if (gv != global_vars_.end()) return gv->second;
    try {
        const WarriorSave w = app.save().load();  // Users (CH0)
        const auto uv = w.variables.find(name);
        if (uv != w.variables.end()) return uv->second;
        const auto pv = w.variables.find("_" + name);
        if (pv != w.variables.end()) return pv->second;
    } catch (const std::exception&) {
    }
    return std::string();
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
            // JS `zt.parse` (`zi.g="81"`, bundle idx 157285): `this.HH =
            // v.su.Ucb(a) ? a : v.su.kU[0]` — the step read from the save,
            // falling back to `kU[0]` = "NotStarted" whenever the saved value
            // is absent or not a member of `v.su.kU` (`internal_settings.xml`
            // L116-126). This is the ENGINE's own default, keyed on the saved
            // value alone — NOT on any harness flag. Post-tutorial profiles
            // carry `END`, so their gate stays closed.
            out = valid_story_step(ctx.story_step) ? ctx.story_step : "NotStarted";
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
        // `Bj` L961: `case "_$CurrentScene": a.Fb.result = this.ta.Xo`. `ta.Xo`
        // is session state written by `wa.ghb` (L934) on every scene change
        // (right before SCENE_LOADED); the `Bj` ctor (L1004) initializes it to
        // "None". Read from the engine, NOT the per-event journal.
        if (token == "_$CurrentScene") {
            out = current_scene_;
            return true;
        }
        // `Bj` L964: `case "_$TimerName": let l=this.ta.dza;
        // a.Fb.result = l!=null ? l : ""`. `ta.dza` (ctor L1004 null) is
        // written by `Ct.swa` (L292) right before `QUEST_EVENT_TIMER_END`;
        // the port stores it in `timer_end_name_` (empty == the JS null -> "").
        if (token == "_$TimerName") {
            out = timer_end_name_;
            return true;
        }
        // `Bj` L964: `_$TabFrom`/`_$TabTo` read `this.ta.XNa`/`YNa`, the pair
        // `v.qwa` (L1212) writes on every screen change.
        if (token == "_$TabFrom") {
            out = ctx.journal.tab_from;
            return true;
        }
        if (token == "_$TabTo") {
            out = ctx.journal.tab_to;
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
        if (token == "_$Iterator") {
            // `zj` L1072 (`EForeach`): `this.parameters.iterator = Pg[lq]`
            // before each `Sl.compare`/`Sl.lF`.
            out = ctx.iterator;
            return true;
        }
        if (token == "_$ButtonName") {
            // `Bj` L960: `case "_$ButtonName": a.Fb.result = this.ta.Av`. `Av`
            // is written by `Vb.Qg` (L2173) before the MapButtonPress fire.
            out = ctx.journal.button_name;
            return true;
        }
        if (token == "_$ButtonType") {
            // `Bj` L961: `case "_$ButtonType": a.Fb.result = this.ta.yYa`.
            out = ctx.journal.button_type;
            return true;
        }
        // `Bj` L960 (offset 492745): `case "_$BestAcquiredArmorLevel": ... :
        // let e=p.o, f=e!=null?e.rt(Rc(b,yb.Elb,yb.Dlb),0):null;
        // a.Fb.result=f!=null?f.value:"0"`. `Rc(b,"_$","_")` (`yb.Elb="_$"`,
        // `yb.Dlb="_"`, L1274298) maps the field to the `_`-prefixed Users
        // variable; absent -> "0".
        if (token == "_$BestAcquiredArmorLevel" ||
            token == "_$BestAcquiredHelmLevel" ||
            token == "_$BestAcquiredMagicLevel" ||
            token == "_$BestAcquiredRangedLevel" ||
            token == "_$BestAcquiredWeaponLevel" || token == "_$BestArmor") {
            const std::string key = "_" + token.substr(2);
            const WarriorSave& w = ctx.live(app);
            auto it = w.variables.find(key);
            if (it == w.variables.end()) it = w.variables.find(key.substr(1));
            out = (it != w.variables.end()) ? it->second : "0";
            return true;
        }
        // `Bj` L963: `case "_$Purchase": a.Fb.result = this.ta.item!=null ?
        // this.ta.item.name : ""`. `Pa.Wz` (L1234) sets `ta.item` before the
        // `QUEST_EVENT_PURCHASE` fire.
        if (token == "_$Purchase") {
            out = ctx.journal.item;
            return true;
        }
        // `Bj.v8a` (L989): `let a=new Fb; b=this.ta.item; b!=null&&(a.M+=b.name,
        // b=this.ta.I_, b!=null&&b!=""&&(a.M+="|",a.M+=this.ta.I_)); return a.M`.
        // `Pa.Bv` (L1211) sets `ta.item`+`ta.I_` before the
        // `QUEST_EVENT_PURCHASE_UNSUCCESSFUL` fire.
        if (token == "_$PurchaseUnsuccessful") {
            out = ctx.journal.item;
            if (!ctx.journal.purchase_failure.empty()) {
                out += "|";
                out += ctx.journal.purchase_failure;
            }
            return true;
        }
        // `Bj` L494107: `case "_$Offer": let h=this.ta.eHa, k=h!=null?h.ab():""`.
        // `ta.eHa` is the offer whose item was just received
        // (`OfferItemRecieved`, `G_` L513220 -> `QUEST_EVENT_OFFER_ITEM_RECIEVED`);
        // the journal carries its name in `offer`.
        if (token == "_$Offer") {
            out = ctx.journal.offer;
            return true;
        }
        // `Bj` L961: `case "_$Deliver": a.Fb.result = this.ta.item!=null ?
        // this.ta.item.name : ""`. The SAME `ta.item` `_$Purchase` reads
        // (L963); `Pa.Wz` (L1234) sets it before the delivery/purchase fire.
        if (token == "_$Deliver") {
            out = ctx.journal.item;
            return true;
        }
        // `Bj` L961: `case "_$EnergyChange": a.Fb.result = K.T(this.ta.fja)`.
        // `ta.fja` (ctor L1004 = 0) is written at fight end; the port models no
        // XP/energy, so it stays the ctor default 0.
        if (token == "_$EnergyChange") {
            out = std::to_string(ctx.journal.energy_change);
            return true;
        }
        // `Bj` L962: `case "_$LevelUp": a.Fb.result = K.T(this.ta.t2)`.
        // `ta.t2` (ctor L1004 = 0) is the fight-end level-up flag; no XP model
        // in the port -> ctor default 0.
        if (token == "_$LevelUp") {
            out = std::to_string(ctx.journal.level_up);
            return true;
        }
        // `Bj` L963: `case "_$PerkName": a.Fb.result = this.ta.Ria`. `ta.Ria`
        // (ctor L1004 = null) is written by the perk activate/deactivate
        // handler before `QUEST_EVENT_ACTIVATE_PERK`; no such event in the port
        // -> null -> "".
        if (token == "_$PerkName") {
            out = ctx.journal.perk_name;
            return true;
        }
        // `Bj` L962: `case "_$GemsPrice": a.Fb.result = K.T(this.ta.Ilb)`.
        // `ta.Ilb` (ctor L1004 = 0), no writer -> 0.
        if (token == "_$GemsPrice") {
            out = std::to_string(ctx.journal.gems_price);
            return true;
        }
        // `Bj` L961: `case "_$ChosenLocale": a.Fb.result = this.ta.exa!=null ?
        // this.ta.exa : ""`. `ta.exa` (ctor L1005 = null) -> "".
        if (token == "_$ChosenLocale") {
            out = ctx.journal.chosen_locale;
            return true;
        }
        // `Bj` L964: `case "_$SetItem": a.Fb.result = this.ta.setItem`.
        // `ta.setItem` (ctor L1004 = "") -> "".
        if (token == "_$SetItem") {
            out = ctx.journal.set_item;
            return true;
        }
        // `Bj` L961: `case "_$ClanTutStepToRun": a.Fb.result = this.ta.H_a`.
        // `ta.H_a` (ctor L1004 = "") has no writer in the shipped bundle -> "".
        if (token == "_$ClanTutStepToRun") {
            out = ctx.journal.clan_tut_step;
            return true;
        }
        // `Bj` L961: `case "_$CurRaidFloor": a.Fb.result = K.T(this.ta.Hlb)`.
        // `ta.Hlb` (ctor L1004 = 0) has no writer -> ctor default 0.
        if (token == "_$CurRaidFloor") {
            out = std::to_string(ctx.journal.cur_raid_floor);
            return true;
        }
        // `Bj` L961: `case "_$FightAvgFPS": a.Fb.result = K.T(this.ta.J_)`.
        // `ta.J_` is the fight's average FPS, written at fight end (`flb`
        // L1213 `d.J_=c` / `kD` L1214 `a.J_=g`); the port models no frame-rate
        // average, so it stays the ctor default (L1004 = 0).
        if (token == "_$FightAvgFPS") {
            out = std::to_string(ctx.journal.fight_avg_fps);
            return true;
        }
        // `Bj` L962: `case "_$GameStarted": a.Fb.result = v.Q1?"1":"0"`. `v.Q1`
        // (L2480 = !1) is the session flag `v.owb` (L1215) sets right before
        // `v.uwb` -> `QUEST_EVENT_SESSION` (`dp.start` L1164); read from the
        // engine state the `SessionStart` fire sets.
        if (token == "_$GameStarted") {
            out = game_started_ ? "1" : "0";
            return true;
        }
        // `Bj` L962: `case "_$InEclipseMode": a.Fb.result = p.o.Yh?"1":"0"`.
        // `p.o.Yh` is the profile's `EclipseMode` attr (profile ctor L248:
        // `this.Yh=(b!=null?b:"Off")=="On"`); the port models no EclipseMode,
        // so the ctor default "Off" -> false -> "0".
        if (token == "_$InEclipseMode") {
            out = "0";
            return true;
        }
        // `Bj` L962: `case "_$InLottery": a.Fb.result = this.ta.Dab?"1":"0"`.
        // `ta.Dab` (ctor L1004 = !1) has no writer -> ctor default false -> "0".
        if (token == "_$InLottery") {
            out = ctx.journal.in_lottery ? "1" : "0";
            return true;
        }
        // `Bj` L962: `case "_$LotteryLastSpinNumber": a.Fb.result =
        // K.T(this.ta.feb)`. `ta.feb` (ctor L1004 = 0) has no writer -> 0.
        if (token == "_$LotteryLastSpinNumber") {
            out = std::to_string(ctx.journal.lottery_last_spin);
            return true;
        }
        // `Bj` L963: `case "_$PackName": a.Fb.result = this.ta.Klb`. `ta.Klb`
        // (ctor L1004 = "") has no writer -> "".
        if (token == "_$PackName") {
            out = ctx.journal.pack_name;
            return true;
        }
        // `Bj` L963: `case "_$PacksSummarySize": a.Fb.result =
        // Sy(this.ta.ME,2)`. `Sy(a,b){return a.toFixed(b)}` (L9): `ta.ME`
        // (ctor L1004 = 0) has no writer -> `(0).toFixed(2)` = "0.00".
        if (token == "_$PacksSummarySize") {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f",
                          static_cast<double>(ctx.journal.packs_summary_size));
            out = buf;
            return true;
        }
        // `Bj` L963: `case "_$RaidPurchase": a.Fb.result = this.ta.Llb`.
        // `ta.Llb` (ctor L1004 = "") has no writer -> "".
        if (token == "_$RaidPurchase") {
            out = ctx.journal.raid_purchase;
            return true;
        }
        // `Bj` L960: `case "_$ActualCoinPackItem": let d=p.items.zua;
        // a.Fb.result = d!=null ? d.name : ""`. `it.zua` (ctor = null) is the
        // coin-pack registry, populated only by `EOa` scanning `items.Dp` for
        // `Yb==I.voa`; the port ships no coin pack -> null -> "".
        if (token == "_$ActualCoinPackItem") {
            out = "";
            return true;
        }
        // `Bj` L961: `case "_$Enchantment": a.Fb.result = this.x6a()`. `x6a`:
        // `let a=new Fb; if(this.ta.Jf.iE!=""){...a.M+=Jf.iE+"|"+Jf.xja+"|"+
        // max(0,Jf.Ec-p.Dc)} return a.M`. `ta.Jf` (ctor `new qv`) defaults
        // `iE==""` -> "".
        if (token == "_$Enchantment") {
            out = "";
            return true;
        }
        // `Bj` L963: the pure no-op `break` cases — `_$PacksCount`, `_$Raid`,
        // `_$RaidAvatar`, `_$RaidId`, `_$RaidMode`, `_$RaidResult` leave
        // `a.Fb.result` untouched (ctor null) -> "".
        if (token == "_$PacksCount" || token == "_$Raid" ||
            token == "_$RaidAvatar" || token == "_$RaidId" ||
            token == "_$RaidMode" || token == "_$RaidResult") {
            out = "";
            return true;
        }
        // Other `Bj` journal fields the shell does not model -> UNKNOWN.
        note_unanswerable(token);
        return false;
    }
    if (token[0] == '_') {
        // `_Name` quest/session variable (JS `p.o.f5a` L133027): Local
        // (`ha.F().q0`) -> Global (`p.o.AG`) -> Users (`p.o.rv` = the save's
        // `<Variables>`). `wkb` (L132880) keys `rv` with `"_" + Name`; the
        // port's maps use the public Name, so the `_` is stripped. Default "0".
        const std::string name = token.substr(1);
        if (ctx.locals != nullptr) {              // Local (CH2)
            const auto lv = ctx.locals->find(name);
            if (lv != ctx.locals->end()) {
                out = lv->second;
                return true;
            }
        }
        const auto gv = global_vars_.find(name);  // Global (CH1)
        if (gv != global_vars_.end()) {
            out = gv->second;
            return true;
        }
        const WarriorSave& w = ctx.live(app);     // Users (CH0)
        const auto it = w.variables.find(name);
        if (it != w.variables.end()) {
            out = it->second;
            return true;
        }
        const auto pit = w.variables.find("_" + name);
        out = (pit != w.variables.end()) ? pit->second : "0";
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
    const std::string& quest, int depth, const std::string& iterator) {
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
            c.iterator = iterator;
            c.locals = &locals;
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
                                         locals, quest, depth + 1, iterator);
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
            // `Tn.S` (`class Tn`, factory `EFightEnd` L485079): `if(ca.Ka()!=
            // null) if(a=ba.Nj(a,Delay),a>0){...new Re(function(){b.kD(!1)},a)}
            // else ca.Ka().kD(!1)`. `ca.Ka()` = the live fight controller;
            // `kD(!1)` ends the fight. The engine holds no controller, so it
            // records the request for the fight scene (never silent).
            fx.fight_end_requests.push_back(attr_or(a.attrs, "Delay", "0"));
            ++fight_end_actions_;
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
        } else if (t == "ChangeTab") {
            // `Hn.parse` L1032: `Tab` (`this.cua`), `Focus` (`this.jN`); both
            // default "". `ba.Pc` resolves the `_$TabTo`/`_$TabFrom` journal
            // names (the only expression the shipped actions use). `Hn.S`
            // L1032: `Ay = vj.E0(name)`, `CX = vj.ifa(Ay)`.
            std::string tab = attr_or(a.attrs, "Tab");
            if (tab == "_$TabTo") tab = journal.tab_to;
            else if (tab == "_$TabFrom") tab = journal.tab_from;
            QuestTabSelect sel;
            sel.tab = tab;
            sel.focus = attr_or(a.attrs, "Focus");
            sel.tab_index = tab_index_for_name(tab);
            sel.screen_id = tab_screen_for_index(sel.tab_index);
            fx.tab_selects.push_back(std::move(sel));
        } else if (t == "Dialog") {
            // D1: the widget-building Types park the chain at this dialog
            // (`He.S` L1051) — see the park branch below.
            const std::string dlg_type = attr_or(a.attrs, "Type");
            bool dlg_queued = false;
            std::size_t dlg_queue_index = 0;
            std::string lines;
            for (const QuestAction& c : a.children) {
                if (c.tag == "Line" || c.tag == "LineButton" ||
                    c.tag == "DeliveryDelay" || c.tag == "PriceLine") {
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
                    // `He.parse` L1055: `Line`/`LineButton`/`DeliveryDelay`/
                    // `PriceLine` all build a row via `He.jkb` (L1056). The
                    // row content type is `PriceLine`->1, `LineButton`->2,
                    // every other row tag ->0.
                    const bool is_row = c.tag == "Line" || c.tag == "LineButton" ||
                                        c.tag == "DeliveryDelay" ||
                                        c.tag == "PriceLine";
                    if (is_row) {
                        std::string text = attr_or(c.attrs, "Text");
                        // `_`-refs: run-locals then the global quest variables
                        // (see `quest_var`). An unresolved ref drops the row.
                        if (!text.empty() && text[0] == '_') {
                            text = quest_var(app, locals, text);
                        }
                        // `He.jkb` (L1042): the row keeps its own caption.
                        dlg.line_buttons.push_back(attr_or(c.attrs, "ButtonText"));
                        if (!text.empty()) {
                            dlg.lines.push_back(text);
                            dlg.line_content_types.push_back(
                                c.tag == "PriceLine"   ? 1
                                : c.tag == "LineButton" ? 2
                                                        : 0);
                            // `He.jkb` L1056-1057: a row carrying `Item`/
                            // `Enchantment` becomes a row button whose nested
                            // actions are its OWN sub-`Yb` (`this.ima`). The
                            // port keeps them per row; `press_dialog` runs the
                            // list by row id (`He.dhb` L1061 `a<this.eOa`).
                            // Before this the nested `<GiveItem>` of a
                            // `DeliveryDelay` row was dropped entirely.
                            std::vector<QuestAction> row_actions;
                            if (!attr_or(c.attrs, "Item").empty() ||
                                !attr_or(c.attrs, "Enchantment").empty()) {
                                row_actions = c.children;
                            }
                            dlg.line_actions.push_back(std::move(row_actions));
                        }
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
                    } else if (c.tag == "DifficultyOf") {
                        // `He.gjb` L1058: `DifficultyOf Fight` -> `this.Yca`
                        // (resolved later by `He.Gz`).
                        dlg.difficulty_fight =
                            quest_var(app, locals, attr_or(c.attrs, "Fight"));
                    } else if (c.tag == "CheckBox") {
                        // `He.Wib` L1058-1059: the `uv` row (`this.Gg`).
                        // `<On>` -> `kY`, `<Off>` -> `jY` (`dhb` L1061
                        // `a==3`/`a==4`).
                        dlg.has_checkbox = true;
                        dlg.checkbox.text = attr_or(c.attrs, "Text");
                        dlg.checkbox.initial_value =
                            attr_or(c.attrs, "InitialValue");
                        dlg.checkbox.align_middle =
                            attr_or(c.attrs, "AlignMiddle", "0") == "1";
                        for (const QuestAction& sub : c.children) {
                            if (sub.tag == "On") {
                                dlg.checkbox.on = sub.children;
                            } else if (sub.tag == "Off") {
                                dlg.checkbox.off = sub.children;
                            }
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
            // `to` (`to.g="218"`) -> `Jpb` (L133404): `p.o.WA(ba.Pc(a,Name),
            // ba.cg(a,Value), this.CH)` for EVERY scope, then
            // `this.CH==0 && p.o.save()`. `parse` maps the `Scope` attr:
            // "Global"->CH1, "Local"->CH2, "Users"/absent->CH0. `ba.cg`
            // resolves the `Value` expression (`_`-refs + `?`-queries); a
            // plain literal passes through. `WA` (L133478): CH0 = the save's
            // `<Variables>` (persisted by `Jpb`'s `save()`), CH1 = the session
            // map `p.o.AG`, CH2 = `ha.F().aH` (`Cja` L517259).
            const std::string scope = attr_or(a.attrs, "Scope");
            const std::string name = attr_or(a.attrs, "Name");
            const std::string raw = attr_or(a.attrs, "Value");
            std::string value = raw;
            bool resolved = true;
            if (!raw.empty() && (raw[0] == '?' || raw[0] == '_')) {
                EvalCtx c;
                c.journal = journal;
                c.iterator = iterator;
                c.locals = &locals;
                c.level = journal.player_level;
                try {
                    const WarriorSave w = app.save().load();
                    c.story_step = w.story_step();
                    c.level = w.level;
                    c.save = w;
                    c.save_loaded = true;
                } catch (const std::exception&) {
                }
                std::string r;
                resolved = resolve_token(app, raw, c, r);
                if (resolved) value = r;
            }
            if (name.empty()) {
                // `qd(a,"_")` (L3387) is a no-op assertion; nothing to write.
            } else if (!resolved) {
                // JS always resolves; the port logs an unanswerable value
                // rather than persisting a raw `?`-expression (a query gap).
                fx.unknown.push_back("SetVariable:" + scope + "/" + name + "=" + raw);
            } else if (scope == "Local") {
                locals[name] = value;
            } else if (scope == "Global") {
                fx.global_vars[name] = value;
            } else {
                fx.set_vars[name] = value;  // Users (CH0) / absent -> save
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
        } else if (t == "ClickHint" || t == "SceneMenuScroll") {
            // `Nz.hi` (L487253) matches both names, so `Fe.Ij` builds
            // `"E"+name`; `Fe.S0a` (L487290) has NO `EClickHint`/
            // `ESceneMenuScroll` case -> `default:a=null` -> `Fe.Wxa()` = the
            // base `S` (L485232), whose `S(a)` only applies `Lock`/`Sound`
            // and resumes the serialized tail — it renders NOTHING. The port
            // matches: silent no-op. (Neither tag occurs in any shipped XML.)
        } else if (t == "ToggleItems") {
            // `Io` L1107 (`EToggleItems`): `Toggle` (default "on"), `Label`
            // (default ""); `p.iMa(ba.Pc(a,Label), ba.Pc(a,Toggle)=="on")`.
            // `p.iMa` L112419 -> `p.o.vq`/`tnb` L267 + `p.items.Jrb`/`hnb`
            // L167: `Jrb` equips every catalog item whose `lock` (= PackLabel,
            // `pL` L322) matches the label (`eMa` L167 -> `Ir(true)` L322
            // `this.yj=a`); `hnb` unequips them (`Ir(false)`). The port writes
            // the owned item's type slot + `equipped` flag into the save — the
            // same write the shop's `Pa.iwa` + `$o` path performs.
            const std::string label = quest_var(app, locals, attr_or(a.attrs, "Label"));
            const std::string toggle = quest_var(app, locals, attr_or(a.attrs, "Toggle", "on"));
            apply_toggle_items(app, label, toggle == "on");
            fx.toggle_items.push_back(label + "=" + (toggle == "on" ? "on" : "off"));
        } else if (t == "Discount") {
            // `Pn` L1064 (`EDiscount`): `Item`, `Percent`, `Toggle`, `Period`,
            // `Sale` (+ NewAmount/NewPrice). `getParameters` (L1065-1067)
            // resolves each attr through the formula lexer and maps it by the
            // ARG order `getParameters(a,b,f,e,c,g,h,k,d)`: `Percent`->`e`
            // (`l.Ie`), `Toggle`->`c` (`>0`), `Period`->`h`
            // (`Math.trunc(l.Ie)`), `Sale`->`d` (`l.Ie>0`). `S` (L1065) then
            // builds the `yf` at `p.o.xa.<item>.Gp` with the end time
            // `a = h.G>0 ? p.Dc + h.G + tz : 0` (`tz` = 0, L2204), `f.V4=d.G`
            // (`Sale`), `f.fE=h.G>0`, and `KA = base*((100-e.G)/100)` ONLY
            // when `e.G>0`; `p.o.xa.vu()` (L301) re-derives. `Toggle="0"`
            // clears it (`b.G.E4()`).
            const std::string ditem_raw = quest_var(app, locals, attr_or(a.attrs, "Item"));
            // `getParameters` L1066: `r=l.toString().split("|")`; `a=r[0]`
            // (the item), `q=r[1]` (the `|count`). `q!="" ? c.G=l.Ie|0 :
            // c.G=-1`.
            std::string ditem = ditem_raw;
            std::string dcount_s;
            int dcount = -1;
            const std::size_t dbar = ditem_raw.find('|');
            if (dbar != std::string::npos) {
                ditem = ditem_raw.substr(0, dbar);
                const std::string right = ditem_raw.substr(dbar + 1);
                const std::size_t dbar2 = right.find('|');
                dcount_s = (dbar2 == std::string::npos) ? right : right.substr(0, dbar2);
                if (!dcount_s.empty()) {
                    dcount = static_cast<int>(std::strtod(dcount_s.c_str(), nullptr));
                }
            }
            const std::string dtgl = quest_var(app, locals, attr_or(a.attrs, "Toggle"));
            const std::string dpct = quest_var(app, locals, attr_or(a.attrs, "Percent"));
            const std::string dper = quest_var(app, locals, attr_or(a.attrs, "Period"));
            const std::string dsale = quest_var(app, locals, attr_or(a.attrs, "Sale"));
            // `this.jsa` (`NewAmount`, default "0") -> `f.G=Math.trunc(l.Ie)`;
            // `this.O9` (`NewPrice`, default "") -> `h.G` (the resolved string).
            const std::string dnamt = quest_var(app, locals, attr_or(a.attrs, "NewAmount", "0"));
            const std::string dnpr = quest_var(app, locals, attr_or(a.attrs, "NewPrice"));
            const int percent = static_cast<int>(std::strtod(dpct.c_str(), nullptr));
            const bool don = std::strtod(dtgl.c_str(), nullptr) > 0.0;
            // `g.G=Math.trunc(l.Ie)` (Period) / `k.G=l.Ie>0` (Sale).
            const long long period =
                static_cast<long long>(std::trunc(std::strtod(dper.c_str(), nullptr)));
            const bool sale = std::strtod(dsale.c_str(), nullptr) > 0.0;
            const int new_amount =
                static_cast<int>(std::trunc(std::strtod(dnamt.c_str(), nullptr)));
            apply_discount(app, ditem, percent, don, period, sale, dcount, new_amount, dnpr);
            fx.discounts.push_back(ditem + ":" + dpct + ":toggle=" + (don ? "1" : "0") +
                                   ":period=" + dper + ":sale=" + (sale ? "1" : "0") +
                                   ":count=" + std::to_string(dcount) + ":new=" + dnamt +
                                   "/" + dnpr);
        } else if (t == "ShowMapButton") {
            // `wo` L1100-1101 (`EShowMapButton`): builds an `hg` from the
            // resolved attrs and calls `Vb.F().Lua(b,null,!0)`. `Lua` (L2167)
            // DEDUPS by name (`m.find(this.ny, d=>d.name==a.name)==null`), so
            // a repeat show of the same name is a no-op. Attributes resolve
            // via `ba.Pc` like every other action.
            const std::string bname =
                quest_var(app, locals, attr_or(a.attrs, "Name"));
            fx.map_button_shows.push_back(bname);
            bool exists = false;
            for (const EngineMapButton& mb : map_buttons_) {
                if (mb.name == bname) {
                    exists = true;
                    break;
                }
            }
            if (!exists && !bname.empty()) {
                EngineMapButton mb;
                mb.name = bname;
                mb.image = quest_var(app, locals, attr_or(a.attrs, "Image"));
                mb.timer = quest_var(app, locals, attr_or(a.attrs, "Timer"));
                mb.show_type = attr_or(a.attrs, "ShowType", "Both");
                map_buttons_.push_back(mb);
                std::fprintf(stdout, "[quest] map button show %s image=%s timer=%s\n",
                             mb.name.c_str(), mb.image.c_str(), mb.timer.c_str());
                std::fflush(stdout);
            }
        } else if (t == "HideMapButton") {
            // `bo` L1086 (`EHideMapButton`): `Vb.F().oKa(ba.Pc(a,Name))` —
            // `oKa` (L2167) finds the `ny` entry with that name, removes it and
            // drops its XML node.
            const std::string bname =
                quest_var(app, locals, attr_or(a.attrs, "Name"));
            fx.map_button_hides.push_back(bname);
            for (std::size_t i = 0; i < map_buttons_.size(); ++i) {
                if (map_buttons_[i].name == bname) {
                    map_buttons_.erase(map_buttons_.begin() +
                                       static_cast<std::ptrdiff_t>(i));
                    std::fprintf(stdout, "[quest] map button hide %s\n",
                                 bname.c_str());
                    std::fflush(stdout);
                    break;
                }
            }
        } else if (t == "ResetDuelTimer") {
            // `ho` L1090 (`EResetDuelTimer`): `Gb.reset(!1)` on the fight
            // controller. No duel-timer controller here -> record.
            fx.duel_timer_resets.push_back("reset");
        } else if (t == "ActivateTimer" || t == "Timer") {
            // `yj` L1024-1025 (`ETimer`/`EActivateTimer`): Name + Value +
            // Absolute. `S` does `p.o.yl.Uaa(name, Absolute ? trunc(kc(Value))
            // : p.Dc + trunc(expr))` — `Uaa` (`bva` L291) REPLACES any
            // same-name timer; a zero ABSOLUTE value is a no-op (L1025
            // `a!=0 && Uaa`). Mutated live (the JS `S` runs during the pass),
            // so a later `?Timer` in the same pass sees it.
            const std::string tname =
                quest_var(app, locals, attr_or(a.attrs, "Name"));
            const std::string tval =
                quest_var(app, locals, attr_or(a.attrs, "Value"));
            const double v = is_numeric(tval) ? to_number(tval) : 0.0;
            const bool abs = attr_bool01(attr_or(a.attrs, "Absolute", "0"));
            if (abs) {
                const double d = std::trunc(v);
                if (d != 0.0) timer_activate(tname, d);
            } else {
                timer_activate(tname, quest_now() + std::trunc(v));
            }
            fx.timer_sets.push_back(tname + "=" + tval +
                                    (abs ? " (abs)" : ""));
        } else if (t == "EndTimer") {
            // `Rn` L1069 (`EEndTimer`): `p.o.yl.H4(Name)` with NO 2nd arg, so
            // `H4`'s `swa` is gated off — the timer is removed WITHOUT firing
            // `TimerEnd`. Name is the RAW attribute (JS never `Pc`-resolves it).
            const std::string tname = attr_or(a.attrs, "Name");
            timer_end(tname);
            fx.timer_ends.push_back(tname);
        } else if (t == "Foreach") {
            // `zj` L1072 (`EForeach`): `Sl = ha.F().AD(Name)` (`ha.AD`
            // L522769 = `m.find(this.OJa, b=>b.name==a)` — the NAMED
            // sub-quest), `p0a()` fills `Pg` from the collection by `Type`
            // (`getType` L1072), and `dLa`/`Qh` loop: bind
            // `parameters.iterator = Pg[lq]`, `Sl.compare(parameters)` (the
            // sub-quest's conditions) and on match `Sl.lF(parameters,!1)`
            // (its actions). The port runs the named quest's conditions +
            // actions once per iterator with `_$Iterator` bound.
            const std::string fname = attr_or(a.attrs, "Name");
            const std::string ftype = attr_or(a.attrs, "Type");
            const QuestDef* sub = nullptr;
            for (const QuestDef& q : quests_) {
                if (q.name == fname) {
                    sub = &q;
                    break;
                }
            }
            if (sub == nullptr) {
                fx.unknown.push_back("Foreach:" + ftype + "/" + fname +
                                     " (no such sub-quest)");
            } else {
                std::vector<std::string> items;
                if (ftype == "Battles") {
                    // `Tob` L1072: `p.F().Jm` battle list (`battle_zone_`).
                    for (const auto& kv : battle_zone_) items.push_back(kv.first);
                } else if (ftype == "Items") {
                    // `$ob` L1072: `p.o.xa.items` slot names (`ab()`).
                    static const char* const kSlots[] = {
                        "Head",  "Fists", "Body",  "Armor", "Weapon",
                        "Helm",  "Ranged", "Magic", "NoRanged", "NoMagic"};
                    for (const char* s : kSlots) items.push_back(s);
                } else {
                    fx.unknown.push_back("Foreach:" + ftype + "/" + fname +
                                         " (collection not modelled)");
                }
                for (const std::string& item : items) {
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
                    c.iterator = item;
                    c.locals = &locals;
                    if (!conditions_hold(app, sub->root, c)) continue;
                    ActionRest s = run_actions(app, sub->actions, journal, fx, locals,
                                               fname, depth + 1, item);
                    if (s.suspended) {
                        s.rest.insert(s.rest.end(), acts.begin() + i + 1, acts.end());
                        return s;
                    }
                    ++foreach_matches_;
                    fx.foreach_runs.push_back(ftype + "/" + fname + ":" + item);
                }
            }
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
            // `Yn.S` (offset 554285; factory `EGiveItem` L485299): split the
            // resolved `Name` on `|` (`li(...,"|")`) — left = item, right =
            // `K.parseInt` count; then `Pa.W$a(name, count, ba.UBa(a,Quantity),
            // ba.Zv(a,PutOn), PackItem)` (L631756) -> `bDa` grants the item.
            EvalCtx gc;
            gc.journal = journal;
            gc.iterator = iterator;
            gc.locals = &locals;
            gc.level = journal.player_level;
            try {
                const WarriorSave w = app.save().load();
                gc.story_step = w.story_step();
                gc.level = w.level;
                gc.save = w;
                gc.save_loaded = true;
            } catch (const std::exception&) {
            }
            const std::string raw_name = attr_or(a.attrs, "Name");
            std::string gname = raw_name;
            bool gok = true;
            if (!raw_name.empty() &&
                (raw_name.find('?') != std::string::npos || raw_name[0] == '_')) {
                gok = resolve_token(app, raw_name, gc, gname);  // `ba.Pc`
            }
            if (!gok) {
                // A genuinely unanswerable Name (an unmodelled op or a missing
                // variable) stays UNKNOWN — never invent one. `?Concat`/`?Slice`
                // ARE modelled (`QNa` L956-957; dispatch `fAa` L967
                // `case "Concat":this.QNa(b,a,1)` / L969 `case "Slice"`), so
                // the shipped `?Concat[ITEM|,?Sum[...]]` names resolve here and
                // grant (probe `[qa] GIVEITEM CONCAT ... PASS`).
                fx.unknown.push_back("GiveItem (Name unresolved): " + raw_name);
            } else {
                int gcount = 0;
                const std::size_t bar = gname.find('|');
                if (bar != std::string::npos) {
                    gcount = parse_int_or(gname.substr(bar + 1), 0);
                    gname = gname.substr(0, bar);
                }
                int quantity = 0;
                const std::string qattr = attr_or(a.attrs, "Quantity");
                if (!qattr.empty()) {
                    std::string qv;
                    if (resolve_token(app, qattr, gc, qv) && is_numeric(qv)) {
                        quantity = static_cast<int>(to_number(qv));
                    }
                }
                if (gname.empty()) {
                    fx.unknown.push_back("GiveItem (empty Name)");
                } else {
                    QuestGiveItem gi;
                    gi.name = gname;
                    gi.count = gcount;
                    gi.quantity = quantity;
                    gi.put_on = attr_bool01(attr_or(a.attrs, "PutOn"));
                    fx.give_items.push_back(std::move(gi));
                }
            }
        } else if (t == "BuyItem") {
            // `sh` (`EBuyItem` g="1D4" L526589; parse L526589, `S` L526709):
            //   `Name` -> `Ba`; `Currency` -> `SB`: Coins=1 / Ruby=2 / Real=3.
            //   `eBa(a)` = `p.items.$b(ba.cg(a, Ba))` — resolve `Name`, then the
            //   catalog lookup (a miss is a NO-OP, `b!=null` gate).
            //   `SB!=3` -> `v.fZ(item, SB)` (L620099 -> `VYa` -> `YDa` L107236
            //   commits the currency + grant, then `Pa.Wz` fires
            //   `QUEST_EVENT_PURCHASE`) and `this.sa()`. `YDa` case 1/2 reads
            //   `p.o.Tb` (money) / `p.o.fd` (bonus = the Ruby balance) and
            //   gates on `a.xf <= e.bb()` (level) + `f-g >= 0` (affordable);
            //   `v.Bv` fires `PurchaseUnsuccessful` on failure.
            //   `SB==3` (Real) is the async store handshake (`ub.si`/`ub.rm`)
            //   — no store in the port, so record it (cf. `BuyOffer`).
            EvalCtx bc;
            bc.journal = journal;
            bc.iterator = iterator;
            bc.locals = &locals;
            bc.level = journal.player_level;
            try {
                const WarriorSave w = app.save().load();
                bc.story_step = w.story_step();
                bc.level = w.level;
                bc.save = w;
                bc.save_loaded = true;
            } catch (const std::exception&) {
            }
            const std::string raw_bi = attr_or(a.attrs, "Name");
            std::string bi_name = raw_bi;
            bool bi_ok = true;
            if (!raw_bi.empty() &&
                (raw_bi.find('?') != std::string::npos || raw_bi[0] == '_')) {
                bi_ok = resolve_token(app, raw_bi, bc, bi_name);  // `ba.cg`
            }
            const std::string bi_cur = attr_or(a.attrs, "Currency");
            const int sb = bi_cur == "Coins"   ? 1
                           : bi_cur == "Ruby"  ? 2
                           : bi_cur == "Real"  ? 3
                                               : 0;
            const CatalogItem* bi_cat =
                bi_ok && !bi_name.empty() ? catalog_find(app, bi_name) : nullptr;
            if (!bi_ok || bi_name.empty()) {
                fx.unknown.push_back("BuyItem (Name unresolved): " + raw_bi);
            } else if (bi_cat == nullptr) {
                // `p.items.$b` miss -> `b==null` -> the action does nothing.
                fx.unknown.push_back("BuyItem (not in catalog): " + bi_name);
            } else if (sb == 3) {
                fx.unknown.push_back("BuyItem:Real (store async): " + bi_name);
            } else {
                // `YDa` L107236: case 1 (`Coins`) -> `p.o.Tb`; case 2 (`Ruby`)
                // -> `p.o.fd`; `g = a.jp()`/`a.nn()` (the resolved price).
                const std::int64_t price =
                    sb == 2 ? catalog_bonus_price(app, bi_name) : bi_cat->price;
                const std::int64_t have = sb == 2 ? bc.save.bonus
                                                  : sb == 1 ? bc.save.money : 0;
                if (bc.save_loaded && sb != 0 && have >= price) {
                    QuestSideEffects::QuestCurrencyWrite cw;
                    cw.type = sb == 2 ? "Bonus" : "Gold";  // `vl` / `Fr`
                    cw.amount = price;
                    cw.take = true;
                    cw.apply = true;
                    fx.currency_writes.push_back(std::move(cw));
                    QuestGiveItem gi;
                    gi.name = bi_name;
                    gi.quantity = 1;  // `gI` -> `fab` grants one
                    fx.give_items.push_back(std::move(gi));
                    fx.purchases.push_back(bi_name);
                    std::fprintf(stdout,
                                 "[quest] BuyItem %s currency=%s price=%d -> buy\n",
                                 bi_name.c_str(), bi_cur.c_str(), price);
                    std::fflush(stdout);
                } else {
                    // `YDa` value==-1 -> `v.Bv(a, e.type)`: record only (the
                    // port has no `PurchaseUnsuccessful` observable here).
                    fx.unknown.push_back("BuyItem (unaffordable): " + bi_name);
                }
            }
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
            // the app clock (see `tutorial_gate_beat`), but gate on the REAL
            // live-tutorial predicate (`zt.VQ()`, bundle idx 156971: `return
            // this.HH != "END"`), NOT a harness flag, so the shipped fresh
            // profile serializes without any arm. The `lJ()` animation shortcut
            // (`ca.Ka()!=null && Ra.length>=1` — an ACTIVE FIGHT) never holds in
            // the Dojo beats, so `TutorialStepTimeout` + the `p.o.zi.LE`
            // step-change (`fire`) are the only resume sources here.
            // Depth 0 only: both lessons sit at the top level of
            // `StoryTutorialWelcome`'s `<Actions>`, so the parked tail is
            // always complete (no outer remainder to re-attach).
            if (depth == 0 && tutorial_live(app) && !tutorial_gate_.active &&
                (t == "StoryTutorialMove" || t == "StoryTutorialPunchbag" ||
                 t == "StoryTutorialDoubleSweep" || t == "StoryTutorialShowBlock")) {
                tutorial_gate_.active = true;
                // `Do`=1, `Eo`=2 (the welcome lessons), `Bo`=3 (the double
                // sweep), `Fo`=4 (the block). All four are the JS actions whose
                // `S()` does NOT call `this.sa()` (L1123/L1125/L1121/L1126):
                // the chain WAITS for the `Re(Cm, TutorialStepTimeout)` timer
                // (or the `p.o.zi.LE` step change) before resuming. Before this
                // the dojo beats ran straight through (record-only), so the
                // lesson never "acted".
                tutorial_gate_.beat =
                    (t == "StoryTutorialMove")       ? 1
                    : (t == "StoryTutorialPunchbag") ? 2
                    : (t == "StoryTutorialDoubleSweep") ? 3
                                                        : 4;
                tutorial_gate_.remaining = kTutorialStepTimeoutSec;
                tutorial_gate_.rest.assign(acts.begin() + i + 1, acts.end());
                tutorial_gate_.journal = journal;
                tutorial_gate_.locals = locals;
                tutorial_gate_.quest = quest;
                try {
                    tutorial_gate_.step_at_park = app.save().load().story_step();
                } catch (const std::exception&) {
                }
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
        } else if (t == "CheckOffersStart") {
            // `Jn` L531140 (`ECheckOffersStart`): `p.Cw.a_a()` — start the
            // eligible offers (`Qba` -> `pwb`). See `offer_check_start`.
            offer_check_start(app);
            fx.offer_actions.push_back("CheckOffersStart");
        } else if (t == "ChangeOfferState") {
            // `En` L528761 (`EChangeOfferState`): `<ChangeOfferState Name Value>`.
            const std::string oname = quest_var(app, locals, attr_or(a.attrs, "Name"));
            const std::string oval = quest_var(app, locals, attr_or(a.attrs, "Value"));
            offer_change_state(app, oname, oval);
            fx.offer_actions.push_back("ChangeOfferState:" + oname + "=" + oval);
        } else if (t == "CheckItemsFromPurchasedOffers") {
            // `In` L530984 (`ECheckItemsFromPurchasedOffers`): `p.Cw.TZa()`.
            offer_check_purchased(app);
            fx.offer_actions.push_back("CheckItemsFromPurchasedOffers");
        } else if (t == "BuyOffer") {
            // `Dn` L528334 (`EBuyOffer`): the store purchase handshake
            // (`ub.si`/`ub.rm` purchase events; `S` waits for the callback).
            // No store in the port -> record only (the port's own purchase
            // path drives `tlb` via `purchase`).
            const std::string bname = quest_var(app, locals, attr_or(a.attrs, "Name"));
            fx.offer_actions.push_back("BuyOffer:" + bname);
        } else if (t == "RestoreOfferItemsPerks") {
            // `RestoreOfferItemsPerks` (offers.xml L360): grants the perks of
            // purchased-offer items on the zone roll. No per-offer perk grant
            // in the port -> record only.
            fx.offer_actions.push_back("RestoreOfferItemsPerks");
        } else if (t == "UnlockCharacter") {
            // `Ko` (`EUnlockCharacter` g="210", factory L486887): `parse` reads
            // `Name` and DISCARDS it (`a.attributes.get("Name")` with no
            // assignment); `S(a){super.S(a); this.sa()}` is a NO-OP in the
            // shipped build. Nothing to execute (cf. `ShowNews`).
        } else if (t == "GiveCurrency" || t == "TakeCurrency") {
            // `Xn` (`EGiveCurrency` g="1F1" L554263) / `rg` (`ETakeCurrency`
            // g="20C" L567355): `tfa`/`lp` resolve `Type`, `Value` (`ab`/`JD`
            // the `Name`), then
            //   `Gold`  -> `p.o.Fr(p.o.Tb +/- v)`   (`money`)
            //   `Bonus` -> `p.o.vl(p.o.fd +/- v, 6)` (`bonus`)
            //   else    -> `p.o.TH(Type, +/- v)`     (`currencies[Type]`).
            // `rg` first gates on `p.o.Xfa(Type,Name,v)` (`Xbb` affordability +
            // `Y5a` current >= v); on failure the `<Error>` chain runs and
            // `J0a` is NOT called.
            EvalCtx cc;
            cc.journal = journal;
            cc.iterator = iterator;
            cc.locals = &locals;
            cc.level = journal.player_level;
            try {
                const WarriorSave w = app.save().load();
                cc.story_step = w.story_step();
                cc.level = w.level;
                cc.save = w;
                cc.save_loaded = true;
            } catch (const std::exception&) {
            }
            const auto resolve_cur = [&](const std::string& raw, std::string& out) {
                out = raw;
                if (raw.empty()) return true;
                if (raw.find('?') != std::string::npos || raw[0] == '_') {
                    return resolve_token(app, raw, cc, out);
                }
                return true;
            };
            std::string ctype;
            std::string cval;
            resolve_cur(attr_or(a.attrs, "Type"), ctype);
            resolve_cur(attr_or(a.attrs, "Value"), cval);
            const std::int64_t amount =
                is_numeric(cval) ? static_cast<std::int64_t>(to_number(cval)) : 0;
            QuestSideEffects::QuestCurrencyWrite cw;
            cw.type = ctype;
            cw.amount = amount;
            if (t == "TakeCurrency") {
                std::string cname;
                resolve_cur(attr_or(a.attrs, "Name"), cname);
                cw.name = cname;
                cw.take = true;
                // `Xbb(a,b)`: "Bonus"/"Gold" always resolvable; "Currency" ->
                // `rea(b)` (the named currency exists); default -> `rea(a)`.
                const std::string key = ctype == "Currency" ? cname : ctype;
                bool known = ctype == "Gold" || ctype == "Bonus";
                if (!known && !key.empty()) {
                    known = cc.save_loaded &&
                            cc.save.currencies.find(key) != cc.save.currencies.end();
                }
                std::int64_t have = 0;
                if (known) {
                    if (ctype == "Gold") {
                        have = cc.save.money;
                    } else if (ctype == "Bonus") {
                        have = cc.save.bonus;
                    } else {
                        const auto it = cc.save.currencies.find(key);
                        have = it != cc.save.currencies.end() ? it->second : 0;
                    }
                }
                cw.apply = known && have >= amount;  // `Xfa` -> `Y5a >= c`
            }
            fx.currency_writes.push_back(std::move(cw));
        } else if (t == "SetDataVersion") {
            // `po` (`ESetDataVersion` g="1E0", factory L950; parse/`S` L1039):
            //   b = `R6a` = Full resolved, kept only when it has exactly 3 dots;
            //   when null/empty: b = `u7a` = Production.Major.Minor.DataVersion;
            //   then `p.F().Oqb(b)` (L181) -> ROOT `<Versions><DataVersion Value>`.
            // `ba.cg` token-substitutes each part (literals pass through).
            const auto part = [&](const char* key) -> std::string {
                const std::string raw = attr_or(a.attrs, key);
                if (raw.empty()) return raw;
                if (raw.find('?') == std::string::npos && raw[0] != '_') return raw;
                EvalCtx cc;
                cc.journal = journal;
                cc.iterator = iterator;
                cc.locals = &locals;
                cc.level = journal.player_level;
                try {
                    const WarriorSave w = app.save().load();
                    cc.level = w.level;
                    cc.save = w;
                    cc.save_loaded = true;
                } catch (const std::exception&) {
                }
                std::string out = raw;
                if (!resolve_token(app, raw, cc, out)) out = raw;
                return out;
            };
            std::string ver;
            const std::string full = part("Full");
            int dots = 0;
            for (char cch : full) {
                if (cch == '.') ++dots;
            }
            if (!full.empty() && dots == 3) ver = full;
            if (ver.empty()) {
                ver = part("Production") + "." + part("Major") + "." +
                      part("Minor") + "." + part("DataVersion");
            }
            fx.data_version_writes.push_back(ver);
        } else if (t == "GivePerk") {
            // `$n` (`EGivePerk`, factory L950; class ends L555926): `parse`
            // reads `ApplyTo`/`Item` and clones the node. `S` resolves `ApplyTo`
            // -> "Item" (`RWa`, enchant path) | "Player" (`jXa` -> `C1a`).
            // Player: `C1a` walks `<Perk Name Level UpgradeLevel>` and, when the
            // perk exists in the catalog (`d8a`), records it via `p.o.co.K1a`.
            const auto resolve_apply = [&](const std::string& raw) -> std::string {
                if (raw.empty()) return raw;
                if (raw.find('?') == std::string::npos && raw[0] != '_') return raw;
                EvalCtx cc;
                cc.journal = journal;
                cc.iterator = iterator;
                cc.locals = &locals;
                cc.level = journal.player_level;
                try {
                    const WarriorSave w = app.save().load();
                    cc.level = w.level;
                    cc.save = w;
                    cc.save_loaded = true;
                } catch (const std::exception&) {
                }
                std::string out = raw;
                if (!resolve_token(app, raw, cc, out)) out = raw;
                return out;
            };
            const std::string apply_to = resolve_apply(attr_or(a.attrs, "ApplyTo"));
            if (apply_to == "Player") {
                // JS `$n.parse` L555480: `this.ga = a.st()` where
                // `st(){return this.children[0]}` (L1262113) — ONLY the first
                // child is cloned; `jXa` -> `C1a` walks that node. So a
                // `<GivePerk>` carrying several `<Perk>` children grants just
                // the FIRST one (the port granted every row before).
                for (const QuestAction& ch : a.children) {
                    if (ch.tag != "Perk") continue;
                    QuestSideEffects::PerkGrant g;
                    g.name = resolve_apply(attr_or(ch.attrs, "Name"));
                    const std::string lv = resolve_apply(attr_or(ch.attrs, "Level"));
                    const std::string uv =
                        resolve_apply(attr_or(ch.attrs, "UpgradeLevel"));
                    g.level = is_numeric(lv) ? static_cast<int>(to_number(lv)) : 0;
                    g.upgrade = is_numeric(uv) ? static_cast<int>(to_number(uv)) : 0;
                    const bool in_catalog =
                        app.has_fight_assets() &&
                        app.fight_assets().perk_catalog.count(g.name) != 0;
                    if (in_catalog) fx.perk_grants.push_back(std::move(g));
                    break;  // `a.st()` = `children[0]`: the first `<Perk>` only
                }
            } else if (apply_to == "Item") {
                // `RWa` (`$n` L555926): `this.ga = a.st().clone()` = the clone
                // of `children[0]` (`st(){return this.children[0]}` L1262115).
                // `Lxa` copies the node's attrs+children; `Eba` evaluates every
                // DESCENDANT attr (not the root's own). `b.fc(this.Bo,a)` then
                // evaluates `Item` and `Pa.cDa(item,[xe.Qd(clone)])` runs the
                // grant. Shipped shape (item_restore_quests.xml:82,
                // quests.xml:3344):
                //   `<GivePerk ApplyTo="Item" Item="X">
                //      <Perk Name=".."><Set .. /></Perk></GivePerk>`
                // — the `<Perk>` IS the `children[0]` model, so `Name`/
                // `ItemType` are read raw and the `<Set>` attrs are evaluated.
                const std::string item =
                    resolve_apply(attr_or(a.attrs, "Item"));  // `this.Bo`
                if (a.children.empty() || item.empty()) {
                    fx.unknown.push_back(
                        item.empty() ? std::string("GivePerk:Item")
                                     : ("GivePerk:Item:" + item));
                } else {
                    const QuestAction& model = a.children.front();  // `a.st()`
                    QuestSideEffects::ItemEnchantGrant eg;
                    eg.item = item;
                    // `xe.Qd` L692882: `Name` = the node's `Name` attr (the
                    // root attr is NOT touched by `Eba`, so kept raw).
                    eg.ench.name = attr_or(model.attrs, "Name");
                    // `xe.Qd`: `ItemType.split("|")` -> `g2`.
                    const std::string it = attr_or(model.attrs, "ItemType");
                    if (!it.empty()) {
                        std::size_t s = 0;
                        for (;;) {
                            const std::size_t bar = it.find('|', s);
                            if (bar == std::string::npos) {
                                eg.ench.item_types.push_back(it.substr(s));
                                break;
                            }
                            eg.ench.item_types.push_back(it.substr(s, bar - s));
                            s = bar + 1;
                        }
                    }
                    // `xe.Qd`: `a.A("Set")` -> `ll` (the `<Set>` attrs, in
                    // map order; JS uses document order, identical for the
                    // shipped single-attr `<Set Aspect=..>`).
                    for (const QuestAction& ch : model.children) {
                        if (ch.tag != "Set") continue;
                        for (const auto& kv : ch.attrs) {
                            eg.ench.sets.push_back({kv.first, resolve_apply(kv.second)});
                        }
                    }
                    if (eg.ench.name.empty()) {
                        fx.unknown.push_back("GivePerk:Item:" + item);
                    } else {
                        fx.enchant_grants.push_back(std::move(eg));
                    }
                }
            } else {
                fx.unknown.push_back(t);
            }
        } else if (t == "Checkpoint") {
            // `Ln` (`ECheckpoint`, L531194): `iLa` upserts the resume point
            // (`p.o.HBa(ZE)` find by quest name, else `p.o.WO(ZE,K_)` create)
            // and `setParameters(action, Faa, index)` then `p.o.save()`.
            // `ZE` = the quest name (`S.initialize(this.name,…)` L483025);
            // `K_` = the quest file; `Faa` = the quest `k7` (Place); `index`
            // = the action's ORDINAL in `<Actions>` (`Haa` L517802:
            // `a.index=d`, `d` = the `for(c=0;c<a.length;)` counter over the
            // `<Actions>` children, L517673). All are read off the QuestDef by
            // name (nested sub-quests pass their file as `quest`, which the
            // name lookup simply misses -> 0/empty).
            QuestSideEffects::Checkpoint cp;
            cp.quest_name = quest;
            cp.checkpoint_index = static_cast<int>(i);  // `Haa` L517802: `a.index=d`
            for (const QuestDef& qd : quests_) {
                if (qd.name == quest) {
                    cp.file_name = qd.file;
                    cp.screen_index = qd.place;
                    break;
                }
            }
            fx.checkpoints.push_back(std::move(cp));
        } else if (t == "UpdateShopItems") {
            // `Po` (`EUpdateShopItems`, L570290): `Oa.get()!=null && a.Imb()`.
            fx.update_shop_items = true;
        } else if (t == "Line" || t == "Button" || t == "Then" || t == "Else" ||
                   t == "Conditions") {
            ActionRest sub = run_actions(app, a.children, journal, fx, locals, quest,
                                         depth + 1, iterator);
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
        // `to.Jpb` CH1 (L133404): the `Scope="Global"` writes land in the
        // session map `p.o.AG` only — `WA` (L133478) does `this.AG.set(a,d)`
        // and `Jpb` skips `save()` unless `CH==0`. Apply in memory, no dirty.
        for (const auto& kv : fx.global_vars) {
            if (!kv.first.empty()) global_vars_[kv.first] = kv.second;
        }
        // `Xn`/`rg` (`EGiveCurrency` g="1F1" / `ETakeCurrency` g="20C") save
        // writes: `Gold` -> `p.o.Fr(p.o.Tb +/- v)` (`money`), `Bonus` ->
        // `p.o.vl(p.o.fd +/- v, 6)` (`bonus`), else `p.o.TH(type, +/- v)`
        // (`currencies[type]`; the "Currency" subtype keys on the resolved
        // `Name`). `rg` with `apply=false` is the `<Error>` branch — no write.
        for (const QuestSideEffects::QuestCurrencyWrite& cw : fx.currency_writes) {
            if (!cw.apply) continue;
            const std::int64_t d = cw.take ? -cw.amount : cw.amount;
            if (d == 0) continue;
            if (cw.type == "Gold") {
                w.money += d;  // `Fr`
                dirty = true;
            } else if (cw.type == "Bonus") {
                w.bonus += d;  // `vl` (no clamp; `vl` just writes)
                dirty = true;
            } else {
                const std::string key = cw.type == "Currency" ? cw.name : cw.type;
                if (!key.empty()) {
                    w.currencies[key] += d;  // `TH` -> `Hua`/`GLa` count+delta
                    dirty = true;
                }
            }
        }
        // `po` (`ESetDataVersion`): `Oqb` (L181) writes the ROOT
        // `<Versions><DataVersion Value>` and saves the whole document.
        for (const std::string& dv : fx.data_version_writes) {
            try {
                app.save().set_data_version(dv);
                std::fprintf(stdout, "[quest] DataVersion -> %s\n", dv.c_str());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[quest] DataVersion write failed: %s\n",
                             e.what());
            }
        }
        // `$n` `GivePerk` `ApplyTo="Player"` (`C1a` -> `p.o.co.K1a` L154884):
        // record each `<Perk Name Level UpgradeLevel>` row (`Ji` -> `<Perks>`).
        for (const QuestSideEffects::PerkGrant& pg : fx.perk_grants) {
            if (pg.name.empty()) continue;
            w.learn_perk(pg.name, pg.level, pg.upgrade);
            dirty = true;
        }
        // `$n` GivePerk `ApplyTo="Item"` (`RWa` L555926 -> `Pa.cDa` L632540 ->
        // `dDa` L632561 -> `rf(item).VXa(models)` L647359 + `Kia` + save):
        // `cDa` gates on `p.items.$b(item)!=null`; `dDa` on the owned holder
        // (`rf(def.name)` = `p.o.xa.Qj`, `Qj` L151521) existing. `VXa` ->
        // `anb` (L647337) removes an existing `<Perk>` with the same `Name`,
        // then `mY` (L646355) appends the new one under `<Enchantments>`.
        for (const QuestSideEffects::ItemEnchantGrant& eg : fx.enchant_grants) {
            if (eg.item.empty() || eg.ench.name.empty()) continue;
            const CatalogItem* ci = catalog_find(app, eg.item);  // `$b` L82463
            if (ci == nullptr) continue;  // no catalog def -> no-op
            WarriorSave::OwnedItem* owned = nullptr;  // `rf(def.name)`
            for (WarriorSave::OwnedItem& it : w.items) {
                if (it.name == ci->name) {
                    owned = &it;
                    break;
                }
            }
            if (owned == nullptr) continue;  // holder absent -> no-op
            for (auto it2 = owned->enchantments.begin();
                 it2 != owned->enchantments.end();) {
                if (it2->name == eg.ench.name) {
                    it2 = owned->enchantments.erase(it2);  // `anb`
                } else {
                    ++it2;
                }
            }
            owned->enchantments.push_back(eg.ench);  // `mY`
            dirty = true;
            std::fprintf(stdout,
                         "[quest] GivePerk item enchant %s <- %s (%zu set)\n",
                         ci->name.c_str(), eg.ench.name.c_str(),
                         eg.ench.sets.size());
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
        // `Yn` GiveItem -> `Pa.W$a` (L631756) -> `bDa`: the item lands in the
        // inventory (`Pa.Kua`); `PutOn` equips it. The shipped promo forms are
        // `Name="ITEM|count"` with no `Quantity`, whose JS net is an UNEQUIPPED
        // grant carrying the stack count (`bDa` auto-equips only when the item
        // level <= the player's, then `c<=0&&!d` unequips).
        for (const QuestGiveItem& gi : fx.give_items) {
            if (gi.name.empty()) continue;
            const CatalogItem* cat = catalog_find(app, gi.name);
            WarriorSave::OwnedItem* owned = nullptr;
            for (WarriorSave::OwnedItem& it : w.items) {
                if (it.name == gi.name) {
                    owned = &it;
                    break;
                }
            }
            if (owned == nullptr) {
                WarriorSave::OwnedItem oi;
                oi.name = gi.name;
                oi.count = gi.quantity > 0 ? gi.quantity : 1;
                if (gi.count > 0) oi.upgrade_level = gi.count;  // `Np`
                w.items.push_back(std::move(oi));
                owned = &w.items.back();
            } else {
                if (gi.quantity > 0) owned->count += gi.quantity;
                else if (gi.count > 0) owned->count = gi.count;
                if (gi.count > 0) owned->upgrade_level = gi.count;
            }
            if (gi.put_on && cat != nullptr) {
                if (cat->type == "Weapon") w.weapon = gi.name;
                else if (cat->type == "Armor") w.armor = gi.name;
                else if (cat->type == "Helm") w.helm = gi.name;
                else if (cat->type == "Ranged") w.ranged = gi.name;
                else if (cat->type == "Magic") w.magic = gi.name;
                owned->equipped = true;
            } else {
                owned->equipped = false;  // `c<=0&&!d` -> `Ir(!1)`
            }
            dirty = true;
            std::fprintf(stdout, "[quest] GiveItem %s count=%d qty=%d putOn=%d\n",
                         gi.name.c_str(), gi.count, gi.quantity, gi.put_on ? 1 : 0);
        }
        for (const std::string& d : fx.fight_end_requests) {
            std::fprintf(stdout,
                         "[quest] FightEnd request (Delay=%s) -> fight scene\n",
                         d.c_str());
        }
        // `Ln` `Checkpoint` (`iLa` L531194 -> `HBа`/`WO`/`setParameters`):
        // upsert the resume point into `<Quests><Quests><Quest Name FileName>`
        // (find by quest name, else append) and write the `QuestParameters`
        // `ScreenIndex`/`ChekPointIndex` (both written by `fl`, L144813).
        for (const QuestSideEffects::Checkpoint& cp : fx.checkpoints) {
            if (cp.quest_name.empty()) continue;
            WarriorSave::QuestState* found = nullptr;
            for (WarriorSave::QuestState& qs : w.quests) {
                if (qs.name == cp.quest_name) {
                    found = &qs;
                    break;
                }
            }
            if (found == nullptr) {
                WarriorSave::QuestState qs;
                qs.name = cp.quest_name;
                w.quests.push_back(std::move(qs));
                found = &w.quests.back();
            }
            if (!cp.file_name.empty()) found->file_name = cp.file_name;
            found->screen_index = cp.screen_index;
            found->checkpoint_index = cp.checkpoint_index;
            // `fl` ctor (L114518): `Et.setParameters` appends `QuestParameters`
            // (`this.node.appendChild("QuestParameters")`) and the `fl` ctor
            // force-defaults `ScreenIndex`/`ChekPointIndex` to "0", so the node
            // EXISTS after any Checkpoint — even a 0/0 one.
            found->has_parameters = true;
            dirty = true;
            ++checkpoint_actions_;
            std::fprintf(stdout,
                         "[quest] Checkpoint quest=%s file=%s screen=%d index=%d\n",
                         cp.quest_name.c_str(), found->file_name.c_str(),
                         cp.screen_index, cp.checkpoint_index);
            std::fflush(stdout);
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
        // `sh` `BuyItem` `SB!=3` (`S` L526709 -> `v.fZ` -> `VYa` L620099 ->
        // `Pa.Wz` L1234): the commit succeeded, so fire `QUEST_EVENT_PURCHASE`
        // for each bought item — the SAME call the Shop's buy uses
        // (`ShopScreen::purchase_price_plate`), AFTER the save so a purchase
        // quest reading `?Purchase(_$Purchase).*` sees the committed state.
        for (const std::string& bought : fx.purchases) {
            ++purchase_actions_;
            (void)purchase(app, bought);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[quest] save apply failed: %s\n", e.what());
    }
}

// --- `Ct` timer registry (JS `p.o.yl`, L291-292) --------------------------
// `Uaa` (L291) -> `bva`: `this.H4(a.name); this.tq.set(a.name, a)` — replace.
void QuestEngine::timer_activate(const std::string& name, double deadline) {
    if (name.empty()) return;
    timers_.erase(name);
    timers_[name] = deadline;
}
// `H4` (L291): remove by name; the JS 2nd arg (which would `swa`) is absent.
void QuestEngine::timer_end(const std::string& name) {
    if (name.empty()) return;
    timers_.erase(name);
}
// `t_a` (L292): fire `TimerEnd` for every `Nv <= now`, THEN remove them.
void QuestEngine::tick_timers(App& app, double now) {
    if (timers_.empty()) return;
    std::vector<std::string> expired;
    for (const auto& kv : timers_) {
        if (kv.second <= now) expired.push_back(kv.first);
    }
    for (const std::string& name : expired) {
        // `$Za` L180xxx (the `p.o.yl.iIa` listener): an expired
        // `OfferTimer_<name>` flips the offer to LastChance/End (`C3a`).
        static const std::string kOfferTimer = "OfferTimer_";
        if (name.size() > kOfferTimer.size() &&
            name.compare(0, kOfferTimer.size(), kOfferTimer) == 0) {
            offer_timer_expired(app, name.substr(kOfferTimer.size()));
        }
        // `swa(a)` (L292): `this.iIa.Z(a); ha.F().ta.dza=a;
        // ha.F().Sf("QUEST_EVENT_TIMER_END")`.
        timer_end_name_ = name;
        ++timer_end_fires_;
        QuestJournal j;
        try {
            j.player_level = app.save().load().level;
        } catch (const std::exception&) {
        }
        fire(app, "TimerEnd", j);
    }
    for (const std::string& name : expired) timers_.erase(name);
}
double QuestEngine::timer_remaining(const std::string& name) const {
    const auto it = timers_.find(name);
    if (it == timers_.end()) return -1.0;  // absent (JS `gJ` -> null)
    const double now = quest_now();
    return it->second > now ? it->second - now : 0.0;  // `v.ZI` L1218
}
std::size_t QuestEngine::run_timer_tick_for_test(App& app, double now) {
    std::size_t n = 0;
    for (const auto& kv : timers_) {
        if (kv.second <= now) ++n;
    }
    tick_timers(app, now);
    return n;
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
    for (const QuestTabSelect& s : fx.tab_selects) tab_queue_.push_back(s);
    for (const std::string& t : fx.click_arm) {
        armed_clicks_.push_back(t);
        std::fprintf(stdout, "[quest] ClickButton armed: %s (callback live)\n", t.c_str());
    }
    if (fx.collapse_nav) collapse_nav_pending_ = true;
    // `Po` `UpdateShopItems` (L570290): `Oa.get().Imb()` on the live shop.
    if (fx.update_shop_items) shop_refresh_pending_ = true;
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

// Test hook (`--tutorial-showblock-probe`): mirror the `Fo` park site
// (quest_engine.cpp ~L3527) at beat 4 with an EMPTY tail, so the resume is
// observable as `tutorial_gate_.active` clearing. The `EquipmentScreen`
// publisher is the ONLY resume source then (the timeout is 15 s away).
void QuestEngine::arm_showblock_gate_for_test(App& app) {
    tutorial_gate_ = TutorialGate{};
    tutorial_gate_.active = true;
    tutorial_gate_.beat = 4;
    tutorial_gate_.remaining = kTutorialStepTimeoutSec;
    tutorial_gate_.rest.clear();
    tutorial_gate_.quest = "TestShowBlock";
    try {
        tutorial_gate_.step_at_park = app.save().load().story_step();
    } catch (const std::exception&) {
    }
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

// JS `Bo`/`Do`/`Eo` `Pf` (sf2.502f0946.js L1121/L1123/L1125) + `Fo` `kg`
// (L1126/L387). The player fighter's animation event is the REAL resume
// condition; `TutorialStepTimeout` (and the `p.o.zi.LE` step change in
// `fire`) stay the fallbacks.
//   `Bo` (beat 3, L1121): `Pf(a){ w9&&(w9=!1,Cm()); a.data.name=="DoubleSweep"
//   &&(w9=!0) }` — arm on the DoubleSweep start, resume on the NEXT start.
//   `Do` (beat 1, L1123): `Pf(){ y9&&(y9=!1,dsa++,dsa>=3&&Cm());
//   Ra[0].zY=="EAnimationMove"&&(y9=!0) }` — 3 armed starts.
//   `Eo` (beat 2, L1125): same with `EAnimationAttack`.
//   `Fo` (beat 4, L1126): `Ad.kg` (animation END) resumes directly.
void QuestEngine::on_lesson_anim(App& app, const std::string& name,
                                 const std::string& type, bool end) {
    if (!tutorial_gate_.active) return;
    const int beat = tutorial_gate_.beat;
    if (beat == 4) {  // `Fo` (L1126): `kg` -> `oHa` -> `Cxa` -> `sa()`.
        if (!end) return;
        std::fprintf(stdout, "[quest] lesson beat 4 block anim-end -> chain resumes\n");
        std::fflush(stdout);
        resume_tutorial_gate(app);
        return;
    }
    if (end) return;  // `Pf` is the animation START (L386 `Gj(a.model,9)`).
    switch (beat) {
        case 3:  // `Bo` (L1121): arm on the DoubleSweep animation start.
            if (tutorial_gate_.anim_armed) {
                tutorial_gate_.anim_armed = false;
                std::fprintf(stdout,
                             "[quest] lesson beat 3 DoubleSweep anim -> chain resumes\n");
                std::fflush(stdout);
                resume_tutorial_gate(app);
                return;
            }
            if (name == "DoubleSweep") tutorial_gate_.anim_armed = true;
            break;
        case 1:  // `Do` (L1123): 3 x (arm on EAnimationMove -> next start).
            if (tutorial_gate_.anim_armed) {
                tutorial_gate_.anim_armed = false;
                if (++tutorial_gate_.anim_count >= 3) {
                    std::fprintf(stdout,
                                 "[quest] lesson beat 1 move x3 anim -> chain resumes\n");
                    std::fflush(stdout);
                    resume_tutorial_gate(app);
                    return;
                }
            }
            if (type == "EAnimationMove") tutorial_gate_.anim_armed = true;
            break;
        case 2:  // `Eo` (L1125): 3 x (arm on EAnimationAttack -> next start).
            if (tutorial_gate_.anim_armed) {
                tutorial_gate_.anim_armed = false;
                if (++tutorial_gate_.anim_count >= 3) {
                    std::fprintf(stdout,
                                 "[quest] lesson beat 2 attack x3 anim -> chain resumes\n");
                    std::fflush(stdout);
                    resume_tutorial_gate(app);
                    return;
                }
            }
            if (type == "EAnimationAttack") tutorial_gate_.anim_armed = true;
            break;
        default:
            break;
    }
}

// JS `zt.VQ()` (`zi`, bundle idx 156971): `return this.HH != "END"`. `HH` is
// the normalized live step (`zt.parse` -> `kU[0]` when absent/invalid), so an
// absent step (fresh profile) or an invalid one reads as NotStarted, i.e.
// live. Only the terminal `END` closes the gate.
bool QuestEngine::tutorial_live(App& app) const {
    try {
        return app.save().load().story_step() != "END";
    } catch (const std::exception&) {
        return true;  // no readable save -> fresh profile
    }
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

// `Hn.S` (L1032-1034): select the target screen's tab. The JS acts only when
// the target screen IS current (`if(wa.F().Td.Tf==this.CX)`); otherwise the
// action completes with no side effect. Case 5's `Ya.rF` is an EMPTY stub
// (L1096890), so the map select is a no-op in the JS too. `Oa.get()==null`/
// `vb.get()==null` arms the `wa.F().Qf` listener because the JS builds screen
// controllers asynchronously; `make_screen` builds them at push here, so the
// owner is live whenever the id matches and the listener is unreachable.
void QuestEngine::do_tab_select(App& app, const QuestTabSelect& sel) {
    const int cur = app.screens().current_id();
    if (cur != sel.screen_id) {
        std::fprintf(stdout,
                     "[quest] ChangeTab tab=%s idx=%d screen=%d: not current (%d), skip\n",
                     sel.tab.c_str(), sel.tab_index, sel.screen_id, cur);
        std::fflush(stdout);
        return;
    }
    ++tab_actions_;  // `Hn` executed (test hook; not a record)
    switch (sel.screen_id) {
        case 4: {  // `Oa.ska(Cj.l6(this.Ay), this.jN)`
            ShopScreen* shop = dynamic_cast<ShopScreen*>(app.screens().top());
            if (shop != nullptr) {
                shop->open_at(sel.tab, sel.focus);
            } else {
                std::fprintf(stdout, "[quest] ChangeTab Shop: Oa null (Qf listener)\n");
            }
            break;
        }
        case 5:  // `Ya.rF(this.Ay)` — the JS stub is EMPTY (L1096890).
            break;
        case 7: {  // `vb.rF(To.hOa(this.Ay), this.jN)`
            EquipmentScreen* prof = dynamic_cast<EquipmentScreen*>(app.screens().top());
            if (prof != nullptr) {
                // `To.hOa` (L1131579): vj index -> profile slot.
                int slot = 5;
                switch (sel.tab_index) {
                    case 10: slot = 0; break;
                    case 11: slot = 1; break;
                    case 12: slot = 2; break;
                    case 13: slot = 3; break;
                    case 15: slot = 4; break;
                    default: slot = 5; break;
                }
                if (prof->select_tab(slot, sel.focus)) {
                    // `hla` L1127569: `b.DI = uh.getName(To.kOa(a))`.
                    int vj = 0;
                    switch (slot) {
                        case 0: vj = 10; break;
                        case 1: vj = 11; break;
                        case 2: vj = 12; break;
                        case 3: vj = 13; break;
                        case 4: vj = 15; break;
                        default: vj = 0; break;
                    }
                    tab_owner_ = tab_name_for_index(vj);
                } else {
                    std::fprintf(stdout, "[quest] ChangeTab Profile slot=%d: no shell tab\n",
                                 slot);
                }
            } else {
                std::fprintf(stdout, "[quest] ChangeTab Profile: vb null (Qf listener)\n");
            }
            break;
        }
        default:
            break;
    }
    std::fprintf(stdout,
                 "[quest] ChangeTab tab=%s focus=%s idx=%d screen=%d -> applied\n",
                 sel.tab.c_str(), sel.focus.c_str(), sel.tab_index, sel.screen_id);
    std::fflush(stdout);
}

// Test hook (`--changetab-probe`): parse+run one in-process action list (the
// same path `fire` uses: run_actions -> apply_effects -> enqueue -> tick).
void QuestEngine::run_action_probe(App& app, const std::vector<QuestAction>& acts,
                                   const QuestJournal& journal) {
    if (!ensure_loaded(app)) return;
    QuestSideEffects fx;
    std::map<std::string, std::string> locals;
    const ActionRest rest = run_actions(app, acts, journal, fx, locals, "<probe>", 0);
    (void)rest;
    apply_effects(app, fx);
    enqueue_effects(app, fx, journal, locals, "<probe>");
    tick(app);
}

std::string QuestEngine::resolve_for_test(App& app, const std::string& expr,
                                          const QuestJournal& journal) {
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
    std::string out;
    if (!resolve_token(app, expr, c, out)) return std::string();
    return out;
}

void QuestEngine::tick(App& app) {
    if (app.headless()) {
        // The driver paths never auto-run: drop anything queued so a stale
        // request can never fire later (defensive; enqueue is gated too).
        pending_.clear();
        nav_queue_.clear();
        shop_queue_.clear();
        tab_queue_.clear();
        collapse_nav_pending_ = false;
        return;
    }
    if (!loaded_) return;
    // `xx` (L198): `p.o.yl.t_a(p.Dc)` every frame — expire due timers (each
    // fires `TimerEnd`). Live path only (headless keeps the record-only
    // contract, like the rest of `tick`).
    tick_timers(app, quest_now());
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
        if (nav_queue_.empty() && shop_queue_.empty() && tab_queue_.empty() &&
            !collapse_nav_pending_ && !shop_refresh_pending_) {
            break;
        }
        std::vector<QuestSceneRequest> navs;
        navs.swap(nav_queue_);
        std::vector<QuestShopOpen> shops;
        shops.swap(shop_queue_);
        std::vector<QuestTabSelect> tabs;
        tabs.swap(tab_queue_);
        const bool collapse = collapse_nav_pending_;
        collapse_nav_pending_ = false;
        const bool refresh_shop = shop_refresh_pending_;
        shop_refresh_pending_ = false;
        if (collapse) set_za_nav_open(false);  // `eo` L1117 -> `za.sxa()`
        for (const QuestSceneRequest& n : navs) do_navigate(app, n);
        for (const QuestShopOpen& s : shops) do_open_shop(app, s);
        for (const QuestTabSelect& s : tabs) do_tab_select(app, s);
        // `Po` (L570290): `Oa.get().Imb()` — refresh the LIVE shop only.
        if (refresh_shop && shop_refresh_items(app)) ++shop_refresh_actions_;
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
        if (!fx.toggle_items.empty()) {
            for (const std::string& s : fx.toggle_items) {
                std::fprintf(stdout, "[quest]   toggle item (record only): %s\n", s.c_str());
            }
        }
        if (!fx.discounts.empty()) {
            for (const std::string& s : fx.discounts) {
                std::fprintf(stdout, "[quest]   discount (record only): %s\n", s.c_str());
            }
        }
        if (!fx.map_button_shows.empty()) {
            for (const std::string& s : fx.map_button_shows) {
                std::fprintf(stdout, "[quest]   show map button (record only): %s\n",
                             s.c_str());
            }
        }
        if (!fx.map_button_hides.empty()) {
            for (const std::string& s : fx.map_button_hides) {
                std::fprintf(stdout, "[quest]   hide map button (record only): %s\n",
                             s.c_str());
            }
        }
        if (!fx.duel_timer_resets.empty()) {
            for (const std::string& s : fx.duel_timer_resets) {
                std::fprintf(stdout, "[quest]   reset duel timer (record only): %s\n",
                             s.c_str());
            }
        }
        if (!fx.timer_sets.empty()) {
            for (const std::string& s : fx.timer_sets) {
                std::fprintf(stdout, "[quest]   timer set (record only): %s\n", s.c_str());
            }
        }
        if (!fx.timer_ends.empty()) {
            for (const std::string& s : fx.timer_ends) {
                std::fprintf(stdout, "[quest]   timer end (record only): %s\n", s.c_str());
            }
        }
        if (!fx.foreach_runs.empty()) {
            for (const std::string& s : fx.foreach_runs) {
                std::fprintf(stdout, "[quest]   foreach (record only): %s\n", s.c_str());
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
    } else if (button_index == 3 || button_index == 4) {
        // `He.dhb` L1061: `a==3`/`a==4` fire the checkbox `<On>` (`kY`) /
        // `<Off>` (`jY`); when no `uv` exists the `a<this.eOa` lookup misses
        // and `dhb` fires NOTHING.
        if (!dlg.has_checkbox) return fights;
        chosen = button_index == 3 ? &dlg.checkbox.on : &dlg.checkbox.off;
        slot = button_index == 3 ? "CheckBoxOn" : "CheckBoxOff";
    } else if (button_index >= 5) {
        // `He.dhb` L1061 `a<this.eOa`: the `ima` row button with this id
        // (base `this.eOa=5`). `b==null` -> `dhb` fires nothing.
        const std::size_t row = static_cast<std::size_t>(button_index - 5);
        if (row >= dlg.line_actions.size()) return fights;
        chosen = &dlg.line_actions[row];
        slot = "Row";
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

// JS `Vb.Qg` (L2173): `Qg(a){ha.F().ta.Av=a; ha.F().Sf(
// "QUEST_EVENT_MAP_BUTTON_PRESS"); let b=this.Agb; b!=null&&b.Z(a)}`. `ta.Av`
// is `Bj.Av` (the `_$ButtonName` payload, L960); `Sf` routes the event through
// the quest hub. The `Agb` signal is the button's own press callback — no port
// consumer, so the fired quests are the observable result.
std::vector<std::string> QuestEngine::press_map_button(App& app,
                                                       const std::string& name) {
    QuestJournal j;
    j.button_name = name;
    std::fprintf(stdout, "[quest] MapButtonPress %s -> event\n", name.c_str());
    std::fflush(stdout);
    return fire(app, "MapButtonPress", j);
}

// JS `Pa.Wz` (L1234): `Pa.bia.Z(a); let b=ha.F().ta,c=b.Nb; b.Nb=hb.empty();
// b.Qv=""; b.bT=""; b.item=a; ha.F().Sf("QUEST_EVENT_PURCHASE"); b.Nb=c`.
// The `Nb`/`Qv`/`bT` clear+restore is dialog-text scratch the port does not
// model (no `_$` reader); the observable effect is `ta.item=a` then the event.
std::vector<std::string> QuestEngine::purchase(App& app, const std::string& item) {
    QuestJournal j;
    j.item = item;
    return fire(app, "Purchase", j);
}

// JS `Pa.Bv` (L1211): `let c=ha.F().ta; c.item=a; switch(b){case 2:a=p.XPa;
// break;case 3:a=p.$Pa;break;case 4:a=p.WPa;break;case 6:a=p.ZPa;break;
// default:a=null} c.I_=a; ha.F().Sf("QUEST_EVENT_PURCHASE_UNSUCCESSFUL")`.
std::vector<std::string> QuestEngine::purchase_unsuccessful(
    App& app, const std::string& item, int code) {
    QuestJournal j;
    j.item = item;
    switch (code) {
        case 2: j.purchase_failure = "Coins"; break;       // `p.XPa` L2472
        case 3: j.purchase_failure = "Ruby"; break;        // `p.$Pa`
        case 4: j.purchase_failure = "Connection"; break;  // `p.WPa`
        case 6: j.purchase_failure = "RaidCurr"; break;    // `p.ZPa`
        default: break;                                    // `default:a=null`
    }
    return fire(app, "PurchaseUnsuccessful", j);
}

std::vector<std::string> QuestEngine::fire(App& app, const std::string& event,
                                           const QuestJournal& journal) {
    std::vector<std::string> fired;
    // `v.owb` (L1215) sets `v.Q1` right before `v.uwb` raises
    // `QUEST_EVENT_SESSION` (`dp.start` L1164): mark the session started so
    // `_$GameStarted` (`Bj` L962) reads "1" from this fire on.
    if (event == "SessionStart") game_started_ = true;
    // JS `Do`/`Eo` register `Cm` on `p.o.zi.LE` — the story-step change event
    // (`zt.PMa` fires `LE`). A step change while a lesson is parked resumes the
    // chain immediately; the `TutorialStepTimeout` is only the fallback.
    if (tutorial_gate_.active) {
        std::string live;
        try {
            live = app.save().load().story_step();
        } catch (const std::exception&) {
        }
        if (live != tutorial_gate_.step_at_park) {
            std::fprintf(stdout, "[quest] step changed -> lesson gate resumes\n");
            std::fflush(stdout);
            resume_tutorial_gate(app);
        }
    }
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
