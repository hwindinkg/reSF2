// anim_model_probe — Phase 2b data-layer probe.
//
// Decompresses + parses the animation and model archives and reports their
// structure:
//   1. animations.*.dat  — container + per-clip binary format (anim_archive)
//   2. models.*.dat      — container + per-model XML (ragdoll/mesh)
//   3. dojo variants     — counts + names
//   4. sanity cross-check of animation names vs moves.xml FileName stems
//
// Usage: anim_model_probe [animations.dat] [models.dat] [animations_dojo.dat]
//                         [models_dojo.dat] [xml.dat]
// Defaults target the reference/ tree at the repo root.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "xml_archive.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace {

constexpr const char* kAnimDat = "reference/www/res/animations.b22c72ff.dat";
constexpr const char* kModelsDat = "reference/www/res/models.473fd74f.dat";
constexpr const char* kAnimDojoDat = "reference/www/res/animations_dojo.3314a7de.dat";
constexpr const char* kModelsDojoDat = "reference/www/res/models_dojo.e57366a0.dat";
constexpr const char* kXmlDat = "reference/www/res/xml.9e0b4b10.dat";

constexpr double kFrameRate = 60.0;  // game's fixed 60 Hz update step

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

// zstd-decompress + parse the shared container format.
std::vector<sf2::data::archive_entry> load_archive(const std::string& path) {
    const std::vector<std::uint8_t> compressed = read_file(path);
    const std::vector<std::uint8_t> decompressed =
        sf2::data::zstd_decompress(compressed);
    return sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());
}

// --- model XML summary -----------------------------------------------------

struct model_summary {
    std::string name;
    int node_count = 0;      // Type="Node"
    int macro_count = 0;     // Type="MacroNode"
    int edge_count = 0;      // Type="Edge"
    int muscle_count = 0;    // Type="Muscle"
    int capsule_count = 0;   // Type="Capsule"
    int triangle_count = 0;  // Type="Triangle"
    std::string first_node_name;
};

model_summary summarize_model(const sf2::data::archive_entry& entry) {
    sf2::data::xml_doc doc;
    doc.parse(entry.data.data(), entry.data.size());

    model_summary s;
    s.name = entry.name;

    const pugi::xml_node scene = doc.root().child("Scene");
    if (!scene) {
        throw std::runtime_error("model '" + entry.name + "': <Scene> not found");
    }

    const pugi::xml_node nodes = scene.child("Nodes");
    if (nodes) {
        for (pugi::xml_node node : nodes.children()) {
            const char* type = node.attribute("Type").value();
            if (std::strcmp(type, "MacroNode") == 0) {
                ++s.macro_count;
            } else {
                ++s.node_count;  // "Node" (and anything else)
            }
            if (s.first_node_name.empty()) {
                s.first_node_name = node.name();
            }
        }
    }

    const pugi::xml_node edges = scene.child("Edges");
    if (edges) {
        for (pugi::xml_node edge : edges.children()) {
            const char* type = edge.attribute("Type").value();
            if (std::strcmp(type, "Muscle") == 0) {
                ++s.muscle_count;
            } else {
                ++s.edge_count;
            }
        }
    }

    const pugi::xml_node figures = scene.child("Figures");
    if (figures) {
        for (pugi::xml_node figure : figures.children()) {
            const char* type = figure.attribute("Type").value();
            if (std::strcmp(type, "Capsule") == 0) {
                ++s.capsule_count;
            } else if (std::strcmp(type, "Triangle") == 0) {
                ++s.triangle_count;
            }
        }
    }
    return s;
}

// --- animation report ------------------------------------------------------

void report_animations(const std::string& path, const std::string& label,
                       const std::map<int, std::string>& node_count_to_first_node) {
    const std::vector<sf2::data::archive_entry> entries = load_archive(path);
    std::cout << "=== " << label << " ===\n";
    std::cout << "source: " << path << "\n";
    std::cout << "animation count: " << entries.size() << "\n\n";

    std::cout << "first 30 animation names:\n";
    const std::size_t shown = entries.size() < 30 ? entries.size() : 30;
    for (std::size_t i = 0; i < shown; ++i) {
        std::cout << "  " << i + 1 << ". " << entries[i].name << "\n";
    }
    std::cout << "\n";

    // Parse every clip; pick 3 samples: entry 0, the first version-0 clip,
    // and the first stance clip.
    std::vector<sf2::data::anim_clip> clips;
    clips.reserve(entries.size());
    std::size_t v0_index = entries.size();
    std::size_t stance_index = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        sf2::data::anim_clip clip = sf2::data::anim_clip_parse(
            entries[i].name, entries[i].data.data(), entries[i].data.size());
        if (clip.version == 0 && v0_index == entries.size()) {
            v0_index = i;
        }
        if (clip.name.find("stance") != std::string::npos && stance_index == entries.size()) {
            stance_index = i;
        }
        clips.push_back(std::move(clip));
    }

    const std::size_t samples[] = {0, v0_index, stance_index};
    std::cout << "sample animations:\n";
    for (std::size_t idx : samples) {
        if (idx >= clips.size()) {
            continue;
        }
        const sf2::data::anim_clip& clip = clips[idx];
        const double duration = clip.frames.size() / kFrameRate;
        std::cout << "  " << clip.name << " (version " << clip.version << ")\n";
        std::cout << "    frames: " << clip.frames.size()
                  << "  duration: " << duration << " s"
                  << "  bones: " << clip.bone_count() << "\n";
        const auto it = node_count_to_first_node.find(static_cast<int>(clip.bone_count()));
        if (it != node_count_to_first_node.end()) {
            std::cout << "    first bone name (model with matching node count, "
                      << it->first << " nodes): " << it->second << "\n";
        } else {
            std::cout << "    first bone name: n/a (bone names live in model XML; "
                         "no model with matching node count)\n";
        }
    }
    std::cout << "\n";
}

// --- model report ----------------------------------------------------------

void report_models(const std::string& path, const std::string& label,
                   std::map<int, std::string>* node_count_to_first_node) {
    const std::vector<sf2::data::archive_entry> entries = load_archive(path);
    std::cout << "=== " << label << " ===\n";
    std::cout << "source: " << path << "\n";
    std::cout << "model count: " << entries.size() << "\n\n";

    std::cout << "first 30 model names:\n";
    const std::size_t shown = entries.size() < 30 ? entries.size() : 30;
    for (std::size_t i = 0; i < shown; ++i) {
        std::cout << "  " << i + 1 << ". " << entries[i].name << "\n";
    }
    std::cout << "\n";

    std::vector<model_summary> summaries;
    summaries.reserve(entries.size());
    for (const sf2::data::archive_entry& entry : entries) {
        model_summary s = summarize_model(entry);
        const int total_nodes = s.node_count + s.macro_count;
        if (node_count_to_first_node != nullptr &&
            node_count_to_first_node->find(total_nodes) == node_count_to_first_node->end()) {
            (*node_count_to_first_node)[total_nodes] = s.first_node_name;
        }
        summaries.push_back(std::move(s));
    }

    std::cout << "sample models:\n";
    // Prefer the first two models that carry a mesh (Triangle figures).
    std::size_t sample_count = 0;
    for (std::size_t i = 0; i < summaries.size() && sample_count < 2; ++i) {
        if (summaries[i].triangle_count == 0) {
            continue;
        }
        const model_summary& s = summaries[i];
        std::cout << "  " << s.name << "\n";
        std::cout << "    bones: " << s.node_count + s.macro_count
                  << " (Node=" << s.node_count << ", MacroNode=" << s.macro_count << ")\n";
        std::cout << "    edges: " << s.edge_count << "  muscles: " << s.muscle_count
                  << "  capsules: " << s.capsule_count
                  << "  triangles (mesh): " << s.triangle_count << "\n";
        std::cout << "    first node name: " << s.first_node_name << "\n";
        ++sample_count;
    }
    std::cout << "\n";
}

// --- dojo variants ---------------------------------------------------------

void report_dojo_names(const std::string& path, const std::string& label) {
    const std::vector<sf2::data::archive_entry> entries = load_archive(path);
    std::cout << "=== " << label << " ===\n";
    std::cout << "source: " << path << "\n";
    std::cout << "count: " << entries.size() << "\n";
    for (const sf2::data::archive_entry& entry : entries) {
        std::cout << "  " << entry.name << "\n";
    }
    std::cout << "\n";
}

// --- sanity cross-check vs moves.xml ---------------------------------------

void cross_check_moves(const std::string& xml_dat_path,
                       const std::vector<sf2::data::archive_entry>& anim_entries) {
    const std::vector<sf2::data::archive_entry> xml_entries = load_archive(xml_dat_path);

    const sf2::data::archive_entry* moves = nullptr;
    for (const sf2::data::archive_entry& entry : xml_entries) {
        if (entry.name == "res/moves.xml") {
            moves = &entry;
            break;
        }
    }
    if (moves == nullptr) {
        throw std::runtime_error("res/moves.xml not found in " + xml_dat_path);
    }

    sf2::data::xml_doc doc;
    doc.parse(moves->data.data(), moves->data.size());

    // Collect FileName stems ("stance_idle.bytes" -> "stance_idle").
    std::set<std::string> file_stems;
    for (pugi::xml_node move : doc.root().child("Movesxml").child("Moves").children("Move")) {
        const char* file_name = move.attribute("FileName").value();
        if (file_name[0] == '\0') {
            continue;
        }
        std::string stem(file_name);
        const std::size_t dot = stem.rfind(".bytes");
        if (dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        file_stems.insert(stem);
    }

    std::size_t matched = 0;
    std::vector<std::string> unmatched;
    for (const sf2::data::archive_entry& entry : anim_entries) {
        if (file_stems.count(entry.name) != 0) {
            ++matched;
        } else {
            unmatched.push_back(entry.name);
        }
    }

    std::cout << "=== sanity cross-check (animation names vs moves.xml FileName) ===\n";
    std::cout << "animations: " << anim_entries.size()
              << "  unique moves.xml FileName stems: " << file_stems.size() << "\n";
    std::cout << "animations matching a moves.xml FileName: " << matched << " / "
              << anim_entries.size() << "\n";
    if (!unmatched.empty()) {
        std::cout << "unmatched animations (first 10):\n";
        const std::size_t shown = unmatched.size() < 10 ? unmatched.size() : 10;
        for (std::size_t i = 0; i < shown; ++i) {
            std::cout << "  " << unmatched[i] << "\n";
        }
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string anim_dat = argc > 1 ? argv[1] : kAnimDat;
    const std::string models_dat = argc > 2 ? argv[2] : kModelsDat;
    const std::string anim_dojo_dat = argc > 3 ? argv[3] : kAnimDojoDat;
    const std::string models_dojo_dat = argc > 4 ? argv[4] : kModelsDojoDat;
    const std::string xml_dat = argc > 5 ? argv[5] : kXmlDat;

    try {
        // Model node-count -> first node name map, built from models.dat and
        // used to cross-reference bone names for the animation samples.
        std::map<int, std::string> node_count_to_first_node;

        report_models(models_dat, "models.dat", &node_count_to_first_node);
        report_animations(anim_dat, "animations.dat", node_count_to_first_node);
        report_dojo_names(anim_dojo_dat, "animations_dojo.dat");
        report_dojo_names(models_dojo_dat, "models_dojo.dat");

        const std::vector<sf2::data::archive_entry> anim_entries = load_archive(anim_dat);
        cross_check_moves(xml_dat, anim_entries);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "anim_model_probe: error: " << e.what() << "\n";
        return 1;
    }
}