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

#include "codec.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

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
    std::string text = read_file_text(path);
    // Dual-format boot read: plain XML starts with `<` (after whitespace);
    // otherwise it is an SF2User envelope (base64+zstd).
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && text[first] != '<') {
        text = envelope_decode(text);
    }
    sf2::data::xml_doc doc;
    doc.parse(text);

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

    // The owned items (JS `$g.parse` reads the Warrior <Items> children).
    out.items.clear();
    for (pugi::xml_node item : warrior.child("Items").children("Item")) {
        WarriorSave::OwnedItem oi;
        if (item.attribute("Name")) oi.name = item.attribute("Name").value();
        oi.count = sf2::data::xml_attr_int(item, "Count", 1);
        oi.equipped = sf2::data::xml_attr_bool(item, "Equipped", false);
        out.items.push_back(std::move(oi));
    }

    // Battle records (JS `iF`): `<Battles><Battle Name="...">` presence.
    out.battles.clear();
    for (pugi::xml_node b : warrior.child("Battles").children("Battle")) {
        if (b.attribute("Name")) out.battles.push_back(b.attribute("Name").value());
    }

    // Fight win counts (JS `yc`): `<Fights>/<Fight Name Wins>` (`Wins`
    // attr name OPEN — no <Fights> in the seed).
    out.fights.clear();
    for (pugi::xml_node f : warrior.child("Fights").children("Fight")) {
        WarriorSave::FightWins fw;
        if (f.attribute("Name")) fw.name = f.attribute("Name").value();
        fw.wins = sf2::data::xml_attr_int(f, "Wins", 0);
        out.fights.push_back(std::move(fw));
    }

    // Quests + variables (JS `kF`/`rv`). Absent in the seed -> empty.
    out.quests.clear();
    out.variables.clear();
    if (pugi::xml_node quests = warrior.child("Quests")) {
        for (pugi::xml_node q : quests.children("Quest")) {
            WarriorSave::QuestState qs;
            if (q.attribute("Name")) qs.name = q.attribute("Name").value();
            if (q.attribute("State")) qs.state = q.attribute("State").value();
            out.quests.push_back(std::move(qs));
        }
        if (pugi::xml_node vars = quests.child("Variables")) {
            for (pugi::xml_node v : vars.children("Variable")) {
                if (v.attribute("Name")) {
                    out.variables[v.attribute("Name").value()] =
                        v.attribute("Value") ? v.attribute("Value").value() : "";
                }
            }
        }
    }

    // MapFocus (`ys` attr; absent in seed -> "").
    out.map_focus.clear();
    if (warrior.attribute("MapFocus")) out.map_focus = warrior.attribute("MapFocus").value();

    // Currencies (`pG`: `<Currencies>/<Currency Name Count>`; Count OPEN).
    out.currencies.clear();
    for (pugi::xml_node c : warrior.child("Currencies").children("Currency")) {
        if (c.attribute("Name")) {
            out.currencies[c.attribute("Name").value()] =
                sf2::data::xml_attr_int(c, "Count", 0);
        }
    }

    // Resistances (`Pw`): ATTRS on <Resistances> (`Resistance_2="0"`).
    out.resistances.clear();
    if (pugi::xml_node res = warrior.child("Resistances")) {
        for (pugi::xml_attribute a : res.attributes()) {
            try {
                out.resistances[a.name()] = std::stoi(a.value());
            } catch (const std::exception&) {
            }
        }
    }
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
    pugi::xml_node warrior = find_warrior(root);
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

    // The owned items (JS `$g` + `Aa.save`): replace the <Items> children.
    // The template always has an <Items> element (the Warrior's equipped
    // Body/Head/Fists/NoRanged/NoMagic); append missing items, patch
    // Count/Equipped on the ones already present. Collect the children into
    // a stable vector first — pugixml's range iterator skips every other
    // node when remove_child is called mid-iteration, so a live loop leaks
    // items across saves (accumulating duplicates).
    pugi::xml_node items = warrior.child("Items");
    if (!items) {
        items = warrior.append_child("Items");
    }
    std::vector<pugi::xml_node> old_items;
    for (pugi::xml_node existing : items.children("Item")) {
        old_items.push_back(existing);
    }
    for (const pugi::xml_node& existing : old_items) {
        items.remove_child(existing);
    }
    for (const WarriorSave::OwnedItem& oi : w.items) {
        pugi::xml_node item = items.append_child("Item");
        item.append_attribute("Name").set_value(oi.name.c_str());
        item.append_attribute("Equipped").set_value(oi.equipped ? "1" : "0");
        item.append_attribute("Count").set_value(oi.count);
    }

    // MapFocus (`ys`): get-or-append (absent in the seed).
    {
        pugi::xml_attribute mf = warrior.attribute("MapFocus");
        if (!mf) mf = warrior.append_attribute("MapFocus");
        mf.set_value(w.map_focus.c_str());
    }

    // Battles (`iF`): replace the <Battle Name> children.
    {
        pugi::xml_node battles = warrior.child("Battles");
        if (!battles) battles = warrior.append_child("Battles");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node b : battles.children("Battle")) old.push_back(b);
        for (const pugi::xml_node& b : old) battles.remove_child(b);
        for (const std::string& name : w.battles) {
            battles.append_child("Battle").append_attribute("Name").set_value(name.c_str());
        }
    }

    // Fights (`yc`): replace the <Fight> children.
    {
        pugi::xml_node fights = warrior.child("Fights");
        if (!fights) fights = warrior.append_child("Fights");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node f : fights.children("Fight")) old.push_back(f);
        for (const pugi::xml_node& f : old) fights.remove_child(f);
        for (const WarriorSave::FightWins& fw : w.fights) {
            pugi::xml_node f = fights.append_child("Fight");
            f.append_attribute("Name").set_value(fw.name.c_str());
            f.append_attribute("Wins").set_value(fw.wins);
        }
    }

    // Quests + variables (`kF`/`rv`).
    {
        pugi::xml_node quests = warrior.child("Quests");
        if (!quests) quests = warrior.append_child("Quests");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node q : quests.children("Quest")) old.push_back(q);
        for (const pugi::xml_node& q : old) quests.remove_child(q);
        for (const WarriorSave::QuestState& qs : w.quests) {
            pugi::xml_node q = quests.append_child("Quest");
            q.append_attribute("Name").set_value(qs.name.c_str());
            q.append_attribute("State").set_value(qs.state.c_str());
        }
        pugi::xml_node vars = quests.child("Variables");
        if (!vars) vars = quests.append_child("Variables");
        std::vector<pugi::xml_node> old_vars;
        for (pugi::xml_node v : vars.children("Variable")) old_vars.push_back(v);
        for (const pugi::xml_node& v : old_vars) vars.remove_child(v);
        for (const auto& kv : w.variables) {
            pugi::xml_node v = vars.append_child("Variable");
            v.append_attribute("Name").set_value(kv.first.c_str());
            v.append_attribute("Value").set_value(kv.second.c_str());
        }
    }

    // Currencies (`pG`).
    {
        pugi::xml_node cur = warrior.child("Currencies");
        if (!cur) cur = warrior.append_child("Currencies");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node c : cur.children("Currency")) old.push_back(c);
        for (const pugi::xml_node& c : old) cur.remove_child(c);
        for (const auto& kv : w.currencies) {
            pugi::xml_node c = cur.append_child("Currency");
            c.append_attribute("Name").set_value(kv.first.c_str());
            c.append_attribute("Count").set_value(kv.second);
        }
    }

    // Resistances (`Pw`): attributes on <Resistances>.
    {
        pugi::xml_node res = warrior.child("Resistances");
        if (!res) res = warrior.append_child("Resistances");
        for (const auto& kv : w.resistances) {
            pugi::xml_attribute a = res.attribute(kv.first.c_str());
            if (!a) a = res.append_attribute(kv.first.c_str());
            a.set_value(kv.second);
        }
    }

    std::ostringstream oss;
    doc.save(oss, "\t", pugi::format_default, pugi::encoding_auto);
    write_file_text(save_path_, oss.str());
}

std::string SaveSystem::envelope_decode(const std::string& envelope_text) {
    // `Aa.load` (L70-71): base64 -> un-zstd (`kb.f3`) -> XML text.
    // Leading/trailing whitespace is tolerated (shells add newlines).
    std::string b64;
    for (char c : envelope_text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') b64.push_back(c);
    }
    const std::vector<std::uint8_t> compressed = sf2::data::base64_decode(b64);
    const std::vector<std::uint8_t> xml =
        sf2::data::zstd_decompress(compressed.data(), compressed.size());
    return std::string(xml.begin(), xml.end());
}

std::string SaveSystem::export_sf2(const std::string& users_xml,
                                   const std::string& packs_xml,
                                   const std::string& flags_json) {
    // `Aa.Ddb/Dpb` (L71-73): `.sf2` = `"SF2" + base64(users+packs+flags)`.
    // The exact concatenation separator is OPEN (no captured .sf2 sample);
    // plain concatenation is used.
    const std::string payload = users_xml + packs_xml + flags_json;
    const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
    return "SF2" + sf2::data::base64_encode(bytes);
}

} // namespace sf2::app
