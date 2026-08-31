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
            if (std::strcmp(type, "Triangle") != 0) {
                if (std::strcmp(type, "Capsule") == 0) {
                    Capsule cap;
                    cap.edge = fig.attribute("Edge").value();
                    cap.radius1 = sf2::data::xml_attr_float(fig, "Radius1");
                    cap.radius2 = sf2::data::xml_attr_float(fig, "Radius2");
                    cap.margin1 = sf2::data::xml_attr_float(fig, "Margin1");
                    cap.margin2 = sf2::data::xml_attr_float(fig, "Margin2");
                    model.capsules.push_back(std::move(cap));
                }
                continue;
            }
            Tri tri;
            tri.n1 = fig.attribute("Node1").value();
            tri.n2 = fig.attribute("Node2").value();
            tri.n3 = fig.attribute("Node3").value();
            if (tri.n1.empty() || tri.n2.empty() || tri.n3.empty()) {
                continue;
            }
            model.tris.push_back(std::move(tri));
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
    }
    return merged;
}

} // namespace sf2::scene
