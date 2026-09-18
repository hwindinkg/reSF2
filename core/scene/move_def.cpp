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

// JS `sa.$h` (`HQ` L2472 `v[7]=65` ...): key-type NAME -> control id.
// 0 = unregistered, which never matches (the buffer holds ids 1..14).
int key_type_id(const std::string& s) {
    if (s == "Up") return 1;
    if (s == "Up-Forward") return 2;
    if (s == "Forward") return 3;
    if (s == "Down-Forward") return 4;
    if (s == "Down") return 5;
    if (s == "Down-Back") return 6;
    if (s == "Back") return 7;
    if (s == "Up-Back") return 8;
    if (s == "Punch") return 9;
    if (s == "Kick") return 10;
    if (s == "Ranged") return 11;
    if (s == "Magic") return 12;
    if (s == "RaidCharge") return 13;
    if (s == "Super") return 14;
    return 0;
}

// JS `vm.parse` (L749): `PressType` NAME -> `zd` list (1=sh/Tap, 2=Fh/Hold,
// 3=released/Release).
int press_type_id(const std::string& s) {
    if (s == "Tap") return 1;
    if (s == "Hold") return 2;
    if (s == "Release") return 3;
    return 1;  // JS: the else-branch of the Hold/Tap/Release chain
}

// `eca(a,b)`/`Eab` (L688-689): "every element of `a` matches a DISTINCT
// element of `b`" (a is a sub-multiset of b; also requires `a.size()<=b.size()`).
bool sub_multiset(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() > b.size()) return false;
    std::vector<bool> used(b.size(), false);
    for (const int x : a) {
        bool hit = false;
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (!used[i] && b[i] == x) {
                used[i] = true;
                hit = true;
                break;
            }
        }
        if (!hit) return false;
    }
    return true;
}

// `zd.$ga(a)` (L688): `eca(sh,a.sh) && eca(Fh,a.Fh) ? eca(released,a.released)
// : false` — compares the three press lists SEPARATELY (no cross-press match).
bool key_spec_ga(const std::vector<std::pair<int, int>>& p,
                 const std::vector<std::pair<int, int>>& q) {
    auto keys_for = [](const std::vector<std::pair<int, int>>& spec,
                       int press) {
        std::vector<int> out;
        for (const auto& kv : spec) {
            if (kv.second == press) out.push_back(kv.first);
        }
        return out;
    };
    const std::vector<int> p_sh = keys_for(p, 1), p_fh = keys_for(p, 2);
    const std::vector<int> q_sh = keys_for(q, 1), q_fh = keys_for(q, 2);
    if (!sub_multiset(p_sh, q_sh)) return false;
    if (!sub_multiset(p_fh, q_fh)) return false;
    return sub_multiset(keys_for(p, 3), keys_for(q, 3));
}

// JS `jc.BAa(a,b)` (L698: `qCa(){let a=m.l(); jc.BAa(this.va.rb,a); return a}`)
// — collect the type-4 (`vm` Keys) leaves of a condition tree, recursing the
// type-8 `Operator` containers. `cb` (Not) is NOT consulted by `$ga`.
void collect_key_specs(const std::vector<Cond>& conds,
                       std::vector<std::vector<std::pair<int, int>>>& out) {
    for (const Cond& c : conds) {
        if (c.op != cond_op::leaf) {
            collect_key_specs(c.children, out);
            continue;
        }
        if (c.type != "Keys") continue;
        std::vector<std::pair<int, int>> spec;
        std::string cur;
        std::vector<std::string> parts;
        for (const char ch : c.keys) {
            if (ch == ',') { parts.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        if (!cur.empty()) parts.push_back(cur);
        for (const std::string& w : parts) {
            const std::size_t colon = w.find(':');
            const std::string ts = colon == std::string::npos ? w : w.substr(0, colon);
            const std::string ps = colon == std::string::npos ? "Tap" : w.substr(colon + 1);
            spec.emplace_back(key_type_id(ts), press_type_id(ps));
        }
        out.push_back(std::move(spec));
    }
}

// JS `ra.c1a`/`ra.b1a` (L683-684): for every move `a`, walk the whole move
// list and append to `a.M7.$Q` every move `e` with `a.priority < e.priority`
// whose KeyPressed spec `$ga`-conflicts with one of `a`'s:
//   `g && a.M7.$Q.push(e)` where `g` = `qCa(e)[n].xn.$ga(qCa(a)[h].xn)`.
void link_mirror_exclusive(std::map<std::string, MoveDef>& moves) {
    std::map<std::string, std::vector<std::vector<std::pair<int, int>>>> qca;
    for (const auto& kv : moves) collect_key_specs(kv.second.conditions, qca[kv.first]);
    for (auto& kv : moves) {
        MoveDef& a = kv.second;
        const std::vector<std::vector<std::pair<int, int>>>& qa = qca[a.name];
        if (qa.empty()) continue;  // `if(c.length>0)` (L683)
        for (const auto& ke : moves) {
            const MoveDef& e = ke.second;
            if (!(a.priority < e.priority)) continue;
            const std::vector<std::vector<std::pair<int, int>>>& qe = qca[e.name];
            if (qe.empty()) continue;
            bool conflict = false;
            for (const auto& xa : qa) {
                for (const auto& xe : qe) {
                    if (key_spec_ga(xe, xa)) { conflict = true; break; }
                }
                if (conflict) break;
            }
            if (conflict) a.mirror_exclusive.push_back(e.name);
        }
    }
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
    // JS `Ul.J3` (L775): `DL = !NoEffect` (an <Interval> attribute).
    out.no_effect = data::xml_attr_bool(node, "NoEffect", false);
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
        // JS `Ul.J3` (L776-777): EVERY `<Hit>` child (not just the first)
        // pushes a window `{name, Start ?? interval.start, End ?? finish}`.
        for (pugi::xml_node hit : node.children("Hit")) {
            Interval::HitWindow w;
            w.name = hit.attribute("Name") ? hit.attribute("Name").value() : "";
            w.start = data::xml_attr_int(hit, "Start", out.start);
            w.end = hit.attribute("End") ? data::xml_attr_int(hit, "End", out.end)
                                         : out.end;
            out.hit_windows.push_back(w);
        }
        if (!out.hit_windows.empty()) out.hit_name = out.hit_windows.front().name;
        if (pugi::xml_node imp = node.child("Impulse")) {
            out.impulse_x = data::xml_attr_float(imp, "X", 0.0f);
            out.impulse_y = data::xml_attr_float(imp, "Y", 0.0f);
            out.impulse_z = data::xml_attr_float(imp, "Z", 0.0f);
            out.has_impulse = true;
        }
        // JS `Ul.qjb` (L777-778): `Xb=Damage/@Value`,
        // `a3=Damage/@NoCritical`, `HC=Damage/@BodyPart`, then child
        // dispatch — EVERY `<Damage Type Shift>` -> SZ (`attack_attrs`),
        // EVERY `<Defense Type>` -> KP (`defense_names`). The old port read
        // `.child("Damage")` (the FIRST sub-block only) and never looked at
        // `<Defense>`, so 572-43 = 529 blocks lost their second attribute
        // and 120 lost their authored defense.
        if (pugi::xml_node dmg = node.child("Damage")) {
            out.has_damage = true;
            out.damage = data::xml_attr_float(dmg, "Value", 0.0f);
            out.no_critical = data::xml_attr_bool(dmg, "NoCritical", false);
            for (pugi::xml_node sub : dmg.children()) {
                const std::string tag = sub.name();
                const char* type = sub.attribute("Type").value();
                const std::string t = type != nullptr ? type : "";
                if (tag == "Damage") {
                    out.attack_attrs.push_back(
                        {t, data::xml_attr_float(sub, "Shift", 0.0f)});
                } else if (tag == "Defense") {
                    out.defense_names.push_back(t);
                }
            }
            // Legacy mirror of the first sub-block (probe/demo printouts).
            if (!out.attack_attrs.empty()) {
                out.damage_type = out.attack_attrs[0].first;
                out.damage_shift = out.attack_attrs[0].second;
            }
        }
        if (pugi::xml_node combo = node.child("Combo")) {
            out.combo_time = data::xml_attr_int(combo, "Time", 0);
        }
    }
}

void parse_cond_children(pugi::xml_node parent, std::vector<Cond>& out);

// JS `Nd.ol` (L705): the `Player` attribute -> the side enum. Me=1, Enemy=2,
// Both=5, Child=4, Parent=3, EnemyChild=6, SuperParent=7, default 0.
int player_from_attr(const char* player) {
    if (player == nullptr) return 0;
    if (std::strcmp(player, "Me") == 0) return 1;
    if (std::strcmp(player, "Enemy") == 0) return 2;
    if (std::strcmp(player, "Both") == 0) return 5;
    if (std::strcmp(player, "Child") == 0) return 4;
    if (std::strcmp(player, "Parent") == 0) return 3;
    if (std::strcmp(player, "EnemyChild") == 0) return 6;
    if (std::strcmp(player, "SuperParent") == 0) return 7;
    return 0;
}

// JS `lz.create` (L737-739): the `<Actions>` child element name -> the
// concrete `cb` sub-class' `type` (its ctor `super(N)`). -1 = no class
// (`lz.create` returns null; `Fa.DIa` L718 skips the node).
int action_type_for(const std::string& tag) {
    // L737: AddBullets=8 (Vl), CameraWeight=13 (Wl), CreatePlayer=0 (mh).
    if (tag == "AddBullets") return 8;
    if (tag == "CameraWeight") return 13;
    if (tag == "CreatePlayer") return 0;
    // L738: Delete=1 (Xl), Effect=5 (Yl), EnableBossAbility=14 (Zl),
    // HitEffect=10 (jg), PlayAnimation=17 ($l), RandomSound=4 (am),
    // SetCooldown=12 (bm), SetEndStage=15 (cm), ShakeScreen=9 (dm),
    // Sound=2 (fm), StopEffect=6 (gm), StopFollowEffect=7 (hm), StopSound=3 (im).
    if (tag == "Delete") return 1;
    if (tag == "Effect") return 5;
    if (tag == "EnableBossAbility") return 14;
    if (tag == "HitEffect") return 10;
    if (tag == "PlayAnimation") return 17;
    if (tag == "RandomSound") return 4;
    if (tag == "SetCooldown") return 12;
    if (tag == "SetEndStage") return 15;
    if (tag == "ShakeScreen") return 9;
    if (tag == "Sound") return 2;
    if (tag == "StopEffect") return 6;
    if (tag == "StopFollowEffect") return 7;
    if (tag == "StopSound") return 3;
    // L739: TryOnEnd=1 (jm - the same code as Delete's Xl), ZoomEffect=11 (km).
    if (tag == "TryOnEnd") return 1;
    if (tag == "ZoomEffect") return 11;
    return -1;
}

// ONE `<Actions>` child -> MoveAction. Mirrors JS `cb.parse` (L724-725) plus
// the per-kind parse bodies (L725-737). The base parse reads `Frame` XOR
// `Event` (the trigger), `Player` and the `<Conditions>` child; the ported
// kinds add their own fields. The remaining kinds keep the base record (their
// own attrs - FileName/Animation/Value/EffectTime/... - have no consumer yet;
// they are reported as follow-up, never faked).
void parse_action(pugi::xml_node node, MoveAction& out) {
    out.kind = node.name();
    out.js_type = action_type_for(out.kind);
    // JS `cb.parse`: `Frame` first (Z5=0), else `Event` (Z5=1).
    if (pugi::xml_attribute f = node.attribute("Frame")) {
        out.frame_trigger = true;
        out.frame = f.as_int();
    } else {
        out.frame_trigger = false;
        if (pugi::xml_attribute e = node.attribute("Event")) out.event = e.value();
    }
    out.player = player_from_attr(node.attribute("Player")
                                      ? node.attribute("Player").value()
                                      : nullptr);
    if (pugi::xml_node cond = node.child("Conditions")) {
        parse_cond_children(cond, out.conditions);
    }
    // `fm` (Sound, L735): Name + Volume + Looped + Voice + PackName.
    if (out.js_type == 2) {
        if (pugi::xml_attribute n = node.attribute("Name")) out.name = n.value();
        out.volume = data::xml_attr_float(node, "Volume", 1.0f);
        out.looped = data::xml_attr_bool(node, "Looped", false);
        if (pugi::xml_attribute v = node.attribute("Voice")) {
            out.has_voice = true;
            out.voice = v.value();
        }
        if (pugi::xml_attribute p = node.attribute("PackName")) out.pack = p.value();
        return;
    }
    // `am` (RandomSound, L733): Voice + every child's Name attr (`qq`).
    if (out.js_type == 4) {
        if (pugi::xml_attribute v = node.attribute("Voice")) {
            out.has_voice = true;
            out.voice = v.value();
        }
        for (pugi::xml_node ch : node.children()) {
            out.names.push_back(ch.attribute("Name")
                                    ? ch.attribute("Name").value()
                                    : "ERR_RAND_SOUND_NO_NAME");
        }
        return;
    }
    // `im` (StopSound, L736) / `gm` (StopEffect, L735-736) /
    // `hm` (StopFollowEffect, L736): a single Name attr.
    if (out.js_type == 3 || out.js_type == 6 || out.js_type == 7) {
        if (pugi::xml_attribute n = node.attribute("Name")) out.name = n.value();
        return;
    }
    // `dm` (ShakeScreen, L734): `this.hw = new em` + `this.hw.parse(a)`
    // (`em.parse` L1288) — Type/PauseTime/EffectTime/AmplitudeX/Y/
    // FrequencyX/Y. `wd.Wvb` (L519) hands `hw` to the camera `ql.DL` (L370).
    if (out.kind == "ShakeScreen") {
        if (pugi::xml_attribute t = node.attribute("Type")) out.shake_type = t.value();
        out.pause_time = data::xml_attr_int(node, "PauseTime", 0);
        out.effect_time = data::xml_attr_int(node, "EffectTime", 0);
        out.amplitude_x = data::xml_attr_float(node, "AmplitudeX", 0.0f);
        out.amplitude_y = data::xml_attr_float(node, "AmplitudeY", 0.0f);
        out.frequency_x = data::xml_attr_float(node, "FrequencyX", 0.0f);
        out.frequency_y = data::xml_attr_float(node, "FrequencyY", 0.0f);
        return;
    }
    // `Wl` (CameraWeight, L726): `time = u.H(Time)`, `$x = u.H(Delay)`.
    if (out.kind == "CameraWeight") {
        out.weight_time = data::xml_attr_float(node, "Time", 0.0f);
        out.weight_delay = data::xml_attr_float(node, "Delay", 0.0f);
        return;
    }
    // `Zl` (EnableBossAbility, L730): `this.value = u.ka(Value)`.
    if (out.kind == "EnableBossAbility") {
        out.bool_value = data::xml_attr_bool(node, "Value", false);
        return;
    }
    // `Vl` (AddBullets, L725): `Type` -> `s6` (MagicBullet 0 /
    // RaidChargeBullet 1 / anything else leaves it undefined = no-op) and
    // `this.value = u.I(Value)`.
    if (out.kind == "AddBullets") {
        const std::string bt =
            node.attribute("Type") ? node.attribute("Type").value() : std::string();
        out.bullet_kind = bt == "MagicBullet" ? 0 : bt == "RaidChargeBullet" ? 1 : -1;
        out.bullet_value = data::xml_attr_int(node, "Value", 0);
        return;
    }
    // `jg` (HitEffect, L731): `FileName` -> `vT`, `StartingRotation` ->
    // `ywb`, `ChangeHitEffectScale` -> `aza` (`u.H` default 0). `wd.Xvb`
    // (L519) consumes exactly these three.
    if (out.kind == "HitEffect") {
        if (pugi::xml_attribute fn = node.attribute("FileName")) {
            out.hit_effect_file = fn.value();
        }
        out.hit_effect_scale = data::xml_attr_float(node, "ChangeHitEffectScale", 0.0f);
        out.hit_effect_rotation = data::xml_attr_float(node, "StartingRotation", 0.0f);
        return;
    }
    // Every other kind: keep the Name attr when present (informational; the
    // kind is parsed data until its consumer system is ported).
    if (pugi::xml_attribute n = node.attribute("Name")) out.name = n.value();
}

void merge_actions(const std::vector<pugi::xml_node>& templates, pugi::xml_node own,
                   std::vector<MoveAction>& out) {
    auto parse_list = [&](pugi::xml_node list) {
        if (!list) return;
        for (pugi::xml_node a : list.children()) {
            MoveAction act;
            parse_action(a, act);
            if (act.js_type < 0) continue;  // JS `lz.create` -> null, skipped
            out.push_back(std::move(act));
        }
    };
    parse_list(own);
    for (pugi::xml_node tpl : templates) parse_list(tpl.child("Actions"));
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
    } else if (out.type == "Item" || out.type == "Weapon") {
        // JS `um` (Item) / `Hm` (Weapon): Type + SubType + Name attrs.
        // Note: JS `um` reads SubType, `Hm` reads SubType too.
        // The XML uses SubType="Fists" etc.
    } else if (out.type == "Player") {
        // JS `Dm` (Player; `sa.oe.set("Player",6)` L705, class g="139" L755):
        //   ctor: `this.LUa = u.I(a.attributes.get("Number"),1)==1`
        //   he:   `a = this.LUa == a.qb; return this.cb?!a:a`
        // `Number` is the side index with default 1 (`u.I(get("Number"),1)`).
        // `qb` is the "controlled fighter" flag on the event context
        // (`a.qb=b.parameters.qb` L680). `Number=1` therefore selects the
        // PLAYER (moves.xml `FistsStartStance-Left`/`…Idle-Left`), any other
        // value the other side (`…-Right`). The element name itself is the
        // semantics flag the evaluator keys on (`eval_player`).
        out.value_int = data::xml_attr_int(node, "Number", 1);
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

// One `<Locks>` block -> `Lock` records. Mirrors `Fa.H3` (L371011): walk the
// block's children through `Tl.create`; an `Operator` group flattens its
// `<Item>` members with `or_=true`.
void parse_locks_node(pugi::xml_node locks, std::vector<Lock>& out) {
    if (!locks) return;
    for (pugi::xml_node item : locks.children("Item")) {
        Lock l;
        if (pugi::xml_attribute t = item.attribute("Type")) l.type = t.value();
        if (pugi::xml_attribute s = item.attribute("SubType")) l.subtype = s.value();
        if (pugi::xml_attribute n = item.attribute("Name")) l.name = n.value();
        l.not_ = data::xml_attr_bool(item, "Not", false);
        out.push_back(std::move(l));
    }
    for (pugi::xml_node op : locks.children("Operator")) {
        for (pugi::xml_node item : op.children("Item")) {
            Lock l;
            if (pugi::xml_attribute t = item.attribute("Type")) l.type = t.value();
            if (pugi::xml_attribute s = item.attribute("SubType")) l.subtype = s.value();
            if (pugi::xml_attribute n = item.attribute("Name")) l.name = n.value();
            l.not_ = data::xml_attr_bool(item, "Not", false);
            l.or_ = true;
            out.push_back(std::move(l));
        }
        // Fail closed on unmodelled Or members too (`Or{<Perk A>,
        // <Perk B>}`): with the perk children dropped the group became
        // EMPTY, so `or_group` stayed false and the move looked
        // lock-free (`AssistantBigMagariYariPlayer`,
        // PERK_ASSISTANTS|PERK_ASSISTANTS_PVP, Priority 110, won the
        // Super key). An unsatisfiable Or member keeps the group
        // satisfiable only by its modelled members.
        for (pugi::xml_node other : op.children()) {
            const std::string tag = other.name();
            if (tag == "Item") continue;
            Lock l;
            l.or_ = true;
            l.never = true;
            out.push_back(std::move(l));
        }
    }
    // Fail closed on every lock kind the port does not model
    // (`<Perk Name=..>` etc.). Dropping the element silently made the
    // move look lock-free: `HermitStormPlayer` (PERK_HERMITSTORM) and
    // `RatWavePlayer` (PERK_RAT_WAVE) then entered the player's move
    // list and won the Up key on Priority. The JS `ra.Hza` tests every
    // lock node, so an untracked one must not pass.
    for (pugi::xml_node other : locks.children()) {
        const std::string tag = other.name();
        if (tag == "Item" || tag == "Operator") continue;
        Lock l;
        l.never = true;
        out.push_back(std::move(l));
    }
}

// JS `Fa.xbb` (L694): `d.locks = Fa.HS("Locks", a, b)` — `Fa.HS(a,b,c)`
// (L371013) reads the node's OWN `<Locks>` first and then EVERY resolved
// Template's (`for(;b<e;) Fa.H3(c[b++].A(a),d)`), so a Template can impose
// locks the move's own block does not carry. The port parsed only the move's
// own block. Same own-then-templates order as `merge_conds`/`merge_intervals`.
void merge_locks(const std::vector<pugi::xml_node>& templates, pugi::xml_node own,
                 std::vector<Lock>& out) {
    parse_locks_node(own, out);
    for (pugi::xml_node tpl : templates) {
        parse_locks_node(tpl.child("Locks"), out);
    }
}

} // namespace

bool parse_moves(const std::string& xml_text, std::map<std::string, MoveDef>& out,
                 std::vector<GlobalTrigger>* global_out) {
    data::xml_doc doc;
    doc.parse(xml_text);
    pugi::xml_node root = doc.root().child("Movesxml");
    if (!root) {
        return false;
    }

    // Root `<Triggers>` (JS `Fa.Exb` L708 -> `ra.Dm`, called from
    // `Fa.parse` with `f = f.A("Triggers")`). Each `<Trigger Name=..>`:
    //   `f.Hc    = Fa.GIa(d)`  -> `<Events>`  (`kz.create`)
    //   `f.rb    = Fa.HS("Conditions",d)` -> `<Conditions>` (`Tl.create`)
    //   `f.locks = Fa.HS("Locks",d)`      -> `<Locks>`      (`Tl.create`)
    //   `f.actions = Fa.CIa(d)`           -> `<Actions>`    (`lz.create`)
    if (global_out != nullptr) {
        global_out->clear();
        if (pugi::xml_node trigs = root.child("Triggers")) {
            for (pugi::xml_node t : trigs.children("Trigger")) {
                GlobalTrigger gt;
                if (pugi::xml_attribute n = t.attribute("Name")) gt.name = n.value();
                if (pugi::xml_node ev = t.child("Events")) {
                    parse_cond_children(ev, gt.events);
                }
                if (pugi::xml_node cd = t.child("Conditions")) {
                    parse_cond_children(cd, gt.conditions);
                }
                if (pugi::xml_node lk = t.child("Locks")) {
                    parse_cond_children(lk, gt.locks);
                }
                if (pugi::xml_node ac = t.child("Actions")) {
                    for (pugi::xml_node a : ac.children()) {
                        MoveAction act;
                        parse_action(a, act);
                        if (act.js_type < 0) continue;  // `lz.create` -> null
                        gt.actions.push_back(std::move(act));
                    }
                }
                global_out->push_back(std::move(gt));
            }
        }
    }

    // Templates table (JS `Fa.kxb`: <Templates><Template Name=..>`).
    pugi::xml_node templates_root = root.child("Templates");

    int move_doc_index = 0;  // `<Move>` document order (`ra.Ul`, L712)
    for (pugi::xml_node move : root.child("Moves").children("Move")) {
        MoveDef def;
        // JS `ra.Ul` document order (see `MoveDef::profile_order`): counts
        // every `<Move>` element, mirroring `Fa.Ueb`'s single pass (L709).
        def.profile_order = move_doc_index++;
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
        def.no_interp = data::xml_attr_bool(move, "NoInterpolationFrames", false);
        def.no_animation = data::xml_attr_bool(move, "NoAnimation", false);  // `Rha`
        def.end_frame = data::xml_attr_int(move, "EndFrame", 0);
        def.priority = data::xml_attr_int(move, "Priority", 0);
        def.style_factor = data::xml_attr_float(move, "StyleFactor", 1.0f);  // `RNa`
        if (pugi::xml_attribute w = move.attribute("TacticWeapon")) {
            def.tactic_weapon = w.value();
            // JS `l.Gsb(n)` (L711): `n = TacticWeapon; QX = n.split("|")`.
            const std::string raw = w.value();
            std::size_t start = 0;
            while (start <= raw.size()) {
                const std::size_t bar = raw.find('|', start);
                const std::size_t stop = bar == std::string::npos ? raw.size() : bar;
                if (stop > start) def.qx.push_back(raw.substr(start, stop - start));
                if (bar == std::string::npos) break;
                start = bar + 1;
            }
        }
        if (pugi::xml_attribute e = move.attribute("TacticEquivalent")) def.tactic_equivalent = e.value();
        if (pugi::xml_attribute m = move.attribute("MirrorNode")) def.mirror_node = m.value();
        // JS `Fa.Ueb` (L712) + `Ru` (L1253): `<Profile Show Rank Icon
        // KeysDescription/>`. `Show="1"` registers the move in the `Ru`
        // catalog (`ra.Ul`) that `v.uQ()` (L1218) feeds the profile Moves
        // tab; `Rank` is the `es.uZ` (L2239) sort key; `image =
        // Ye.qI(Icon)` (L1863: first '.' -> '/') is the `skills` atlas
        // frame name; `KeysDescription` is the `ls.ymb` (L2238) label key.
        if (pugi::xml_node prof = move.child("Profile")) {
            def.profile_show = data::xml_attr_bool(prof, "Show", false);
            def.profile_rank = data::xml_attr_int(prof, "Rank", 0);
            if (pugi::xml_attribute ic = prof.attribute("Icon")) {
                def.profile_image = ic.value();
            }
            if (pugi::xml_attribute kd = prof.attribute("KeysDescription")) {
                def.profile_keys = kd.value();
            }
            // JS `Ye.qI` (L1863): `Eb.replace(a, ".", "/")` - a plain string
            // pattern, so only the FIRST '.' is replaced.
            const std::size_t dot = def.profile_image.find('.');
            if (dot != std::string::npos) def.profile_image[dot] = '/';
        }

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

        // Actions (JS `Fa.CIa` L718): own `<Actions>` first, then each
        // inherited template's, in template order.
        merge_actions(templates, move.child("Actions"), def.actions);

        // Locks (own + inherited Template `<Locks>`; `Fa.HS("Locks",a,b)`).
        merge_locks(templates, move.child("Locks"), def.locks);

        // Align.
        if (pugi::xml_node align = move.child("Align")) {
            def.align.has_align = true;
            if (pugi::xml_attribute a = align.attribute("Axis")) {
                def.align.axis = a.value();
                // JS `jva` (L719): Axis split on '|' sets cI/dI/MY; an absent
                // Axis leaves all three true (already the struct default).
                def.align.axis_x = def.align.axis_y = def.align.axis_z = false;
                std::size_t start = 0;
                while (start <= def.align.axis.size()) {
                    const std::size_t sep = def.align.axis.find('|', start);
                    const std::string tok = def.align.axis.substr(
                        start,
                        sep == std::string::npos ? std::string::npos : sep - start);
                    if (tok == "X") def.align.axis_x = true;
                    else if (tok == "Y") def.align.axis_y = true;
                    else if (tok == "Z") def.align.axis_z = true;
                    if (sep == std::string::npos) break;
                    start = sep + 1;
                }
            }
            if (pugi::xml_attribute sm = align.attribute("ShiftModelNode")) {
                def.align.shift_model_node = sm.value();
            }
            if (pugi::xml_node pivot = align.child("Pivot")) {
                if (pugi::xml_attribute o = pivot.attribute("Object")) def.align.pivot_object = o.value();
                if (pugi::xml_attribute p = pivot.attribute("Part")) def.align.pivot_part = p.value();
                if (pugi::xml_attribute p = pivot.attribute("Player")) def.align.pivot_player = p.value();
            }
            if (pugi::xml_node pos = align.child("Position")) {
                if (pugi::xml_attribute o = pos.attribute("Object")) def.align.pos_object = o.value();
                if (pugi::xml_attribute p = pos.attribute("Part")) def.align.pos_part = p.value();
                if (pugi::xml_attribute p = pos.attribute("Player")) def.align.pos_player = p.value();
                if (pugi::xml_attribute sx = pos.attribute("ShiftX")) def.align.shift_x = sx.as_float();
                if (pugi::xml_attribute sy = pos.attribute("ShiftY")) def.align.shift_y = sy.as_float();
            }
        }

        // <Velocity> (JS `Fa.ykb` L721-722): `b = move.A("Velocity")`, else
        // the first inherited template that has one. Fields X/Y/Z -> `wua`,
        // Ax/Ay/Az -> `Coa`, SaveVelocity -> `qta`.
        auto parse_velocity = [](pugi::xml_node vel, Velocity& v) {
            v.has_velocity = true;
            v.x = data::xml_attr_float(vel, "X", 0.0f);
            v.y = data::xml_attr_float(vel, "Y", 0.0f);
            v.z = data::xml_attr_float(vel, "Z", 0.0f);
            v.ax = data::xml_attr_float(vel, "Ax", 0.0f);
            v.ay = data::xml_attr_float(vel, "Ay", 0.0f);
            v.az = data::xml_attr_float(vel, "Az", 0.0f);
            v.save_velocity = data::xml_attr_bool(vel, "SaveVelocity", false);
        };
        if (pugi::xml_node vel = move.child("Velocity")) {
            parse_velocity(vel, def.velocity);
        } else {
            for (pugi::xml_node tpl_node : templates) {
                if (pugi::xml_node vel = tpl_node.child("Velocity")) {
                    parse_velocity(vel, def.velocity);
                    break;
                }
            }
        }

        // <Rotation> (JS `Fa.Yjb(l, k.A("Rotation"))` L722 — the move's OWN
        // element; `Yjb` reads `Angle` + the `<Position>` child into
        // `jc.zX`/`jc.AX`). `AX` is an `ee` object-ref (`Fa.Yjb` L722).
        if (pugi::xml_node rot = move.child("Rotation")) {
            def.rotation.has_rotation = true;
            def.rotation.angle = data::xml_attr_float(rot, "Angle", 0.0f);
            if (pugi::xml_node pos = rot.child("Position")) {
                def.rotation.pos_player =
                    pos.attribute("Player") ? pos.attribute("Player").value() : "Null";
                def.rotation.pos_object =
                    pos.attribute("Object") ? pos.attribute("Object").value() : "";
                def.rotation.pos_part =
                    pos.attribute("Part") ? pos.attribute("Part").value() : "";
                def.rotation.shift_x = data::xml_attr_float(pos, "ShiftX", 0.0f);
                def.rotation.shift_y = data::xml_attr_float(pos, "ShiftY", 0.0f);
            }
        }

        out.emplace(def.name, std::move(def));
    }
    // JS `ra.c1a` (L683): the mirror-conflict table is linked over the WHOLE
    // parsed move list, after every move exists.
    link_mirror_exclusive(out);
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
