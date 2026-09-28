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

// The per-language Title -> text caches (one `lang` per `res_root`). Keyed by
// BOTH `res_root` and the language: the settings dialog re-localizes every
// string to the DISPLAYED language (`un.Pj(key) = bf(bf(IVa,key),$u)`, class
// `Pb` `static bf(a,b){try{return a[b]}catch(c){return null}}` off 0x5760;
// `Pj(a){...}` off 0xf3364) while `G.Rq()` still points at the committed one,
// so the two tables must coexist. The JS `IVa` table ships embedded for all
// ten `iv` languages (off 0xf1261); the port's equivalent is the per-language
// `lang/<lang>.<hash>.xml` Words table, cached per language.
inline std::unordered_map<std::string, std::string>& lang_cache(
    const std::string& res_root, const std::string& lang) {
    static std::unordered_map<std::string,
                              std::unordered_map<std::string, std::string>> caches;
    return caches[res_root + "\n" + (lang.empty() ? "en" : lang)];
}

// Loads one resolved `<lang>.<hash>.xml` lang file into ITS language cache
// (called once per language by the shell). Never throws.
inline void lang_table_load(const std::string& res_root, const std::string& lang,
                            const std::string& path) {
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
        std::unordered_map<std::string, std::string>& out = lang_cache(res_root, lang);
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

// Drops one language's cached table so a language switch re-reads its
// `<lang>.<hash>.xml` (the JS reload does `X.clear(this.QU)` before refilling,
// off 0x73586).
inline void lang_cache_clear(const std::string& res_root, const std::string& lang) {
    lang_cache(res_root, lang).clear();
}

// Looks up `key` in the `lang` table, then the EN table (the JS `G.bg` L2394
// EN fallback), returning `fallback` when both lack it.
inline std::string lang_text(const std::string& res_root, const std::string& lang,
                             const std::string& key, const std::string& fallback) {
    const auto& table = lang_cache(res_root, lang);
    const auto it = table.find(key);
    if (it != table.end() && !it->second.empty()) return it->second;
    if (lang != "en") {
        const auto& en = lang_cache(res_root, "en");
        const auto eit = en.find(key);
        if (eit != en.end() && !eit->second.empty()) return eit->second;
    }
    return fallback;
}

// The ACTIVE language for `res_root` — the JS `QU` global the `Y.na` lookup
// reads (`X.clear(this.QU); ... lang/<G.Rq()>.xml ... Words`, off 0x73586).
// `ensure_lang` sets it; `loc`/the settings `Pj` pass their language
// explicitly. Defaults to EN until the shell loads a table.
inline std::string& lang_active(const std::string& res_root) {
    static std::unordered_map<std::string, std::string> active;
    return active[res_root];
}

inline void lang_set_active(const std::string& res_root, const std::string& lang) {
    lang_active(res_root) = lang.empty() ? "en" : lang;
}

// Active-language lookup (the `Y.na` 3-arg path used where the language is not
// threaded through, e.g. `quest_panel`). Falls back to EN when unset.
inline std::string lang_text(const std::string& res_root, const std::string& key,
                             const std::string& fallback) {
    return lang_text(res_root, lang_active(res_root), key, fallback);
}

} // namespace sf2::app
