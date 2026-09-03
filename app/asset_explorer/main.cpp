// asset_explorer — first data-layer tool.
//
// 1. Decompresses the zstd xml.dat archive, parses the container, extracts
//    every file to reference/extracted/xml/.
// 2. Prints summaries:
//    - moves.xml        (from the archive): move count + first 20 moves
//    - dojo params XML  (plain file on disk): layers + ModelsViewer + Wall/Floor
//    - users_default.xml (from the archive): warrior attributes
//
// Usage: asset_explorer [xml.dat] [dojo_params.xml] [extract_dir]
// Defaults target the reference/ tree at the repo root.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "xml_archive.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace {

constexpr const char* kDefaultXmlDat =
    "reference/www/res/xml.9e0b4b10.dat";
constexpr const char* kDefaultDojoParams =
    "reference/www/res/locations/dojo/dojo_params.b78df4b4.xml";
constexpr const char* kDefaultExtractDir = "reference/extracted/xml";

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

const sf2::data::archive_entry* find_entry(
    const std::vector<sf2::data::archive_entry>& entries, const std::string& name) {
    for (const sf2::data::archive_entry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// --- moves.xml summary -----------------------------------------------------

void print_moves_summary(const sf2::data::archive_entry& moves_entry) {
    sf2::data::xml_doc doc;
    doc.parse(moves_entry.data.data(), moves_entry.data.size());

    const pugi::xml_node moves_node = doc.root().child("Movesxml").child("Moves");
    if (!moves_node) {
        throw std::runtime_error("moves.xml: <Movesxml><Moves> not found");
    }

    int count = 0;
    for (pugi::xml_node move : moves_node.children("Move")) {
        (void)move;
        ++count;
    }

    std::cout << "=== moves.xml ===\n";
    std::cout << "move count: " << count << "\n\n";

    int shown = 0;
    for (pugi::xml_node move : moves_node.children("Move")) {
        if (shown >= 20) {
            break;
        }
        const char* name = move.attribute("Name").value();
        const char* type = move.attribute("Type").value();
        const char* tmpl = move.attribute("Template").value();
        const int first_frame = sf2::data::xml_attr_int(move, "FirstFrame", -1);
        const int end_frame = sf2::data::xml_attr_int(move, "EndFrame", -1);
        const int mid_frames = sf2::data::xml_attr_int(move, "MidFrames", -1);
        const int priority = sf2::data::xml_attr_int(move, "Priority", -1);

        // Damage = sum of <HitFrame Damage="N"> across all events.
        int damage = 0;
        for (pugi::xpath_node hit : move.select_nodes(".//HitFrame")) {
            damage += sf2::data::xml_attr_int(hit.node(), "Damage", 0);
        }

        std::cout << "  " << shown + 1 << ". " << name << "\n";
        std::cout << "     Type=" << (type[0] ? type : "-")
                  << "  Template=" << (tmpl[0] ? tmpl : "-")
                  << "  Priority=" << priority << "\n";
        std::cout << "     FirstFrame=" << first_frame << "  EndFrame=" << end_frame
                  << "  MidFrames=" << mid_frames << "  Damage=" << damage << "\n";
        ++shown;
    }
    std::cout << "\n";
}

// --- dojo params summary ---------------------------------------------------

void print_dojo_summary(const std::string& dojo_path) {
    const std::vector<std::uint8_t> bytes = read_file(dojo_path);
    sf2::data::xml_doc doc;
    doc.parse(bytes.data(), bytes.size());

    const pugi::xml_node root = doc.root().child("Root");
    if (!root) {
        throw std::runtime_error("dojo params: <Root> not found");
    }

    std::cout << "=== " << dojo_path << " ===\n";
    std::cout << "Width=" << sf2::data::xml_attr_int(root, "Width", -1)
              << "  Height=" << sf2::data::xml_attr_int(root, "Height", -1)
              << "  Wall=" << sf2::data::xml_attr_int(root, "Wall", -1)
              << "  Floor=" << sf2::data::xml_attr_int(root, "Floor", -1)
              << "  Pages=" << sf2::data::xml_attr_int(root, "Pages", -1)
              << "  Color=" << root.attribute("Color").value() << "\n";

    int layer_index = 0;
    for (pugi::xml_node layer : root.children("Layer")) {
        ++layer_index;
        const int type = sf2::data::xml_attr_int(layer, "Type", -1);
        const float factor = sf2::data::xml_attr_float(layer, "Factor", 0.0f);
        const float scaling = sf2::data::xml_attr_float(layer, "Scaling", 1.0f);

        std::cout << "  Layer " << layer_index << ": Type=" << type
                  << "  Factor=" << factor << "  Scaling=" << scaling << "\n";

        for (pugi::xml_node child : layer.children()) {
            const char* tag = child.name();
            if (std::strcmp(tag, "ModelsViewer") == 0) {
                std::cout << "    ModelsViewer: PlayerPositionX="
                          << sf2::data::xml_attr_float(child, "PlayerPositionX", 0.0f)
                          << "  PlayerPositionY="
                          << sf2::data::xml_attr_float(child, "PlayerPositionY", 0.0f)
                          << "  EnemyPositionX="
                          << sf2::data::xml_attr_float(child, "EnemyPositionX", 0.0f)
                          << "  EnemyPositionY="
                          << sf2::data::xml_attr_float(child, "EnemyPositionY", 0.0f)
                          << "\n";
            } else {
                std::cout << "    <" << tag << "> ClassName="
                          << child.attribute("ClassName").value() << "\n";
            }
        }
    }
    std::cout << "  layer count: " << layer_index << "\n\n";
}

// --- users_default.xml summary ---------------------------------------------

void print_users_summary(const sf2::data::archive_entry& users_entry) {
    sf2::data::xml_doc doc;
    doc.parse(users_entry.data.data(), users_entry.data.size());

    const pugi::xml_node warrior = doc.root().child("Root").child("Warriors").child("Warrior");
    if (!warrior) {
        throw std::runtime_error("users_default.xml: <Warriors><Warrior> not found");
    }

    std::cout << "=== users_default.xml ===\n";
    std::cout << "Warrior ID=" << warrior.attribute("ID").value() << "\n";
    std::cout << "  Money=" << sf2::data::xml_attr_int(warrior, "Money", -1)
              << "  Bonus=" << sf2::data::xml_attr_int(warrior, "Bonus", -1)
              << "  Strength=" << sf2::data::xml_attr_int(warrior, "Strength", -1)
              << "  Stamina=" << sf2::data::xml_attr_int(warrior, "Stamina", -1)
              << "  Level=" << sf2::data::xml_attr_int(warrior, "Level", -1)
              << "  Experience=" << sf2::data::xml_attr_int(warrior, "Experience", -1)
              << "  Power=" << sf2::data::xml_attr_int(warrior, "Power", -1) << "\n";
    std::cout << "  Weapon=" << warrior.attribute("Weapon").value()
              << "  Armor=" << warrior.attribute("Armor").value()
              << "  Helm=" << warrior.attribute("Helm").value()
              << "  Ranged=" << warrior.attribute("Ranged").value()
              << "  Magic=" << warrior.attribute("Magic").value() << "\n";
    std::cout << "  Tutorial=" << warrior.attribute("Tutorial").value()
              << "  CurrentZone=" << warrior.attribute("CurrentZone").value()
              << "  Skeleton=" << warrior.attribute("Skeleton").value()
              << "  Difficulty=" << warrior.attribute("Difficulty").value() << "\n";

    const pugi::xml_node items = warrior.child("Items");
    if (items) {
        std::cout << "  Items:\n";
        for (pugi::xml_node item : items.children("Item")) {
            std::cout << "    " << item.attribute("Name").value()
                      << "  Equipped=" << item.attribute("Equipped").value()
                      << "  Count=" << item.attribute("Count").value() << "\n";
        }
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string xml_dat = argc > 1 ? argv[1] : kDefaultXmlDat;
    const std::string dojo_params = argc > 2 ? argv[2] : kDefaultDojoParams;
    const std::string extract_dir = argc > 3 ? argv[3] : kDefaultExtractDir;

    try {
        // 1. Decompress + parse the archive.
        const std::vector<std::uint8_t> compressed = read_file(xml_dat);
        const std::vector<std::uint8_t> decompressed =
            sf2::data::zstd_decompress(compressed);
        const std::vector<sf2::data::archive_entry> entries =
            sf2::data::xml_archive_parse(decompressed.data(), decompressed.size());

        std::cout << "=== xml.dat archive ===\n";
        std::cout << "source: " << xml_dat << "\n";
        std::cout << "compressed size: " << compressed.size() << " bytes\n";
        std::cout << "decompressed size: " << decompressed.size() << " bytes\n";
        std::cout << "file count: " << entries.size() << "\n";

        std::size_t total_data = 0;
        for (const sf2::data::archive_entry& entry : entries) {
            total_data += entry.data.size();
        }
        std::cout << "total file data: " << total_data << " bytes\n\n";

        std::cout << "first 50 files:\n";
        const std::size_t shown = entries.size() < 50 ? entries.size() : 50;
        for (std::size_t i = 0; i < shown; ++i) {
            std::cout << "  " << i + 1 << ". " << entries[i].name << " ("
                      << entries[i].data.size() << " bytes)\n";
        }
        std::cout << "\n";

        // 2. Extract everything.
        const std::size_t written =
            sf2::data::xml_archive_extract(entries, extract_dir);
        std::cout << "extracted " << written << " files to " << extract_dir << "\n\n";

        // 3. Summaries.
        const sf2::data::archive_entry* moves = find_entry(entries, "res/moves.xml");
        if (moves == nullptr) {
            throw std::runtime_error("res/moves.xml not found in archive");
        }
        print_moves_summary(*moves);

        print_dojo_summary(dojo_params);

        const sf2::data::archive_entry* users =
            find_entry(entries, "res/users_default.xml");
        if (users == nullptr) {
            throw std::runtime_error("res/users_default.xml not found in archive");
        }
        print_users_summary(*users);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "asset_explorer: error: " << e.what() << "\n";
        return 1;
    }
}