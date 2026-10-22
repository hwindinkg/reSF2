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

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "trigger.hpp"  // sf2::scene::ItemPerkRef (the per-item `Oa`)

namespace sf2::scene {

// One `<Perk Name="X"><Set k="v"/></Perk>` row of a Warrior/Template
// `<Perks>` (JS `ur` clones the `<Set>` children onto the warrior's `AK`).
// The mode path KEEPS the ref (name + `<Set>`) so `Wk()` (L811) can merge the
// `<Set>` overrides onto the live trigger, exactly as the stage path's
// `equipped_perks` does for the first warrior.
struct StagePerkRef {
    std::string name;
    std::map<std::string, std::string> set;  // `<Set>` attrs (raw)
};

// One Fight <Warrior> (or <Template> member / group member).
struct StageWarrior {
    std::string template_name;  // Template= ("" = inline/base)
    std::string tactic;  // Tactic= (Beginner/Standard/...)
    std::map<std::string, std::string> attrs;  // all other attrs (stats)
    std::vector<std::string> items;  // <Items><Item Name>
    std::vector<StagePerkRef> perks;  // <Perks><Perk Name + <Set>>
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
    // `<Perk Name=".."><Set a="b"/><RatingEvaluation .../></Perk>` (JS
    // `fj.parse` L868: `Lc = v.Rg.jn(Name)` then
    // `Lc.clone(a.A("Set"), a.A("RatingEvaluation"))`). Only the `<Perk>` rule
    // carries these; every other tag leaves them empty. `Alb`/`rmb`
    // (L38384-38385) then append the resolved model to the side's live
    // `parameters.Oa`, so the perk's triggers fire (JS `ca.ZOa` L398-399).
    std::map<std::string, std::string> perk_set;
    std::map<std::string, std::string> perk_rating;
};

// One <Reward> row. The JS fields are float64 (`Yg.Tb`/`Uo`/`exp`/`ph`); the
// shipped `<Reward Money>` reaches 7.46e12, so the money-carrying fields are
// int64 (`< 2^53` is exact for every JS integer here).
struct StageReward {
    std::int64_t money = 0, exp = 0, bonus = 0;
    std::int64_t prize_base = 0;  // `ph` (PrizeBase attr; JS default -1)
};

struct StageFight {
    std::string name;
    // `?Fight.Description` base `Sb`: the `<Fight Description>` attr (`IIa`
    // L98560 `e=b.attributes.get("Description"); a.jla(e!=null?e:"")`).
    std::string description;
    // `IIa` L98652 `a.d4=u.I(b.attributes.get("Power"),1)` — JS default 1.
    int power = 1;
    int rounds = 2;
    int round_time = 99;
    int replays = 1;  // Replays= (entry repeat cap data, map-side)
    // `Nn` (`IIa` L98652): `u.I(b.attributes.get("ReplayInterval"))` — the
    // per-fight replay interval in SECONDS (JS default 0 when absent).
    // `?Fight[...].TimeLeft` (`f9a` L727530) counts it down.
    int replay_interval = 0;
    double health_recovery = 1.0;  // `qDa` (default 1)
    std::vector<StageReward> rewards;
    std::vector<StageWarrior> warriors;
    std::vector<StageRule> rules;
};

struct StageBattle {
    std::string name, type, location, music;
    // The stages.xml `<Zone Name>` the battle sits in. The SAME battle name
    // repeats across zones with different Warriors/Rules (`Duel`/`Tournament`/
    // `Challenge`/...), so a resolver must zone-scope exactly as
    // `app::battle_warrior` does — never the document's first name match.
    std::string zone;
    std::vector<StageFight> fights;
};

struct TemplateDef {
    std::string name, base, first_name;
    std::vector<std::string> items;
    // The template's own `<Perks>` (JS `xc.AK`): `battle_warrior` resolves the
    // `<Template>` chain's perks (base-first) and appends the `<Warrior>`'s
    // own on top; the mode path (`resolve_tournament_fight`/
    // `resolve_survival_warrior`) must do the same — the shipped `Default`
    // template carries 5 perks (incl. `PERK_BACK_FLIP_KICK`) that a tournament
    // `<Warrior>` (own `<Perks>` = {PERK_BEGINNER}) inherits. Dropping them
    // removed every template-perk-gated move from the mode enemy's `me`
    // (`BackFlipKick` fell out; the AI could never pick it).
    std::vector<StagePerkRef> perks;
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
    std::vector<StagePerkRef> perks;  // name + `<Set>` refs (JS `xc.AK`)
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

// JS numbers are float64 and the shipped `<Reward Money>` reaches 7.46e12
// (`stages.xml` ZONE_7 titan rows), which `std::stoi`/`as_int` would either
// throw on (-> default) or saturate. `stoll` is exact for every JS integer
// magnitude here (< 2^53).
inline std::int64_t xml_i64(const pugi::xml_node& n, const char* attr,
                            std::int64_t def) {
    const pugi::xml_attribute a = n.attribute(attr);
    if (!a) return def;
    try {
        return std::stoll(a.value());
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

// JS `lzb`/`pGa` (L101849) + the `xc` parse (L191): the EFFECTIVE
// `<AttributesAlign>` rows of a stage warrior.
//
// The chain (char offsets in reference/www/sf2.502f0946.js):
//   * L198 `ur(d, g.Ze)` — a `<Warrior Template="T">` clones the resolved
//     template object `T.Ze` (`nfa` L198), so it STARTS from the template's
//     `IY`; L191 (`d=a.A("AttributesAlign") … b.IY.push(g)`) then APPENDS the
//     Warrior node's own first `<AttributesAlign>` rows. Inherited FIRST, own
//     LAST (no clear, so it is an append).
//   * L198 `lzb` resolves a template's chain: `g = e.Os(a.Ze.node.clone())`
//     (deep `Rb.clone`, L1261758) then `pGa(g, d.node)`; `T.Ze=ur(g, a.Ze)`.
//   * L106212 `pGa` appends the derived node's `<AttributesAlign>` AFTER the
//     cloned base's, and its removal guard is `a.attributes.get("AttributesAlign")`
//     (an ATTRIBUTE lookup, always null) so the base block is never removed.
//     `Rb.A(tag)` (L1259690) returns the FIRST child with that name, so
//     `g.A("AttributesAlign")` is the BASE's block: the derived template's own
//     rows are shadowed, and each chain level appends the base's rows again.
//
// Shipped stages.xml has exactly ONE `<AttributesAlign>` inside `<Templates>`
// — `<Template Name="Default">`'s (offset 1058701; the other 345 blocks are on
// Zone `<Warrior>` nodes) — and all 188 templates carry `Template="Default"`
// (or a chain through it), so EVERY named template resolves to `Default`'s
// rows. The recursion `res(T) = res(base) + [res(base)[0]]` therefore yields
// 1..n copies of `Default`'s 7 rows per chain depth (Ascension_2→Ninja_…→Default
// = 3 copies). `pAa` (L1204) only min/maxes over the kept rows, which is
// duplicate-invariant, so this port keeps the one-copy form: `Default`'s rows +
// the warrior's own. `Ci.a5a` (L800) then keeps only the max-`Priority` rows —
// which is how a per-warrior set overrides `Default`'s (e.g. Dojo_Disciple's
// two `Priority="1"` rows, or BOSS_LYNX f1's `1.008`/`0` pair at stages.xml).
// `default_align` reads that same one-copy `Default` block.
//
// NOTE on the TEMPLATE source node: it must be `<Stages>/<Warriors>/<Templates>`
// (JS L198 `this.vG.A("Warriors")…A("Templates")`), NOT a root sibling — see
// the call site in `screens.cpp::battle_warrior`.
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
            if (!p.attribute("Name")) continue;
            StagePerkRef ref;
            ref.name = p.attribute("Name").value();
            // JS `ur`: `e.A("Set")` is cloned onto the perk (`hXa`), so the
            // `<Set>` overrides survive resolution (the mode path's `Wk()`).
            const pugi::xml_node set = p.child("Set");
            if (set) {
                for (const pugi::xml_attribute a : set.attributes()) {
                    ref.set[a.name()] = a.value();
                }
            }
            out.perks.push_back(std::move(ref));
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
    // JS `fj.parse` (L868): the `<Perk>` rule's `<Set>`/`<RatingEvaluation>`
    // children (cloned onto the resolved perk model). Direct children only.
    if (el.name() == std::string("Perk")) {
        for (const pugi::xml_node st : el.children("Set")) {
            for (const pugi::xml_attribute a : st.attributes())
                rule.perk_set[a.name()] = a.value();
        }
        for (const pugi::xml_node rt : el.children("RatingEvaluation")) {
            for (const pugi::xml_attribute a : rt.attributes())
                rule.perk_rating[a.name()] = a.value();
        }
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
                if (z.attribute("Name")) battle.zone = z.attribute("Name").value();
                if (b.attribute("Type")) battle.type = b.attribute("Type").value();
                if (b.attribute("Location")) battle.location = b.attribute("Location").value();
                if (b.attribute("Music")) battle.music = b.attribute("Music").value();
                for (const pugi::xml_node f : b.children("Fight")) {
                    StageFight fight;
                    if (f.attribute("Name")) fight.name = f.attribute("Name").value();
                    fight.power = xml_int(f, "Power", 1);
                    if (f.attribute("Description")) fight.description = f.attribute("Description").value();
                    fight.rounds = xml_int(f, "Rounds", 2);
                    fight.round_time = xml_int(f, "RoundTime", 99);
                    fight.replays = xml_int(f, "Replays", 1);
                    fight.replay_interval = xml_int(f, "ReplayInterval", 0);
                    fight.health_recovery = xml_num(f, "HealthRecovery", 1.0);
                    const pugi::xml_node rewards = f.child("Rewards");
                    if (rewards) {
                        for (const pugi::xml_node r : rewards.children("Reward")) {
                            StageReward rw;
                            rw.money = xml_i64(r, "Money", 0);
                            rw.exp = xml_i64(r, "Exp", 0);
                            rw.bonus = xml_i64(r, "Bonus", 0);
                            rw.prize_base = xml_i64(r, "PrizeBase", -1);
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
                // The template's own `<Perks>` (JS `xc.AK` parse), mirroring
                // `battle_warrior` (screens.cpp) — the base of the mode enemy's
                // resolved perk set.
                const pugi::xml_node perks = t.child("Perks");
                if (perks) {
                    for (const pugi::xml_node p : perks.children("Perk")) {
                        if (!p.attribute("Name")) continue;
                        StagePerkRef ref;
                        ref.name = p.attribute("Name").value();
                        const pugi::xml_node set = p.child("Set");
                        if (set) {
                            for (const pugi::xml_attribute a : set.attributes()) {
                                ref.set[a.name()] = a.value();
                            }
                        }
                        def.perks.push_back(std::move(ref));
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

// Append `src` perks onto `dst`, deduped by Name with the LATER row's `<Set>`
// winning — the JS `xc.AK` merge `battle_warrior` performs (template chain
// base-first, then the Warrior's own). Used by the mode resolvers so a
// tournament/survival enemy inherits its `<Template>` chain's perks exactly as
// the stage path does.
inline void append_perks_unique(std::vector<StagePerkRef>& dst,
                                const std::vector<StagePerkRef>& src) {
    for (const StagePerkRef& ref : src) {
        if (ref.name.empty()) continue;
        bool merged = false;
        for (StagePerkRef& e : dst) {
            if (e.name == ref.name) {
                e.set = ref.set;
                merged = true;
                break;
            }
        }
        if (!merged) dst.push_back(ref);
    }
}

// Template chain perks (base-first, deduped by Name) — the `<Template>`
// `<Perks>` half of `xc.AK`. Mirrors `template_items` (the `<Items>` half).
inline std::vector<StagePerkRef> template_perks(
    const std::string& name, const std::map<std::string, TemplateDef>& templates) {
    std::vector<StagePerkRef> out;
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
        append_perks_unique(out, templates.at(*it).perks);
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

// `efa`'s (L7360-7420) group walk for ONE warrior of a survival row. `f` is
// the CALL-LOCAL pick index (`efa`'s `f`, L7370 `f = e++` inside
// `for(e = 0; e < b;)`), `used` the per-group NoDoubles memory of the SAME
// call (`h[k]`, L7364-7368) — fresh per `efa` call, shared by all its picks.
inline bool resolve_survival_one(
    const StageWarrior& slot, int f,
    const std::map<std::string, TemplateDef>& templates,
    const std::map<std::string, GroupDef>& groups,
    std::function<double()> draw01, std::vector<std::vector<int>>& used,
    ResolvedWarrior& out) {
    out = ResolvedWarrior();
    out.tactic = slot.tactic;
    out.attrs = slot.attrs;
    out.perks = slot.perks;
    // The ordered equip list the JS `parameters.jt()` exposes (see the `ur`
    // note in the group walk below); filled for both the group and the
    // no-group path, then handed to the app layer's equip-by-type bucketing.
    std::vector<std::string> merged_items;
    if (slot.groups.empty()) {
        out.template_name = slot.template_name;
    } else {
        // `efa` group walk (L89866-L90100): EVERY resolvable group
        // contributes — not a first-match pick. `q=a.groups`; per group `k`
        // the member `g` is picked, then folded into the accumulator:
        //   `l==null ? l=g : (l=this.ur(g.node,l), this.$Wa(l))`
        // So the FIRST member is the base and each later member's node is
        // merged on top (`ur`, L94004) — later groups OVERRIDE the earlier
        // stats/items/perks. `ur` NEVER writes `Template` (`.ML`), so
        // `out.template_name` stays the FIRST resolvable member's template.
        // `$Wa` (L93528) is `this.pUa.push(a)` — a registry with no field
        // effect, so it is not modelled. When NO group resolves, `efa` falls
        // back to a clone of the row (`c.length==0 && c.push(a.clone())`).
        bool have_base = false;
        // JS `ur` (L7838-7868) `<Items>` hop: `efa` (L7394-7400) merges EVERY
        // group member's resolved node ON TOP of the accumulator
        // (`l==null ? l=g : (l=this.ur(g.node,l))`), and `ur`'s `<Items>` block
        // EQUIPS each item by TYPE (`b.hk(f.type,f)`, L7861) — a later member
        // therefore OVERRIDES an earlier member in the same slot. `g.node` is
        // the member's FULL node (its `<Template>` chain merged with the member
        // via `F0a`/`pGa`, L8218-8222), so the member's resolved template-chain
        // items ride along. Reproduce the ordered equip list here; the app-side
        // `resolve_enemy_loadout` buckets last-wins by type (the SAME `<Items>`
        // equip semantics). Before this the resolver kept only the FIRST
        // member's template chain, so a survival enemy (Survival_1
        // `Template="Default"` + a RANDOM Generic_1 Ninja) never got the Ninja's
        // weapon: the ported enemy kept `Fists` while the JS one carried e.g.
        // `WEAPON_NINJA_SWORD` (SubType `NinjaSword`), which changes the
        // `FistsStartStanceIdle-Right` `<Locks>` outcome (readable.js:28897
        // `d.items = a.parameters.jt()` = the EQUIPPED slots).
        std::size_t gi = 0;
        for (const StageWarrior::GroupRef& g : slot.groups) {
            const std::size_t group_index = gi++;
            if (used.size() <= group_index) used.resize(group_index + 1);
            const auto it = groups.find(g.name);
            if (it == groups.end() || it->second.members.empty()) continue;
            const std::vector<StageWarrior>& pool = it->second.members;
            std::size_t pick = 0;
            if (g.random && draw01) {
                // JS `x8a` (L9167) = ONE draw (`Ix.length * Da.pg.jf() | 0`);
                // `y8a` (L9171) = draw, then RE-DRAW while the index is
                // already in THIS `efa` call's per-group memory
                // (`while(true){ ... e[d]==b && (b = len*Da.pg.jf()|0) ... }`).
                // The port's old sequential `(pick+1)%len` on a collision was
                // a divergence: the JS consumes a NEW stream value instead, so
                // every later draw (and the whole fight) shifted. Bounded to
                // 64 re-draws so a constant stream cannot hang (the JS spins
                // forever on one too).
                pick = static_cast<std::size_t>(
                           draw01() * static_cast<double>(pool.size())) %
                       pool.size();
                if (g.no_doubles) {
                    for (int attempt = 0; attempt < 64; ++attempt) {
                        bool dup = false;
                        for (int u : used[group_index]) {
                            if (u == static_cast<int>(pick)) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup) break;
                        if (attempt == 63) {
                            pick = (pick + 1) % pool.size();
                            break;
                        }
                        pick = static_cast<std::size_t>(
                                   draw01() * static_cast<double>(pool.size())) %
                               pool.size();
                    }
                    used[group_index].push_back(static_cast<int>(pick));
                }
            } else {
                pick = static_cast<std::size_t>(f) % pool.size();
            }
            const StageWarrior& m = pool[pick];
            // `l==null ? l=g`: only the first resolvable member stamps the
            // template (`ur` ignores Template on the merge hops).
            if (!have_base) {
                out.template_name =
                    m.template_name.empty() ? slot.template_name : m.template_name;
                have_base = true;
            }
            if (!m.tactic.empty()) out.tactic = m.tactic;
            for (const auto& kv : m.attrs) out.attrs[kv.first] = kv.second;
            // `ur`'s `<Items>` hop (see above): the member's resolved
            // template-chain items then the member's own `<Items>`, in order.
            if (!m.template_name.empty()) {
                for (const std::string& it2 :
                     template_items(m.template_name, templates)) {
                    merged_items.push_back(it2);
                }
            }
            for (const std::string& it2 : m.items) merged_items.push_back(it2);
            // `ur`'s `<Perks>` hop appends, deduped by Name (L97347).
            for (const StagePerkRef& pr : m.perks) {
                bool have = false;
                for (const StagePerkRef& cur : out.perks) {
                    if (cur.name == pr.name) {
                        have = true;
                        break;
                    }
                }
                if (!have) out.perks.push_back(pr);
            }
        }
        if (!have_base) out.template_name = slot.template_name;
    }
    // The Fight `<Warrior>`'s own `<Items>` merge LAST (`ur(a.node, l)` at
    // L7406 when the row carries no `Template`), i.e. they override the groups'
    // gear in the same slot.
    for (const std::string& it2 : slot.items) merged_items.push_back(it2);
    out.items = std::move(merged_items);
    if (!out.template_name.empty()) {
        // No resolvable group member (and no row items) -> fall back to the
        // row template's chain (the `c.length==0 && c.push(a.clone())` case).
        if (out.items.empty()) out.items = template_items(out.template_name, templates);
        out.first_name = template_first_name(out.template_name, templates);
        // JS `xc.AK` merge (`ur`): the `<Template>` chain's `<Perks>` are the
        // base, the row/group perks append on top (dedup by Name, later `<Set>`
        // wins). Without this a survival warrior lost every template-perk-gated
        // move (the shipped `Default` chain carries 5, incl.
        // `PERK_BACK_FLIP_KICK`).
        std::vector<StagePerkRef> merged_perks =
            template_perks(out.template_name, templates);
        append_perks_unique(merged_perks, out.perks);
        out.perks = std::move(merged_perks);
    } else if (out.items.empty()) {
        out.items = slot.items;
    }
    return true;
}

// One `efa(row, count)` CALL (L7360): resolve EVERY warrior of the row that
// holds `wave` (`b = f.number * this.ag`, L59012) in a single call, so the
// per-group NoDoubles memory is shared by the picks of that call and reset
// for the next one. Returns them in row order.
inline bool resolve_survival_roster(
    const StageFight& fight, int wave,
    const std::map<std::string, TemplateDef>& templates,
    const std::map<std::string, GroupDef>& groups,
    std::function<double()> draw01, std::vector<ResolvedWarrior>& out) {
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
    out.clear();
    std::vector<std::vector<int>> used;
    for (int i = 0; i < slot->number; ++i) {
        ResolvedWarrior r;
        if (!resolve_survival_one(*slot, i, templates, groups, draw01, used, r)) {
            return false;
        }
        out.push_back(std::move(r));
    }
    return true;
}

// Single-wave wrapper (kept for the golden test): resolves the row's FULL
// roster in ONE `efa` call — the draws are that call's, not one per warrior —
// and returns the `wave`-th warrior of it.
inline bool resolve_survival_warrior(
    const StageFight& fight, int wave,
    const std::map<std::string, TemplateDef>& templates,
    const std::map<std::string, GroupDef>& groups, std::function<double()> draw01,
    std::vector<std::string>& used_templates, ResolvedWarrior& out) {
    std::vector<ResolvedWarrior> roster;
    if (!resolve_survival_roster(fight, wave, templates, groups, draw01, roster)) {
        return false;
    }
    int w = wave;
    for (const StageWarrior& cand : fight.warriors) {
        if (w < cand.number) break;
        w -= cand.number;
    }
    if (w < 0 || w >= static_cast<int>(roster.size())) return false;
    out = roster[static_cast<std::size_t>(w)];
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
        // JS `D0(i)` (L728050) row select for a single-warrior `<Fight>`: the
        // series cursor `PU` (`Da.PU` = `Rk`, the warrior/wave index) is 0
        // here, and `kD` (L622194) uses `D0(PU + (won?1:0))` -> row 1 on a win.
        // The row is only ever GRANTED on the terminal win, where `D0(1)` is
        // the LAST row of a 2-row fight (`$L` L730889 also takes the last).
        out.reward = f.rewards.back();
    }
    if (!f.warriors.empty()) {
        const StageWarrior& w = f.warriors[0];
        out.enemy.template_name = w.template_name;
        out.enemy.tactic = w.tactic;
        out.enemy.attrs = w.attrs;
        // JS `xc.AK` merge (`ur`): the `<Template>` chain's `<Perks>` are the
        // base, the `<Warrior>`'s own `<Perks>` append on top (dedup by Name,
        // the Warrior's `<Set>` wins). The shipped tournament `<Warrior
        // Template="Man_Fist">` owns only `PERK_BEGINNER` but inherits the
        // `Default` chain's 5 perks (incl. `PERK_BACK_FLIP_KICK`); dropping
        // them excluded `BackFlipKick` (and the other template-perk moves) from
        // the mode enemy's `me`, so its first Random pick resolved to a move
        // the JS keeps.
        out.enemy.perks = template_perks(w.template_name, templates);
        append_perks_unique(out.enemy.perks, w.perks);
        if (!w.template_name.empty()) {
            out.enemy.items = template_items(w.template_name, templates);
            out.enemy.first_name = template_first_name(w.template_name, templates);
        }
        // JS `ur` (L7838-7868): the row's own `<Items>` merge ON TOP of the
        // template chain (equip by TYPE), so APPEND (not replace) — the app
        // layer's `resolve_enemy_loadout` buckets last-wins by type. The old
        // replace dropped the chain's `Skeleton` (see screens.cpp note).
        for (const std::string& it2 : w.items) out.enemy.items.push_back(it2);
    }
    return true;
}

// Reward row select (JS `dl.D0(i)` L728050 called from `v.kD` L622194):
// `c = b.PU; f.zd() && ++c; b.sR() && (c = f.zd() ? 1 : 0)`, where `PU` =
// `Rk` (the warrior/wave cursor, set by `bea` L210375) and `sR()` =
// `Xs.length>1 ? pT>1 : false` (L729188). For the shipped content `sR()` is
// always false (single `<Warrior>` per fight; Survival `Rounds="1"`), so the
// row is `wave + (won?1:0)`; the terminal win therefore lands on
// `rewards.back()` (rows = waves + 1 in every shipped Survival fight). The
// row is granted ONLY on a win (the port grants nothing on a loss, matching
// `f.zd()` gating in the flow), so the last row is the JS-exact grant value.
inline StageReward reward_for(const std::string& battle_type, const StageFight& fight,
                              int wave, bool won) {
    (void)battle_type;
    if (fight.rewards.empty()) return StageReward();
    // JS `v.kD` L622187: `c = b.PU; f.zd() && ++c; b.sR() && (c = f.zd()?1:0)`
    // then `f.pwa(b.D0(c), ...)`; `D0(a) = this.wi.length>a ? this.wi[a] : null`
    // (L728049). `sR() = this.Xs.length>1 ? this.pT>1 : !1` (L729188) with
    // `pT` = `<Fight Rounds>`. Every shipped mode fight is `Xs.length==1`
    // (one `<Warrior>`; Survival `Number=N` is the wave count, not `Xs`), and
    // every shipped multi-`<Warrior>` fight is `Rounds="1"`, so `sR()` is
    // FALSE for the whole shipped content -> the row is `PU + (won?1:0)`,
    // with `PU` = `Rk` (`bea` L413 `this.Da.PU=this.Rk`; the earlier
    // `PU=wi.length-2` is overwritten). `D0` clamps an out-of-range index to
    // null, which the caller treats as "no grant"; the port clamps to the last
    // row instead (the shipped win row is always `wi[Rk+1]`, Rk = waves-1).
    int idx;
    if (fight.warriors.size() > 1 && fight.rounds > 1) {
        idx = won ? 1 : 0;  // `sR()`
    } else {
        idx = wave + (won ? 1 : 0);
    }
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(fight.rewards.size()))
        idx = static_cast<int>(fight.rewards.size()) - 1;
    return fight.rewards[static_cast<std::size_t>(idx)];
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
    // Locks items for the enemy move list (Type / SubType / Name).
    std::vector<OwnedItem> owned;
    std::map<std::string, double> attrs;  // numeric stat overrides
    // The resolved warrior's own `<Perks>` with their `<Set>` refs. These are
    // the FIRST input of `Wk()` (L811) — before the equipped items' catalog
    // perks — so a mode enemy's `<Set>` overrides must ride along.
    std::vector<StagePerkRef> perks;
    // JS `Wk` L811-812: after `AK` (the warrior `<Perks>` above), EVERY
    // equipped item's `Oa` (its list.xml `<Perks>`/`<Enchantments><Perk>`)
    // with each `<Set>` override. The app resolves `owned` names against the
    // item catalog into these refs (the same shape the stage path's
    // `equipped_perks` builds); `apply_mode_setup` appends them AFTER the
    // warrior perks (AK-before-Oa). Before this the mode path dropped them.
    std::vector<sf2::scene::ItemPerkRef> item_refs;
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

// Build a ModeSetup from a resolved ModeFight. `enemy_owned` (Type /
// SubType / Name rows, `Hm.he` L758) comes from the app layer (item catalog);
// numeric attrs parse from the warrior strings (non-numeric skipped). Rules mapping:
// `<Attributes DamageFactor ApplyTo=Player/Bot>` → side adds;
// `<NoBulletsReplenishment ApplyTo=Player>` → flag; Eclipse → noted
// OPEN (no Cea setter found; bp stays 1.0).
inline ModeSetup mode_setup_from_fight(
    const ModeFight& mf,
    const std::vector<OwnedItem>& enemy_owned) {
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
    out.enemy.perks = mf.enemy.perks;
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
