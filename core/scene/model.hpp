#pragma once

// Fighter ragdoll model: the game's `Yc`/`Dl` structures (MODEL_FORMAT §1).
//
// A model XML (one entry of models.*.dat) has <Nodes> (bones: Node /
// MacroNode / CenterOfMass), <Edges> (joints — not needed for rendering)
// and <Figures> (Type="Triangle" mesh + Type="Capsule" colliders — capsules
// are skipped for rendering this milestone).
//
// Bones ARE the mesh vertices: each triangle references 3 bone names; the
// per-frame render samples each referenced bone's (x, y) and drops z.
// Bone order matters — animation clips are indexed by bone order
// (clip bone i = merged model bone i).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf2::scene {

// One bone: bind (rest) position in model space. `y` is already negated
// (the game parses H(X, -Y, Z)); MacroNode X is additionally negated.
struct Bone {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool is_macro = false;
};

// One mesh triangle: the three referenced bone NAMES. The game resolves
// these against the merged fighter hierarchy (Yc.mkb -> a.Ic(name) uses the
// shared `Va.Xca` map); resolution therefore happens in build_fighter_model,
// not in model_parse.
struct Tri {
    std::string n1;
    std::string n2;
    std::string n3;
};

// A name-resolved mesh triangle (indices into the merged bone list), used by
// the Fighter renderer.
struct TriResolved {
    int i1 = 0;
    int i2 = 0;
    int i3 = 0;
};

// A collision capsule (Type="Capsule", spans an edge). Parsed for
// completeness; not rendered in this milestone.
struct Capsule {
    std::string edge;  // referenced <Edges> name
    float radius1 = 0.0f;
    float radius2 = 0.0f;
    float margin1 = 0.0f;
    float margin2 = 0.0f;
};

// A MacroNode's weighted child list (the game's `Fl.children`, Yc.FIa +
// Yc.dGa): the macro's position is the weighted average of its child bones'
// CURRENT positions (Fl.seb). Only needed for macros not driven by the
// animation clip.
struct MacroChildren {
    std::vector<std::string> child_names;  // referenced bone names (XML order)
    std::vector<float> weights;            // LCC1..N barycentric weights
};

// A parsed ragdoll model (one models.*.dat entry).
struct Model {
    std::vector<Bone> bones;  // document order — clip bone i = bones[i]
    std::vector<Tri> tris;    // unresolved names (part models)
    std::vector<TriResolved> resolved_tris;  // merged: indices into `bones`
    std::vector<Capsule> capsules;
    // MacroNode name -> child list (weighted), for non-clip macro posing.
    std::unordered_map<std::string, MacroChildren> macro_children;
    // Name -> bone index (first definition wins across merged parts).
    std::unordered_map<std::string, int> bone_index;

    int bone_by_name(const std::string& name) const {
        const auto it = bone_index.find(name);
        return it != bone_index.end() ? it->second : -1;
    }
};

// Parses one model XML document. Throws std::runtime_error on malformed
// input (missing <Scene>, unresolved triangle node refs are skipped).
Model model_parse(const std::uint8_t* xml, std::size_t size);

// Merges multiple models into one fighter body: bones are appended in order
// (first definition wins on name conflict, preserving order so clip indices
// stay valid for the SKELETON — the model list must start with the
// skeleton); triangles and capsules are concatenated.
Model build_fighter_model(const std::vector<Model>& parts);

} // namespace sf2::scene
