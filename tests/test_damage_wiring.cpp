// tests/test_damage_wiring.cpp
//
// Unit test for the GAP-3 damage wiring: equipment -> AttributeSet
// aggregation feeding Model::getTotalDamage (game+0x4527B4).
//
// Pins the behaviour of engine/game/attribute_aggregation.hpp:
//   * empty inventory -> every tracked attribute present and 0
//     (reset_to_zero() seeds the full set, so the damage path NEVER sees
//     the -1e35 getParameter sentinel -- feeding it into powf(2, x/10)
//     underflows to 0 and would silently zero all damage)
//   * equipped items contribute additively, summed across slots, including
//     UnarmedDamage (which is NOT part of the golden-pinned
//     add_item_contribution() signature and goes through add() separately)
//   * the enemy baseline equals <AlignTargetAttributes> exactly
//   * attribute_difference() between the aggregated sets feeds
//     attribute_difference_factor() -- the exponential curve the old linear
//     placeholder never produced
//
// Harness style mirrors tests/test_attributes_golden.cpp.

#include "../engine/game/attribute_aggregation.hpp"
#include "../engine/game/damage_formula.hpp"
#include "../engine/format/list_parser.hpp"
#include "../engine/game/inventory.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace resf2::game;
namespace fmt = resf2::format;
namespace inventory = resf2::inventory;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static bool near_eq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Add an item to the catalog, own it, and equip it in the given slot.
static void equip(fmt::ListData& catalog, inventory::Inventory& inv,
                  const fmt::ListItem& item, const char* slot) {
    catalog.items.push_back(item);
    inv.add_item(item.name);
    inv.equip(slot, item.name);
}

static fmt::ListItem make_item(const char* name, float weapon, float unarmed,
                               float body, float head, float ranged, float magic) {
    fmt::ListItem item;
    item.name = name;
    item.weapon_damage = weapon;
    item.unarmed_damage = unarmed;
    item.body_defense = body;
    item.head_defense = head;
    item.ranged_damage = ranged;
    item.magic_damage = magic;
    return item;
}

static void test_empty_inventory_is_all_zero() {
    std::printf("\n-- empty inventory -> all seven attributes present, all 0 --\n");
    fmt::ListData catalog;
    inventory::Inventory inv;  // nothing owned, nothing equipped

    const AttributeSet attrs = aggregate_equipment_attributes(catalog, inv);
    CHECK(attrs.size() == attribute_names().size(),
          "every tracked attribute is present after aggregation");
    for (const auto& n : attribute_names()) {
        CHECK(!attribute_is_missing(attrs.get(n)),
              (n + " never reports the -1e35 sentinel").c_str());
        CHECK(near_eq(attrs.get(n), 0.0f), (n + " is seeded to 0").c_str());
    }
}

static void test_equipped_items_sum_into_attributes() {
    std::printf("\n-- weapon + armor + helm equipped -> summed values --\n");
    fmt::ListData catalog;
    inventory::Inventory inv;
    equip(catalog, inv, make_item("Sword", 22, 0, 0, 0, 0, 0), inventory::kSlotWeapon);
    equip(catalog, inv, make_item("Armor", 0, 0, 8, 0, 0, 0), inventory::kSlotArmor);
    equip(catalog, inv, make_item("Helm", 0, 0, 0, 4, 0, 0), inventory::kSlotHelmet);

    const AttributeSet attrs = aggregate_equipment_attributes(catalog, inv);
    CHECK(attrs.raw("WeaponDamage") == 22, "WeaponDamage 22 from the sword");
    CHECK(attrs.raw("UnarmedDamage") == 0, "UnarmedDamage 0 (sword carries none)");
    CHECK(attrs.raw("BodyDefense") == 8, "BodyDefense 8 from the armor");
    CHECK(attrs.raw("HeadDefense") == 4, "HeadDefense 4 from the helm");
    CHECK(attrs.raw("RangedDamage") == 0, "RangedDamage untouched at 0");
    CHECK(attrs.raw("MagicDamage") == 0, "MagicDamage untouched at 0");
}

static void test_same_attribute_stacks_across_slots() {
    std::printf("\n-- two contributors to the same attribute stack --\n");
    fmt::ListData catalog;
    inventory::Inventory inv;
    // UnarmedDamage goes through add() (NOT add_item_contribution -- that
    // signature is golden-pinned), so stacking it across slots proves the
    // separate add path works. Ranged/Magic slots cover the remaining
    // add_item_contribution parameters.
    equip(catalog, inv, make_item("Gloves", 0, 3, 0, 0, 0, 0), inventory::kSlotArmor);
    equip(catalog, inv, make_item("Band", 0, 2, 0, 0, 0, 0), inventory::kSlotHelmet);
    equip(catalog, inv, make_item("Bow", 0, 0, 0, 0, 7, 0), inventory::kSlotRanged);
    equip(catalog, inv, make_item("Wand", 0, 0, 0, 0, 0, 9), inventory::kSlotMagic);

    const AttributeSet attrs = aggregate_equipment_attributes(catalog, inv);
    CHECK(attrs.raw("UnarmedDamage") == 5, "UnarmedDamage 3 + 2 stacks to 5");
    CHECK(attrs.raw("RangedDamage") == 7, "RangedDamage 7 from the bow");
    CHECK(attrs.raw("MagicDamage") == 9, "MagicDamage 9 from the wand");
}

static void test_enemy_baseline_matches_align_targets() {
    std::printf("\n-- enemy baseline equals <AlignTargetAttributes> exactly --\n");
    const AttributeSet enemy = seed_enemy_baseline_attributes();
    for (const auto& t : align_target_attributes()) {
        CHECK(near_eq(enemy.get_or(t.name, -1.0f), static_cast<float>(t.value)),
              (std::string(t.name) + " matches the alignment target").c_str());
    }
    // Values verified against internalSettings.xml (see attributes.hpp).
    CHECK(enemy.raw("WeaponDamage") == 12, "baseline WeaponDamage 12");
    CHECK(enemy.raw("UnarmedDamage") == 0, "baseline UnarmedDamage 0");
    CHECK(enemy.raw("BodyDefense") == 12, "baseline BodyDefense 12");
    CHECK(enemy.raw("HeadDefense") == 5, "baseline HeadDefense 5");
    CHECK(enemy.raw("RangedDamage") == 12, "baseline RangedDamage 12");
    CHECK(enemy.raw("MagicDamage") == 12, "baseline MagicDamage 12");
    CHECK(enemy.raw("EnchantmentResistance") == 12,
          "baseline EnchantmentResistance 12 (align target, not a tracer name)");
    CHECK(enemy.raw("RangedQuantity") == 0,
          "RangedQuantity stays 0 (not an align target)");
}

static void test_difference_feeds_curve_without_sentinel() {
    std::printf("\n-- aggregated sets -> difference -> doubling-range curve --\n");
    fmt::ListData catalog;
    inventory::Inventory inv;
    equip(catalog, inv, make_item("Sword", 22, 0, 0, 0, 0, 0), inventory::kSlotWeapon);

    const AttributeSet player = aggregate_equipment_attributes(catalog, inv);
    const AttributeSet enemy = seed_enemy_baseline_attributes();

    // The sentinel must NEVER reach powf: every tracked attribute on both
    // fighters is a real number after aggregation/seeding.
    for (const auto& n : attribute_names()) {
        CHECK(!attribute_is_missing(player.get(n)),
              ("player " + n + " is a real value, not the sentinel").c_str());
        CHECK(!attribute_is_missing(enemy.get(n)),
              ("enemy " + n + " is a real value, not the sentinel").c_str());
    }

    // Sword 22 vs baseline BodyDefense 12: +10 = one doubling range = x2.
    float d = attribute_difference(player, "WeaponDamage", enemy, "BodyDefense");
    CHECK(near_eq(d, 10.0f), "WeaponDamage advantage is 22 - 12 = 10");
    CHECK(near_eq(attribute_difference_factor(d), 2.0f),
          "ten points ahead DOUBLES damage (game+0x60E794)");

    // Unarmed 0 vs baseline BodyDefense 12: -12 -> 2^(-1.2) ~= 0.4353.
    d = attribute_difference(player, "UnarmedDamage", enemy, "BodyDefense");
    CHECK(near_eq(d, -12.0f), "unarmed disadvantage is 0 - 12 = -12");
    CHECK(near_eq(attribute_difference_factor(d), 0.4353f, 1e-3f),
          "unarmed vs armored baseline roughly HALVES damage");

    // The factor is a real multiplier, never the underflowed-sentinel 0.
    CHECK(attribute_difference_factor(d) > 0.0f,
          "difference factor never collapses to 0 (sentinel would do that)");
}

int main() {
    std::printf("=== damage wiring test (equipment -> AttributeSet -> getTotalDamage) ===\n");
    test_empty_inventory_is_all_zero();
    test_equipped_items_sum_into_attributes();
    test_same_attribute_stacks_across_slots();
    test_enemy_baseline_matches_align_targets();
    test_difference_feeds_curve_without_sentinel();
    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
