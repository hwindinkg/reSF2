// tests/test_damage_formula_golden.cpp
//
// Golden test for the ORIGINAL damage formula, Model::getTotalDamage
// @ game+0x4527B4. Reference: tests/golden/damage_formula.golden.json,
// provenance: reverse/analysis/PORT_GAPS.md ("GAP-3 RESOLVED").
//
// The function was located via the single xref to its assert string
// "Model::getTotalDamage - wtf so strong" -- a developer sanity warning that
// fires when damage escapes [0, 100000]. That assert is a strong hint about the
// design: the curve is EXPONENTIAL and therefore unbounded.
//
// Key facts pinned here:
//   * factor = 2 ^ (attribute_delta / DamageDoublingRange), range = 10.0
//     => every 10 points of attribute advantage doubles the damage
//   * 2.0 is the BASE of the power, not a trailing "x2" multiplier
//   * a disabled attribute term returns 1.0, never 0
//   * the range check WARNS but does not clamp

#include "../engine/game/damage_formula.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>

using namespace resf2::game;

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++failed; } \
    else { std::printf("  PASS: %s\n", msg); ++passed; } \
} while (0)

static bool near_eq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

static void test_doubling_range() {
    std::printf("\n-- DamageDoublingRange = 10 --\n");
    // <DamageDoublingRange Value="10"/>, verified live as 10.0f.
    CHECK(near_eq(DamageFormulaConstants::kDamageDoublingRange, 10.0f),
          "doubling range is 10.0 (from internalSettings.xml + runtime)");
    CHECK(near_eq(DamageFormulaConstants::kPowBase, 2.0f),
          "power base is 2.0 (mov r0,#0x40000000)");

    // The defining property: N doubling ranges multiply damage by 2^N.
    CHECK(near_eq(attribute_difference_factor(0.0f), 1.0f),
          "delta 0 -> factor 1.0 (no advantage)");
    CHECK(near_eq(attribute_difference_factor(10.0f), 2.0f),
          "delta 10 -> factor 2.0 (one range DOUBLES damage)");
    CHECK(near_eq(attribute_difference_factor(20.0f), 4.0f),
          "delta 20 -> factor 4.0 (two ranges quadruple)");
    CHECK(near_eq(attribute_difference_factor(30.0f), 8.0f),
          "delta 30 -> factor 8.0");
    CHECK(near_eq(attribute_difference_factor(-10.0f), 0.5f),
          "delta -10 -> factor 0.5 (disadvantage halves)");

    // Guard the divide-by-zero the original would perform.
    CHECK(near_eq(attribute_difference_factor(50.0f, 0.0f), 1.0f),
          "zero doubling range is guarded, returns neutral 1.0");
}

static void test_disabled_term_is_neutral() {
    std::printf("\n-- disabled term = 1.0, not 0 --\n");
    // moveq r4, #0x3f800000 in both helpers.
    CHECK(near_eq(attribute_factor(0.5f, 12.0f, false), 1.0f),
          "disabled factor returns 1.0 (0x3F800000), multiplicatively neutral");
    CHECK(!near_eq(attribute_factor(0.5f, 12.0f, false), 0.0f),
          "disabled factor is NOT 0 (would zero the whole product)");
    CHECK(near_eq(attribute_factor(0.5f, 12.0f, true), 64.0f),
          "enabled factor is 2^(0.5*12) = 64");
}

static void test_exponential_not_linear() {
    std::printf("\n-- exponential vs the engine's old linear form --\n");
    // reSF2 used: attribute_multiplier = 1.0 + damage_factor_base * attr
    const float w = 0.0001f;

    // The trap: both forms agree exactly at attr == 0, which is the value the
    // engine hardcoded -- so the wrong curve was undetectable.
    CHECK(near_eq(1.0f + w * 0.0f, attribute_factor(w, 0.0f, true)),
          "linear and exponential AGREE at attr == 0 (why the bug was hidden)");

    // And diverge everywhere else.
    bool diverges = true;
    static const float kAttrs[] = {1.0f, 5.0f, 12.0f, 100.0f};
    for (std::size_t i = 0; i < sizeof(kAttrs) / sizeof(kAttrs[0]); ++i) {
        const float attr = kAttrs[i];
        const float lin = 1.0f + w * attr;
        const float exp = attribute_factor(w, attr, true);
        if (near_eq(lin, exp, 1e-9f)) diverges = false;
    }
    CHECK(diverges, "the two forms differ for every attr != 0");

    // At large attribute values the difference is dramatic, not a rounding
    // detail: 2^(w*attr) grows without bound while 1+w*attr stays near 1.
    const float big = 100000.0f;
    const float lin_big = 1.0f + w * big;                    // 11.0
    const float exp_big = attribute_factor(w, big, true);    // 2^10 = 1024
    CHECK(exp_big > lin_big * 50.0f,
          "at large attributes the exponential form is orders of magnitude larger");
}

static void test_multiplication_order() {
    std::printf("\n-- multiplication order (base * f2 * f1 * f3 * add) --\n");
    // Float multiplication is not associative, so the emitted order matters.
    // Note the disassembly multiplies f2 BEFORE f1.
    DamageInputs in;
    in.base_attribute = 3.0f;
    in.base_weight = 0.25f;          // base = 2^0.75
    in.attacker_weight = 0.1f;
    in.attacker_attribute = 10.0f;   // f1 = 2^1 = 2
    in.attacker_enabled = true;
    in.defender_weight = 0.05f;
    in.defender_attribute = 20.0f;   // f2 = 2^1 = 2
    in.defender_enabled = true;
    in.attribute_difference = 10.0f; // f3 = 2
    in.hit_damage = 0.1f;
    in.enemy_damage_bonus = 0.0f;

    const float base = std::pow(2.0f, 3.0f * 0.25f);
    const float expected = base * 2.0f * 2.0f * 2.0f * 0.1f;
    const float got = get_total_damage(in);
    CHECK(near_eq(got, expected),
          "product matches the emitted order with all four factors");

    // All-neutral case: damage reduces to the additive pair.
    DamageInputs n;
    n.hit_damage = 0.4f;
    n.enemy_damage_bonus = 0.1f;
    CHECK(near_eq(get_total_damage(n), 0.5f),
          "with neutral factors damage == hit[0x48] + enemy[0x774]");
}

static void test_additive_pair_and_post_multipliers() {
    std::printf("\n-- additive pair + post-clamp multipliers --\n");
    // add = hit[0x48] + enemy[0x774]; both were missing from reSF2.
    DamageInputs in;
    in.hit_damage = 0.3f;
    in.enemy_damage_bonus = 0.2f;
    CHECK(near_eq(get_total_damage(in), 0.5f),
          "enemy_damage_bonus is ADDED to hit damage, not multiplied");

    // crit and the two enemy multipliers apply AFTER the max(0) clamp.
    in.crit_factor = 2.0f;
    in.enemy_multiplier_a = 1.5f;
    in.enemy_multiplier_b = 2.0f;
    CHECK(near_eq(get_total_damage(in), 0.5f * 2.0f * 1.5f * 2.0f),
          "crit * enemy[0x678] * enemy[0x6AC] applied after the clamp");
}

static void test_negative_clamped_but_high_only_warns() {
    std::printf("\n-- clamp semantics --\n");
    // max(dmg, 0.0) before the post-multipliers.
    DamageInputs neg;
    neg.hit_damage = -5.0f;
    neg.crit_factor = 3.0f;
    CHECK(near_eq(get_total_damage(neg), 0.0f),
          "negative damage is clamped to 0 before the post-multipliers");

    // The upper bound only WARNS -- it must not clamp, or strong builds diverge.
    DamageInputs strong;
    strong.hit_damage = 1.0f;
    strong.attribute_difference = 200.0f;   // 2^20 = 1048576
    const float got = get_total_damage(strong);
    CHECK(got > DamageFormulaConstants::kSanityMax,
          "damage above 100000 is RETURNED unchanged (assert warns, never clamps)");
    CHECK(near_eq(got, std::pow(2.0f, 20.0f), 1e-3f),
          "the over-limit value is exactly 2^(200/10)");
}

static void test_why_the_assert_exists() {
    std::printf("\n-- the assert is a design tell --\n");
    // The warning exists because the curve is unbounded: a large attribute
    // advantage blows past the sanity limit. This is the clearest evidence that
    // the model is exponential rather than linear.
    CHECK(attribute_difference_factor(100.0f) > 1000.0f,
          "delta 100 (ten doubling ranges) already gives 1024x");
    CHECK(attribute_difference_factor(200.0f) > DamageFormulaConstants::kSanityMax,
          "delta 200 exceeds the 100000 sanity limit -> the warning fires");
    // A linear model could never reach that from sane inputs, so the presence
    // of the assert corroborates the recovered curve.
    CHECK(1.0f + 0.0001f * 200.0f < 2.0f,
          "the old linear model could never trigger the assert (stays near 1.0)");
}

int main() {
    std::printf("=== damage formula golden test (getTotalDamage @ game+0x4527B4) ===\n");
    test_doubling_range();
    test_disabled_term_is_neutral();
    test_exponential_not_linear();
    test_multiplication_order();
    test_additive_pair_and_post_multipliers();
    test_negative_clamped_but_high_only_warns();
    test_why_the_assert_exists();
    std::printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
