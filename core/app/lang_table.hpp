#pragma once

// Runtime lang lookup (visible shell, Phase 7 round 3) — replaces the
// hardcoded EN Sensei lines with the real table, falling back to embedded
// EN when the file/string is missing.
//
// Source (read-only): `reference/www/res/lang/en.af2d6604.xml`
// (`<Localization><Words><Word Title="tutorial_move">Let me see...</Word>`
// — single-line hashed XML). Lookup is by `Title` attribute; the hashed
// `en.*.xml` path is resolved by the shell (prefix scan of `<res_root>/lang`,
// the same pattern as the controller-atlas loader) and handed to
// `lang_table_load()` once. Missing file/keys are silent (headless-safe);
// `lang_text()` falls back to the caller-supplied EN.
//
// JS cites: lang asset `lang/{lang}.xml` loaded at Preloader (JS_FLOW.md §9,
// `Rg.load` L1967); quest globals like `NotificationTextMove` resolve into
// these titles (FLOW_STATIC.md §1, `tutorial_quests.xml` chain).

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "xml_doc.hpp"

namespace sf2::app {

// The shared Title -> text cache (one res_root per process run).
inline std::unordered_map<std::string, std::string>& lang_cache(
    const std::string& res_root) {
    static std::unordered_map<std::string, std::string> cached;
    static std::string loaded_root;
    if (loaded_root != res_root) {
        loaded_root = res_root;
        cached.clear();
    }
    return cached;
}

// Loads one resolved lang file into the cache (called once by the shell;
// `path` = the hashed `en.<hash>.xml`). Never throws.
inline void lang_table_load(const std::string& res_root, const std::string& path) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        if (data.empty()) return;
        sf2::data::xml_doc doc;
        doc.parse(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        const pugi::xml_node root = doc.root().first_child();
        if (!root) return;
        // Walk the tree; every <Word Title="k">text</Word> is an entry (the
        // file nests Words under Localization — depth varies, single line).
        std::unordered_map<std::string, std::string>& out = lang_cache(res_root);
        std::vector<pugi::xml_node> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const pugi::xml_node cur = stack.back();
            stack.pop_back();
            for (pugi::xml_node ch = cur.first_child(); ch; ch = ch.next_sibling()) {
                if (std::string(ch.name()) == "Word" && !ch.attribute("Title").empty()) {
                    const std::string key = ch.attribute("Title").value();
                    if (out.find(key) == out.end()) out[key] = ch.child_value();
                }
                stack.push_back(ch);
            }
        }
    } catch (const std::exception&) {
    }
}

// Looks up `key`, returning `fallback` when the table/file lacks it.
inline std::string lang_text(const std::string& res_root, const std::string& key,
                             const std::string& fallback) {
    const auto& table = lang_cache(res_root);
    const auto it = table.find(key);
    if (it == table.end() || it->second.empty()) return fallback;
    return it->second;
}

} // namespace sf2::app
