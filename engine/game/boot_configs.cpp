// engine/game/boot_configs.cpp
//
// Wave 8 — boot-time config parsers (see boot_configs.hpp). Each loader uses
// the engine's own XmlDocument on the REAL shipped files (assets/ is the
// authentic original tree — byte-identical to the device pulls) and exposes
// counts/keys the fidelity tests pin against the device evidence.

#include "boot_configs.hpp"

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

bool parse_forge(const std::string& path, ForgeConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false; const auto* forge_node = root->first_child("Forge"); if (!forge_node) return false;
    bool first = true;
    for (const auto& child : forge_node->children) {
        if (child.name == "AspectScale") {
            for (const auto& a : child.children) {
                if (a.name != "Aspect") continue;
                ++out.aspects;
                if (first) { out.first_aspect_value = std::atoi(a.attr("Value").c_str()); first = false; }
            }
        } else if (child.name == "Recipe") {
            for (const auto& section : child.children)
                if (section.name == "Prices")
                    for (const auto& pr : section.children)
                        if (pr.name == "Price") ++out.price_rows;
        }
    }
    return true;
}

bool parse_perks(const std::string& path, PerksConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false; const auto* perks_node = root->first_child("Perks"); if (!perks_node) return false;
    for (const auto& p : perks_node->children) {
        if (p.name != "Perk") continue;
        ++out.perks;
        if (p.attr("Name") == "PERK_DOUBLE_SWEEP") out.has_double_sweep = true;
    }
    return true;
}

bool parse_achievements(const std::string& path, AchievementsConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false; const auto* ach_node = root->first_child("Achievements"); if (!ach_node) return false;
    bool first = true;
    for (const auto& c : ach_node->children) {
        if (c.name != "Counter") continue;
        ++out.counters;
        if (first) { out.first_counter = c.attr("Name"); first = false; }
        for (const auto& a : c.children)
            if (a.name == "Achievement") ++out.achievements;
    }
    return true;
}

bool parse_character_progress(const std::string& path, CharacterProgressConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false; const auto* progress_node = root->first_child("Progress"); if (!progress_node) return false;
    const auto* thresholds = progress_node->first_child("Thresholds");
    if (!thresholds) return false;
    for (const auto& t : thresholds->children) {
        if (t.name != "Threshold") continue;
        ++out.thresholds;
        const int level = std::atoi(t.attr("Level").c_str());
        const int exp = std::atoi(t.attr("Exp").c_str());
        if (level == 1) out.first_exp = exp;
        if (level > out.max_level) out.max_level = level;
    }
    return true;
}
bool parse_quests(const std::string& path, QuestConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false;
    const auto* quests_node = root->first_child("Quests");
    if (!quests_node) return false;
    // [Wave 8] Count every <Quest> anywhere under <Quests> — 6 of the 504
    // are nested inside sibling <Quest> elements (conditional chains).
    bool first = true;
    std::vector<const resf2::format::XmlNode*> stack{quests_node};
    while (!stack.empty()) {
        const auto* n = stack.back();
        stack.pop_back();
        for (const auto& child : n->children) {
            if (child.name == "Quest") {
                ++out.quests;
                if (first) {
                    out.first_quest = child.attr("Name");
                    out.first_priority = child.attr("Priority");
                    first = false;
                }
            }
            stack.push_back(&child);
        }
    }
    return true;
}
bool parse_config_cdn(const std::string& path, CdnConfig& out) {
    resf2::format::XmlDocument doc;
    if (!doc.load_file(path)) return false;
    const auto* root = doc.root();
    if (!root) return false; const auto* data_node = root->first_child("data"); if (!data_node) return false;
    // [Wave 8] <data> carries 13 sections; <item> rows can sit one level
    // deeper inside section sub-tables — count every <item> descendant.
    std::vector<const resf2::format::XmlNode*> stack{data_node};
    while (!stack.empty()) {
        const auto* n = stack.back();
        stack.pop_back();
        for (const auto& child : n->children) {
            if (child.name == "item") {
                ++out.total_items;
                if (n->name == "platform") {
                    ++out.platform_items;
                    if (child.attr("PlatformID") == "2") out.android_name = child.attr("Name");
                } else if (n->name == "versions") {
                    ++out.version_items;
                }
            }
            stack.push_back(&child);
        }
    }
    return true;
}

}  // namespace

bool load_boot_configs(const std::string& asset_root, BootConfigs& out) {
    out = BootConfigs{};

    // Pre-save boot configs, in the order the original opens them
    // (LIVE_BOOT_TRACE: perks.xml 11.26 -> forge/CharacterProgress/
    // Achievements.xml 12.02-12.55).
    const std::string perks_path = resolve(asset_root, "perks.xml");
    const std::string forge_path = resolve(asset_root, "forge.xml");
    const std::string cprogress_path = resolve(asset_root, "CharacterProgress.xml");
    const std::string achievements_path = resolve(asset_root, "Achievements.xml");

    bool ok = true;
    if (!perks_path.empty()) {
        if (parse_perks(perks_path, out.perks)) out.events.push_back("perks.xml");
        else ok = false;
    }
    if (!forge_path.empty()) {
        if (parse_forge(forge_path, out.forge)) out.events.push_back("forge.xml");
        else ok = false;
    }
    if (!cprogress_path.empty()) {
        if (parse_character_progress(cprogress_path, out.progress))
            out.events.push_back("CharacterProgress.xml");
        else ok = false;
    }
    if (!achievements_path.empty()) {
        if (parse_achievements(achievements_path, out.achievements))
            out.events.push_back("Achievements.xml");
        else ok = false;
    }

    // purchased.xml is runtime-generated — not shipped, not in the device
    // pull. Its absence must never fail the boot (HEURISTIC-TODO: when a
    // copy lands, parse it and flip the flag).
    const std::string purchased_path = resolve(asset_root, "purchased.xml");
    out.purchased_tolerated = purchased_path.empty() || !std::filesystem::exists(purchased_path);
    if (!purchased_path.empty() && std::filesystem::exists(purchased_path)) {
        resf2::format::XmlDocument doc;
        ok = doc.load_file(purchased_path) && ok;
        out.purchased_tolerated = false;
    }

    return ok;
}

// quests.xml and config_cdn.xml load later in the boot (after the save —
// 17.67 s / 18.37 s in the original). The engine loads them from init_location
// and records their events there; these standalone parsers exist so the
// fidelity test can pin the real counts without a full boot.
bool load_quests_config(const std::string& asset_root, QuestConfig& out) {
    const std::string p = resolve(asset_root, "quests.xml");
    if (p.empty()) return false;
    return parse_quests(p, out);
}

bool load_cdn_config(const std::string& asset_root, CdnConfig& out) {
    const std::string p = resolve(asset_root, "config_cdn.xml");
    if (p.empty()) return false;
    return parse_config_cdn(p, out);
}

}  // namespace resf2::game
