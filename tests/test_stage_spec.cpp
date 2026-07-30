// tests/test_stage_spec.cpp
//
// Specification test for the campaign data (assets/stages.xml) -- zones,
// battles, fights, and the enemies inside them.
//
// Written against the shipped data, so the original satisfies it by
// construction. It exercises the ENGINE'S OWN PARSER
// (engine/format/stage_parser.cpp), so a failure means reSF2 is dropping data
// it needs, not that the test is stale.
//
// Known gaps this is designed to expose (see PORT_PLAN.md):
//   * StageWarrior has no equipment: <Warrior> carries <Items><Item Name=..>
//     children (2200 of them) naming the weapon/armour/helm the enemy wears.
//     Without them every opponent fights bare-handed and looks wrong.
//   * StageWarrior has no perks: 544 <Perk> children.
//   * StageWarrior ignores 11 attributes present in the data, including
//     RangedDamage, MagicDamage, EnchantmentResistance, MagicInitialCharge --
//     exactly the inputs the recovered damage formula needs.
//   * StageFight ignores <Rules>, which carry per-fight modifiers such as
//     <NoPerks Name="EndStanceClear"/>.
//   * StageWarrior ignores <AttributesAlign><Delta Factor Shift>, the
//     per-opponent alignment the damage formula's Delta.Factor term reads.

#include "../engine/format/stage_parser.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <cstring>
#include <functional>
#include <string>

using namespace resf2::format;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static std::string find_stages() {
    for (const char* p : {"assets/stages.xml",
                          "assets/files/assets/stages.xml",
                          "assets/assets/stages.xml"}) {
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

int main() {
    std::printf("=== campaign specification (assets/stages.xml) ===\n");

    const auto path = find_stages();
    if (path.empty()) {
        std::fprintf(stderr, "stages.xml not found; run from the repo root\n");
        return 1;
    }

    StageParser parser;
    StageData data;
    const bool ok = parser.load_file(path, data);
    std::printf("loaded %s\n", path.c_str());
    CHECK(ok, "stages.xml loads through the engine's StageParser");
    if (!ok) {
        std::fprintf(stderr, "  parser error: %s\n", parser.error().c_str());
        return 1;
    }

    // ---- zones ----
    std::printf("\n-- zones --\n");
    std::printf("  %zu zones\n", data.zones.size());
    CHECK(data.zones.size() == 8,
          "8 zones: Punchbag + ZONE_1..ZONE_7");
    std::set<std::string> zone_names;
    for (const auto& z : data.zones) zone_names.insert(z.name);
    for (const char* z : {"Punchbag", "ZONE_1", "ZONE_7"})
        CHECK(zone_names.count(z) > 0,
              (std::string("zone '") + z + "' is present").c_str());

    // ---- battles ----
    std::printf("\n-- battles --\n");
    std::size_t battles = 0, with_location = 0, with_icon = 0, with_preview = 0;
    std::set<std::string> types;
    for (const auto& z : data.zones) {
        for (const auto& b : z.battles) {
            ++battles;
            if (!b.location.empty()) ++with_location;
            if (!b.icon.empty()) ++with_icon;
            if (!b.preview.empty()) ++with_preview;
            types.insert(b.type);
        }
    }
    std::printf("  %zu battles, %zu types, location=%zu icon=%zu preview=%zu\n",
                battles, types.size(), with_location, with_icon, with_preview);
    CHECK(battles == 124, "124 battles across all zones");
    CHECK(types.size() == 16,
          "16 distinct battle types (BOSSES, TOURNAMENT, SURVIVAL, ...)");
    // The map cannot draw a node without its art.
    CHECK(with_icon >= 106, "106 battles carry an Icon for the map node");
    CHECK(with_preview >= 106, "106 battles carry a Preview image");
    CHECK(with_location >= 118, "118 battles name the location to fight in");
    for (const char* t : {"BOSSES", "TOURNAMENT", "SURVIVAL", "CHALLENGE",
                          "FINAL_BATTLE", "TUTORIAL", "DUMMY"})
        CHECK(types.count(t) > 0,
              (std::string("battle type '") + t + "' is represented").c_str());

    // ---- fights and warriors ----
    std::printf("\n-- fights / warriors --\n");
    std::size_t fights = 0, warriors = 0;
    std::size_t with_template = 0, with_tactic = 0, with_power = 0;
    for (const auto& z : data.zones)
        for (const auto& b : z.battles)
            for (const auto& f : b.fights) {
                ++fights;
                for (const auto& w : f.warriors) {
                    ++warriors;
                    if (!w.template_name.empty()) ++with_template;
                    if (!w.tactic.empty()) ++with_tactic;
                    if (w.warrior_power > 0.0f) ++with_power;
                }
            }
    std::printf("  %zu fights, %zu warriors (template=%zu tactic=%zu power=%zu)\n",
                fights, warriors, with_template, with_tactic, with_power);
    CHECK(fights >= 700, "the campaign has 700+ fights");
    CHECK(warriors >= 650, "those fights define 650+ opponents");
    CHECK(with_template >= 600,
          "opponents reference a Template that supplies their base build");
    CHECK(with_tactic >= 700,
          "769 opponents name a Tactic, which selects their AI profile");

    // ---- the gaps ----
    // These read fields StageWarrior/StageFight do not have yet. They are
    // written as data-presence checks against the raw XML so the test states
    // what must be parsed, and they fail loudly while the structs lack it.
    std::printf("\n-- data the parser must not drop --\n");
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    const std::string xml = ss.str();

    auto count = [&](const char* needle) {
        std::size_t n = 0, pos = 0;
        const std::size_t len = std::strlen(needle);
        while ((pos = xml.find(needle, pos)) != std::string::npos) { ++n; pos += len; }
        return n;
    };

    const auto items_in_xml = count("<Item Name=");
    const auto perks_in_xml = count("<Perk ");
    const auto align_in_xml = count("<AttributesAlign>");
    const auto rules_in_xml = count("<Rules>");
    std::printf("  XML has: Item=%zu Perk=%zu AttributesAlign=%zu Rules=%zu\n",
                items_in_xml, perks_in_xml, align_in_xml, rules_in_xml);

    // Equipment: 2200 <Item> children name each opponent's gear.
    std::size_t parsed_items = 0, parsed_perks = 0, parsed_align = 0, parsed_rules = 0;
    std::size_t all_warriors = 0;
    // Warriors nest (a wave/variant is a child <Warrior>), so counting only the
    // top level under-reports the roster by about a third.
    std::function<void(const StageWarrior&)> visit =
        [&](const StageWarrior& w) {
            ++all_warriors;
            parsed_items += w.items.size();
            parsed_perks += w.perks.size();
            parsed_align += w.attributes_align.size();
            for (const auto& v : w.variants) visit(v);
        };
    for (const auto& z : data.zones)
        for (const auto& b : z.battles)
            for (const auto& f : b.fights) {
                parsed_rules += f.rules.size();
                for (const auto& w : f.warriors) visit(w);
            }
    for (const auto& g : data.warrior_groups)
        for (const auto& w : g.warriors) visit(w);
    std::printf("  warriors reachable (incl. nested + groups): %zu\n", all_warriors);
    std::printf("  templates=%zu warrior_groups=%zu\n",
                data.templates.size(), data.warrior_groups.size());
    CHECK(all_warriors >= 1200,
          "1227 <Warrior> elements exist; nested variants and pool members "
          "must all be reachable");
    CHECK(data.warrior_groups.size() >= 5,
          "<WarriorGroups> pools are parsed -- SURVIVAL/TOURNAMENT draw from them");
    // The template table is what gives an opponent its look and gear: 722
    // warriors reference one by name.
    CHECK(data.templates.size() >= 190,
          "191 <Template> builds are parsed -- opponents reference them by name");
    std::size_t tpl_with_avatar = 0, tpl_with_items = 0, tpl_with_parent = 0;
    for (const auto& t : data.templates) {
        if (!t.avatar.empty()) ++tpl_with_avatar;
        if (!t.items.empty()) ++tpl_with_items;
        if (!t.parent.empty()) ++tpl_with_parent;
    }
    std::printf("  templates: avatar=%zu items=%zu inherits=%zu\n",
                tpl_with_avatar, tpl_with_items, tpl_with_parent);
    CHECK(tpl_with_avatar >= 190,
          "every template names an Avatar texture (opponent portraits)");
    CHECK(tpl_with_parent >= 185,
          "templates inherit via Template=\"...\" and must be resolved");
    CHECK(tpl_with_items >= 100,
          "templates carry <Items> -- the weapon/armour/helm an opponent wears");
    std::printf("  parser kept: Item=%zu Perk=%zu AttributesAlign=%zu Rules=%zu\n",
                parsed_items, parsed_perks, parsed_align, parsed_rules);

    CHECK(parsed_items >= 1000,
          "opponent EQUIPMENT (<Items><Item Name=..>) is parsed -- without it "
          "every enemy fights bare-handed");
    CHECK(parsed_perks >= 400,
          "opponent PERKS (<Perk>) are parsed");
    CHECK(parsed_align >= 150,
          "<AttributesAlign><Delta Factor Shift> is parsed -- the damage "
          "formula's Delta.Factor term reads it");
    CHECK(parsed_rules >= 400,
          "per-fight <Rules> are parsed (e.g. <NoPerks Name=\"EndStanceClear\"/>)");

    // Attributes the damage formula needs, present in the data.
    std::printf("\n-- warrior attributes the damage formula needs --\n");
    std::size_t ranged = 0, magic = 0, ench = 0, charge = 0;
    std::function<void(const StageWarrior&)> visit_attrs =
        [&](const StageWarrior& w) {
            if (w.ranged_damage != 0.0f) ++ranged;
            if (w.magic_damage != 0.0f) ++magic;
            if (w.enchantment_resistance != 0.0f) ++ench;
            if (w.magic_initial_charge != 0.0f) ++charge;
            for (const auto& v : w.variants) visit_attrs(v);
        };
    for (const auto& z : data.zones)
        for (const auto& b : z.battles)
            for (const auto& f : b.fights)
                for (const auto& w : f.warriors) visit_attrs(w);
    for (const auto& g : data.warrior_groups)
        for (const auto& w : g.warriors) visit_attrs(w);
    std::printf("  parsed: RangedDamage=%zu MagicDamage=%zu "
                "EnchantmentResistance=%zu MagicInitialCharge=%zu\n",
                ranged, magic, ench, charge);
    // These are exactly the inputs of the recovered damage formula
    // (Model::getTotalDamage @ game+0x4527B4). Dropping them means ranged and
    // magic opponents cannot be simulated correctly at all.
    CHECK(ranged >= 15, "RangedDamage is parsed (18 warriors set it)");
    CHECK(magic >= 15, "MagicDamage is parsed (17 warriors set it)");
    CHECK(ench >= 80, "EnchantmentResistance is parsed (89 warriors set it)");
    CHECK(charge >= 60, "MagicInitialCharge is parsed (64 warriors set it)");

    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    if (failed) {
        std::printf("\nFailures name campaign data the original uses and reSF2\n"
                    "currently discards; see PORT_PLAN.md.\n");
    }
    return failed == 0 ? 0 : 1;
}
