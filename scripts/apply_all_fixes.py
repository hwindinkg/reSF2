#!/usr/bin/env python3
"""Apply all critical fixes to main.cpp in one pass."""
import re

with open('/home/z/my-project/work/reSF2/main.cpp', 'r') as f:
    code = f.read()

# === FIX 1: Replace AnimationData struct with correct .bin format ===
old_anim = re.search(r'// ---------- Animation system ----------.*?static uint32_t read_u32_le.*?\n}', code, re.DOTALL).group()

new_anim = '''// ---------- Animation system ----------
// .bin format (from Gymnast-Tool-Suite Blender plugin):
// u32 frame_count (LE)
// Per frame: 1 byte skip + u32 node_count (LE) + node_count * 3 floats (X, Y, -Z) LE
// Node order: ALL skeleton.xml nodes in XML order (54 Node + 1 COM + 12 MacroNode = 67)
// Positions are ABSOLUTE (world space). Local = abs - NPivot_world.
struct AnimationData {
    std::string name;
    int frame_count = 0;
    std::vector<std::vector<std::tuple<float,float,float>>> node_positions;

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        auto sz = (size_t)f.tellg();
        if (sz < 4) return false;
        f.seekg(0);
        std::vector<uint8_t> data(sz);
        f.read((char*)data.data(), sz);
        frame_count = read_u32_le(data.data(), 0);
        if (frame_count <= 0 || frame_count > 10000) return false;
        node_positions.resize(frame_count);
        size_t offset = 4;
        for (int fi = 0; fi < frame_count; ++fi) {
            if (offset + 5 > sz) break;
            uint32_t nc = read_u32_le(data.data(), offset + 1);
            offset += 5;
            auto& nodes = node_positions[fi];
            for (uint32_t i = 0; i < nc && offset + 12 <= sz; ++i) {
                float fx, fy, fneg_z;
                memcpy(&fx, &data[offset], 4);
                memcpy(&fy, &data[offset+4], 4);
                memcpy(&fneg_z, &data[offset+8], 4);
                nodes.push_back({fx, fy, -fneg_z});
                offset += 12;
            }
        }
        return !node_positions.empty();
    }

    bool get_node_pos(int fi, int idx, float& x, float& y, float& z) const {
        if (fi < 0 || fi >= (int)node_positions.size()) return false;
        const auto& nodes = node_positions[fi];
        if (idx < 0 || idx >= (int)nodes.size()) return false;
        auto [nx, ny, nz] = nodes[idx];
        x = nx; y = ny; z = nz;
        return true;
    }

    static uint32_t read_u32_le(const uint8_t* p, size_t off) {
        return (uint32_t)p[off] | ((uint32_t)p[off+1] << 8) |
               ((uint32_t)p[off+2] << 16) | ((uint32_t)p[off+3] << 24);
    }
};'''

code = code.replace(old_anim, new_anim)

# === FIX 2: Add MoveDef struct after AnimationData ===
code = code.replace(
    'struct BodyNode {',
    '''// ---------- Move definition (from moves.xml) ----------
struct MoveDef {
    std::string name, filename, template_name;
    int first_frame = 0, end_frame = 0, priority = 0;
    int attack_start = -1, attack_end = -1;
    std::vector<std::string> attack_edges;
    float damage = 0.0f;
    std::vector<std::string> key_types;
};

struct BodyNode {'''
)

# === FIX 3: Add ordered_node_names_ and moves_ members ===
code = code.replace(
    'std::unordered_map<std::string, SkelNode> skeleton_nodes_;\n    std::unordered_map<std::string, SkelEdge> skeleton_edges_;',
    'std::unordered_map<std::string, SkelNode> skeleton_nodes_;\n    std::unordered_map<std::string, SkelEdge> skeleton_edges_;\n    std::vector<std::string> ordered_node_names_;\n    std::unordered_map<std::string, MoveDef> moves_;\n    std::string current_move_;\n    bool bag_hit_ = false;\n    float prev_npivot_x_ = 0.0f;'
)

# === FIX 4: Add load_moves() call after load_animations() ===
code = code.replace(
    'load_animations();',
    'load_animations();\n        load_moves();'
)

# === FIX 5: Fix load_skeleton to parse MacroNodes and COM, build ordered_node_names_ ===
old_skel_end = 'std::printf("  Skeleton: %zu nodes\\n", skeleton_nodes_.size());'
new_skel_end = '''// Parse MacroNodes
        pos = 0;
        while ((pos = nodes_xml.find("Type=\\"MacroNode\\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
            }
            pos = end + 2;
        }
        // Parse CenterOfMass
        pos = 0;
        while ((pos = nodes_xml.find("Type=\\"CenterOfMass\\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
            }
            pos = end + 2;
        }
        // Build ordered_node_names_ (all nodes in XML order)
        ordered_node_names_.clear();
        pos = 0;
        while (true) {
            auto tag_start = nodes_xml.find('<', pos);
            if (tag_start == std::string::npos) break;
            auto tag_end = nodes_xml.find("/>", tag_start);
            if (tag_end == std::string::npos) break;
            auto tag = nodes_xml.substr(tag_start, tag_end - tag_start);
            if (tag.find("X=\\"") != std::string::npos && tag.find("Y=\\"") != std::string::npos) {
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    ordered_node_names_.push_back(tag.substr(1, sp - 1));
                }
            }
            pos = tag_end + 2;
        }
        std::printf("  Skeleton: %zu nodes, %zu ordered\\n", skeleton_nodes_.size(), ordered_node_names_.size());'''
code = code.replace(old_skel_end, new_skel_end)

# === FIX 6: Replace render_body_model capsule rendering with circle caps ===
old_render = '''        ren::Color4B cap_col{200, 200, 210, 255};
        for (auto& c : body_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            float r = (c.radius1 + c.radius2) * 0.5f * 0.9f;
            // Draw as world-space thick line (2 triangles forming a rectangle)
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float thickness = std::max(r, 1.0f);
            float ht = thickness;
            // 4 corners: (x1+px*ht, y1+py*ht), (x2+px*ht, y2+py*ht), (x2-px*ht, y2-py*ht), (x1-px*ht, y1-py*ht)
            // Draw as 2 triangles
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, cap_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, cap_col);
        }'''
new_render = '''        ren::Color4B silhouette_col{20, 20, 25, 255};
        for (auto& c : body_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                player_pos_x_, player_pos_y_, facing_right_, pivot_local_y);
            float r = (c.radius1 + c.radius2) * 0.5f * 0.9f;
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            renderer_->draw_filled_circle_world(x1, y1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(x2, y2, ht, silhouette_col);
        }'''
code = code.replace(old_render, new_render)

# === FIX 7: Replace render_character to remove skeleton lines ===
old_char = re.search(r'void render_character\(\) \{.*?\n    \}', code, re.DOTALL).group()
new_char = '''void render_character() {
        render_body_model();
    }'''
code = code.replace(old_char, new_char)

# === FIX 8: Replace update_animation with correct format ===
old_update = re.search(r'void update_animation\(uint32_t.*?\n    \}', code, re.DOTALL).group()
new_update = '''void update_animation(uint32_t dt_ms) {
        anim_node_pos_.clear();
        auto it = animations_.find(current_anim_);
        if (it == animations_.end()) { return; }
        auto& anim = it->second;
        if (anim.frame_count == 0 || ordered_node_names_.empty()) return;
        int npivot_idx = -1;
        for (int i = 0; i < (int)ordered_node_names_.size(); ++i)
            if (ordered_node_names_[i] == "NPivot") { npivot_idx = i; break; }
        if (npivot_idx < 0) return;
        float dt = dt_ms / 1000.0f;
        anim_time_ += dt * anim_speed_ / 30.0f;
        float frame_f = anim_time_ * 30.0f;
        int frame_idx = (int)frame_f;
        if (anim_loop_) { if (anim.frame_count > 0) frame_idx %= anim.frame_count; }
        else if (frame_idx >= anim.frame_count) frame_idx = anim.frame_count - 1;
        if (frame_idx < 0) frame_idx = 0;
        int next_idx = anim.frame_count > 0 ? ((frame_idx + 1) % anim.frame_count) : 0;
        float alpha = frame_f - (int)frame_f;
        if (alpha < 0) alpha = 0; if (alpha > 1) alpha = 1;
        float npx0, npy0, npz0, npx1, npy1, npz1;
        if (!anim.get_node_pos(frame_idx, npivot_idx, npx0, npy0, npz0)) return;
        if (!anim.get_node_pos(next_idx, npivot_idx, npx1, npy1, npz1)) { npx1 = npx0; npy1 = npy0; }
        float npivot_x = npx0 + (npx1 - npx0) * alpha;
        float npivot_y = npy0 + (npy1 - npy0) * alpha;
        // Root motion for step animations
        if (current_anim_ == "step_forward" || current_anim_ == "step_back") {
            float delta = npivot_x - prev_npivot_x_;
            if (std::abs(delta) < 50.0f) player_pos_x_ += delta * 0.9f;
            prev_npivot_x_ = npivot_x;
        } else { prev_npivot_x_ = npivot_x; }
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float npivot_rest_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 169.48f;
        for (int i = 0; i < (int)ordered_node_names_.size() && i < 67; ++i) {
            const std::string& name = ordered_node_names_[i];
            float x0, y0, z0, x1, y1, z1;
            if (!anim.get_node_pos(frame_idx, i, x0, y0, z0)) continue;
            if (!anim.get_node_pos(next_idx, i, x1, y1, z1)) { x1 = x0; y1 = y0; }
            float abs_x = x0 + (x1 - x0) * alpha;
            float abs_y = y0 + (y1 - y0) * alpha;
            float local_x = abs_x - npivot_x;
            float local_y = abs_y - npivot_y;
            anim_node_pos_[name] = {local_x, local_y + npivot_rest_y};
        }
    }'''
code = code.replace(old_update, new_update)

# === FIX 9: Fix play_animation to reset prev_npivot_x_ ===
old_play = re.search(r'void play_animation\(const std::string& name, bool loop = true\) \{.*?\n    \}', code, re.DOTALL).group()
new_play = '''void play_animation(const std::string& name, bool loop = true) {
        if (animations_.count(name)) {
            current_anim_ = name; anim_time_ = 0.0f; anim_loop_ = loop;
        }
    }'''
code = code.replace(old_play, new_play)

# === FIX 10: Replace load_animations with correct file list ===
old_load = re.search(r'void load_animations\(\) \{.*?std::printf\("  Animations loaded: %zu\\n", animations_\.size\(\)\);\n    \}', code, re.DOTALL).group()
new_load = '''void load_animations() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> search_dirs = {
            root/"assets"/"animations"/"binary", root/"animations"/"binary",
            root/"assets"/"animations", root/"animations",
        };
        const char* anim_names[] = {
            "fists1_stance_idle", "fists2_stance_idle",
            "high_punch", "heavy_punch", "low_punch",
            "double_punch", "spinning_punch", "upper_cut",
            "high_kick", "front_kick", "back_kick",
            "sweep", "low_kick",
            "step_forward", "step_back",
            "stance_1", "stance_2",
        };
        for (auto& name : anim_names) {
            for (auto& dir : search_dirs) {
                auto path = dir / (std::string(name) + ".bin");
                if (std::filesystem::exists(path)) {
                    AnimationData anim; anim.name = name;
                    if (anim.load(path.string())) { animations_[name] = std::move(anim); break; }
                }
            }
        }
        if (animations_.count("fists1_stance_idle")) animations_["fists_idle"] = animations_["fists1_stance_idle"];
        std::printf("  Animations loaded: %zu\\n", animations_.size());
    }

    void load_moves() {
        auto root = std::filesystem::path(asset_root_);
        std::string moves_path;
        for (auto& dir : {root/"assets"/"animations", root/"animations"}) {
            auto path = dir / "moves.xml";
            if (std::filesystem::exists(path)) { moves_path = path.string(); break; }
        }
        if (moves_path.empty()) return;
        auto xml = read_text(moves_path);
        size_t pos = 0;
        while ((pos = xml.find("<Move ", pos)) != std::string::npos) {
            if (pos > 4 && xml.substr(pos - 4, 4) == "<!--") { pos += 6; continue; }
            auto end_tag = xml.find(">", pos);
            if (end_tag == std::string::npos) break;
            auto tag = xml.substr(pos, end_tag - pos);
            MoveDef move;
            move.name = xml_attr(tag, "Name");
            move.filename = xml_attr(tag, "FileName");
            move.template_name = xml_attr(tag, "Template");
            move.priority = (int)tof(xml_attr(tag, "Priority"));
            auto move_end = xml.find("</Move>", pos);
            if (move_end == std::string::npos) { pos = end_tag; continue; }
            auto inner = xml.substr(end_tag + 1, move_end - end_tag - 1);
            size_t ip = inner.find("Type=\\"Attack\\"");
            if (ip != std::string::npos) {
                auto ts = inner.rfind('<', ip); auto te = inner.find("/>", ip);
                if (ts != std::string::npos && te != std::string::npos) {
                    auto iv_tag = inner.substr(ts, te - ts);
                    move.attack_start = (int)tof(xml_attr(iv_tag, "Start"));
                    move.attack_end = (int)tof(xml_attr(iv_tag, "End"));
                }
            }
            ip = 0;
            while ((ip = inner.find("<Edge ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto ename = xml_attr(inner.substr(ip, te - ip), "Name");
                if (!ename.empty()) move.attack_edges.push_back(ename);
                ip = te + 2;
            }
            ip = inner.find("<Damage ");
            if (ip != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te != std::string::npos) move.damage = tof(xml_attr(inner.substr(ip, te - ip), "Value"));
            }
            if (!move.filename.empty()) moves_[move.name] = std::move(move);
            pos = move_end + 7;
        }
        std::printf("  Moves loaded: %zu\\n", moves_.size());
    }'''
code = code.replace(old_load, new_load)

# === FIX 11: Replace the movement+combat section in on_update ===
old_move = re.search(r'// Player movement\n            float speed = 0\.4f.*?// Camera debug move.*?cam_y_ -= cam_speed;\n        \}\n    \}', code, re.DOTALL).group()
new_move = '''// === MOVEMENT (from moves.xml: step_forward/step_back = Type="MOVE") ===
            bool want_left = input.keys_down[(size_t)plat::Key::A] || input.keys_down[(size_t)plat::Key::ArrowLeft];
            bool want_right = input.keys_down[(size_t)plat::Key::D] || input.keys_down[(size_t)plat::Key::ArrowRight];
            if (hit_anim_ == 0) {
                if (want_left && !want_right) {
                    facing_right_ = false;
                    if (current_anim_ != "step_back" && animations_.count("step_back"))
                        play_animation("step_back", true);
                } else if (want_right && !want_left) {
                    facing_right_ = true;
                    if (current_anim_ != "step_forward" && animations_.count("step_forward"))
                        play_animation("step_forward", true);
                } else {
                    if (current_anim_ != "fists_idle" && current_anim_.find("punch") == std::string::npos &&
                        current_anim_.find("kick") == std::string::npos && current_anim_.find("cut") == std::string::npos)
                        play_animation("fists_idle", true);
                }
            }
            cam_x_ = player_pos_x_ + 200.0f;
            renderer_->camera().set_target(cam_x_, cam_y_);
            renderer_->camera().set_zoom(zoom_);

            // === COMBAT (from moves.xml) ===
            if (input.keys_just_pressed[(size_t)plat::Key::Space] && hit_anim_ == 0) {
                std::string mn, an;
                if (want_right) { mn = "DoublePunch"; an = "double_punch"; }
                else if (want_left) { mn = "SpinningPunch"; an = "spinning_punch"; }
                else if (input.keys_down[(size_t)plat::Key::W] || input.keys_down[(size_t)plat::Key::ArrowUp]) { mn = "UpperCut"; an = "upper_cut"; }
                else if (input.keys_down[(size_t)plat::Key::S] || input.keys_down[(size_t)plat::Key::ArrowDown]) { mn = "LowPunch"; an = "low_punch"; }
                else { mn = "HighPunch"; an = "high_punch"; }
                if (animations_.count(an)) {
                    play_animation(an, false); current_move_ = mn;
                    hit_anim_ = (uint32_t)(animations_[an].frame_count * 1000.0f / 30.0f);
                }
            }
            if (input.keys_just_pressed[(size_t)plat::Key::K] && hit_anim_ == 0) {
                std::string mn, an;
                if (input.keys_down[(size_t)plat::Key::S] || input.keys_down[(size_t)plat::Key::ArrowDown]) { mn = "Sweep"; an = "sweep"; }
                else if (want_left) { mn = "BackKick"; an = "back_kick"; }
                else if (want_right) { mn = "FrontKick"; an = "front_kick"; }
                else { mn = "HighKick"; an = "high_kick"; }
                if (animations_.count(an)) {
                    play_animation(an, false); current_move_ = mn;
                    hit_anim_ = (uint32_t)(animations_[an].frame_count * 1000.0f / 30.0f);
                }
            }
            if (hit_anim_ > 0) {
                hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
                if (!bag_hit_ && bag_model_ && location_) {
                    auto ai = animations_.find(current_anim_);
                    if (ai != animations_.end()) {
                        int cf = (int)(anim_time_ * 30.0f);
                        int fc = ai->second.frame_count;
                        if (cf >= fc/4 && cf <= fc*3/4) {
                            float bx = location_->enemy_x - 857.0f;
                            if (std::abs(player_pos_x_ - bx) < 400.0f) { bag_swing_ = 800; bag_hit_ = true; }
                        }
                    }
                }
                if (hit_anim_ == 0) { play_animation("fists_idle", true); current_move_.clear(); bag_hit_ = false; }
            }
            if (bag_swing_ > 0) bag_swing_ -= std::min<uint32_t>(bag_swing_, dt);
            update_animation(dt);
            if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
            if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;
        }
    }'''
code = code.replace(old_move, new_move)

# === FIX 12: Add #include <cstdint> if missing ===
if '#include <cstring>' not in code:
    code = code.replace('#include <cstdio>', '#include <cstdio>\n#include <cstring>')

# === FIX 13: Enable anim_node_pos_ override in resolve_body_node ===
old_resolve = '''        // NOTE: anim_node_pos_ override is intentionally DISABLED.
        // The .bin animation node ordering does not match skeleton.xml ordering,
        // and skeleton_nodes_ is an unordered_map (random iteration order), so
        // applying the per-node animation floats directly assigns garbage
        // coordinates to random body parts. This caused the character to
        // "tear apart into polygons" (limbs jumping to wildly wrong locations).
        // Re-enable only after the original engine's node-mapping table is
        // recovered from the .s3e binary and a deterministic mapping is built.
        // auto ait = anim_node_pos_.find(name);
        // if (ait != anim_node_pos_.end()) {
        //     float lx = ait->second.first, ly = ait->second.second;
        //     float sx = (face_right ? lx : -lx) * 0.9f;
        //     float sy = world_cy + (ly - pivot_local_y) * 0.9f;
        //     return {world_cx + sx, sy};
        // }'''
new_resolve = '''        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            float lx = ait->second.first, ly = ait->second.second;
            float sx = (face_right ? lx : -lx) * 0.9f;
            float sy = world_cy + (ly - pivot_local_y) * 0.9f;
            return {world_cx + sx, sy};
        }'''
if old_resolve in code:
    code = code.replace(old_resolve, new_resolve)

with open('/home/z/my-project/work/reSF2/main.cpp', 'w') as f:
    f.write(code)

print("All fixes applied!")
print(f"File size: {len(code)} bytes")
