// tests/test_attributes_golden.cpp
//
// Golden test for the character attribute system,
// [ORIGINAL] Model::getParameter @ game+0x6275F4.
//
// Provenance: reverse/analysis/PORT_GAPS.md, section "Character attributes".
// The getter decompiles to:
//
//     float getParameter(Model* self, const std::string* name) {
//         int value = 0;
//         if (map_lookup(self + 0x1C4, name, &value, 1, 0))
//             return (float)value;              // vcvt.f32.s32
//         warn("Parameter \"%s\" not found!");
//         return -1e35f;                        // literal at 0x8F67E658
//     }
//
// The three behaviours pinned here are the ones easy to get wrong:
//   * values are INTEGERS converted to float on read
//   * a miss returns the sentinel -1e35, NOT 0
//   * the alignment-table lookup (game+0x60DF98) is a DIFFERENT lookup that
//     defaults to 0.0 on a miss -- the two must not be unified
//
// Feeding the sentinel into powf(2.0, delta/10) underflows to 0 and would
// silently zero all damage, so the distinction is load-bearing.

#include "../engine/game/attributes.hpp"
#include "../engine/game/damage_formula.hpp"

#include <cmath>
#include <cstdio>

using namespace resf2::game;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static bool near_eq(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

static void test_attribute_set_basics() {
    std::printf("\n-- storage is integer, read as float --\n");
    AttributeSet a;
    a.set("WeaponDamage", 12);
    CHECK(near_eq(a.get("WeaponDamage"), 12.0f),
          "an int value reads back as the same float");
    CHECK(a.raw("WeaponDamage") == 12, "stored as an integer");

    a.add("WeaponDamage", 5);
    CHECK(a.raw("WeaponDamage") == 17, "add() accumulates");
    a.add("BodyDefense", 3);
    CHECK(a.raw("BodyDefense") == 3, "add() on an absent name creates it");
}

static void test_missing_returns_sentinel_not_zero() {
    std::printf("\n-- a miss returns the sentinel, not 0 --\n");
    AttributeSet a;
    const float v = a.get("NoSuchAttribute");
    CHECK(attribute_is_missing(v), "absent attribute reports as missing");
    CHECK(v < -1e34f, "the sentinel is about -1e35 (literal at 0x8F67E658)");
    CHECK(!near_eq(v, 0.0f), "the sentinel is NOT 0 -- that would be a silent lie");

    // Why it matters: the sentinel through the damage curve collapses to zero.
    const float bogus = attribute_difference_factor(v);
    CHECK(bogus == 0.0f || bogus < 1e-30f,
          "sentinel through powf(2, x/10) underflows -- must never reach it");

    CHECK(near_eq(a.get_or("NoSuchAttribute", 0.0f), 0.0f),
          "get_or() folds the sentinel for call sites that want a number");
}

static void test_attribute_name_set() {
    std::printf("\n-- the attribute set matches the original's tracer --\n");
    const auto& names = attribute_names();
    CHECK(names.size() == 7,
          "seven attributes, as read by the tracer at game+0x628788");
    CHECK(names[0] == "WeaponDamage" && names[1] == "UnarmedDamage" &&
          names[2] == "BodyDefense" && names[3] == "HeadDefense" &&
          names[4] == "RangedDamage" && names[5] == "MagicDamage" &&
          names[6] == "RangedQuantity",
          "names and order match the '- WeaponDamage: %3.3f' block");

    const auto& targets = align_target_attributes();
    CHECK(targets.size() == 7,
          "AlignTargetAttributes has 7 entries (112 bytes / 16 per record)");
    // The two sets are deliberately different.
    bool has_ench = false, has_qty = false;
    for (const auto& t : targets) {
        if (std::string(t.name) == "EnchantmentResistance") has_ench = true;
        if (std::string(t.name) == "RangedQuantity") has_qty = true;
    }
    CHECK(has_ench, "align targets include EnchantmentResistance");
    CHECK(!has_qty, "align targets do NOT include RangedQuantity");

    // Values verified against internalSettings.xml.
    int weapon = -1, head = -1, unarmed = -1;
    for (const auto& t : targets) {
        if (std::string(t.name) == "WeaponDamage") weapon = t.value;
        if (std::string(t.name) == "HeadDefense") head = t.value;
        if (std::string(t.name) == "UnarmedDamage") unarmed = t.value;
    }
    CHECK(weapon == 12, "WeaponDamage target is 12");
    CHECK(head == 5, "HeadDefense target is 5 (the odd one out)");
    CHECK(unarmed == 0, "UnarmedDamage target is 0");
}

static void test_reset_and_equipment_aggregation() {
    std::printf("\n-- reset + equipment aggregation --\n");
    AttributeSet a;
    a.reset_to_zero();
    CHECK(a.size() == attribute_names().size(),
          "reset_to_zero seeds every tracked attribute");
    CHECK(!attribute_is_missing(a.get("WeaponDamage")),
          "after reset nothing reports as missing");
    CHECK(near_eq(a.get("WeaponDamage"), 0.0f), "seeded to zero");

    // Two items stack additively.
    a.add_item_contribution(10.0f, 0.0f, 0.0f, 0.0f, 0.0f);   // a weapon
    a.add_item_contribution(0.0f, 8.0f, 4.0f, 0.0f, 0.0f);    // armour + helm
    CHECK(a.raw("WeaponDamage") == 10, "weapon damage from the weapon");
    CHECK(a.raw("BodyDefense") == 8, "body defense from the armour");
    CHECK(a.raw("HeadDefense") == 4, "head defense from the helm");
}

static void test_difference_feeds_the_damage_curve() {
    std::printf("\n-- difference -> doubling-range curve --\n");
    AttributeSet attacker, defender;
    attacker.reset_to_zero();
    defender.reset_to_zero();

    // Equal attributes: no advantage, factor 1.0.
    attacker.set("WeaponDamage", 12);
    defender.set("BodyDefense", 12);
    float d = attribute_difference(attacker, "WeaponDamage",
                                   defender, "BodyDefense");
    CHECK(near_eq(d, 0.0f), "equal attributes -> zero difference");
    CHECK(near_eq(attribute_difference_factor(d), 1.0f),
          "zero difference -> factor 1.0");

    // Ten points ahead doubles the damage (DamageDoublingRange = 10).
    attacker.set("WeaponDamage", 22);
    d = attribute_difference(attacker, "WeaponDamage", defender, "BodyDefense");
    CHECK(near_eq(d, 10.0f), "ten points of advantage");
    CHECK(near_eq(attribute_difference_factor(d), 2.0f),
          "ten points ahead DOUBLES damage");

    // Ten behind halves it.
    attacker.set("WeaponDamage", 2);
    d = attribute_difference(attacker, "WeaponDamage", defender, "BodyDefense");
    CHECK(near_eq(d, -10.0f), "ten points of disadvantage");
    CHECK(near_eq(attribute_difference_factor(d), 0.5f),
          "ten points behind HALVES damage");

    // An absent attribute defaults to 0 on THIS path (game+0x60DF98), which is
    // different from getParameter's sentinel.
    AttributeSet empty;
    d = attribute_difference(attacker, "WeaponDamage", empty, "BodyDefense");
    CHECK(near_eq(d, 2.0f),
          "absent defense attribute counts as 0 here, not as the sentinel");
}

static void test_end_to_end_damage_with_attributes() {
    std::printf("\n-- end to end: attributes through getTotalDamage --\n");
    AttributeSet attacker, defender;
    attacker.reset_to_zero();
    defender.reset_to_zero();
    attacker.set("WeaponDamage", 32);
    defender.set("BodyDefense", 12);

    DamageInputs in;
    in.hit_damage = 0.1f;
    in.attribute_difference =
        attribute_difference(attacker, "WeaponDamage", defender, "BodyDefense");

    // 20 points ahead = two doubling ranges = 4x.
    const float dmg = get_total_damage(in);
    CHECK(near_eq(in.attribute_difference, 20.0f), "difference is 20");
    CHECK(near_eq(dmg, 0.4f), "0.1 base * 4x from two doubling ranges = 0.4");

    // The old linear model would have produced 0.1 * (1 + 0.0001*20) ~= 0.1000,
    // i.e. no meaningful scaling at all. That gap is the point of this port.
    const float linear = 0.1f * (1.0f + 0.0001f * 20.0f);
    CHECK(dmg > linear * 3.0f,
          "exponential model scales where the old linear one did not");
}

int main() {
    std::printf("=== attributes golden test (getParameter @ game+0x6275F4) ===\n");
    test_attribute_set_basics();
    test_missing_returns_sentinel_not_zero();
    test_attribute_name_set();
    test_reset_and_equipment_aggregation();
    test_difference_feeds_the_damage_curve();
    test_end_to_end_damage_with_attributes();
    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
