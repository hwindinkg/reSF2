#include "moves.hpp"
#include "../format/xml_doc.hpp"
#include "../core/math.hpp"
#include <cstdio>
#include <charconv>
#include <fstream>

namespace resf2::fight {

static float parse_float(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    float v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

static int parse_int(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    int v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

// Parse "1key|Central|Unarmed|Punch" like template strings
void MoveDatabase::parse_template(MoveDef& move, const std::string& tmpl) {
    if (tmpl.empty()) return;

    std::vector<std::string> parts;
    size_t start = 0;
    while (start < tmpl.size()) {
        auto sep = tmpl.find('|', start);
        if (sep == std::string::npos) {
            parts.push_back(tmpl.substr(start));
            break;
        }
        parts.push_back(tmpl.substr(start, sep - start));
        start = sep + 1;
    }

    for (auto& p : parts) {
        // Key count
        if (p == "1key") move.key_count = 1;
        else if (p == "2key") move.key_count = 2;
        else if (p == "3key") move.key_count = 3;
        // Direction
        else if (p == "Central") move.direction = "Central";
        else if (p == "Forward") move.direction = "Forward";
        else if (p == "Back") move.direction = "Back";
        else if (p == "Up") move.direction = "Up";
        else if (p == "Down") move.direction = "Down";
        else if (p == "UpForward") move.direction = "UpForward";
        else if (p == "UpBack") move.direction = "UpBack";
        else if (p == "DownForward") move.direction = "DownForward";
        else if (p == "DownBack") move.direction = "DownBack";
        // Move type
        else if (p == "Punch") move.move_type = "Punch";
        else if (p == "Kick") move.move_type = "Kick";
        else if (p == "Block") move.move_type = "Block";
        else if (p == "Hit") move.move_type = "Hit";
        else if (p == "Jump") move.move_type = "Jump";
        else if (p == "Idle") move.move_type = "Idle";
        else if (p == "Move") move.move_type = "Move";
        else if (p == "Step") move.move_type = "Step";
        else if (p == "DoubleStep") move.move_type = "DoubleStep";
        else if (p == "Retreat") move.move_type = "Retreat";
        else if (p == "Stance") move.move_type = "Stance";
        else if (p == "IdleStance") move.move_type = "IdleStance";
        // Weapon filter
        else if (p == "Unarmed") move.lock_weapon = "Unarmed";
    }
}

bool MoveDatabase::load_from_xml(const std::string& xml_content) {
    format::XmlDocument doc;
    if (!doc.parse(xml_content)) {
        std::fprintf(stderr, "[moves] XML parse error: %s\n", doc.error().c_str());
        return false;
    }

    const auto* root = doc.root();
    if (!root) return false;

    auto* movesxml = root->first_child("Movesxml");
    if (!movesxml) {
        std::fprintf(stderr, "[moves] No <Movesxml> root\n");
        return false;
    }

    auto* moves_node = movesxml->first_child("Moves");
    if (!moves_node) {
        std::fprintf(stderr, "[moves] No <Moves> section\n");
        return false;
    }

    // Parse each <Move> element
    for (const auto& child : moves_node->children) {
        if (child.name != "Move") continue;

        MoveDef move;

        // Basic attributes
        move.name = child.attr("Name");
        move.filename = child.attr("FileName");
        move.template_name = child.attr("Template");

        if (move.name.empty() || move.filename.empty()) continue;

        // Parse template string
        parse_template(move, move.template_name);

        // Optional XML attributes
        int first_frame = parse_int(child.attr("FirstFrame"));
        int end_frame = parse_int(child.attr("EndFrame"));
        int priority = parse_int(child.attr("Priority"));

        // Tactic weapon (from TacticWeapon attribute or lock_weapon)
        move.tactic_weapon = child.attr("TacticWeapon");

        // MidFrames (default 2 in moves.xml)
        move.mid_frames = parse_int(child.attr("MidFrames", "2"));

        // Parse Align / Pivot sub-elements
        auto* align = child.first_child("Align");
        if (align) {
            auto* pivot = align->first_child("Pivot");
            if (pivot) {
                auto obj = pivot->attr("Object");
                auto part = pivot->attr("Part");
                if (obj == "Nodes" && !part.empty()) {
                    // Store pivot node name for MoveInside
                    // (main.cpp stores as moveinside_pivot_node)
                }
            }
        }

        // Parse Locks
        auto* locks = child.first_child("Locks");
        if (locks) {
            // Check for simple <Item> or <Operator> wrapping
            auto* op = locks->first_child("Operator");
            if (op) {
                // OR/AND operator - find any weapon locks
                for (auto& item : op->children) {
                    if (item.name == "Item") {
                        auto type = item.attr("Type");
                        auto sub = item.attr("SubType");
                        if (type == "Weapon" && !sub.empty()) {
                            move.lock_weapon = sub;
                        }
                    }
                }
            } else {
                // Direct <Item> children
                for (auto& item : locks->children) {
                    if (item.name == "Item") {
                        auto type = item.attr("Type");
                        auto sub = item.attr("SubType");
                        if (type == "Weapon" && !sub.empty()) {
                            move.lock_weapon = sub;
                        }
                    }
                }
            }
            // Check for <Perk>
            auto* perk = locks->first_child("Perk");
            if (perk) {
                move.lock_perk = perk->attr("Name");
            }
        }

        // Parse Sound events from <Actions>
        auto* actions = child.first_child("Actions");
        if (actions) {
            for (auto& snd : actions->children) {
                if (snd.name == "Sound") {
                    MoveDef::SoundEvent se;
                    se.sound = snd.attr("Name");
                    se.time = parse_float(snd.attr("Frame"));
                    if (!se.sound.empty()) {
                        move.sound_events.push_back(std::move(se));
                    }
                }
            }
        }

        // Parse Conditions
        auto* conditions = child.first_child("Conditions");
        if (conditions) {
            // Distance
            auto* dist = conditions->first_child("Distance");
            if (dist) {
                move.distance.active = true;
                move.distance.min_dist = parse_float(dist->attr("Min"));
                move.distance.max_dist = parse_float(dist->attr("Max", "99999"));
            }
            // CurrentAnimation
            auto* cur_anim = conditions->first_child("CurrentAnimation");
            if (cur_anim) {
                move.required_current_animation = cur_anim->attr("Name");
            }
        }

        // Parse Intervals (from <Intervals> in both <Move> and <Template>)
        // For now, parse intervals directly from <Move> children
        auto* intervals = child.first_child("Intervals");
        if (intervals) {
            for (auto& iv : intervals->children) {
                if (iv.name != "Interval") continue;
                auto type = iv.attr("Type");
                auto name = iv.attr("Name");
                float start = parse_float(iv.attr("Start"));
                float end = parse_float(iv.attr("End"));

                if (type == "Attack" || name == "Attack") {
                    MoveDef::AttackInterval ai;
                    ai.start = start;
                    ai.end = end;
                    // Parse Damage
                    auto* dmg = iv.first_child("Damage");
                    if (dmg) {
                        ai.damage = parse_int(dmg->attr("Value"));
                    }
                    // Parse Impulse
                    auto* imp = iv.first_child("Impulse");
                    if (imp) {
                        ai.impulse.x = parse_float(imp->attr("X"));
                        ai.impulse.y = parse_float(imp->attr("Y"));
                    }
                    // Parse Hit name
                    auto* hit = iv.first_child("Hit");
                    if (hit) {
                        ai.hit_type = hit->attr("Name");
                    }
                    move.attack_intervals.push_back(ai);
                } else if (type == "Block" || name == "Block") {
                    // Block interval
                } else if (name == "Uninterrupt" || type == "Uninterrupt") {
                    MoveDef::UninterruptInterval ui;
                    // Uninterrupt uses End attribute only or Start/End
                    auto ustart = iv.attr("Start");
                    auto uend = iv.attr("End");
                    if (!uend.empty() && ustart.empty()) {
                        ui.start = 0;
                        ui.end = parse_float(uend);
                    } else {
                        ui.start = parse_float(ustart);
                        ui.end = parse_float(uend);
                    }
                    move.uninterrupt_intervals.push_back(ui);
                }
            }
        }

        // Parse direct children <Interval> (some moves have intervals directly)
        for (auto& iv : child.children) {
            if (iv.name == "Interval") {
                auto type = iv.attr("Type");
                auto name = iv.attr("Name");
                if (name == "Uninterrupt" || type == "Uninterrupt") {
                    MoveDef::UninterruptInterval ui;
                    ui.start = parse_float(iv.attr("Start"));
                    ui.end = parse_float(iv.attr("End"));
                    move.uninterrupt_intervals.push_back(ui);
                }
            }
        }

        moves_[move.name] = std::move(move);
    }

    return true;
}

bool MoveDatabase::load_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[moves] Cannot open: %s\n", path.c_str());
        return false;
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return load_from_xml(data);
}

const MoveDef* MoveDatabase::find(const std::string& name) const {
    auto it = moves_.find(name);
    return it != moves_.end() ? &it->second : nullptr;
}

const MoveDef* MoveDatabase::find_by_filename(const std::string& filename) const {
    for (auto& [n, m] : moves_) {
        if (m.filename == filename) return &m;
    }
    return nullptr;
}

std::vector<const MoveDef*> MoveDatabase::query(const MoveQuery& q) const {
    std::vector<const MoveDef*> results;
    for (auto& [n, m] : moves_) {
        // Match direction
        if (!q.direction.empty() && m.direction != q.direction) continue;
        // Match move type
        if (!q.move_type.empty() && m.move_type != q.move_type) continue;
        // Match key count
        if (q.key_count > 0 && m.key_count != q.key_count) continue;
        // Match current animation (for chain combos)
        if (!q.current_animation.empty() && !m.required_current_animation.empty() &&
            m.required_current_animation != q.current_animation) continue;
        // Match weapon
        if (!q.tactic_weapon.empty() && !m.lock_weapon.empty() &&
            m.lock_weapon != "Unarmed" && m.lock_weapon != q.tactic_weapon) continue;
        // In uninterrupt: only allow 3key combos
        if (q.in_uninterrupt && m.key_count < 3) continue;

        results.push_back(&m);
    }
    return results;
}

} // namespace resf2::fight
