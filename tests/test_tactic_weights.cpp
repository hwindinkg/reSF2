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
#include <filesystem>
#include <fstream>
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

// Writes `xml` as tacticSettings.xml into a fresh temp dir and loads it.
static bool load_xml_string(TacticSettings& out, const std::string& xml,
                            const char* dirname) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / dirname;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    {
        std::ofstream f(dir / "tacticSettings.xml", std::ios::binary);
        f << xml;
    }
    bool ok = out.load(dir.string());
    fs::remove_all(dir, ec);
    return ok;
}

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

    // ---- Step A1 (ADR-005 D5/D6): AnimationFactors probe term + extended
    // ---- TacticContext. Neutral-by-zero: no behavior change when unset.

    std::printf("\n=== AnimationFactors probe term (ADR-005 D5, a.a6.S5a) ===\n");
    {
        TacticWeight w;
        w.base = 0; w.curve = TacticWeight::Curve::kLinear;
        w.shift = 1.0f;
        w.animation_factors = 3.0f;

        TacticContext c;
        // animation_factor == 0 -> the probe term contributes nothing.
        CHECK_NEAR(w.score(c), 1.0, "probe term neutral when animation_factor == 0");

        // animation_factor=2 * animation_factors=3 -> score rises by exactly 6.
        c.animation_factor = 2.0f;
        CHECK_NEAR(w.score(c), 7.0, "probe adds animation_factor * animation_factors");
    }

    std::printf("\n=== Extended TacticContext defaults (ADR-005 D2) ===\n");
    {
        TacticContext c;
        CHECK(c.animation_factor == 0 && c.strikes == 0 && c.round_factor == 0 &&
              c.self_interval == 0 && c.enemy_interval == 0,
              "extended context floats default to 0");
        CHECK(c.current_animation.empty(), "current_animation defaults to empty");
    }

    std::printf("\n=== <AnimationFactors> child parsing ===\n");
    {
        // Real XML shape (assets/tacticSettings.xml): <AnimationFactors> is a
        // child of an <Animation>/<...Chance> element, per target animation.
        const std::string xml =
            "<TacticsSettings><Tactics>"
            "<Tactic Name=\"T\"><AnimationWeights>"
            "<Animation Name=\"RangedPlayer\" Base=\"400\">"
            "<AnimationFactors Animation=\"Throw\" DamageFactor=\"4\" CounterFactor=\"0.5\"/>"
            "</Animation>"
            "<Animation Base=\"100\"/>"
            "</AnimationWeights></Tactic>"
            "</Tactics></TacticsSettings>";
        TacticSettings s;
        CHECK(load_xml_string(s, xml, "resf2_tw_a1"), "synthetic XML loads");
        const TacticDef* td = s.tactic("T");
        CHECK(td != nullptr, "tactic T present");
        if (td) {
            const TacticWeight* w = td->weight_for("RangedPlayer");
            CHECK(w && w->animation_factor_entries.size() == 1,
                  "one per-target AnimationFactors entry parsed");
            if (w && w->animation_factor_entries.size() == 1) {
                const auto& e = w->animation_factor_entries[0];
                CHECK(e.animation == "Throw", "entry targets Animation=\"Throw\"");
                CHECK_NEAR(e.factors.damage_factor, 4.0, "entry DamageFactor=4");
                CHECK_NEAR(e.factors.counter_factor, 0.5, "entry CounterFactor=0.5");
            }
            // The plain <Animation Base="100"/> carries no entries.
            const TacticWeight* plain = td->weight_for("Unlisted");
            CHECK(plain && plain->animation_factor_entries.empty(),
                  "weight without children has no probe entries");
        }
    }

    // ---- Step A2 (ADR-005 D2): the 20 decision-level keys on TacticDef,
    // ---- shaped exactly like the real assets/tacticSettings.xml schema.

    std::printf("\n=== Decision-level keys: full parse ===\n");
    {
        const std::string xml =
            "<TacticsSettings><Tactics>"
            "<Tactic Name=\"Full\" Type=\"ExpectedWait\">"
            "<AnimationWeights><Animation Base=\"100\"/></AnimationWeights>"
            "<UseDefense>"
            "<CounterAttackChance Base=\"0.1\" Limit=\"1\"/>"
            "<DodgeChance         Base=\"0.2\" Limit=\"1\"/>"
            "<BlockChance         Base=\"0.3\" Limit=\"1\"/>"
            "</UseDefense>"
            "<UseSafeAttackChance     Base=\"0.4\" Limit=\"1\"/>"
            "<TableAttackChance       Base=\"0.5\" Limit=\"1\"/>"
            "<CautiousMovementsChance Base=\"0.6\" Limit=\"1\"/>"
            "<DodgeMissilesChance     Base=\"0.7\" Limit=\"1\"/>"
            "<DodgeMagicChance        Base=\"0.8\" Limit=\"1\"/>"
            "<QuickAttacks>"
            "<QuickAttackChance Animation=\"Throw\" Base=\"0.05\" Limit=\"0.3\">"
            "<AnimationFactors Animation=\"Throw\" DamageFactor=\"4\"/>"
            "</QuickAttackChance>"
            "<QuickAttackChance Animation=\"ShortAttack\" Base=\"0.2\"/>"
            "</QuickAttacks>"
            "<Evades>"
            "<EvadeChance Animation=\"Throw\" Base=\"0.02\" Limit=\"1\"/>"
            "</Evades>"
            "<DistanceError><Min Base=\"1\"/><Max Base=\"2\"/></DistanceError>"
            "<FrameError><Min Base=\"3\"/><Max Base=\"4\"/></FrameError>"
            "<ResponseDelay><Min Base=\"5\"/><Max Base=\"6\"/></ResponseDelay>"
            "<EnemyResponseDelay><Min Base=\"30\"/><Max Base=\"60\"/></EnemyResponseDelay>"
            "<ExpectedWait>"
            "<Animation Name=\"Step\" Base=\"1000\" Limit=\"1000\"/>"
            "<Animation Base=\"3\" HealthFactor=\"3\" Limit=\"15\"/>"
            "</ExpectedWait>"
            "<Memory Strikes=\"3\" RoundFactor=\"1\"/>"
            "</Tactic>"
            "</Tactics></TacticsSettings>";
        TacticSettings s;
        CHECK(load_xml_string(s, xml, "resf2_tw_a2_full"), "full-key XML loads");
        const TacticDef* d = s.tactic("Full");
        CHECK(d != nullptr, "tactic Full present");
        if (d) {
            // <UseDefense> is a presence-gate container of 3 chance curves.
            CHECK(d->use_defense, "UseDefense presence gate set");
            CHECK_NEAR(d->counter_attack_chance.base, 0.1, "CounterAttackChance");
            CHECK_NEAR(d->dodge_chance.base, 0.2, "DodgeChance");
            CHECK_NEAR(d->block_chance.base, 0.3, "BlockChance");
            // Standalone chance curves.
            CHECK_NEAR(d->use_safe_attack_chance.base, 0.4, "UseSafeAttackChance");
            CHECK_NEAR(d->table_attack_chance.base, 0.5, "TableAttackChance");
            CHECK_NEAR(d->cautious_movements_chance.base, 0.6, "CautiousMovementsChance");
            CHECK_NEAR(d->dodge_missiles_chance.base, 0.7, "DodgeMissilesChance");
            CHECK_NEAR(d->dodge_magic_chance.base, 0.8, "DodgeMagicChance");
            // <QuickAttacks>/<Evades>: per-animation entries, order preserved;
            // entry count == the stage repeat count (ADR R6).
            CHECK(d->quick_attack_chances.size() == 2, "two QuickAttackChance entries");
            if (d->quick_attack_chances.size() == 2) {
                CHECK(d->quick_attack_chances[0].first == "Throw",
                      "QuickAttackChance[0] Animation=Throw (order preserved)");
                CHECK_NEAR(d->quick_attack_chances[0].second.base, 0.05,
                           "QuickAttackChance[0] Base");
                CHECK(d->quick_attack_chances[0].second.animation_factor_entries.size() == 1,
                      "QuickAttackChance[0] carries its AnimationFactors child");
                CHECK(d->quick_attack_chances[1].first == "ShortAttack",
                      "QuickAttackChance[1] Animation=ShortAttack");
            }
            CHECK(d->evade_chances.size() == 1 &&
                  d->evade_chances[0].first == "Throw",
                  "one EvadeChance entry for Throw");
            // DistanceError/FrameError/ResponseDelay/EnemyResponseDelay are
            // <Min Base/><Max Base/> ranges, not scalars.
            CHECK(d->distance_error.min == 1 && d->distance_error.max == 2,
                  "DistanceError range 1..2");
            CHECK(d->frame_error.min == 3 && d->frame_error.max == 4,
                  "FrameError range 3..4");
            CHECK(d->response_delay.min == 5 && d->response_delay.max == 6,
                  "ResponseDelay range 5..6");
            CHECK(d->enemy_response_delay.min == 30 && d->enemy_response_delay.max == 60,
                  "EnemyResponseDelay range 30..60");
            // <ExpectedWait> is an animation-weight list (same shape as
            // animation_weights), not a single curve.
            CHECK(d->expected_wait.size() == 2, "ExpectedWait list of 2");
            if (d->expected_wait.size() == 2) {
                CHECK(d->expected_wait[0].first == "Step", "ExpectedWait[0]=Step");
                CHECK_NEAR(d->expected_wait[0].second.base, 1000.0, "ExpectedWait[0] Base");
                CHECK(d->expected_wait[1].first.empty(), "ExpectedWait[1] is the catch-all");
                CHECK_NEAR(d->expected_wait[1].second.health_factor, 3.0,
                           "ExpectedWait[1] HealthFactor");
            }
            // <Memory Strikes RoundFactor/>; a Memory depth attribute is
            // absent in this dump -> 0 (ring-depth source flagged R5).
            CHECK(d->strikes == 3, "Memory Strikes=3");
            CHECK_NEAR(d->round_factor, 1.0, "Memory RoundFactor=1");
            CHECK(d->memory == 0, "Memory depth attr absent -> 0");
        }
    }

    std::printf("\n=== Decision-level keys: template inheritance ===\n");
    {
        // Same rule as animation_weights: locally-declared key wins, else
        // inherit from the Template chain. Presence-based keys (UseDefense)
        // inherit only when not locally present.
        const std::string xml =
            "<TacticsSettings><Tactics>"
            "<Tactic Name=\"Base\" Type=\"Tabular\">"
            "<UseDefense><BlockChance Base=\"0.3\"/></UseDefense>"
            "<UseSafeAttackChance Base=\"0.4\"/>"
            "<DodgeMissilesChance Base=\"0.7\"/>"
            "<QuickAttacks><QuickAttackChance Animation=\"Throw\" Base=\"0.05\"/></QuickAttacks>"
            "<ResponseDelay><Min Base=\"5\"/><Max Base=\"6\"/></ResponseDelay>"
            "<ExpectedWait><Animation Name=\"Step\" Base=\"10\"/></ExpectedWait>"
            "<Memory Strikes=\"3\" RoundFactor=\"1\"/>"
            "</Tactic>"
            "<Tactic Name=\"Child\" Template=\"Base\" Type=\"Tabular\">"
            "<UseSafeAttackChance Base=\"0.9\"/>"
            "</Tactic>"
            "</Tactics></TacticsSettings>";
        TacticSettings s;
        CHECK(load_xml_string(s, xml, "resf2_tw_a2_inherit"), "inheritance XML loads");
        const TacticDef* d = s.tactic("Child");
        CHECK(d != nullptr, "tactic Child present");
        if (d) {
            CHECK_NEAR(d->use_safe_attack_chance.base, 0.9,
                       "locally-declared chance wins over template");
            CHECK_NEAR(d->dodge_missiles_chance.base, 0.7,
                       "undeclared chance inherited from template");
            CHECK(d->use_defense && d->block_chance.base > 0.29f,
                  "UseDefense container inherited when not locally present");
            CHECK(d->quick_attack_chances.size() == 1,
                  "QuickAttacks entries inherited");
            CHECK(d->response_delay.min == 5 && d->response_delay.max == 6,
                  "ResponseDelay range inherited");
            CHECK(d->expected_wait.size() == 1, "ExpectedWait list inherited");
            CHECK(d->strikes == 3 && d->round_factor > 0.99f,
                  "Memory Strikes/RoundFactor inherited");
        }
    }

    std::printf("\n=== Decision-type validation (Strange tactic type) ===\n");
    {
        // [ORIGINAL] decision types: Tabular (default, incl. absent Type) and
        // ExpectedWait; the binary rejects everything else with
        // "Strange tactic type: %s" (PORT_GAPS.md:168-169).
        const std::string xml =
            "<TacticsSettings><Tactics>"
            "<Tactic Name=\"OkTabular\" Type=\"Tabular\"><AnimationWeights><Animation Base=\"1\"/></AnimationWeights></Tactic>"
            "<Tactic Name=\"OkDefault\"><AnimationWeights><Animation Base=\"1\"/></AnimationWeights></Tactic>"
            "<Tactic Name=\"OkWait\" Type=\"ExpectedWait\"><AnimationWeights><Animation Base=\"1\"/></AnimationWeights></Tactic>"
            "<Tactic Name=\"BadBogus\" Type=\"Bogus\"><AnimationWeights><Animation Base=\"1\"/></AnimationWeights></Tactic>"
            "<Tactic Name=\"BadRandom\" Type=\"Random\"><AnimationWeights><Animation Base=\"1\"/></AnimationWeights></Tactic>"
            "</Tactics></TacticsSettings>";
        TacticSettings s;
        CHECK(load_xml_string(s, xml, "resf2_tw_a2_types"), "type-validation XML loads");
        CHECK(s.tactic("OkTabular") != nullptr, "Type=Tabular accepted");
        CHECK(s.tactic("OkDefault") != nullptr, "absent Type accepted (Tabular default)");
        CHECK(s.tactic("OkWait") != nullptr, "Type=ExpectedWait accepted");
        CHECK(s.tactic("BadBogus") == nullptr, "Type=Bogus rejected (Strange tactic type)");
        CHECK(s.tactic("BadRandom") == nullptr, "Type=Random rejected (Strange tactic type)");
        CHECK(s.count() == 3, "only the 3 valid tactics loaded");
    }

    std::printf("\n=== Real assets: assets/tacticSettings.xml ===\n");
    {
        // Real-data consequence of the 2-type accept list (grep-verified
        // 2026-07-31): 13 <Tactic> elements -> 12 unique names (duplicate
        // Titan_Aggressive) -> 11 loaded, because Beginner (Type="Random")
        // is rejected with the [ORIGINAL] "Strange tactic type: %s" print.
        // If binary evidence later shows Random accepted, the accept-list
        // change is one line + this count bumps to 12.
        TacticSettings s;
        CHECK(s.load("assets"), "real tacticSettings.xml loads");
        CHECK(s.tactic("Standard") != nullptr, "Standard present");
        CHECK(s.tactic("NoTables") != nullptr, "NoTables present");
        CHECK(s.tactic("UseTables") != nullptr, "UseTables present");
        CHECK(s.tactic("Beginner") == nullptr,
              "Beginner (Type=Random) skipped per the 2-type accept list");
        CHECK(s.count() == 11, "13 elements - 1 duplicate - 1 skipped = 11 tactics");
        // Schema spot-pins against the real data: Standard has Template=
        // UseTables and declares only AnimationWeights, so every decision key
        // is inherited; NoTables declares its own EnemyResponseDelay range.
        const TacticDef* std_t = s.tactic("Standard");
        CHECK(std_t && std_t->use_defense &&
              std_t->quick_attack_chances.size() == 2 &&
              std_t->expected_wait.size() == 2,
              "Standard inherits decision keys from UseTables");
        const TacticDef* nt = s.tactic("NoTables");
        CHECK(nt && nt->enemy_response_delay.min == 30 &&
              nt->enemy_response_delay.max == 60,
              "NoTables EnemyResponseDelay 30..60");
        CHECK(nt && nt->strikes == 3, "NoTables Memory Strikes=3");
    }

    // ---- Step A3 (ADR-005 D4): RngSource injection into the roulette.

    std::printf("\n=== RngSource injection (ADR-005 D4) ===\n");
    {
        TacticSettings s;
        TacticDef def;
        def.name = "T";
        TacticWeight wa; wa.base = 500; wa.curve = TacticWeight::Curve::kLinear;
        TacticWeight wb; wb.base = 500; wb.curve = TacticWeight::Curve::kLinear;
        def.animation_weights.emplace_back("A", wa);
        def.animation_weights.emplace_back("B", wb);
        const std::vector<std::string> cands = {"A", "B"};
        TacticContext c;

        // Seeded LCG honoring the RngSource contract: values in [0, RAND_MAX],
        // the same range as std::rand, so the draw formula is unchanged.
        auto make_lcg = [](unsigned seed) {
            return [seed]() mutable -> unsigned {
                seed = seed * 1103515245u + 12345u;
                return (seed >> 16) % ((unsigned)RAND_MAX + 1u);
            };
        };
        auto sequence = [&](unsigned seed, int draws) {
            std::vector<int> picks;
            auto lcg = make_lcg(seed);
            // std::ref: the generator state must be shared, not copied per call.
            resf2::game::RngSource rng = std::ref(lcg);
            for (int i = 0; i < draws; ++i)
                picks.push_back(s.choose(def, cands, c, rng));
            return picks;
        };

        const std::vector<int> run1 = sequence(42, 24);
        const std::vector<int> run2 = sequence(42, 24);
        CHECK(run1 == run2, "same seed -> identical pick sequence (24 draws)");
        const std::vector<int> run3 = sequence(7, 24);
        CHECK(run1 != run3, "different seeds -> different pick sequence");

        // All-zero weights still return -1 with an injected source.
        const std::vector<std::string> zc = {"Z"};  // no weight entry -> 0
        auto z_lcg = make_lcg(1);
        resf2::game::RngSource rng = std::ref(z_lcg);
        CHECK(s.choose(def, zc, c, rng) == -1,
              "all-zero weights -> -1 with injected rng");

        // choose_debug reports the same weights via the injected source.
        std::vector<float> w1, w2;
        auto l1 = make_lcg(99), l2 = make_lcg(99);
        resf2::game::RngSource r1 = std::ref(l1), r2 = std::ref(l2);
        int p1 = s.choose_debug(def, cands, c, w1, r1);
        int p2 = s.choose_debug(def, cands, c, w2, r2);
        CHECK(p1 == p2 && w1 == w2, "choose_debug deterministic with same seed");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
