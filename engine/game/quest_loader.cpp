// engine/game/quest_loader.cpp
//
// quests.xml parser — see quest_loader.hpp for the design notes. The XML
// walk mirrors the other boot parsers (boot_configs.cpp): the engine's own
// XmlDocument over the real shipped file.

#include "quest_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "engine/format/xml_doc.hpp"

namespace resf2::game {

namespace {

std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Resolve a config file: <root>/<name>, <root>/assets/<name>. Returns "" if
// absent everywhere.
std::string resolve(const std::string& asset_root, const std::string& name) {
    std::error_code ec;
    for (const auto& base : {asset_root, asset_root + "/assets"}) {
        const std::string p = base + "/" + name;
        if (std::filesystem::exists(p, ec)) return p;
    }
    return "";
}

// Map an action element name to its ActionType value (FUN_101c3db0 table).
int action_type_of(const std::string& tag) {
    if (tag == "Dialog")          return static_cast<int>(quest::ActionType::Dialog);
    if (tag == "ActScreen")       return static_cast<int>(quest::ActionType::ActScreen);
    if (tag == "ShowBattle")      return static_cast<int>(quest::ActionType::ShowBattle);
    if (tag == "HideBattle")      return static_cast<int>(quest::ActionType::HideBattle);
    if (tag == "Checkpoint")      return static_cast<int>(quest::ActionType::Checkpoint);
    if (tag == "SetVariable")     return static_cast<int>(quest::ActionType::SetVariable);
    if (tag == "LevelUpDialog")   return static_cast<int>(quest::ActionType::LevelUpDialog);
    if (tag == "OpenZone")        return static_cast<int>(quest::ActionType::OpenZone);
    if (tag == "OpenShop")        return static_cast<int>(quest::ActionType::OpenShop);
    if (tag == "GiveItem")        return static_cast<int>(quest::ActionType::GiveItem);
    if (tag == "GiveCurrency")    return static_cast<int>(quest::ActionType::GiveCurrency);
    if (tag == "SetMapFocus")     return static_cast<int>(quest::ActionType::SetMapFocus);
    if (tag == "SetCurrentZone")  return static_cast<int>(quest::ActionType::SetCurrentZone);
    if (tag == "BuyItem")         return static_cast<int>(quest::ActionType::BuyItem);
    // "Activate" (run another quest by ActionID), "ToggleItems",
    // "AttachQuestFile", "SetShopCategory", ... — parsed but unhandled by
    // the engine; the dispatcher logs them.
    return static_cast<int>(quest::ActionType::Unknown);
}

// Parse one <Quest> (or nested sibling <Quest>) element. `out` receives the
// record; nested <Quest> children are walked by the caller's stack.
void parse_quest(const resf2::format::XmlNode& node, QuestDef& out) {
    out.name = node.attr("Name");
    if (const auto* events = node.first_child("Events")) {
        for (const auto& ev : events->children)
            if (!ev.name.empty()) out.events.push_back(ev.name);
    }
    if (const auto* conds = node.first_child("Conditions")) {
        for (const auto& c : conds->children) {
            if (c.name.empty()) continue;
            QuestCondition qc;
            qc.op = c.name;
            qc.v1 = c.attr("Value1");
            qc.v2 = c.attr("Value2");
            qc.negate = (c.attr("Not") == "1");
            out.conditions.push_back(std::move(qc));
        }
    }
    if (const auto* actions = node.first_child("Actions")) {
        for (const auto& a : actions->children) {
            if (a.name.empty()) continue;
            quest::QuestAction qa;
            qa.type = action_type_of(a.name);
            for (const auto& attr : a.attributes)
                qa.attributes[attr.name] = attr.value;
            // [ORIGINAL] <ShowBattle Name=..> / <OpenZone Name=..> /
            // <SetVariable Name=..> / <GiveItem Name=..>: the "Name"
            // attribute is the action target. <Dialog Title=..> targets the
            // speaker title key.
            qa.target = a.attr("Name", a.attr("Title"));
            if (qa.type == static_cast<int>(quest::ActionType::Dialog)) {
                // <Dialog> carries its lines as nested <Line Text=..> keys;
                // the Dialogue scene localizes them on render.
                for (const auto& ln : a.children)
                    if (ln.name == "Line" && !ln.attr("Text").empty())
                        qa.dialog_lines.push_back(ln.attr("Text"));
            }
            out.actions.push_back(std::move(qa));
        }
    }
}

}  // namespace

bool load_quest_defs(const std::string& asset_root, std::vector<QuestDef>& out) {
    const std::string p = resolve(asset_root, "quests.xml");
    if (p.empty()) return false;
    const std::string xml = read_text(p);
    if (xml.empty()) return false;

    resf2::format::XmlDocument doc;
    if (!doc.parse(xml)) return false;
    const auto* root = doc.root();
    if (!root) return false;
    const auto* quests_node = root->first_child("Quests");
    if (!quests_node) return false;

    // [Wave 8] 6 of the 498 <Quest> elements are nested inside sibling
    // <Quest> elements (conditional chains) — walk the whole subtree.
    out.clear();
    std::vector<const resf2::format::XmlNode*> stack{quests_node};
    while (!stack.empty()) {
        const auto* n = stack.back();
        stack.pop_back();
        for (const auto& child : n->children) {
            if (child.name == "Quest") {
                QuestDef def;
                parse_quest(child, def);
                if (!def.name.empty()) out.push_back(std::move(def));
            }
            stack.push_back(&child);
        }
    }
    std::printf("[QUEST] loaded %zu quest defs from %s\n", out.size(), p.c_str());
    return true;
}

}  // namespace resf2::game
