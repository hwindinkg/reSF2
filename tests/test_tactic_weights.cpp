// tests/test_tactic_weights.cpp
//
// Pins the enemy-AI weight math (engine/game/tactic_settings) to the values
// the PC build produces. The curves are the whole game of the AI: get the
// asymmetry between Linear and Exponential wrong and every decision skews.
//
// Reference: sf2_beautified.js class `cc` — Gb (score), QYa (Linear curve),
// NYa (Exponential curve), iCa (weight lookup), jL (roulette pick).

#include "../engine/game/tactic_settings.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using resf2::game::TacticContext;
using resf2::game::TacticDef;
using resf2::game::TacticSettings;
using resf2::game::TacticWeight;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Approx float compare — the curves use pow(), so exact equality is wrong.
#define CHECK_NEAR(a, b, msg) do { \
    double _va = (double)(a), _vb = (double)(b); \
    if (std::fabs(_va - _vb) > 1e-3) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %f, expected %f\n", \
                     __LINE__, msg, _va, _vb); ++tests_failed; \
    } else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

int main() {
    std::printf("\n=== Linear curve (QYa) ===\n");
    {
        // Standard's ForwardStep: Base=800 AntiLimit=800 Limit=1600
        //                         DistanceFactor=0.002 Shift=-0.25
        TacticWeight w;
        w.base = 800; w.anti_limit = 800; w.limit = 1600;
        w.distance_factor = 0.002f; w.shift = -0.25f;
        w.curve = TacticWeight::Curve::kLinear;

        TacticContext c;
        // distance 0 -> score = 0*0.002 - 0.25 = -0.25 (negative branch)
        // QYa: 800 + (800-800)*min(1,0.25) = 800  (AntiLimit==Base, flat)
        c.distance = 0;
        CHECK_NEAR(w.evaluate(c), 800.0, "ForwardStep @dist=0 -> Base");

        // distance 625 -> 625*0.002 - 0.25 = 1.0 (>=0 branch, saturates)
        // QYa: 800 + (1600-800)*min(1,1.0) = 1600
        c.distance = 625;
        CHECK_NEAR(w.evaluate(c), 1600.0, "ForwardStep @dist=625 -> Limit (saturated)");

        // distance 250 -> 250*0.002 - 0.25 = 0.25
        // QYa: 800 + (1600-800)*0.25 = 1000
        c.distance = 250;
        CHECK_NEAR(w.evaluate(c), 1000.0, "ForwardStep @dist=250 -> interpolated 1000");

        // score exactly 0 -> Base, both curves agree here
        // distance 125 -> 125*0.002 - 0.25 = 0
        c.distance = 125;
        CHECK_NEAR(w.evaluate(c), 800.0, "ForwardStep @score=0 -> Base");
    }

    std::printf("\n=== Linear curve towards AntiLimit ===\n");
    {
        // Standard's BackStep: Base=250 AntiLimit=250 Limit=1
        //                      DistanceFactor=0.0025 Shift=-0.25
        // With AntiLimit==Base it's flat on the negative side; use Retreat
        // instead which has a live AntiLimit spread against Base.
        // Retreat: Base=100 AntiLimit=100 Limit=1 DistanceFactor=0.0025 Shift=-0.25
        TacticWeight w;
        w.base = 100; w.anti_limit = 100; w.limit = 1;
        w.distance_factor = 0.0025f; w.shift = -0.25f;
        w.curve = TacticWeight::Curve::kLinear;

        TacticContext c;
        // distance 500 -> 500*0.0025 - 0.25 = 1.0 -> Limit=1
        c.distance = 500;
        CHECK_NEAR(w.evaluate(c), 1.0, "Retreat @dist=500 -> Limit=1 (close => don't retreat)");

        // distance 0 -> -0.25 -> negative branch: 100 + (100-100)*0.25 = 100
        c.distance = 0;
        CHECK_NEAR(w.evaluate(c), 100.0, "Retreat @dist=0 -> Base=100");
    }

    std::printf("\n=== Exponential curve (NYa) ===\n");
    {
        // Synthetic entry to exercise both branches of NYa distinctly.
        // Base=100 Limit=1000 AntiLimit=10, Exponential.
        TacticWeight w;
        w.base = 100; w.limit = 1000; w.anti_limit = 10;
        w.curve = TacticWeight::Curve::kExponential;

        TacticContext c;  // all zero; drive score via shift
        // score 0 -> both branches give Base
        w.shift = 0;
        CHECK_NEAR(w.evaluate(c), 100.0, "Exp @score=0 -> Base");

        // score +1 -> NYa >=0: Limit + (Base-Limit)*2^-1
        //                    = 1000 + (100-1000)*0.5 = 550
        w.shift = 1;
        CHECK_NEAR(w.evaluate(c), 550.0, "Exp @score=+1 -> 550");

        // score +2 -> 1000 + (100-1000)*0.25 = 775
        w.shift = 2;
        CHECK_NEAR(w.evaluate(c), 775.0, "Exp @score=+2 -> 775");

        // score -1 -> NYa <0: AntiLimit + (Base-AntiLimit)*2^-1
        //                   = 10 + (100-10)*0.5 = 55
        w.shift = -1;
        CHECK_NEAR(w.evaluate(c), 55.0, "Exp @score=-1 -> 55");
    }

    std::printf("\n=== Score accumulation (Gb terms) ===\n");
    {
        // Verify each factor multiplies the right context field.
        TacticWeight w;
        w.base = 0; w.limit = 0; w.anti_limit = 0;  // curve neutral-ish
        w.curve = TacticWeight::Curve::kLinear;
        w.health_factor = 2.0f;         // multiplies (1 - health)
        w.enemy_health_factor = 3.0f;   // multiplies (1 - enemy_health)
        w.hit_factor = 5.0f;            // multiplies hits
        w.distance_factor = 0.5f;       // multiplies distance
        w.shift = 1.0f;

        TacticContext c;
        c.health = 0.25f;       // (1-0.25)=0.75 * 2   = 1.5
        c.enemy_health = 0.5f;  // (1-0.5)=0.5  * 3    = 1.5
        c.hits = 2;             // 2 * 5               = 10
        c.distance = 4;         // 4 * 0.5             = 2
        // total = 1.5 + 1.5 + 10 + 2 + shift(1) = 16
        CHECK_NEAR(w.score(c), 16.0, "Gb accumulates all terms + shift");
    }

    std::printf("\n=== iCa weight lookup (first-match + catch-all) ===\n");
    {
        TacticDef def;
        def.name = "T";
        TacticWeight named; named.base = 42;
        TacticWeight fallback; fallback.base = 7;
        def.animation_weights.emplace_back("ShortAttack", named);
        def.animation_weights.emplace_back("", fallback);  // catch-all last

        const TacticWeight* a = def.weight_for("ShortAttack");
        CHECK(a && a->base == 42, "weight_for named -> named entry");
        const TacticWeight* b = def.weight_for("Nonexistent");
        CHECK(b && b->base == 7, "weight_for unknown -> catch-all");

        TacticDef empty; empty.name = "E";
        CHECK(empty.weight_for("x") == nullptr, "weight_for on empty -> null");
    }

    std::printf("\n=== jL roulette pick ===\n");
    {
        // Two candidates, weights 1000 vs 0. The zero-weight one can never win.
        TacticSettings s;  // choose() is const and needs no loaded data.
        TacticDef def;
        def.name = "T";
        TacticWeight big; big.base = 1000; big.curve = TacticWeight::Curve::kLinear;
        TacticWeight zero; zero.base = 0; zero.curve = TacticWeight::Curve::kLinear;
        def.animation_weights.emplace_back("A", big);
        def.animation_weights.emplace_back("B", zero);

        std::vector<std::string> cands = {"A", "B"};
        TacticContext c;
        std::srand(12345);
        bool only_a = true;
        for (int i = 0; i < 200; ++i) {
            int idx = s.choose(def, cands, c);
            if (idx != 0) { only_a = false; break; }
        }
        CHECK(only_a, "roulette never picks a zero-weight candidate");

        // All-zero weights -> -1 (no decision), matching jL returning -1.
        std::vector<std::string> zc = {"B"};
        CHECK(s.choose(def, zc, c) == -1, "all-zero weights -> -1");

        // A biased-but-nonzero split should reach both eventually.
        TacticWeight small; small.base = 200; small.curve = TacticWeight::Curve::kLinear;
        def.animation_weights[1].second = small;  // B now weight 200
        bool saw_a = false, saw_b = false;
        for (int i = 0; i < 500 && !(saw_a && saw_b); ++i) {
            int idx = s.choose(def, cands, c);
            if (idx == 0) saw_a = true;
            if (idx == 1) saw_b = true;
        }
        CHECK(saw_a && saw_b, "roulette reaches both nonzero candidates");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
