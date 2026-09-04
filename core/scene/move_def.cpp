// moves.xml parser — mirrors JS `Fa.parse`/`Fa.Ueb`/`Fa.xbb` (sf2.502f0946.js).
//
// JS study (cite):
//   - `Fa.Ueb(a,b,c,d,e)` parses the <Moves> list (JS L "13C" class Fa):
//       name=Name, id=ID, fileName=FileName, Eza=strip .bytes,
//       XJ=MidFrames, qx=FirstFrame, Lj=EndFrame, priority=Priority,
//       Ltb(NoMagicRecharge), bha(NoWallRepulsion), RNa=StyleFactor,
//       MS=Physics, yda=EndsStage, Ktb(Looped), WGa=NoInterpolationFrames,
//       Rha=NoAnimation, iva=AlignOnParentWallCollision, uja() loads the
//       animation clip, ava(name) registers the name, J2.Grb(MirrorNode),
//       jtb(CameraCOMAlignStage), Gsb(TacticWeapon), TacticEquivalent,
//       type="EAnimationMove"/"EAnimationAttack" (Type=="MOVE"/"ATTACK").
//       Then `Fa.amb(g,l,b)` merges inherited Template tag conditions, and
//       `Fa.xbb(k,g,l)` parses the sub-objects (see header).
//   - Template inheritance: `Fa.dMa(a,b,c)` walks the Template "A|B|C"
//       string; each tag resolves to a <Template Name=..> element in the
//       templates table (Fa.kxb) and its content is cloned into the move.
//   - `Fa.xbb` reads: Events (GIa), Conditions (HS), Locks (HS), Tactics
//       (djb), Intervals (xjb/LIa), Align (Hib), SetDirection (hjb),
//       Transitions (Cxb), Shop (Mub), Actions (CIa).
//   - Intervals: `Fa.LIa` — each <Interval Type=..> maps via `fe.G0`
//       (0 default, 2 Uninterrupt, 3 SelfUninterrupt, 4 Attack, 5 Block,
//       6 Invulnerable, 7 Invisible). Type=="Attack" -> class `Ul` which
//       additionally parses AttackingParts, Hit, Impulse, Damage, Combo.
//
// The native parser reproduces the same data (conditions/intervals merged
// from own + inherited templates), enough for the condition evaluator.

#include "scene/move_def.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "xml_doc.hpp"

namespace sf2::scene {

namespace {

// Split on '|' (JS `a.split("|")`).
std::vector<std::string> split_pipe(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : s) {
        if (ch == '|') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}

// JS `fe.init`: interval type resolution. The NAME overrides the Type:
//   Name=="Unstable"->1, "Uninterrupt"->2, "SelfUninterrupt"->3; else
//   `fe.G0(Type)` (Attack=4, Block=5, Invisible=7, Invulnerable=6, else 0).
int interval_type_from_name(const std::string& t) {
    if (t == "Attack") return 4;
    if (t == "Block") return 5;
    if (t == "Invisible") return 7;
    if (t == "Invulnerable") return 6;
    return 0;
}

// JS `fe.init` full type resolution (name first, then Type attr).
int interval_type_resolve(const std::string& name, const std::string& type_attr) {
    if (name == "Unstable") return 1;
    if (name == "Uninterrupt") return 2;
    if (name == "SelfUninterrupt") return 3;
    return interval_type_from_name(type_attr);
}

// JS `fe.init`: interval start/end frames.
//   start = Start attr (default 0); finish = End attr, else pva+2
//   (pva = the move's EndFrame, or the loaded animation length when the
//   move has no EndFrame — the JS `jc.Lj` is set from the clip frame count
//   in `Vlb`/`Cdb` when EndFrame is absent; the parser only sees the XML,
//   so it defaults to 0 and the caller resolves the real end frame).
void parse_interval(pugi::xml_node node, int end_frame_default, Interval& out) {
    out.name = node.attribute("Name") ? node.attribute("Name").value() : "";
    const std::string type = node.attribute("Type") ? node.attribute("Type").value() : "";
    out.type = interval_type_resolve(out.name, type);
    // JS `Ul.J3` (L774-775): `<IgnoresBlock/>` -> DDa (+ hga names);
    // `<IgnoresInvulnerable Name="A|B"/>` -> jga (+ iga bypass names).
    // NOTE: child ELEMENTS, not attributes (159/154 live hits in moves.xml).
    if (node.child("IgnoresBlock")) {
        out.ignores_block = true;
        const char* names = node.child("IgnoresBlock").attribute("Name").value();
        if (names != nullptr) {
            std::string cur;
            for (const char* p = names; ; ++p) {
                if (*p == '|' || *p == '\0') {
                    if (!cur.empty()) out.ignore_block_names.push_back(cur);
                    cur.clear();
                    if (*p == '\0') break;
                } else {
                    cur.push_back(*p);
                }
            }
        }
    }
    if (node.child("IgnoresInvulnerable")) {
        out.ignores_invuln = true;
        const char* names = node.child("IgnoresInvulnerable").attribute("Name").value();
        if (names != nullptr) {
            std::string cur;
            for (const char* p = names; ; ++p) {
                if (*p == '|' || *p == '\0') {
                    if (!cur.empty()) out.invuln_bypass_names.push_back(cur);
                    cur.clear();
                    if (*p == '\0') break;
                } else {
                    cur.push_back(*p);
                }
            }
        }
    }
    out.start = data::xml_attr_int(node, "Start", 0);
    if (node.attribute("End")) {
        out.end = data::xml_attr_int(node, "End", 2147483647);
    } else {
        out.end = end_frame_default + 2;  // JS: this.pva+2
    }

    // Attack sub-type (JS `Ul.J3`): AttackingParts + Hit + Impulse + Damage.
    if (out.type == 4) {
        if (pugi::xml_node parts = node.child("AttackingParts")) {
            for (pugi::xml_node edge : parts.children("Edge")) {
                if (edge.attribute("Name")) {
                    out.attacking_parts.push_back(edge.attribute("Name").value());
                }
            }
        }
        if (pugi::xml_node hit = node.child("Hit")) {
            out.hit_name = hit.attribute("Name") ? hit.attribute("Name").value() : "";
        }
        if (pugi::xml_node imp = node.child("Impulse")) {
            out.impulse_x = data::xml_attr_float(imp, "X", 0.0f);
            out.impulse_y = data::xml_attr_float(imp, "Y", 0.0f);
            out.impulse_z = data::xml_attr_float(imp, "Z", 0.0f);
            out.has_impulse = true;
        }
        if (pugi::xml_node dmg = node.child("Damage")) {
            out.damage = data::xml_attr_float(dmg, "Value", 0.0f);
            out.no_critical = data::xml_attr_bool(dmg, "NoCritical", false);
            if (pugi::xml_node sub = dmg.child("Damage")) {
                out.damage_type = sub.attribute("Type") ? sub.attribute("Type").value() : "";
                out.damage_shift = data::xml_attr_float(sub, "Shift", 0.0f);
            }
        }
        if (pugi::xml_node combo = node.child("Combo")) {
            out.combo_time = data::xml_attr_int(combo, "Time", 0);
        }
    }
}

// JS `Fa.H3`/`Fa.HS`: parse a <Conditions>/<Locks> list of child nodes into
// Cond trees. `create` mirrors `Tl.create` (element name -> typed cond).
void parse_cond_node(pugi::xml_node node, Cond& out);

void parse_cond_children(pugi::xml_node parent, std::vector<Cond>& out) {
    for (pugi::xml_node child : parent.children()) {
        Cond c;
        parse_cond_node(child, c);
        out.push_back(std::move(c));
    }
}

void parse_cond_node(pugi::xml_node node, Cond& out) {
    out.type = node.name();
    out.not_ = data::xml_attr_bool(node, "Not", false);
    const char* player = node.attribute("Player") ? node.attribute("Player").value() : nullptr;
    // JS `Nd.ol`: Me=1, Enemy=2, Both=5, Child=4, Parent=3, ... default 0.
    if (player != nullptr) {
        if (std::strcmp(player, "Me") == 0) out.player = 1;
        else if (std::strcmp(player, "Enemy") == 0) out.player = 2;
        else if (std::strcmp(player, "Both") == 0) out.player = 5;
        else if (std::strcmp(player, "Child") == 0) out.player = 4;
        else if (std::strcmp(player, "Parent") == 0) out.player = 3;
        else if (std::strcmp(player, "EnemyChild") == 0) out.player = 6;
        else if (std::strcmp(player, "SuperParent") == 0) out.player = 7;
    }

    if (out.type == "Operator") {
        // JS `up.parse`: Type="Or" -> dv=1, Type="And" -> dv=2.
        const char* type = node.attribute("Type") ? node.attribute("Type").value() : "";
        out.op = std::strcmp(type, "Or") == 0 ? cond_op::or_
               : std::strcmp(type, "And") == 0 ? cond_op::and_
               : cond_op::not_;
        parse_cond_children(node, out.children);
        return;
    }

    // Leaf condition — capture the attributes the evaluator reads.
    out.op = cond_op::leaf;
    if (node.attribute("Name")) out.name = node.attribute("Name").value();
    if (node.attribute("Type")) out.subtype = node.attribute("Type").value();
    if (node.attribute("SubType")) out.subtype = node.attribute("SubType").value();
    if (node.attribute("Subtype")) out.subtype = node.attribute("Subtype").value();
    if (node.attribute("Value")) out.value = node.attribute("Value").value();

    if (out.type == "Distance") {
        // JS `qm`: Axis (X->0, Y->1, default 2), Min/Max (of()), From/To (ee).
        const char* axis = node.attribute("Axis") ? node.attribute("Axis").value() : nullptr;
        out.axis = axis == nullptr ? 2 : (std::strcmp(axis, "X") == 0 ? 0 : 1);
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
        if (pugi::xml_node from = node.child("From")) {
            out.from_player = from.attribute("Player")
                ? (std::strcmp(from.attribute("Player").value(), "Enemy") == 0 ? 2 : 1)
                : 1;
            out.from_obj = from.attribute("Object") ? from.attribute("Object").value() : "Pivot";
            out.from_part = from.attribute("Part") ? from.attribute("Part").value() : "";
        }
        if (pugi::xml_node to = node.child("To")) {
            out.to_player = to.attribute("Player")
                ? (std::strcmp(to.attribute("Player").value(), "Enemy") == 0 ? 2 : 1)
                : 2;
            out.to_obj = to.attribute("Object") ? to.attribute("Object").value() : "Pivot";
            out.to_part = to.attribute("Part") ? to.attribute("Part").value() : "";
        }
    } else if (out.type == "Keys") {
        // JS `vm`: child <Key Type=.. PressType=../> — Tap/Hold/Release.
        // The evaluator sees the key types the fighter has buffered.
        std::vector<std::string> parts;
        for (pugi::xml_node key : node.children("Key")) {
            std::string t = key.attribute("Type") ? key.attribute("Type").value() : "";
            std::string p = key.attribute("PressType") ? key.attribute("PressType").value() : "";
            parts.push_back(t + ":" + p);
        }
        out.keys.clear();
        for (const std::string& s : parts) {
            if (!out.keys.empty()) out.keys += ",";
            out.keys += s;
        }
    } else if (out.type == "CurrentInterval" || out.type == "IntervalEnd" ||
               out.type == "IntervalStart") {
        // JS `tm` (CurrentInterval): Name + Type (Attack/Block/Invulnerable).
        // `fe.G0` maps Type string -> interval type.
        if (node.attribute("Type")) {
            out.value_int = interval_type_resolve(out.name, node.attribute("Type").value());
        }
    } else if (out.type == "RoundStage") {
        // JS `Em`: Name = StartStance/Fight/EndStance/TryOn.
        out.value_int = 0;
        if (out.name == "StartStance") out.value_int = 1;
        else if (out.name == "Fight") out.value_int = 2;
        else if (out.name == "EndStance") out.value_int = 3;
        else if (out.name == "TryOn") out.value_int = 7;
    } else if (out.type == "Health") {
        // JS `rm`: Min/Max on the health ratio (yDa/zDa).
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
    } else if (out.type == "Random") {
        // JS `xp`: Chance attribute -> 0..100.
        out.value_int = data::xml_attr_int(node, "Chance", 0);
    } else if (out.type == "Round") {
        // JS `yp`: Number attribute -> round index (1-based).
        out.value_int = data::xml_attr_int(node, "Number", 0);
    } else if (out.type == "PhysicsFrameNumber") {
        // JS `Cm`: Min/Max frame number (defaults -1 = unset).
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = static_cast<float>(data::xml_attr_int(node, "Min", -1));
        out.max = static_cast<float>(data::xml_attr_int(node, "Max", -1));
    } else if (out.type == "Bullets") {
        // JS `lp`: Min/Max + Type ("MagicBullet"/"RaidChargeBullet").
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
    } else if (out.type == "BattleType") {
        // JS `lm`: Value attr ("FightNone" default).
        if (node.attribute("Value")) out.value = node.attribute("Value").value();
    } else if (out.type == "BossAbilityState") {
        // JS `nm`: Value attr (bool) — always false if set.
        out.value_int = data::xml_attr_bool(node, "Value", false) ? 1 : 0;
    } else if (out.type == "Style") {
        // JS `Ap`: Min/Max style enum (Turtle=0..Crazy=4) + Player.
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = 0; out.max = 0;
        // (Style numeric enums not needed for the evaluator — see note in
        // conditions.cpp.)
    } else if (out.type == "Name") {
        // JS `Am`: Value attr = fighter name.
        if (node.attribute("Value")) out.name = node.attribute("Value").value();
    } else if (out.type == "ModelMirrored") {
        // JS `Dm`: no attrs (checks the fighter's mirrored flag).
    } else if (out.type == "RoundResult") {
        // JS `Fm`: Name (Victory/Defeat) + Type (Timeout/Ringout).
        out.value_int = 0;
        if (out.name == "Victory") out.value_int = 1;
        else if (out.name == "Defeat") out.value_int = 2;
    } else if (out.type == "ModExists" || out.type == "ModExpires") {
        // JS `tp`: Name + Namespace. `ModExists` checks the mods set.
        // (ModExpires is a trigger, not a condition — parsed here as a
        //  leaf with the same name match for completeness.)
        if (node.attribute("Namespace")) out.value = node.attribute("Namespace").value();
    } else if (out.type == "Perk") {
        // JS `Bm`: Name attr = perk name; checks my/enemy perk lists.
    } else if (out.type == "Item" || out.type == "Weapon" || out.type == "Player") {
        // JS `um` (Item) / `Hm` (Weapon): Type + SubType + Name attrs.
        // Note: JS `um` reads SubType, `Hm` reads SubType too.
        // The XML uses SubType="Fists" etc.
    } else if (out.type == "CurrentAnimation") {
        // JS `lg`: Name + Physics attr + $Move/$NoAnimation$ special values.
        // (handled by evaluator)
    } else if (out.type == "Combo") {
        // JS `mp`: Range via Ag (Combo counter).
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
    } else if (out.type == "Pain") {
        // JS `vp`: Range (pain value).
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
    } else if (out.type == "MagicCharge") {
        // JS `sp`: Range (magic charge).
        out.has_min = node.attribute("Min") != nullptr;
        out.has_max = node.attribute("Max") != nullptr;
        out.min = data::xml_attr_float(node, "Min", 0.0f);
        out.max = data::xml_attr_float(node, "Max", 0.0f);
    } else if (out.type == "InTheArea") {
        // JS `qp`: no attrs — true iff the fighter is in the arena.
    } else if (out.type == "PerkStart") {
        // JS `wp`: always true.
    } else if (out.type == "Direction") {
        // JS `pm`: From/To refs + sign check.
        if (node.attribute("Name")) out.name = node.attribute("Name").value();
    } else if (out.type == "Hit") {
        // JS `sm`: Type + Name (last-hit type/animation).
        if (node.attribute("Type")) out.subtype = node.attribute("Type").value();
        if (node.attribute("Name")) out.name = node.attribute("Name").value();
    } else if (out.type == "ModelExists") {
        // JS `ym`: Name attr.
    } else if (out.type == "Screen") {
        // JS `Gm`: Name attr (Fight/Profile/Shop*).
        if (node.attribute("Name")) out.name = node.attribute("Name").value();
    } else if (out.type == "Birth") {
        // JS `mm`: Name attr = the fighter's aK (birth name).
    }
}

// JS `Fa.dMa` template walk: given a move's Template string, resolve each
// tag to its <Template> node (Fa.kxb table) and merge (clone) the template's
// Conditions/Locks/Intervals/Align/SetDirection into the move. The JS merges
// onto the move's OWN content (Fa.amb + Fa.xbb read both). We emulate by
// returning the list of template nodes to merge FROM.
void collect_templates(pugi::xml_node templates_root, const std::string& template_str,
                       std::vector<pugi::xml_node>& out,
                       std::set<std::string>& visited) {
    if (template_str.empty()) {
        return;
    }
    for (const std::string& tag : split_pipe(template_str)) {
        if (tag.empty() || !visited.insert(tag).second) {
            continue;
        }
        for (pugi::xml_node tpl : templates_root.children("Template")) {
            if (pugi::xml_attribute n = tpl.attribute("Name")) {
                if (tag == n.value()) {
                    out.push_back(tpl);
                    // Templates can inherit other templates (Fa.dMa recurses).
                    if (pugi::xml_attribute sub = tpl.attribute("Template")) {
                        collect_templates(templates_root, sub.value(), out, visited);
                    }
                    break;
                }
            }
        }
    }
}

void merge_conds(const std::vector<pugi::xml_node>& templates, pugi::xml_node own,
                 std::vector<Cond>& out) {
    if (own) {
        parse_cond_children(own, out);
    }
    for (pugi::xml_node tpl : templates) {
        if (pugi::xml_node tc = tpl.child("Conditions")) {
            parse_cond_children(tc, out);
        }
    }
}

void merge_intervals(const std::vector<pugi::xml_node>& templates, pugi::xml_node own,
                     int end_frame, std::vector<Interval>& out) {
    auto parse_list = [&](pugi::xml_node list) {
        if (!list) return;
        for (pugi::xml_node it : list.children("Interval")) {
            Interval iv;
            parse_interval(it, end_frame, iv);
            out.push_back(std::move(iv));
        }
    };
    parse_list(own);
    for (pugi::xml_node tpl : templates) {
        parse_list(tpl.child("Intervals"));
    }
}

} // namespace

bool parse_moves(const std::string& xml_text, std::map<std::string, MoveDef>& out) {
    data::xml_doc doc;
    doc.parse(xml_text);
    pugi::xml_node root = doc.root().child("Movesxml");
    if (!root) {
        return false;
    }

    // Templates table (JS `Fa.kxb`: <Templates><Template Name=..>`).
    pugi::xml_node templates_root = root.child("Templates");

    for (pugi::xml_node move : root.child("Moves").children("Move")) {
        MoveDef def;
        if (pugi::xml_attribute n = move.attribute("Name")) def.name = n.value();
        if (def.name.empty()) {
            continue;
        }
        const char* tpl = move.attribute("Template") ? move.attribute("Template").value() : "";
        for (const std::string& tag : split_pipe(tpl)) {
            if (!tag.empty()) def.template_tags.insert(tag);
        }
        if (pugi::xml_attribute t = move.attribute("Type")) def.type = t.value();
        if (pugi::xml_attribute f = move.attribute("FileName")) def.file_name = f.value();
        def.mid_frames = data::xml_attr_int(move, "MidFrames", 0);
        def.first_frame = data::xml_attr_int(move, "FirstFrame", 0);
        def.end_frame = data::xml_attr_int(move, "EndFrame", 0);
        def.priority = data::xml_attr_int(move, "Priority", 0);
        if (pugi::xml_attribute w = move.attribute("TacticWeapon")) def.tactic_weapon = w.value();
        if (pugi::xml_attribute e = move.attribute("TacticEquivalent")) def.tactic_equivalent = e.value();
        if (pugi::xml_attribute m = move.attribute("MirrorNode")) def.mirror_node = m.value();

        // Resolve Template inheritance.
        std::vector<pugi::xml_node> templates;
        std::set<std::string> visited;
        collect_templates(templates_root, tpl, templates, visited);

        // Conditions (own + inherited).
        merge_conds(templates, move.child("Conditions"), def.conditions);

        // Events (JS `Fa.GIa` L715 + `kz.create` L771): the event names are
        // the <Events> child element names ("KeyPressed", "AnimationEnd",
        // "IntervalEnd", ...).
        auto merge_events = [&](pugi::xml_node list) {
            if (!list) return;
            for (pugi::xml_node ev : list.children()) {
                def.events.insert(ev.name());
            }
        };
        merge_events(move.child("Events"));
        for (pugi::xml_node tpl_node : templates) {
            merge_events(tpl_node.child("Events"));
        }

        // Tactics conditions (JS `Fa.djb` — Tactics/Conditions).
        if (pugi::xml_node tactics = move.child("Tactics")) {
            if (pugi::xml_node tc = tactics.child("Conditions")) {
                parse_cond_children(tc, def.tactics);
            }
        }
        for (pugi::xml_node tpl_node : templates) {
            if (pugi::xml_node ttc = tpl_node.child("Tactics")) {
                if (pugi::xml_node tc = ttc.child("Conditions")) {
                    parse_cond_children(tc, def.tactics);
                }
            }
        }

        // Intervals (own + inherited).
        merge_intervals(templates, move.child("Intervals"), def.end_frame, def.intervals);

        // Locks.
        if (pugi::xml_node locks = move.child("Locks")) {
            for (pugi::xml_node item : locks.children("Item")) {
                Lock l;
                if (pugi::xml_attribute t = item.attribute("Type")) l.type = t.value();
                if (pugi::xml_attribute s = item.attribute("SubType")) l.subtype = s.value();
                if (pugi::xml_attribute n = item.attribute("Name")) l.name = n.value();
                def.locks.push_back(std::move(l));
            }
            for (pugi::xml_node op : locks.children("Operator")) {
                for (pugi::xml_node item : op.children("Item")) {
                    Lock l;
                    if (pugi::xml_attribute t = item.attribute("Type")) l.type = t.value();
                    if (pugi::xml_attribute s = item.attribute("SubType")) l.subtype = s.value();
                    if (pugi::xml_attribute n = item.attribute("Name")) l.name = n.value();
                    l.or_ = true;
                    def.locks.push_back(std::move(l));
                }
            }
        }

        // Align.
        if (pugi::xml_node align = move.child("Align")) {
            def.align.has_align = true;
            if (pugi::xml_attribute a = align.attribute("Axis")) def.align.axis = a.value();
            if (pugi::xml_node pivot = align.child("Pivot")) {
                if (pugi::xml_attribute o = pivot.attribute("Object")) def.align.pivot_object = o.value();
                if (pugi::xml_attribute p = pivot.attribute("Part")) def.align.pivot_part = p.value();
                if (pugi::xml_attribute p = pivot.attribute("Player")) def.align.pivot_player = p.value();
            }
            if (pugi::xml_node pos = align.child("Position")) {
                if (pugi::xml_attribute o = pos.attribute("Object")) def.align.pos_object = o.value();
                if (pugi::xml_attribute p = pos.attribute("Part")) def.align.pos_part = p.value();
                if (pugi::xml_attribute p = pos.attribute("Player")) def.align.pos_player = p.value();
            }
        }

        out.emplace(def.name, std::move(def));
    }
    return true;
}

std::string cond_to_string(const Cond& c, int depth) {
    std::ostringstream os;
    std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    os << indent;
    if (c.op != cond_op::leaf) {
        const char* opname = c.op == cond_op::and_ ? "AND" : (c.op == cond_op::or_ ? "OR" : "NOT");
        os << "Operator " << opname << (c.not_ ? " [Not]" : "") << "\n";
        for (const Cond& child : c.children) {
            os << cond_to_string(child, depth + 1);
        }
        return os.str();
    }
    os << c.type << (c.name.empty() ? "" : " Name=" + c.name)
       << (c.subtype.empty() ? "" : " Type=" + c.subtype)
       << (c.not_ ? " [Not]" : "");
    if (c.has_min || c.has_max) {
        os << " range[" << c.min << ".." << c.max << "]";
    }
    if (c.type == "Keys") os << " keys=" << c.keys;
    os << "\n";
    return os.str();
}

} // namespace sf2::scene
