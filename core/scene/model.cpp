// Model loader: model XML -> Model (bones/tris/capsules) and the merged
// fighter body (MODEL_FORMAT §1-2, Yc.Mia/Ijb/nkb + Dl).

#include "scene/model.hpp"

#include <cstring>
#include <stdexcept>

#include "xml_doc.hpp"

namespace sf2::scene {

namespace {

// Yc.Ijb (L571-572): every <Nodes> child becomes a bone.
//   pos = H(X, -Y, Z); MacroNode X is negated (mirror).
Bone parse_bone(const pugi::xml_node& node) {
    Bone b;
    b.name = node.name();
    b.x = sf2::data::xml_attr_float(node, "X");
    b.y = -sf2::data::xml_attr_float(node, "Y");
    b.z = sf2::data::xml_attr_float(node, "Z");
    const char* type = node.attribute("Type").value();
    b.is_macro = std::strcmp(type, "MacroNode") == 0;
    if (b.is_macro) {
        b.x = -b.x;
    }
    b.mass = sf2::data::xml_attr_float(node, "Mass", 1.0f);
    return b;
}

} // namespace

Model model_parse(const std::uint8_t* xml, std::size_t size) {
    sf2::data::xml_doc doc;
    doc.parse(xml, size);
    const pugi::xml_node scene = doc.root().child("Scene");
    if (!scene) {
        throw std::runtime_error("model: <Scene> not found");
    }

    Model model;

    // <Nodes> — bones in document order. `CenterOfMass` is also a bone
    // (the game's `Vc`); MacroNodes are bones too.
    if (const pugi::xml_node nodes = scene.child("Nodes")) {
        for (const pugi::xml_node node : nodes.children()) {
            Bone b = parse_bone(node);
            if (model.bone_index.count(b.name) != 0) {
                continue;  // first definition wins (merged hierarchy)
            }
            const int idx = static_cast<int>(model.bones.size());
            model.bone_index[b.name] = idx;
            model.bones.push_back(b);

            // Capture the MacroNode child list (Yc.FIa: NodesCount,
            // ChildNodeN + LCCN weights).
            if (b.is_macro) {
                MacroChildren mc;
                const int count = sf2::data::xml_attr_int(node, "NodesCount", 0);
                for (int c = 1; c <= count; ++c) {
                    const std::string child =
                        node.attribute(("ChildNode" + std::to_string(c)).c_str()).value();
                    if (child.empty()) {
                        continue;
                    }
                    mc.child_names.push_back(child);
                    mc.weights.push_back(
                        sf2::data::xml_attr_float(node,
                                                  ("LCC" + std::to_string(c)).c_str()));
                }
                model.macro_children[b.name] = std::move(mc);
            }
        }
    }

    // <Figures Type="Triangle"> — mesh. Node1/2/3 are bone names; the bones
    // ARE the vertices. Names are kept unresolved: the game resolves them
    // against the merged hierarchy (`Yc.mkb` -> `a.Ic(name)` on the shared
    // `Va.Xca` map), and part triangles routinely reference skeleton bones
    // that only exist after merging.
    if (const pugi::xml_node figures = scene.child("Figures")) {
        for (const pugi::xml_node fig : figures.children()) {
            const char* type = fig.attribute("Type").value();
            if (std::strcmp(type, "Triangle") == 0) {
                Tri tri;
                tri.n1 = fig.attribute("Node1").value();
                tri.n2 = fig.attribute("Node2").value();
                tri.n3 = fig.attribute("Node3").value();
                if (tri.n1.empty() || tri.n2.empty() || tri.n3.empty()) {
                    continue;
                }
                model.tris.push_back(std::move(tri));
            } else if (std::strcmp(type, "Quad") == 0) {
                // JS has no Quad in the shipped models (all Triangle), but
                // some tools export quads. Split into two triangles (1,2,3)
                // and (1,3,4) preserving winding (JS dv.ia order).
                const std::string n1 = fig.attribute("Node1").value();
                const std::string n2 = fig.attribute("Node2").value();
                const std::string n3 = fig.attribute("Node3").value();
                const std::string n4 = fig.attribute("Node4").value();
                if (n1.empty() || n2.empty() || n3.empty() || n4.empty()) {
                    continue;
                }
                Tri t1;
                t1.n1 = n1;
                t1.n2 = n2;
                t1.n3 = n3;
                model.tris.push_back(std::move(t1));
                Tri t2;
                t2.n1 = n1;
                t2.n2 = n3;
                t2.n3 = n4;
                model.tris.push_back(std::move(t2));
            } else if (std::strcmp(type, "Capsule") == 0) {
                Capsule cap;
                cap.edge = fig.attribute("Edge").value();
                cap.radius1 = sf2::data::xml_attr_float(fig, "Radius1");
                cap.radius2 = sf2::data::xml_attr_float(fig, "Radius2");
                cap.margin1 = sf2::data::xml_attr_float(fig, "Margin1");
                cap.margin2 = sf2::data::xml_attr_float(fig, "Margin2");
                model.capsules.push_back(std::move(cap));
            }
        }
    }

    // <Edges> - fight-physics collision capsules (JS `Yc.jjb` L572: each
    // edge becomes a `yu` capsule spanning its two endpoint bones). Only
    // `Collisible="1"` edges enter the hit-test list `Dl.Nl.oI`; the
    // others are structural (cloth/visual). The `Defense` attribute
    // ("BodyDefense"/"HeadDefense") selects the damage multiplier and
    // `BodyPart` is the hit region.
    if (const pugi::xml_node edges = scene.child("Edges")) {
        for (const pugi::xml_node edge : edges.children()) {
            EdgeDef ed;
            ed.name = edge.name();
            ed.end1 = edge.attribute("End1").value();
            ed.end2 = edge.attribute("End2").value();
            ed.length = sf2::data::xml_attr_float(edge, "Length");
            ed.radius = sf2::data::xml_attr_float(edge, "Radius");
            ed.margin1 = sf2::data::xml_attr_float(edge, "Margin1");
            ed.margin2 = sf2::data::xml_attr_float(edge, "Margin2");
            ed.collisible = sf2::data::xml_attr_bool(edge, "Collisible", false);
            ed.body_part = edge.attribute("BodyPart").value();
            ed.defense = edge.attribute("Defense").value();
            ed.blood = sf2::data::xml_attr_bool(edge, "Blood", false);
            ed.shock = sf2::data::xml_attr_bool(edge, "Shock", false);
            if (ed.name.empty() || ed.end1.empty() || ed.end2.empty()) {
                continue;
            }
            model.edges.push_back(std::move(ed));
        }
    }

    return model;
}

Model build_fighter_model(const std::vector<Model>& parts) {
    Model merged;
    for (const Model& part : parts) {
        // Append part bones (first definition wins on name conflict).
        for (const Bone& bone : part.bones) {
            if (merged.bone_index.count(bone.name) != 0) {
                continue;  // skeleton bones defined first win
            }
            const int idx = static_cast<int>(merged.bones.size());
            merged.bone_index[bone.name] = idx;
            merged.bones.push_back(bone);
        }
        for (const Tri& tri : part.tris) {
            const int i1 = merged.bone_by_name(tri.n1);
            const int i2 = merged.bone_by_name(tri.n2);
            const int i3 = merged.bone_by_name(tri.n3);
            if (i1 < 0 || i2 < 0 || i3 < 0) {
                continue;  // game: Yc.mkb drops the triangle if any node is missing
            }
            merged.resolved_tris.push_back(TriResolved{i1, i2, i3});
        }
        // Macro children: merge child lists (first definition wins).
        for (const auto& kv : part.macro_children) {
            if (merged.macro_children.count(kv.first) == 0) {
                merged.macro_children[kv.first] = kv.second;
            }
        }
        merged.capsules.insert(merged.capsules.end(), part.capsules.begin(),
                               part.capsules.end());
        merged.edges.insert(merged.edges.end(), part.edges.begin(),
                            part.edges.end());
    }
    return merged;
}

} // namespace sf2::scene
