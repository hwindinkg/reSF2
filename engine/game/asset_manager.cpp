// engine/game/asset_manager.cpp
//
// AssetManager implementation — asset loading: textures, atlases,
// skeletons, body models, moves, fonts, sounds.

#include "asset_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "engine/renderer/stb_image.h"
#include "engine/format/stage_parser.hpp"
#include "game.hpp"  // for read_text, read_file, tof, toi, model_paths

namespace resf2::game {

namespace plist = resf2::reverse::plist;
namespace fmt = resf2::format;

// [ORIGINAL] Gather <Key Type="." PressType="."/> bindings from a condition
// subtree. <Keys> can sit inside nested <Operator> elements for composite
// conditions (22 of the 325 blocks do), so this recurses instead of looking a
// fixed depth down.
static void collect_move_keys(const fmt::XmlNode& node, MoveDef& move) {
    if (node.name == "Key") {
        move.key_types.push_back(node.attr("Type"));
        // Absent PressType means Tap. Two moves can share a key and differ only
        // here, so dropping it makes every hold resolve to its tap variant.
        auto press = node.attr("PressType");
        if (press.empty()) press = "Tap";
        if (press == "Hold") move.needs_hold = true;
        move.key_press_types.push_back(std::move(press));
        return;
    }
    for (const auto& child : node.children) collect_move_keys(child, move);
}

// ---------- load_atlas ----------

void AssetManager::load_atlas(const std::string& name, const std::string& loc,
                              const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    for (const auto& dir : {root/"assets"/"1536"/"locations"/loc,
                             root/"assets"/"1536"/"textures",
                             root/"assets"/"1536",
                             root/"1536"/"locations"/loc,
                             root/"1536"/"textures",
                             root/"1536",
                             root/"assets",
                             root}) {
        auto pp = dir/(name+".plist"), pn = dir/(name+".png");
        if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
            auto result = plist::parse(read_text(pp.string()));
            if (!result) {
                std::fprintf(stderr, "[atlas] '%s': %s did not parse\n",
                             name.c_str(), pp.string().c_str());
                continue;
            }
            auto png_data = read_file(pn.string());
            int aw, ah, ach;
            auto* atlas_px = stbi_load_from_memory(
                (const stbi_uc*)png_data.data(), (int)png_data.size(),
                &aw, &ah, &ach, 4);
            auto tex = std::make_unique<ren::Texture2D>();
            if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                     png_data.size())) {
                if (atlas_px) stbi_image_free(atlas_px);
                continue;
            }
            AtlasRef a;
            a.texture = std::move(tex);
            a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
            if (atlas_px) {
                for (auto& [fname, idx] : a.atlas->name_index) {
                    auto& frame = a.atlas->frames[idx];
                    if (!frame.rotated) continue;
                    // [ORIGINAL] For a rotated frame the plist's `frame` rect
                    // carries the sprite's UNROTATED size, while the region
                    // stored in the atlas is that rect transposed — cocos2d-x
                    // passes both to CCSpriteFrame::initWithTexture(rect,
                    // rotated, ...) and swaps when it builds the texture
                    // coordinates. TexturePacker rotates 90 degrees clockwise
                    // going in, so coming out:
                    //     source(x, y) = atlas(ax + (fh - 1 - y), ay + x)
                    // with fw x fh the UNROTATED size.
                    //
                    // This used to build the output as atlas_h x atlas_w and
                    // walk `fh` across the atlas' X — so it read the wrong
                    // region AND produced a transposed texture. dojo's
                    // layer_3_2 (a 256x60 floor plank) came out 60x256 and was
                    // then squeezed into a 256x60 quad: three of the six floor
                    // planks rendered as garbage slivers, which is exactly the
                    // "floor is three segments with holes" on screen.
                    int fw = frame.atlas_w;
                    int fh = frame.atlas_h;
                    auto ctex = std::make_unique<ren::Texture2D>();
                    std::vector<std::uint8_t> px((size_t)fw * fh * 4);
                    for (int y = 0; y < fh; ++y) {
                        for (int x = 0; x < fw; ++x) {
                            int sx = frame.atlas_x + (fh - 1 - y);
                            int sy = frame.atlas_y + x;
                            if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                            int src_idx = (sy * aw + sx) * 4;
                            int dst_idx = (y * fw + x) * 4;
                            px[dst_idx+0] = atlas_px[src_idx+0];
                            px[dst_idx+1] = atlas_px[src_idx+1];
                            px[dst_idx+2] = atlas_px[src_idx+2];
                            px[dst_idx+3] = atlas_px[src_idx+3];
                        }
                    }
                    ctex->init_rgba(fw, fh, px.data());
                    std::string n = fname;
                    if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                    a.cropped[n] = std::move(ctex);
                }
                stbi_image_free(atlas_px);
            }
            std::printf("  Atlas '%s': %zu frames, %zu pre-cropped\n",
                        name.c_str(), a.atlas->frames.size(), a.cropped.size());
            atlases_[name] = std::move(a);
            return;
        }
    }
    std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
}

// ---------- load_skeleton ----------

void AssetManager::load_skeleton(const std::string& asset_root, const std::string& /*location*/) {
    auto candidates = model_paths(asset_root, "skeleton.xml");
    std::string path;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) { std::printf("  skeleton.xml NOT FOUND!\n"); return; }
    auto xml = read_text(path);

    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[skeleton] xml_doc parse error: %s\n", doc.error().c_str());
        return;
    }

    auto* scene = doc.root()->first_child("Scene");
    if (!scene) { std::printf("  skeleton.xml: no <Scene>\n"); return; }

    // Parse <Nodes> section
    auto* nodes_section = scene->first_child("Nodes");
    if (!nodes_section) { std::printf("  skeleton.xml: no <Nodes>\n"); return; }

    skeleton_nodes_.clear();
    ordered_node_names_.clear();
    int macro_count = 0;

    for (const auto& child : nodes_section->children) {
        auto x = child.attr("X");
        if (x.empty()) continue;

        SkelNode n;
        n.name = child.name;
        n.x = tof(x);
        n.y = tof(child.attr("Y"));
        n.z = tof(child.attr("Z"));
        skeleton_nodes_[n.name] = n;
        ordered_node_names_.push_back(n.name);

        std::string type = child.attr("Type");
        if (type == "MacroNode") ++macro_count;
    }
    // Store NPivot Y baseline from skeleton rest-pose data
    {
        auto np_it = skeleton_nodes_.find("NPivot");
        if (np_it != skeleton_nodes_.end()) {
            stance_npivot_y_ = np_it->second.y;
        }
    }
    std::printf("  Skeleton: %zu nodes (%d MacroNodes, ordered: %zu)\n",
                skeleton_nodes_.size(), macro_count, ordered_node_names_.size());

    // Parse <Edges> section for Edge and Muscle types
    skeleton_edges_.clear();
    auto* edges_section = scene->first_child("Edges");
    if (edges_section) {
        for (const auto& child : edges_section->children) {
            std::string type = child.attr("Type");
            if (type == "Edge" || type == "Muscle") {
                SkelEdge e;
                e.name = child.name;
                e.end1 = child.attr("End1");
                e.end2 = child.attr("End2");
                e.radius = tof(child.attr("Radius"));
                e.margin1 = tof(child.attr("Margin1"));
                e.margin2 = tof(child.attr("Margin2"));
                skeleton_edges_[e.name] = e;
            }
        }
    }
    std::printf("  Skeleton: %zu edges\n", skeleton_edges_.size());
}

// ---------- load_body_model ----------

void AssetManager::load_body_model(const std::string& asset_root, const std::string& /*location*/, bool is_bag) {
    auto candidates = model_paths(asset_root, "body.xml");
    std::string path;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) { std::printf("  body.xml NOT FOUND!\n"); return; }
    auto xml = read_text(path);
    auto model = std::make_unique<BodyModel>();
    parse_body_model_xml(xml, model.get(), "BODY-");
    // [ORIGINAL] Also load head.xml as part of the body model.
    auto head_candidates = model_paths(asset_root, "head.xml");
    std::string head_path;
    for (const auto& p : head_candidates) {
        if (std::filesystem::exists(p)) { head_path = p.string(); break; }
    }
    if (!head_path.empty()) {
        auto head_xml = read_text(head_path);
        parse_body_model_xml(head_xml, model.get(), "HEAD-");
        std::printf("  head.xml loaded (merged into body model)\n");
    }
    // Assign to the correct model pointer
    if (!is_bag) {
        body_model_ = std::move(model);
    } else {
        bag_model_ = std::move(model);
    }
}

// ---------- parse_body_model_xml ----------

void AssetManager::parse_body_model_xml(const std::string& xml, BodyModel* model, const std::string& tag_prefix) {
    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[body] xml_doc parse error: %s\n", doc.error().c_str());
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    if (!scene) { std::printf("  body/head.xml: no <Scene>\n"); return; }

    if (auto* ns = scene->first_child("Nodes")) {
        for (const auto& child : ns->children) {
            std::string type = child.attr("Type");
            if (type == "Node") {
                BodyNode n;
                n.name = child.name;
                n.x = tof(child.attr("X"));
                n.y = tof(child.attr("Y"));
                n.z = tof(child.attr("Z"));
                model->nodes[n.name] = n;
            } else if (type == "MacroNode") {
                BodyMacroNode mn;
                mn.name = child.name;
                mn.children[0] = child.attr("ChildNode1");
                mn.children[1] = child.attr("ChildNode2");
                mn.children[2] = child.attr("ChildNode3");
                mn.children[3] = child.attr("ChildNode4");
                mn.lcc[0] = tof(child.attr("LCC1"));
                mn.lcc[1] = tof(child.attr("LCC2"));
                mn.lcc[2] = tof(child.attr("LCC3"));
                mn.lcc[3] = tof(child.attr("LCC4"));
                model->macro_nodes[mn.name] = mn;
            }
        }
    }

    if (auto* es = scene->first_child("Edges")) {
        for (const auto& child : es->children) {
            if (child.attr("Type") != "Edge") continue;
            BodyEdge e;
            e.name = child.name;
            e.end1 = child.attr("End1");
            e.end2 = child.attr("End2");
            e.radius = tof(child.attr("Radius"));
            e.collisible = (child.attr("Collisible") == "1");
            model->edges.push_back(e);
        }
    }

    if (auto* fs = scene->first_child("Figures")) {
        for (const auto& child : fs->children) {
            std::string type = child.attr("Type");
            if (type == "Capsule") {
                BodyCapsule c;
                c.edge_name = child.attr("Edge");
                c.radius1 = tof(child.attr("Radius1"));
                c.radius2 = tof(child.attr("Radius2"));
                c.margin1 = tof(child.attr("Margin1"));
                c.margin2 = tof(child.attr("Margin2"));
                model->capsules.push_back(c);
            } else if (type == "Triangle") {
                BodyTriangle t;
                t.n1 = child.attr("Node1");
                t.n2 = child.attr("Node2");
                t.n3 = child.attr("Node3");
                model->triangles.push_back(t);
            }
        }
    }

    std::printf("  [%s] model: %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                tag_prefix.c_str(), model->nodes.size(), model->edges.size(),
                model->capsules.size(), model->triangles.size());
}

// ---------- load_punching_bag_model ----------

void AssetManager::load_punching_bag_model(const std::string& asset_root) {
    auto skel_candidates = model_paths(asset_root, "skeleton_punching_bag.xml");
    auto fig_candidates = model_paths(asset_root, "punching_bag.xml");
    std::string skel_path, fig_path;
    for (const auto& p : skel_candidates)
        if (std::filesystem::exists(p)) { skel_path = p.string(); break; }
    for (const auto& p : fig_candidates)
        if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
    if (skel_path.empty()) { std::printf("  skeleton_punching_bag.xml NOT FOUND!\n"); return; }

    bag_model_ = std::make_unique<BodyModel>();

    fmt::XmlDocument skel_doc;
    if (!skel_doc.parse(read_text(skel_path))) {
        std::fprintf(stderr, "[punching_bag] skel parse error: %s\n", skel_doc.error().c_str());
        return;
    }
    auto* scene = skel_doc.root()->first_child("Scene");
    if (!scene) { std::printf("  skeleton_punching_bag.xml: no <Scene>\n"); return; }

    if (auto* ns = scene->first_child("Nodes")) {
        for (const auto& child : ns->children) {
            std::string type = child.attr("Type");
            if (type != "Node" && type != "CenterOfMass") continue;
            BodyNode n;
            n.name = child.name;
            n.x = tof(child.attr("X"));
            n.y = tof(child.attr("Y"));
            n.mass = tof(child.attr("Mass"), 1.0f);
            n.fixed = (toi(child.attr("Fixed")) != 0);
            n.attenuation = tof(child.attr("Attenuation"), 0.02f);
            n.cloth = (toi(child.attr("Cloth")) != 0);
            bag_model_->nodes[n.name] = n;
        }
    }

    if (auto* es = scene->first_child("Edges")) {
        for (const auto& child : es->children) {
            if (child.attr("Type") != "Edge") continue;
            BodyEdge e;
            e.name = child.name;
            e.end1 = child.attr("End1");
            e.end2 = child.attr("End2");
            e.radius = tof(child.attr("Radius"));
            e.collisible = (child.attr("Collisible") == "1");
            bag_model_->edges.push_back(e);
        }
    }

    if (!fig_path.empty()) {
        fmt::XmlDocument fig_doc;
        if (fig_doc.parse(read_text(fig_path))) {
            if (auto* fs = fig_doc.root()->first_child("Scene"); fs && (fs = fs->first_child("Figures"))) {
                for (const auto& child : fs->children) {
                    if (child.attr("Type") != "Capsule") continue;
                    BodyCapsule c;
                    c.edge_name = child.attr("Edge");
                    c.radius1 = tof(child.attr("Radius1"));
                    c.radius2 = tof(child.attr("Radius2"));
                    c.margin1 = tof(child.attr("Margin1"));
                    c.margin2 = tof(child.attr("Margin2"));
                    bag_model_->capsules.push_back(c);
                }
            }
        }
    }
    std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                bag_model_->nodes.size(), bag_model_->edges.size(),
                bag_model_->capsules.size());
}

// ---------- load_enemy_weapon ----------

void AssetManager::load_enemy_weapon(const std::string& weapon_name, const std::string& asset_root) {
    auto candidates = model_paths(asset_root, weapon_name.c_str());
    std::string fig_path;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
    }
    if (fig_path.empty()) { std::printf("  Enemy weapon '%s' NOT FOUND!\n", weapon_name.c_str()); return; }
    enemy_weapon_model_ = std::make_unique<BodyModel>();
    fmt::XmlDocument doc;
    if (!doc.parse(read_text(fig_path))) {
        std::fprintf(stderr, "[weapon] xml parse error: %s\n", doc.error().c_str());
        enemy_weapon_model_.reset(); return;
    }
    auto* scene = doc.root()->first_child("Scene");
    if (!scene) { enemy_weapon_model_.reset(); return; }
    if (auto* ns = scene->first_child("Nodes")) {
        for (const auto& child : ns->children) {
            std::string type = child.attr("Type");
            // [U1] Weapon models ship ONLY MacroNodes; the old
            // Type="Node"/"CenterOfMass" filter parsed ZERO nodes. Parse
            // them like load_player_weapon does.
            if (type == "MacroNode") {
                BodyMacroNode mn;
                mn.name = child.name;
                mn.children[0] = child.attr("ChildNode1");
                mn.children[1] = child.attr("ChildNode2");
                mn.children[2] = child.attr("ChildNode3");
                mn.children[3] = child.attr("ChildNode4");
                // [R1] LCC weights turn the Weapon-Node* children into the
                // rendered mesh position (weapon figures reference the
                // MacroNodes; zeroed LCC made them all resolve to the
                // origin - the invisible weapon).
                mn.lcc[0] = tof(child.attr("LCC1"));
                mn.lcc[1] = tof(child.attr("LCC2"));
                mn.lcc[2] = tof(child.attr("LCC3"));
                mn.lcc[3] = tof(child.attr("LCC4"));
                enemy_weapon_model_->macro_nodes[mn.name] = mn;
            }
            BodyNode n;
            n.name = child.name;
            n.x = tof(child.attr("X"));
            n.y = tof(child.attr("Y"));
            n.mass = tof(child.attr("Mass"), 1.0f);
            n.fixed = (toi(child.attr("Fixed")) != 0);
            n.attenuation = tof(child.attr("Attenuation"), 0.02f);
            enemy_weapon_model_->nodes[n.name] = n;
        }
    }
    if (auto* es = scene->first_child("Edges")) {
        for (const auto& child : es->children) {
            if (child.attr("Type") != "Edge") continue;
            BodyEdge e;
            e.name = child.name;
            e.end1 = child.attr("End1");
            e.end2 = child.attr("End2");
            e.radius = tof(child.attr("Radius"));
            enemy_weapon_model_->edges.push_back(e);
        }
    }
    if (auto* fs = scene->first_child("Figures")) {
        for (const auto& child : fs->children) {
            std::string type = child.attr("Type");
            if (type == "Capsule") {
                BodyCapsule c;
                c.edge_name = child.attr("Edge");
                c.radius1 = tof(child.attr("Radius1"));
                c.radius2 = tof(child.attr("Radius2"));
                c.margin1 = tof(child.attr("Margin1"));
                c.margin2 = tof(child.attr("Margin2"));
                enemy_weapon_model_->capsules.push_back(c);
            } else if (type == "Triangle") {
                // [U1] Weapon figures are Triangles; the Capsule-only filter
                // parsed none.
                BodyTriangle t;
                t.n1 = child.attr("Node1");
                t.n2 = child.attr("Node2");
                t.n3 = child.attr("Node3");
                enemy_weapon_model_->triangles.push_back(t);
            }
        }
    }
    std::printf("  Enemy weapon '%s': %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                weapon_name.c_str(), enemy_weapon_model_->nodes.size(),
                enemy_weapon_model_->edges.size(),
                enemy_weapon_model_->capsules.size(),
                enemy_weapon_model_->triangles.size());
}

// ---------- Equipment models (armor / helm) ----------
//
// [P3] Equipped armor/helm model files come from the list.xml Model
// attribute (users.xml Armor="ARMOR_ROBE" -> armor_robe.xml, Helm="Head" ->
// head.xml — LIVE_GAME_EVIDENCE Q4). The files ship the same MacroNode /
// Edge / Capsule structure as weapons; armor capsules reference the
// fighter's E* skeleton edges (EArm_1, EChest...), so the render pass draws
// them over the animated body.

static void parse_macro_model_xml(const std::string& xml, BodyModel* model) {
    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[equipment] xml parse error: %s\n", doc.error().c_str());
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    if (!scene) return;
    if (auto* ns = scene->first_child("Nodes")) {
        for (const auto& child : ns->children) {
            std::string type = child.attr("Type");
            if (type == "MacroNode") {
                BodyMacroNode mn;
                mn.name = child.name;
                mn.children[0] = child.attr("ChildNode1");
                mn.children[1] = child.attr("ChildNode2");
                mn.children[2] = child.attr("ChildNode3");
                mn.children[3] = child.attr("ChildNode4");
                model->macro_nodes[mn.name] = mn;
            }
            BodyNode n;
            n.name = child.name;
            n.x = tof(child.attr("X"));
            n.y = tof(child.attr("Y"));
            n.mass = tof(child.attr("Mass"), 1.0f);
            n.fixed = (toi(child.attr("Fixed")) != 0);
            n.attenuation = tof(child.attr("Attenuation"), 0.02f);
            model->nodes[n.name] = n;
        }
    }
    if (auto* es = scene->first_child("Edges")) {
        for (const auto& child : es->children) {
            if (child.attr("Type") != "Edge") continue;
            BodyEdge e;
            e.name = child.name;
            e.end1 = child.attr("End1");
            e.end2 = child.attr("End2");
            e.radius = tof(child.attr("Radius"));
            model->edges.push_back(e);
        }
    }
    if (auto* fs = scene->first_child("Figures")) {
        for (const auto& child : fs->children) {
            std::string type = child.attr("Type");
            if (type == "Capsule") {
                BodyCapsule c;
                c.edge_name = child.attr("Edge");
                c.radius1 = tof(child.attr("Radius1"));
                c.radius2 = tof(child.attr("Radius2"));
                c.margin1 = tof(child.attr("Margin1"));
                c.margin2 = tof(child.attr("Margin2"));
                model->capsules.push_back(c);
            } else if (type == "Triangle") {
                BodyTriangle t;
                t.n1 = child.attr("Node1");
                t.n2 = child.attr("Node2");
                t.n3 = child.attr("Node3");
                model->triangles.push_back(t);
            }
        }
    }
}

void AssetManager::load_armor_model(const std::string& model_file, const std::string& asset_root) {
    armor_model_.reset();
    auto candidates = model_paths(asset_root, model_file.c_str());
    std::string fig_path;
    for (const auto& p : candidates)
        if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
    if (fig_path.empty()) {
        std::printf("  Armor model '%s' NOT FOUND!\n", model_file.c_str());
        return;
    }
    armor_model_ = std::make_unique<BodyModel>();
    parse_macro_model_xml(read_text(fig_path), armor_model_.get());
    std::printf("  Armor model '%s': %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                model_file.c_str(), armor_model_->nodes.size(), armor_model_->edges.size(),
                armor_model_->capsules.size(), armor_model_->triangles.size());
}

void AssetManager::load_helm_model(const std::string& model_file, const std::string& asset_root) {
    helm_model_.reset();
    auto candidates = model_paths(asset_root, model_file.c_str());
    std::string fig_path;
    for (const auto& p : candidates)
        if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
    if (fig_path.empty()) {
        std::printf("  Helm model '%s' NOT FOUND!\n", model_file.c_str());
        return;
    }
    helm_model_ = std::make_unique<BodyModel>();
    parse_macro_model_xml(read_text(fig_path), helm_model_.get());
    std::printf("  Helm model '%s': %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                model_file.c_str(), helm_model_->nodes.size(), helm_model_->edges.size(),
                helm_model_->capsules.size(), helm_model_->triangles.size());
}

// ---------- weapon_tactic_to_model_file ----------

std::string AssetManager::weapon_tactic_to_model_file(const std::string& tactic) const {
    std::string lower;
    for (char c : tactic) lower += (char)std::tolower((unsigned char)c);
    static const std::unordered_map<std::string, std::string> special = {
        {"Fists", ""},
        {"TwoHanded", "weapon_composite_sword.xml"},
        {"BigSwords", "weapon_big_swords.xml"},
        {"CompositeSword", "weapon_composite_sword.xml"},
        {"CompositeSpear", "weapon_composite_spear.xml"},
        {"CompositeStaff", "weapon_composite_staff.xml"},
        {"CompositeScythe", "weapon_composite_scythe.xml"},
        {"GiantSword", "weapon_giant_sword.xml"},
        {"PowerFists", "weapon_power_fists.xml"},
        {"Glaivebow", "weapon_glaivebow.xml"},
        {"SilverGlaive", "weapon_silver_glaive.xml"},
        {"OneHandedSword", "weapon_one_handed_sword.xml"},
        {"NinjaSword", "weapon_ninja_sword.xml"},
        {"ShogunKatana", "weapon_katana.xml"},
        {"WandererStaff", "weapon_staff.xml"},
        {"TonfaGuns", "weapon_tonfa_guns.xml"},
        {"SharpTonfa", "weapon_sharp_tonfa.xml"},
        {"SteelClaws", "weapon_steel_claws.xml"},
        {"ShockerClaws", "weapon_shocker_claws.xml"},
        {"ButcherKnives", "weapon_butcher_knives.xml"},
        {"CrescentKnives", "weapon_crescent_knives.xml"},
        {"ElectroHammers", "weapon_electro_hammers.xml"},
        {"FireBatons", "weapon_fire_batons.xml"},
        {"BattleHammers", "weapon_battle_hammers.xml"},
        {"TwoHandedBlunt", "weapon_two_handed_cudgel.xml"},
        {"HermitSwords", "weapon_hermit_swords.xml"},
        {"Knobsticks", "weapon_knobsticks.xml"},
        {"MagariYari", "weapon_magari_yari.xml"},
        {"ShuangGou", "weapon_shuang_gou.xml"},
        {"ChineseSabers", "weapon_chinese_sabers.xml"},
        {"IndianKatar", "weapon_indian_katar.xml"},
        {"MonkKatars", "weapon_indian_katar.xml"},
        {"Shuriken", "weapon_knives.xml"},
        {"Kunai", "weapon_kunai.xml"},
        {"FireBall", "magic_fireball.xml"},
        {"Energyball", "magic_energy_ball.xml"},
        {"LightningArrow", "magic_lightning.xml"},
        {"MagicDeathRay", "magic_death_ray.xml"},
        {"MagicAsteroid", "magic_asteroid.xml"},
        {"MassBomb", "magic_mass_bomb.xml"},
        {"MagicBomb", "magic_mass_bomb.xml"},
        {"MagicFireAura", "magic_fire_aura.xml"},
        {"MagicAcidCloud", "magic_fire_aura.xml"},
        {"RootStun", "magic_root_stun.xml"},
        {"FirePillar", "magic_fire_pillar.xml"},
        {"Sawblade", "weapon_sawblade.xml"},
        {"DoubleScythe", "weapon_sectional_scythe.xml"},
    };
    auto it = special.find(tactic);
    if (it != special.end()) return it->second;
    // Generic: "weapon_<lowercased>.xml" — try common patterns.
    // [P1] Note: this copy is legacy/dead (the Game's own inline loader in
    // game_clean.hpp is what runs); its item->Model resolution lives there,
    // where the list data is available. No filesystem probe here — there is
    // no asset_root in scope.
    std::string try_name = "weapon_" + lower + ".xml";
    return try_name;
}

// ---------- load_player_weapon ----------

void AssetManager::load_player_weapon(const std::string& tactic, const std::string& asset_root) {
    std::string model_file = weapon_tactic_to_model_file(tactic);
    if (model_file.empty()) {
        weapon_model_.reset();
        return;
    }
    auto candidates = model_paths(asset_root, model_file.c_str());
    std::string fig_path;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
    }
    if (fig_path.empty()) {
        std::printf("  Player weapon '%s' model NOT FOUND (tried: %s)!\n",
                   tactic.c_str(), model_file.c_str());
        weapon_model_.reset();
        return;
    }
    weapon_model_ = std::make_unique<BodyModel>();
    fmt::XmlDocument doc;
    if (!doc.parse(read_text(fig_path))) {
        std::fprintf(stderr, "[weapon] parse error for %s: %s\n",
                    model_file.c_str(), doc.error().c_str());
        weapon_model_.reset();
        return;
    }
    auto* scene = doc.root()->first_child("Scene");
    if (!scene) { weapon_model_.reset(); return; }

    if (auto* ns = scene->first_child("Nodes")) {
        for (const auto& child : ns->children) {
            std::string type = child.attr("Type");
            if (type == "MacroNode") {
                BodyMacroNode mn;
                mn.name = child.name;
                mn.children[0] = child.attr("ChildNode1");
                mn.children[1] = child.attr("ChildNode2");
                mn.children[2] = child.attr("ChildNode3");
                mn.children[3] = child.attr("ChildNode4");
                // [R1] LCC weights turn the Weapon-Node* children into the
                // rendered mesh position; zeroed LCC collapsed the mesh to
                // the origin.
                mn.lcc[0] = tof(child.attr("LCC1"));
                mn.lcc[1] = tof(child.attr("LCC2"));
                mn.lcc[2] = tof(child.attr("LCC3"));
                mn.lcc[3] = tof(child.attr("LCC4"));
                weapon_model_->macro_nodes[mn.name] = mn;
            }
            BodyNode n;
            n.name = child.name;
            n.x = tof(child.attr("X"));
            n.y = tof(child.attr("Y"));
            n.mass = tof(child.attr("Mass"), 1.0f);
            n.fixed = (toi(child.attr("Fixed")) != 0);
            weapon_model_->nodes[n.name] = n;
        }
    }
    if (auto* es = scene->first_child("Edges")) {
        for (const auto& child : es->children) {
            if (child.attr("Type") != "Edge") continue;
            BodyEdge e;
            e.name = child.name;
            e.end1 = child.attr("End1");
            e.end2 = child.attr("End2");
            e.radius = tof(child.attr("Radius"));
            weapon_model_->edges.push_back(e);
        }
    }
    if (auto* fs = scene->first_child("Figures")) {
        for (const auto& child : fs->children) {
            if (child.attr("Type") == "Capsule") {
                BodyCapsule c;
                c.edge_name = child.attr("Edge");
                c.radius1 = tof(child.attr("Radius1"));
                c.radius2 = tof(child.attr("Radius2"));
                c.margin1 = tof(child.attr("Margin1"));
                c.margin2 = tof(child.attr("Margin2"));
                weapon_model_->capsules.push_back(c);
            } else if (child.attr("Type") == "Triangle") {
                // [U1] Weapon figures are Triangles; render needs them.
                BodyTriangle t;
                t.n1 = child.attr("Node1");
                t.n2 = child.attr("Node2");
                t.n3 = child.attr("Node3");
                weapon_model_->triangles.push_back(t);
            }
        }
    }
    std::printf("  Player weapon '%s' (%s): %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                tactic.c_str(), model_file.c_str(),
                weapon_model_->nodes.size(), weapon_model_->edges.size(),
                weapon_model_->capsules.size(), weapon_model_->triangles.size());
}

// ---------- load_animations ----------

void AssetManager::load_animations(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    // [ORIGINAL] Use correct paths — no double "assets"
    std::vector<std::filesystem::path> search_dirs = {
        root/"assets"/"animations"/"binary",
        root/"animations"/"binary",
        root/"assets"/"animations",
        root/"animations",
    };

    std::filesystem::path anim_dir;
    for (auto& dir : search_dirs) {
        if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir)) {
            anim_dir = dir;
            break;
        }
    }

    if (anim_dir.empty()) {
        std::printf("  Animations: NO DIRECTORY FOUND! Searched:\n");
        for (auto& dir : search_dirs) std::printf("    %s\n", dir.string().c_str());
        return;
    }

    int loaded = 0;
    for (auto& entry : std::filesystem::directory_iterator(anim_dir)) {
        if (entry.path().extension() != ".bin") continue;
        std::string name = entry.path().stem().string();
        if (animations_.count(name)) continue;
        AnimationData anim;
        anim.name = name;
        if (anim.load(entry.path().string())) {
            animations_[name] = std::move(anim);
            loaded++;
        }
    }

    for (auto& [move_name, move] : moves_) {
        if (move.filename.empty()) continue;
        std::string anim_name = move.filename;
        if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
            anim_name = anim_name.substr(0, anim_name.size()-4);
        if (animations_.count(anim_name)) continue;
        for (auto& dir : search_dirs) {
            auto path = dir / (anim_name + ".bin");
            if (std::filesystem::exists(path)) {
                AnimationData anim;
                anim.name = anim_name;
                if (anim.load(path.string())) {
                    animations_[anim_name] = std::move(anim);
                    loaded++;
                }
                break;
            }
        }
    }

    if (animations_.count("fists1_stance_idle") && !animations_.count("fists_idle")) {
        animations_["fists_idle"] = animations_["fists1_stance_idle"];
    }

    std::printf("  Animations loaded: %zu (from %s)\n", animations_.size(), anim_dir.string().c_str());
}

// ---------- load_moves ----------

void AssetManager::load_moves(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    // [ORIGINAL] Use correct paths — no double "assets"
    std::vector<std::filesystem::path> search_dirs = {
        root/"assets"/"animations",
        root/"animations",
        root/"assets",
    };

    std::string moves_path;
    for (auto& dir : search_dirs) {
        auto path = dir / "moves.xml";
        if (std::filesystem::exists(path)) { moves_path = path.string(); break; }
    }
    if (moves_path.empty()) {
        std::printf("  moves.xml NOT FOUND!\n");
        return;
    }

    parse_moves_xml(read_text(moves_path));
}

void AssetManager::parse_moves_xml(const std::string& xml) {
    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[moves] xml_doc parse error: %s\n", doc.error().c_str());
        return;
    }

    const auto* root_node = doc.root();
    if (!root_node) return;

    const auto* moves_node = root_node->first_child("Moves");
    if (!moves_node) {
        auto* mx = root_node->first_child("Movesxml");
        if (mx) moves_node = mx->first_child("Moves");
    }
    if (!moves_node) {
        std::fprintf(stderr, "[moves] No <Moves> section found\n");
        return;
    }

    for (const auto& child : moves_node->children) {
        if (child.name != "Move") continue;

        MoveDef move;
        move.name = child.attr("Name");
        move.filename = child.attr("FileName");
        move.template_name = child.attr("Template");
        move.first_frame = (int)tof(child.attr("FirstFrame", "-1"));
        move.end_frame = (int)tof(child.attr("EndFrame"));
        move.priority = (int)tof(child.attr("Priority"));
        move.mid_frames = (int)tof(child.attr("MidFrames", "2"));
        move.tactic_weapon = child.attr("TacticWeapon");
        move.is_attack = (child.attr("Type") == "ATTACK");

        // Template string parsing
        if (!move.template_name.empty()) {
            std::string tmpl = move.template_name;
            size_t start = 0;
            std::vector<std::string> parts;
            while (start < tmpl.size()) {
                auto sep = tmpl.find('|', start);
                if (sep == std::string::npos) { parts.push_back(tmpl.substr(start)); break; }
                parts.push_back(tmpl.substr(start, sep - start));
                start = sep + 1;
            }
            if (parts.size() >= 1) move.key_count = toi(parts[0].substr(0, parts[0].find("key")));
            if (parts.size() >= 2) move.direction = parts[1];
            if (parts.size() >= 3) {
                if (parts[2] == "Unarmed") move.is_unarmed = true;
                else move.weapon_filter = parts[2];
            }
            if (parts.size() >= 4) move.move_type = parts[3];
            move.is_jump = (move.template_name.find("Jump") != std::string::npos);
            move.is_short_attack = (move.template_name.find("ShortAttack") != std::string::npos);
            move.is_retreat = (move.template_name.find("Retreat") != std::string::npos);
            move.is_step = (move.template_name.find("Step") != std::string::npos);
            move.is_double_step = (move.template_name.find("DoubleStep") != std::string::npos);
            move.is_block = (move.template_name.find("Block") != std::string::npos);
            move.is_stance = (move.template_name.find("Stance") != std::string::npos);
            move.is_idle = (move.template_name.find("Idle") != std::string::npos);
            move.is_not_titan = (move.template_name.find("NotTitan") != std::string::npos);
        }

        // Parse sub-elements
        for (const auto& sub : child.children) {
            if (sub.name == "Damage") {
                move.damage = tof(sub.attr("Value"));
            } else if (sub.name == "Distance") {
                move.distance_min = tof(sub.attr("Min"));
                move.distance_max = tof(sub.attr("Max"));
                move.has_distance_cond = true;
            } else if (sub.name == "Attack") {
                // Backward compat: first Attack sets attack_start/end/edges/damage/impulse
                move.attack_start = (int)tof(sub.attr("Start"));
                move.attack_end = (int)tof(sub.attr("End"));
                std::string edges_str = sub.attr("Edges");
                size_t epos = 0;
                while (epos < edges_str.size()) {
                    auto comma = edges_str.find(',', epos);
                    if (comma == std::string::npos) { move.attack_edges.push_back(edges_str.substr(epos)); break; }
                    move.attack_edges.push_back(edges_str.substr(epos, comma - epos));
                    epos = comma + 1;
                }
                move.damage = tof(sub.attr("Damage"));
                move.impulse_x = tof(sub.attr("ImpulseX"));
                move.impulse_y = tof(sub.attr("ImpulseY"));
            } else if (sub.name == "Block") {
                move.block_start = (int)tof(sub.attr("Start"));
            } else if (sub.name == "Uninterrupt") {
                move.uninterrupt_start = (int)tof(sub.attr("Start"));
                move.uninterrupt_end = (int)tof(sub.attr("End"));
            } else if (sub.name == "Keys") {
                for (const auto& key : sub.children) {
                    if (key.name == "Key") {
                        move.key_types.push_back(key.attr("Type"));
                        // [ORIGINAL] PressType distinguishes a tap from a hold.
                        // Absent means Tap. Two moves can share a key and
                        // differ only here, so dropping it made every hold
                        // resolve to the tap variant.
                        auto press = key.attr("PressType");
                        if (press.empty()) press = "Tap";
                        if (press == "Hold") move.needs_hold = true;
                        move.key_press_types.push_back(std::move(press));
                    }
                }
            } else if (sub.name == "Locks") {
                for (const auto& lock : sub.children) {
                    if (lock.name == "Item") {
                        move.required_perk = lock.attr("Perk");
                        move.required_weapon_subtype = lock.attr("SubType");
                    } else if (lock.name == "Perk") {
                        move.required_perk = lock.attr("Name");
                    }
                }
            } else if (sub.name == "Align") {
                move.has_align = true;
                move.align_axis = sub.attr("Axis");
                move.align_x = move.align_axis.find('X') != std::string::npos;
                move.align_y = move.align_axis.find('Y') != std::string::npos;
                move.align_z = move.align_axis.find('Z') != std::string::npos;
                move.align_shift_model_node = sub.attr("ShiftModelNode");
                // [ORIGINAL] MoveInfo::parseAlign @ 0x1017e140 reads Object,
                // Part and Player from BOTH <Pivot> and <Position>. Only the
                // pivot side was being read here, which lost the distinction
                // between the four Position targets — and 658 of the 800
                // <Align> blocks in moves.xml use Object="Pivot", the one that
                // anchors the animation to the model's current node.
                auto to_object = [](const std::string& o) {
                    if (o == "Nodes")     return MoveDef::AlignObject::Nodes;
                    if (o == "Wall")      return MoveDef::AlignObject::Wall;
                    if (o == "Animation") return MoveDef::AlignObject::Animation;
                    if (o == "Pivot")     return MoveDef::AlignObject::Pivot;
                    return MoveDef::AlignObject::None;
                };
                if (auto* pivot = sub.first_child("Pivot")) {
                    std::string obj = pivot->attr("Object");
                    move.align_pivot_object = to_object(obj);
                    if (obj == "Animation") {
                        move.moveinside_is_animation = true;
                    } else {
                        move.moveinside_pivot_node = pivot->attr("Part");
                    }
                }
                if (auto* position = sub.first_child("Position")) {
                    move.align_position_object = to_object(position->attr("Object"));
                    move.align_position_node = position->attr("Part");
                    move.align_shift_x = tof(position->attr("ShiftX"));
                    move.align_shift_y = tof(position->attr("ShiftY"));
                }
            } else if (sub.name == "Conditions") {
                for (const auto& cond : sub.children) {
                    if (cond.name == "CurrentAnimation") {
                        move.required_current_animation = cond.attr("Name");
                    } else {
                        // [ORIGINAL] The key bindings live at
                        //   <Move><Conditions><Keys><Key Type=.. PressType=../>
                        // NOT directly under <Move>, and 22 of the 325 <Keys>
                        // blocks sit inside nested <Operator Type="Or">
                        // elements expressing ALTERNATIVE chords for the same
                        // move. Because only the <Move><Keys> path was handled,
                        // key_types came back empty for every bound move -- no
                        // move was reachable from input at all, which is why the
                        // controls did not respond.
                        collect_move_keys(cond, move);
                    }
                }
            } else if (sub.name == "Intervals" || sub.name == "Interval") {
                // [ORIGINAL] moves.xml nests the intervals:
                //   <Intervals>
                //     <Interval Name="Uninterrupt" End="9"/>
                //     <Interval Type="Block" Start="10"/>
                //     <Interval Type="Attack" Start="4" End="5">
                //       <AttackingParts><Edge Name="EForearm_2"/>...</AttackingParts>
                //       <Damage Value="0.11"><Damage Type="UnarmedDamage" Shift="-10"/></Damage>
                //       <Impulse X="245" Y="0" Z="0"/>
                //       <Hit Name="High"/>
                //     </Interval>
                //   </Intervals>
                //
                // The loop this branch lives in walks the direct children of
                // <Move>, so it only ever saw <Intervals> — and there was no
                // case for it. Every attack, block and uninterrupt window in
                // the game was therefore parsed as "absent": 359 moves in
                // moves.xml declare an attack interval and none of them
                // reached MoveDef.
                const auto& interval_nodes = (sub.name == "Intervals")
                                                 ? sub.children
                                                 : std::vector<fmt::XmlNode>{};
                auto parse_interval = [&](const fmt::XmlNode& node) {
                    MoveDef::Interval iv;
                    iv.type = node.attr("Type");
                    iv.name = node.attr("Name");
                    iv.start = tof(node.attr("Start"));
                    // [ORIGINAL] A missing End means "to the end of the
                    // animation" — e.g. <Interval Type="Block" Start="10"/>
                    // and 44 of the Attack intervals in moves.xml. Reading it
                    // as 0 turns those into an empty window that can never
                    // fire, so absence is recorded as -1.
                    const std::string end_s = node.attr("End");
                    iv.end = end_s.empty() ? -1.0f : tof(end_s);
                    // Flat form kept for any file that uses it.
                    iv.damage = (int)tof(node.attr("Damage"));
                    iv.impulse_x = tof(node.attr("ImpulseX"));
                    iv.impulse_y = tof(node.attr("ImpulseY"));
                    iv.hit_type = node.attr("HitType");

                    // [ORIGINAL] IntervalAttack flags from 0x10115d80
                    // Apply to MoveDef if this is an Attack interval
                    if (iv.type == "Attack") {
                        if (node.attr("IgnoresBlock") == "true" || node.attr("IgnoresBlock") == "1")
                            move.ignores_block = true;
                        if (node.attr("NoEffect") == "true" || node.attr("NoEffect") == "1")
                            move.no_effect = true;
                    }

                    std::string edges_str = node.attr("Edges");
                    size_t epos2 = 0;
                    while (epos2 < edges_str.size()) {
                        auto comma = edges_str.find(',', epos2);
                        if (comma == std::string::npos) {
                            iv.edges.push_back(edges_str.substr(epos2));
                            break;
                        }
                        iv.edges.push_back(edges_str.substr(epos2, comma - epos2));
                        epos2 = comma + 1;
                    }
                    // Nested form, which is what moves.xml actually uses.
                    for (const auto& kid : node.children) {
                        if (kid.name == "AttackingParts") {
                            for (const auto& e : kid.children)
                                if (e.name == "Edge") {
                                    iv.edges.push_back(e.attr("Name"));
                                    // Also add to move's attacking_parts if this is an Attack
                                    if (iv.type == "Attack")
                                        move.attacking_parts.push_back(e.attr("Name"));
                                }
                        } else if (kid.name == "Damage") {
                            iv.damage_value = tof(kid.attr("Value"));
                            // [ORIGINAL] The nested
                            //   <Damage Value="0.11"><Damage Type="UnarmedDamage" Shift="-10"/></Damage>
                            // names the ATTRIBUTE this attack reads and the
                            // shift added to it before the f3 difference
                            // ("DamageAttribute(+Shift)" — LIVE_GAME_EVIDENCE
                            // Q3: real HighPunch ships UnarmedDamage Shift=-10).
                            // It was dropped entirely, so the unarmed-damage
                            // shift never reached the damage formula (P10).
                            for (const auto& inner : kid.children)
                                if (inner.name == "Damage") {
                                    iv.damage_attr = inner.attr("Type");
                                    iv.damage_attr_shift = (int)tof(inner.attr("Shift"));
                                }
                        } else if (kid.name == "Impulse") {
                            iv.impulse_x = tof(kid.attr("X"));
                            iv.impulse_y = tof(kid.attr("Y"));
                        } else if (kid.name == "Hit") {
                            iv.hit_type = kid.attr("Name");
                        } else if (kid.name == "ComplexInterval") {
                            iv.condition_anim = kid.attr("CurrentAnimation");
                        }
                    }
                    if (auto* ci = node.first_child("ComplexInterval"))
                        iv.condition_anim = ci->attr("CurrentAnimation");
                    move.intervals.push_back(iv);
                };
                if (sub.name == "Interval") {
                    parse_interval(sub);
                } else {
                    for (const auto& node : interval_nodes)
                        if (node.name == "Interval") parse_interval(node);
                }
            } else if (sub.name == "IgnoresInvulnerable") {
                move.ignores_invulnerable = sub.attr("Name");
            } else if (sub.name == "IntervalAttack") {
                // [ORIGINAL] IntervalAttack flags from 0x10115d80
                move.ignores_block = (sub.attr("IgnoresBlock") == "true" || sub.attr("IgnoresBlock") == "1");
                move.no_effect = (sub.attr("NoEffect") == "true" || sub.attr("NoEffect") == "1");
                // Parse attacking parts (skeleton edge names)
                for (const auto& part : sub.children) {
                    if (part.name == "AttackingParts") {
                        for (const auto& edge : part.children) {
                            if (edge.name == "Edge") {
                                move.attacking_parts.push_back(edge.attr("Name"));
                            }
                        }
                    }
                }
            } else if (sub.name == "Actions") {
                for (const auto& action : sub.children) {
                    if (action.name == "Sound") {
                        MoveDef::SoundEvent se;
                        se.time = tof(action.attr("Time"));
                        se.sound = action.attr("Name");
                        move.sound_events.push_back(se);
                    }
                }
            }
        }

        // Populate backward-compat fields from first interval of each type
        // [ORIGINAL] moves.xml keys Attack and Block off Type= and the rest off
        // Name=. The previous predicate demanded Type="Attack" AND
        // Name="Attack" at the same time, which no interval in the file
        // satisfies, so it never fired even for the moves whose <Intervals>
        // block did get read.
        for (auto& iv : move.intervals) {
            if (iv.type == "Attack") {
                if (move.attack_start < 0) {
                    move.attack_start = (int)iv.start;
                    move.attack_end = (int)iv.end;
                    if (move.attack_edges.empty()) move.attack_edges = iv.edges;
                    if (move.damage == 0.0f) move.damage = iv.damage_value;
                    if (move.impulse_x == 0.0f) move.impulse_x = iv.impulse_x;
                    if (move.impulse_y == 0.0f) move.impulse_y = iv.impulse_y;
                    // [P10] The damage attribute + shift (nested <Damage>).
                    if (move.damage_attr.empty()) move.damage_attr = iv.damage_attr;
                    if (move.damage_attr_shift == 0) move.damage_attr_shift = iv.damage_attr_shift;
                }
            } else if (iv.type == "Block" || iv.name == "Block") {
                if (move.block_start < 0) move.block_start = (int)iv.start;
            } else if (iv.name == "Uninterrupt") {
                if (move.uninterrupt_start < 0) move.uninterrupt_start = (int)iv.start;
                if (move.uninterrupt_end < 0) move.uninterrupt_end = (int)iv.end;
            } else if (iv.name == "SemiUninterrupt") {
                // [ORIGINAL] SemiUninterrupt: can be interrupted by attacks
                // but not by movement. From IntervalAttack::getFactors @ 0x10115910.
                // moves.xml: 81 moves declare it (e.g. DoubleStepForward End=2).
                if (move.semi_uninterrupt_end < 0) {
                    move.semi_uninterrupt_start = (int)iv.start;
                    move.semi_uninterrupt_end = (int)iv.end;
                }
            } else if (iv.name == "SelfUninterrupt") {
                // [ORIGINAL] SelfUninterrupt: can only be interrupted by itself
                // (combo chains). moves.xml: 4 moves declare it.
                if (move.self_uninterrupt_start < 0) {
                    move.self_uninterrupt_start = (int)iv.start;
                    move.self_uninterrupt_end = (int)iv.end;
                }
            }
        }

        // Invulnerable intervals
        for (const auto& sub : child.children) {
            if (sub.name == "Invulnerable") {
                for (const auto& inv : sub.children) {
                    if (inv.name == "Interval") {
                        MoveDef::InvulnerableInterval inviv;
                        inviv.start = tof(inv.attr("Start"));
                        inviv.end = tof(inv.attr("End"));
                        inviv.name = inv.attr("Name");
                        move.invulnerable_intervals.push_back(inviv);
                    }
                }
            }
        }

        moves_[move.name] = move;
    }

    std::printf("  Moves loaded: %zu\n", moves_.size());
}

// ---------- load_animation (single .bin) ----------

void AssetManager::load_animation(const std::string& anim_name, const std::string& asset_root, const std::string& search_dir) {
    // [HEURISTIC-TODO] This is a stub — the actual implementation is in Game's inline
    // play_animation method which loads animations on demand.
    // For now, the bulk load_animations() method handles everything.
    (void)anim_name;
    (void)asset_root;
    (void)search_dir;
}

// ---------- load_loading_screen ----------

void AssetManager::load_loading_screen(const std::string& asset_root, int window_w, int window_h) {
    auto root = std::filesystem::path(asset_root);
    std::string xml_path;
    for (const auto& dir : {root/"assets"/"1536"/"textures"/"fullscreen",
                             root/"1536"/"textures"/"fullscreen",
                             root/"assets"/"1536"/"fullscreen",
                             root/"1536"/"fullscreen"}) {
        auto p = dir/"startLoading.xml";
        if (std::filesystem::exists(p)) { xml_path = p.string(); break; }
    }
    if (xml_path.empty()) {
        std::printf("  startLoading.xml not found, loading screen will be blank\n");
        return;
    }
    fmt::XmlDocument doc;
    if (!doc.parse(read_text(xml_path))) {
        std::fprintf(stderr, "[loading] xml parse error: %s\n", doc.error().c_str());
        return;
    }
    auto* root_node = doc.root();
    if (!root_node || root_node->name != "Images") return;
    for (const auto& child : root_node->children) {
        if (child.name != "Image") continue;
        auto file = child.attr("File");
        auto x = tof(child.attr("X"));
        auto y = tof(child.attr("Y"));
        std::filesystem::path img_path;
        for (const auto& base : {root/"assets"/"1536", root/"1536", root/"assets", root}) {
            auto p = base / file;
            if (std::filesystem::exists(p)) { img_path = p; break; }
        }
        if (!img_path.empty()) {
            auto data = read_file(img_path.string());
            int w, h, ch;
            auto* px = stbi_load_from_memory(
                (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
            if (px) {
                auto tex = std::make_unique<ren::Texture2D>();
                tex->init_rgba(w, h, px);
                stbi_image_free(px);
                loading_images_.push_back({std::move(tex), x, y});
            }
        }
    }
    if (loading_images_.empty()) {
        std::printf("  No loading images found, loading screen will be blank\n");
    }
}

// ---------- render_loading_screen ----------

void AssetManager::render_loading_screen(ren::Renderer& renderer, plat::Platform& platform, float /*progress*/, float load_scale) {
    float tw = 1820.0f * load_scale, th = 1024.0f * load_scale;
    float ox = (platform.window_width() - tw) / 2.0f;
    float oy = (platform.window_height() - th) / 2.0f;
    for (auto& img : loading_images_) {
        if (!img.texture) continue;
        float w = img.texture->width() * load_scale;
        float h = img.texture->height() * load_scale;
        float x = ox + (img.x + 910.0f) * load_scale;
        float y = oy + (img.y + 512.0f) * load_scale;
        renderer.draw_textured_quad_screen(*img.texture, x, y, w, h);
    }
}

// ---------- load_hud_textures ----------

void AssetManager::load_hud_textures(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
        load_texture_atlas_to_hud(base/"textures"/"panels"/"top",
                                  "batchPanelsTop");
        load_texture_atlas_to_hud(base/"textures"/"buttons"/"dojo",
                                  "batchButtonsDojo");
        load_texture_atlas_to_hud(base/"textures"/"fight"/"bars",
                                  "batchFightBars");
        load_texture_atlas_to_hud(base/"textures"/"buttons"/"fight",
                                  "batchButtonsFight");
        load_texture_atlas_to_hud(base/"textures"/"effects"/"fight",
                                  "hit_blade");
        load_texture_atlas_to_hud(base/"textures"/"fight"/"hits",
                                  "hitBatch");
    }
    // [ORIGINAL] Character avatars are loose PNGs in image/users/image/
    // (quests.xml references them via Image="character_sensei" etc.).
    for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
        auto users_dir = base/"image"/"users"/"image";
        if (!std::filesystem::exists(users_dir)) continue;
        for (auto& entry : std::filesystem::directory_iterator(users_dir)) {
            if (entry.path().extension() != ".png") continue;
            auto name = entry.path().stem().string();
            if (hud_textures_.count(name)) continue;  // already loaded
            auto data = read_file(entry.path().string());
            int w, h, ch;
            auto* px = stbi_load_from_memory(
                (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
            if (px) {
                auto tex = std::make_unique<ren::Texture2D>();
                tex->init_rgba(w, h, px);
                stbi_image_free(px);
                hud_textures_[name] = std::move(tex);
            }
        }
    }
    std::printf("  HUD textures loaded: %zu\n", hud_textures_.size());
}

// ---------- load_menu_textures ----------

void AssetManager::load_menu_textures(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
        load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"screens",
                                  "batchButtonsMenuScreens");
        // [U2] The shop's category tabs (Weapon/Armor/Helmet/Ranged_weapon/
        // Magic + _active/_pushed states) and the Wear/Bag buttons. Without
        // this atlas the shop rendered text glyphs for the tabs.
        load_texture_atlas_to_hud(base/"textures"/"screens"/"shop"/"buttons",
                                  "shopButtons");
    }
    for (auto it = hud_textures_.begin(); it != hud_textures_.end(); ) {
        if (it->first.find("_normal") != std::string::npos ||
            it->first.find("_active") != std::string::npos ||
            it->first.find("_pushed") != std::string::npos ||
            it->first.find("_Normal") != std::string::npos ||
            it->first.find("_Active") != std::string::npos ||
            it->first.find("_Pushed") != std::string::npos) {
            menu_textures_[it->first] = std::move(it->second);
            it = hud_textures_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
        auto scroll_dir = base/"textures"/"scrolls"/"common";
        for (auto& name : {"MenuRoll_left", "MenuRoll_center", "MenuRoll_right",
                           "Roll_left", "Roll_center", "Roll_right",
                           "Paper_left", "Paper_right", "Shadow_roll"}) {
            auto path = scroll_dir / (std::string(name) + ".png");
            if (std::filesystem::exists(path)) {
                auto data = read_file(path.string());
                int w, h, ch;
                auto* px = stbi_load_from_memory(
                    (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                if (px) {
                    auto tex = std::make_unique<ren::Texture2D>();
                    tex->init_rgba(w, h, px);
                    stbi_image_free(px);
                    scroll_textures_[name] = std::move(tex);
                }
            }
        }
    }
    std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                menu_textures_.size(), scroll_textures_.size());
}

// ---------- load_texture_atlas_to_hud ----------

void AssetManager::load_texture_atlas_to_hud(
    const std::filesystem::path& dir, const std::string& atlas_name)
{
    auto pp = dir / (atlas_name + ".plist");
    auto pn = dir / (atlas_name + ".png");
    if (!std::filesystem::exists(pp) || !std::filesystem::exists(pn)) return;
    auto result = plist::parse(read_text(pp.string()));
    if (!result) return;
    auto png_data = read_file(pn.string());
    int aw, ah, ach;
    auto* atlas_px = stbi_load_from_memory(
        (const stbi_uc*)png_data.data(), (int)png_data.size(),
        &aw, &ah, &ach, 4);
    if (!atlas_px) return;
    for (auto& [name, idx] : result->name_index) {
        auto& frame = result->frames[idx];
        int fw = frame.rotated ? frame.atlas_h : frame.atlas_w;
        int fh = frame.rotated ? frame.atlas_w : frame.atlas_h;
        auto tex = std::make_unique<ren::Texture2D>();
        std::vector<std::uint8_t> px((size_t)fw * fh * 4);
        for (int y = 0; y < fh; ++y) {
            for (int x = 0; x < fw; ++x) {
                int sx, sy;
                if (frame.rotated) {
                    sx = frame.atlas_x + (fh - 1 - y);
                    sy = frame.atlas_y + x;
                } else {
                    sx = frame.atlas_x + x;
                    sy = frame.atlas_y + y;
                }
                if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                int src_idx = (sy * aw + sx) * 4;
                int dst_idx = (y * fw + x) * 4;
                px[dst_idx+0] = atlas_px[src_idx+0];
                px[dst_idx+1] = atlas_px[src_idx+1];
                px[dst_idx+2] = atlas_px[src_idx+2];
                px[dst_idx+3] = atlas_px[src_idx+3];
            }
        }
        tex->init_rgba(fw, fh, px.data());
        std::string n = name;
        if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
        hud_textures_[n] = std::move(tex);
    }
    stbi_image_free(atlas_px);
}

// ---------- load_hud_font ----------

void AssetManager::load_hud_font(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    std::vector<std::filesystem::path> candidates = {
        root/"assets"/"1536"/"fonts"/"rus"/"optima.fnt",
        root/"assets"/"1536"/"fonts"/"obelix.fnt",
        root/"1536"/"fonts"/"rus"/"optima.fnt",
        root/"1536"/"fonts"/"obelix.fnt",
    };
    std::string fnt_path, png_path;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            fnt_path = p.string();
            auto png = p.parent_path() / (p.stem().string() + ".png");
            if (!std::filesystem::exists(png)) {
                auto xml = read_text(fnt_path);
                auto fp = xml.find("file=\"");
                if (fp != std::string::npos) {
                    fp += 6;
                    auto end = xml.find('"', fp);
                    std::string png_name = xml.substr(fp, end - fp);
                    auto png2 = p.parent_path() / png_name;
                    if (std::filesystem::exists(png2)) png = png2;
                }
            }
            if (std::filesystem::exists(png)) {
                png_path = png.string();
                break;
            }
        }
    }
    // Every failure here used to be a bare `return`. If the font does not load,
    // EVERY string in the game silently disappears — the HUD numerals, the
    // scroll captions, the map labels — and nothing says why. That is the same
    // class of defect as the masking boxes and the fight-button atlas.
    if (fnt_path.empty()) {
        std::fprintf(stderr, "[font] no .fnt found; tried %zu paths under %s\n",
                     candidates.size(), asset_root.c_str());
        for (const auto& p : candidates)
            std::fprintf(stderr, "[font]   %s\n", p.string().c_str());
        return;
    }
    if (png_path.empty()) {
        std::fprintf(stderr, "[font] '%s' has no page texture next to it\n",
                     fnt_path.c_str());
        return;
    }
    auto result = font::parse(read_text(fnt_path));
    if (!result) {
        std::fprintf(stderr, "[font] '%s' did not parse\n", fnt_path.c_str());
        return;
    }
    hud_font_ = std::make_shared<font::ParsedFont>(std::move(*result));
    auto png_data = read_file(png_path);
    auto tex = std::make_unique<ren::Texture2D>();
    if (!tex->init_from_png((const uint8_t*)png_data.data(), png_data.size())) {
        std::fprintf(stderr, "[font] page '%s' did not decode (%zu bytes)\n",
                     png_path.c_str(), png_data.size());
        return;
    }
    hud_font_tex_ = std::move(tex);
    std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                fnt_path.c_str(), hud_font_->chars.size());
}

// ---------- load_sounds ----------

void AssetManager::load_sounds(const std::string& asset_root) {
    auto& eng = aud::AudioEngine::instance();
    eng.init();
    auto root = std::filesystem::path(asset_root);
    // [ORIGINAL] Use correct paths — no double "assets"
    std::vector<std::filesystem::path> sound_dirs = {
        root/"assets"/"sounds",
        root/"sounds",
    };
    std::filesystem::path sound_dir;
    for (const auto& d : sound_dirs) {
        if (std::filesystem::exists(d)) { sound_dir = d; break; }
    }
    if (sound_dir.empty()) {
        std::printf("[audio] sounds dir not found\n");
        return;
    }
    // Load all .wav files from the sound directory
    size_t count = 0;
    for (auto& entry : std::filesystem::directory_iterator(sound_dir)) {
        if (entry.path().extension() != ".wav") continue;
        std::string name = entry.path().stem().string();
        eng.load_sound_file(name, entry.path().string());
        ++count;
    }
    std::printf("[audio] loaded %zu sounds from %s\n",
                count, sound_dir.string().c_str());
}

// ---------- load_sound (static) ----------

void AssetManager::load_sound(const std::string& name, const std::string& asset_root) {
    auto& eng = aud::AudioEngine::instance();
    auto root = std::filesystem::path(asset_root);
    // [ORIGINAL] Use correct paths — no double "assets"
    std::vector<std::filesystem::path> search_dirs = {
        root/"assets"/"sounds",
        root/"sounds",
    };
    for (const auto& d : search_dirs) {
        auto p = d / (name + ".wav");
        if (std::filesystem::exists(p)) {
            eng.load_sound_file(name, p.string());
            return;
        }
    }
}

// ---------- load_stages ----------

void AssetManager::load_stages(const std::string& asset_root) {
    if (stages_loaded_) return;
    auto root = std::filesystem::path(asset_root);
    // [ORIGINAL] Try correct path first, then fallback
    auto stages_path = root / "assets/stages.xml";
    if (!std::filesystem::exists(stages_path)) {
        stages_path = root / "assets/files/assets/stages.xml";
    }
    if (std::filesystem::exists(stages_path)) {
        auto stages_text = read_text(stages_path.string());
        fmt::StageParser parser;
        if (parser.parse(stages_text, stage_data_)) {
            stages_loaded_ = true;
            std::printf("[STAGE] Loaded %zu zones\n", stage_data_.zones.size());
        }
    }
}

// ---------- load_internal_settings ----------
//
// [ORIGINAL] Parses damage-related settings from internalSettings.xml.
// Binary ref: internalSettings parsing at 0x10291370
// Extracts DamageFactor, BlockDamageFactor, AverageBaseDamage, CriticalHit
// parameters used in the damage formula.

void AssetManager::load_internal_settings(const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    auto settings_path = root / "internalSettings.xml";
    if (!std::filesystem::exists(settings_path)) {
        settings_path = root / "files/assets/internalSettings.xml";
    }
    if (!std::filesystem::exists(settings_path)) {
        std::printf("[SETTINGS] internalSettings.xml not found, using defaults\n");
        return;
    }

    auto text = read_text(settings_path.string());
    fmt::XmlDocument doc;
    if (!doc.parse(text)) {
        std::fprintf(stderr, "[SETTINGS] Failed to parse internalSettings.xml: %s\n", doc.error().c_str());
        return;
    }
    auto* root_node = doc.root();
    if (!root_node) {
        std::printf("[SETTINGS] internalSettings.xml has no root node\n");
        return;
    }

    auto tof = [](const std::string& s) -> float {
        if (s.empty()) return 0.0f;
        try { return std::stof(s); } catch (...) { return 0.0f; }
    };

    // [ORIGINAL] <DamageFactor Base="0.0001" Attribute="DamageFactor"/>
    for (const auto& child : root_node->children) {
        if (child.name == "DamageFactor") {
            damage_settings_.damage_factor_base = tof(child.attr("Base"));
            std::printf("[SETTINGS] DamageFactor.Base = %f\n", damage_settings_.damage_factor_base);
        }
        else if (child.name == "BlockDamageFactor") {
            // [ORIGINAL] <BlockDamageFactor Base="0.0001" Attribute="BlockDamageFactor" />
            damage_settings_.block_damage_factor_base = tof(child.attr("Base"));
            std::printf("[SETTINGS] BlockDamageFactor.Base = %f\n", damage_settings_.block_damage_factor_base);
        }
        else if (child.name == "AverageBaseDamage") {
            // [ORIGINAL] <AverageBaseDamage Value="0.1" />
            damage_settings_.average_base_damage = tof(child.attr("Value"));
            std::printf("[SETTINGS] AverageBaseDamage = %f\n", damage_settings_.average_base_damage);
        }
        else if (child.name == "CriticalHit") {
            for (const auto& crit_child : child.children) {
                if (crit_child.name == "Probability") {
                    damage_settings_.crit_probability_base = tof(crit_child.attr("Base"));
                }
                else if (crit_child.name == "Damage") {
                    damage_settings_.crit_damage_base = tof(crit_child.attr("Base"));
                }
            }
            std::printf("[SETTINGS] CriticalHit: prob_base=%f dmg_base=%f\n",
                        damage_settings_.crit_probability_base, damage_settings_.crit_damage_base);
        }
    }
}

} // namespace resf2::game
