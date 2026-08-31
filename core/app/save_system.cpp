// Save system implementation — users.xml save/load (JS `Aa`/`SF2User`).
//
// The save document mirrors users_default.xml (see save_system.hpp for the
// JS line refs). On first run (no save file) the template is parsed and
// returned; `save()` rewrites the current Warrior's progression into the
// document and writes it to `save_path`.

#include "app/save_system.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "xml_doc.hpp"

namespace sf2::app {

namespace {

std::string read_file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("SaveSystem: cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<std::size_t>(size));
    in.read(data.data(), size);
    if (!in) {
        throw std::runtime_error("SaveSystem: cannot read " + path);
    }
    return std::string(data.begin(), data.end());
}

void write_file_text(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("SaveSystem: cannot write " + path);
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw std::runtime_error("SaveSystem: write failed " + path);
    }
}

// Locates the first <Warrior> under <Root>/<Warriors>.
pugi::xml_node find_warrior(pugi::xml_node root) {
    pugi::xml_node warriors = root.child("Warriors");
    if (!warriors) {
        return {};
    }
    return warriors.child("Warrior");
}

} // namespace

SaveSystem::SaveSystem(std::string save_path, std::string default_path)
    : save_path_(std::move(save_path)), default_path_(std::move(default_path)) {}

bool SaveSystem::has_save() const {
    std::error_code ec;
    return std::filesystem::exists(save_path_, ec);
}

WarriorSave SaveSystem::load() {
    const std::string path = has_save() ? save_path_ : default_path_;
    sf2::data::xml_doc doc;
    doc.parse(read_file_text(path));

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") {
        throw std::runtime_error("SaveSystem: save root element missing in " + path);
    }
    const pugi::xml_node warrior = find_warrior(root);
    if (!warrior) {
        throw std::runtime_error("SaveSystem: no <Warrior> in " + path);
    }

    WarriorSave out;
    out.id = sf2::data::xml_attr_int(warrior, "ID", 1);
    if (warrior.attribute("FirstName")) out.first_name = warrior.attribute("FirstName").value();
    out.money = sf2::data::xml_attr_int(warrior, "Money", 0);
    out.bonus = sf2::data::xml_attr_int(warrior, "Bonus", 50);
    out.strength = sf2::data::xml_attr_int(warrior, "Strength", 3);
    out.stamina = sf2::data::xml_attr_int(warrior, "Stamina", 3);
    out.level = sf2::data::xml_attr_int(warrior, "Level", 1);
    out.experience = sf2::data::xml_attr_int(warrior, "Experience", 0);
    out.power = sf2::data::xml_attr_int(warrior, "Power", 5);
    if (warrior.attribute("Skeleton")) out.skeleton = warrior.attribute("Skeleton").value();
    if (warrior.attribute("Armor")) out.armor = warrior.attribute("Armor").value();
    if (warrior.attribute("Helm")) out.helm = warrior.attribute("Helm").value();
    if (warrior.attribute("Weapon")) out.weapon = warrior.attribute("Weapon").value();
    if (warrior.attribute("Ranged")) out.ranged = warrior.attribute("Ranged").value();
    if (warrior.attribute("Magic")) out.magic = warrior.attribute("Magic").value();
    if (warrior.attribute("Tutorial")) out.tutorial = warrior.attribute("Tutorial").value();
    if (warrior.attribute("Tactic")) out.tactic = warrior.attribute("Tactic").value();
    if (warrior.attribute("CurrentZone")) out.current_zone = warrior.attribute("CurrentZone").value();
    return out;
}

void SaveSystem::save(const WarriorSave& w) {
    // Load the current document (the save, or the template when none yet),
    // patch the Warrior attributes, and write back. This preserves the
    // full users.xml structure (Items/Battles/Versions/...) exactly like
    // the game's `Aa.save` (which re-serializes the whole Rb document).
    const std::string src_path = has_save() ? save_path_ : default_path_;
    sf2::data::xml_doc doc;
    doc.parse(read_file_text(src_path));

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") {
        throw std::runtime_error("SaveSystem: cannot save — root element missing");
    }
    const pugi::xml_node warrior = find_warrior(root);
    if (!warrior) {
        throw std::runtime_error("SaveSystem: cannot save — no <Warrior>");
    }

    warrior.attribute("Money").set_value(w.money);
    warrior.attribute("Bonus").set_value(w.bonus);
    warrior.attribute("Strength").set_value(w.strength);
    warrior.attribute("Stamina").set_value(w.stamina);
    warrior.attribute("Level").set_value(w.level);
    warrior.attribute("Experience").set_value(w.experience);
    warrior.attribute("Power").set_value(w.power);
    warrior.attribute("Armor").set_value(w.armor.c_str());
    warrior.attribute("Helm").set_value(w.helm.c_str());
    warrior.attribute("Weapon").set_value(w.weapon.c_str());
    warrior.attribute("Ranged").set_value(w.ranged.c_str());
    warrior.attribute("Magic").set_value(w.magic.c_str());
    warrior.attribute("Tutorial").set_value(w.tutorial.c_str());
    warrior.attribute("Tactic").set_value(w.tactic.c_str());
    warrior.attribute("CurrentZone").set_value(w.current_zone.c_str());

    std::ostringstream oss;
    doc.save(oss, "\t", pugi::format_default, pugi::encoding_auto);
    write_file_text(save_path_, oss.str());
}

} // namespace sf2::app
