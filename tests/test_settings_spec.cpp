// tests/test_settings_spec.cpp
//
// Specification test for user settings and localization.
//
// Source of truth: assets/userSettings.xml, which the original reads on start.
// It defines far more than a settings screen: sound/music levels, the language
// file, the starting loadout, the sell-back rate, the per-style reward table
// and the critical-hit constants.
//
// Written against the shipped data, so the original satisfies it by
// construction. Failures name settings reSF2 never reads.
//
// Known problem this exposes: the engine hardcodes load_localization("rus")
// (game_clean.hpp), while userSettings.xml selects
// <Language Value="assets/localizations/eng.xml" Number="0"/>. So the shipped
// default language is ignored, and there is no way for a settings screen to
// change it.

#include "../engine/format/xml_doc.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace resf2::format;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static std::string find_one(const char* const* paths, int n) {
    for (int i = 0; i < n; ++i)
        if (std::filesystem::exists(paths[i])) return paths[i];
    return {};
}

int main() {
    std::printf("=== settings + localization specification ===\n");

    static const char* kSettings[] = {
        "assets/files/assets/userSettings.xml",
        "assets/userSettings.xml",
        "assets/assets/userSettings.xml",
    };
    const auto spath = find_one(kSettings, 3);
    if (spath.empty()) {
        std::fprintf(stderr, "userSettings.xml not found; run from repo root\n");
        return 1;
    }
    const auto xml = read_file(spath);
    std::printf("loaded %s (%zu bytes)\n", spath.c_str(), xml.size());

    XmlDocument doc;
    CHECK(doc.parse(xml), "userSettings.xml parses");
    const XmlNode* root = doc.root();
    if (root && root->name == "#document") {
        if (const XmlNode* s = root->first_child("Settings")) root = s;
    }
    if (!root) { std::fprintf(stderr, "no <Settings>\n"); return 1; }

    // ---- audio ----
    std::printf("\n-- audio --\n");
    const XmlNode* sounds = root->first_child("Sounds");
    CHECK(sounds != nullptr, "<Sounds> section is present");
    if (sounds) {
        const XmlNode* snd = sounds->first_child("Sound");
        const XmlNode* mus = sounds->first_child("Music");
        CHECK(snd != nullptr, "<Sound Value= Mute=> defines the SFX channel");
        CHECK(mus != nullptr, "<Music Value= Mute=> defines the music channel");
        if (snd) {
            CHECK(!snd->attr("Value").empty(),
                  "Sound has a Value (a 0..1 level, not a boolean)");
            CHECK(!snd->attr("Mute").empty(),
                  "Sound has a separate Mute flag -- level and mute are independent, so muting must not lose the level");
        }
    }

    // ---- language ----
    std::printf("\n-- language --\n");
    const XmlNode* lang = root->first_child("Language");
    CHECK(lang != nullptr, "<Language> selects the localization file");
    if (lang) {
        const auto value = lang->attr("Value");
        std::printf("  Language Value=\"%s\" Number=\"%s\"\n",
                    value.c_str(), lang->attr("Number").c_str());
        CHECK(value.find("localizations/") != std::string::npos,
              "Language names a file under assets/localizations/");
        CHECK(value.find("eng.xml") != std::string::npos,
              "the shipped default language is ENGLISH -- the engine hardcodes \"rus\" and so ignores this setting");
        CHECK(!lang->attr("Number").empty(),
              "Language carries a Number, the index a settings screen cycles");
    }

    // ---- the language files that must all be selectable ----
    std::printf("\n-- available localizations --\n");
    std::string locdir;
    for (const char* d : {"assets/localizations", "assets/assets/localizations",
                          "assets/files/assets/localizations"}) {
        if (std::filesystem::is_directory(d)) { locdir = d; break; }
    }
    CHECK(!locdir.empty(), "the localizations directory exists");
    std::size_t langs = 0, total_words = 0;
    if (!locdir.empty()) {
        for (const auto& e : std::filesystem::directory_iterator(locdir)) {
            if (e.path().extension() != ".xml") continue;
            ++langs;
            const auto body = read_file(e.path().string());
            std::size_t pos = 0, words = 0;
            while ((pos = body.find("<Word Title=\"", pos)) != std::string::npos) {
                ++words; pos += 13;
            }
            total_words += words;
        }
    }
    std::printf("  %zu language files, %zu localized strings in total\n",
                langs, total_words);
    CHECK(langs >= 11, "11+ languages ship and each must be selectable");
    CHECK(total_words > 20000, "the localization tables are substantial");

    // ---- starting loadout ----
    std::printf("\n-- config / starting loadout --\n");
    const XmlNode* cfg = root->first_child("Config");
    CHECK(cfg != nullptr, "<Config> holds the new-game state");
    if (cfg) {
        for (const char* k : {"Avatar", "Skeleton", "Location", "Armor",
                              "Helmet", "Weapon", "TutorialWeapon",
                              "TutorialBoss"}) {
            CHECK(cfg->first_child(k) != nullptr,
                  (std::string("<") + k + "> is defined for a new game").c_str());
        }
        const XmlNode* sell = cfg->first_child("SellItems");
        CHECK(sell != nullptr && !sell->attr("Value").empty(),
              "SellItems gives the sell-back rate (0.1 = 10% of price)");
        const XmlNode* loss = cfg->first_child("LossReward");
        CHECK(loss != nullptr,
              "LossReward defines what a defeat still pays out");
        const XmlNode* crit_cfg = cfg->first_child("Critical");
        CHECK(crit_cfg != nullptr, "Config also embeds critical constants");
    }

    // ---- reward table ----
    std::printf("\n-- rewards --\n");
    const XmlNode* rp = cfg ? cfg->first_child("RewardsPrize") : nullptr;
    CHECK(rp != nullptr, "<RewardsPrize> is the end-of-fight bonus table");
    if (rp) {
        for (const char* k : {"Perfect", "FirstStrike", "ComboCount", "HeadShot"})
            CHECK(rp->first_child(k) != nullptr,
                  (std::string("bonus '") + k + "' is defined").c_str());
        CHECK(rp->first_child("Styles") != nullptr,
              "style rewards are grouped under <Styles>");
        const XmlNode* styles = rp->first_child("Styles");
        CHECK(styles != nullptr, "<Styles> pays a bonus per fighting style");
        if (styles) {
            std::set<std::string> names;
            for (const auto& c : styles->children) names.insert(c.name);
            std::printf("  styles: %zu\n", names.size());
            for (const char* s : {"Turtle", "Hard", "Brutal", "Agressive",
                                  "Crazy", "Fantastic"})
                CHECK(names.count(s) > 0,
                      (std::string("style '") + s + "' has a reward value").c_str());
        }
    }

    // ---- combat constants ----
    std::printf("\n-- combat constants --\n");
    const XmlNode* bd = cfg ? cfg->first_child("BlockDamage") : nullptr;
    CHECK(bd != nullptr && !bd->attr("Value").empty(),
          "BlockDamage is the fraction that leaks through a block");
    const XmlNode* crit = cfg ? cfg->first_child("Critical") : nullptr;
    CHECK(crit != nullptr, "<Critical> holds the critical-hit constants");
    if (crit) {
        for (const char* k : {"CriticalDamage", "CriticalProbality",
                              "CriticalPauseTime", "CriticalEffectTime",
                              "CriticalAmplitudeX", "CriticalFrequencyX",
                              "CriticalAmplitudeY", "CriticalFrequencyY"})
            CHECK(crit->first_child(k) != nullptr,
                  (std::string("critical constant '") + k + "' is defined").c_str());
    }

    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    if (failed) {
        std::printf("\nFailures name settings the original honours and reSF2\n"
                    "does not read; see PORT_PLAN.md.\n");
    }
    return failed == 0 ? 0 : 1;
}
