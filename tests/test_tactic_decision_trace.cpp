// tests/test_tactic_decision_trace.cpp
//
// Pins the TacticDecisionPipeline (engine/game/tactic_pipeline, ADR-005 D1)
// to the tracer's decision-order contract (PORT_GAPS.md:171-178,
// MEMORY_INDEXING_R56.md §3.1, GOLDEN_TESTS.md §2):
//
//   UseDefense -> UseSafeAttack -> TableAttack -> DodgeMissiles ->
//   QuickAttack[i] -> Evade[i] -> UseCautiousMovements
//   then DistanceError / FrameError / Intervals / EnemyIntervals /
//   DecisionType / Decision {Wait=%d}
//
// The first stage that fires wins; none firing yields the idle decision.
// Stage internals (chance comparator R3, index mapping R6, wait mapping R4)
// are JS-port defaults pending the P3 golden.

#include "../engine/game/tactic_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using resf2::game::DecisionStage;
using resf2::game::DecisionTrace;
using resf2::game::RngSource;
using resf2::game::TacticContext;
using resf2::game::TacticDecision;
using resf2::game::TacticDef;
using resf2::game::TacticMemory;
using resf2::game::TacticSettings;
using resf2::game::TacticTableSet;
using resf2::game::TacticWeight;
using resf2::game::chance_fires;
using resf2::game::decide;
using resf2::game::evade_animation;
using resf2::game::quick_attack_animation;
using resf2::game::stage_label;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
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

// Deterministic RngSource values for the R3 comparator pins:
// roll == 0.0 (fires for any curve > 0) and roll == 1.0 (fires only for
// curve > 1.0).
static const RngSource kRollZero = [] { return 0u; };
static const RngSource kRollOne = [] { return (unsigned)RAND_MAX; };

// Seeded LCG honoring the ADR-005 D4 contract ([0, RAND_MAX]); state shared
// across the draws of one decision via std::ref.
static auto make_lcg(unsigned seed) {
    return [seed]() mutable -> unsigned {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) % ((unsigned)RAND_MAX + 1u);
    };
}

// Full synthetic tactic set. All chance curves use Base=2 for "always fires"
// (roll < 2 holds for every rng value) and Base=0 for "never fires".
static const char* kTacticXml =
    "<TacticsSettings><Tactics>"

    // No chance declared anywhere -> idle decision; QuickAttack/Evade entries
    // still print their (zero) lines, so the full order contract is visible.
    "<Tactic Name=\"Nothing\" Type=\"Tabular\">"
    "<AnimationWeights>"
    "<Animation Name=\"Step\" Base=\"100\"/>"
    "<Animation Base=\"0\"/>"
    "</AnimationWeights>"
    "<QuickAttacks>"
    "<QuickAttackChance Animation=\"Throw\" Base=\"0\"/>"
    "<QuickAttackChance Animation=\"ShortAttack\" Base=\"0\"/>"
    "</QuickAttacks>"
    "<Evades><EvadeChance Animation=\"Throw\" Base=\"0\"/></Evades>"
    "<DistanceError><Min Base=\"1\"/><Max Base=\"2\"/></DistanceError>"
    "<FrameError><Min Base=\"3\"/><Max Base=\"4\"/></FrameError>"
    "</Tactic>"

    // UseDefense block wins (first stage fires); later stages must still
    // trace their lines.
    "<Tactic Name=\"Aggressive\" Type=\"Tabular\">"
    "<UseDefense>"
    "<CounterAttackChance Base=\"0\"/>"
    "<DodgeChance Base=\"0\"/>"
    "<BlockChance Base=\"2\"/>"
    "</UseDefense>"
    "<QuickAttacks><QuickAttackChance Animation=\"Throw\" Base=\"2\"/></QuickAttacks>"
    "</Tactic>"

    // Standalone chance stages.
    "<Tactic Name=\"SafeOnly\" Type=\"Tabular\">"
    "<UseSafeAttackChance Base=\"2\"/>"
    "<AnimationWeights><Animation Name=\"Step\" Base=\"100\"/></AnimationWeights>"
    "</Tactic>"
    "<Tactic Name=\"MissilesOnly\" Type=\"Tabular\">"
    "<DodgeMissilesChance Base=\"2\"/>"
    "</Tactic>"
    "<Tactic Name=\"CautiousOnly\" Type=\"Tabular\">"
    "<CautiousMovementsChance Base=\"2\"/>"
    "<AnimationWeights><Animation Name=\"Step\" Base=\"100\"/></AnimationWeights>"
    "</Tactic>"

    // Looped stages: per-entry lines and the R6 document-order animation.
    "<Tactic Name=\"Quick\" Type=\"Tabular\">"
    "<QuickAttacks>"
    "<QuickAttackChance Animation=\"Throw\" Base=\"2\"/>"
    "<QuickAttackChance Animation=\"ShortAttack\" Base=\"0\"/>"
    "</QuickAttacks>"
    "</Tactic>"
    "<Tactic Name=\"EvadeT\" Type=\"Tabular\">"
    "<Evades><EvadeChance Animation=\"Throw\" Base=\"2\"/></Evades>"
    "</Tactic>"

    // Stage 3: attack_table path (real .atf) and animation_weights fallback.
    "<Tactic Name=\"TableTactic\" Type=\"Tabular\">"
    "<TableAttackChance Base=\"2\"/>"
    "<AnimationWeights>"
    "<Animation Name=\"Step\" Base=\"100\"/>"
    "<Animation Base=\"100\"/>"
    "</AnimationWeights>"
    "</Tactic>"

    // Epilogue jitter ranges.
    "<Tactic Name=\"Ranged\" Type=\"Tabular\">"
    "<DistanceError><Min Base=\"10\"/><Max Base=\"20\"/></DistanceError>"
    "<FrameError><Min Base=\"30\"/><Max Base=\"40\"/></FrameError>"
    "</Tactic>"

    // ExpectedWait type -> the wait pick over <ExpectedWait>.
    "<Tactic Name=\"Waiter\" Type=\"ExpectedWait\">"
    "<ExpectedWait><Animation Name=\"Step\" Base=\"1000\" Limit=\"1000\"/></ExpectedWait>"
    "</Tactic>"
    "<Tactic Name=\"WaiterZero\" Type=\"ExpectedWait\">"
    "<ExpectedWait><Animation Base=\"-5\"/></ExpectedWait>"
    "</Tactic>"

    "</Tactics></TacticsSettings>";

// The tracer's line prefixes in the fixed order: the 14 lines of a full
// trace — 8 stage lines (the looped stages print one line per entry) plus
// the 6 epilogue lines.
static const char* kTracerOrder[] = {
    "UseDefense:", "UseSafeAttack:", "TableAttack:", "DodgeMissiles:",
    "QuickAttack[1]:", "QuickAttack[2]:", "Evade[1]:",
    "UseCautiousMovements:", "DistanceError:", "FrameError:",
    "Intervals:", "EnemyIntervals:", "DecisionType:", "Decision {Wait=",
};

static bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

static TacticContext default_ctx() { return TacticContext{}; }

int main() {
    TacticSettings s;
    CHECK(load_xml_string(s, kTacticXml, "resf2_tdp_all"), "synthetic tactic set loads");

    std::printf("\n=== stage_label prefixes ===\n");
    {
        CHECK(std::string(stage_label(DecisionStage::kUseDefense)) == "UseDefense",
              "UseDefense label");
        CHECK(std::string(stage_label(DecisionStage::kUseSafeAttack)) == "UseSafeAttack",
              "UseSafeAttack label");
        CHECK(std::string(stage_label(DecisionStage::kTableAttack)) == "TableAttack",
              "TableAttack label");
        CHECK(std::string(stage_label(DecisionStage::kDodgeMissiles)) == "DodgeMissiles",
              "DodgeMissiles label");
        CHECK(std::string(stage_label(DecisionStage::kQuickAttack)) == "QuickAttack",
              "QuickAttack label");
        CHECK(std::string(stage_label(DecisionStage::kEvade)) == "Evade", "Evade label");
        CHECK(std::string(stage_label(DecisionStage::kUseCautiousMovements)) ==
                  "UseCautiousMovements",
              "UseCautiousMovements label");
    }

    std::printf("\n=== R3 comparator: chance_fires ===\n");
    {
        TacticWeight w;
        w.curve = TacticWeight::Curve::kLinear;
        TacticContext c = default_ctx();

        w.base = 0.5f;
        CHECK(chance_fires(w, c, kRollZero), "roll 0.0 vs 0.5 -> fires");
        CHECK(!chance_fires(w, c, kRollOne), "roll 1.0 vs 0.5 -> no");

        w.base = 0;
        CHECK(!chance_fires(w, c, kRollZero), "zero curve never fires (0 < 0 false)");

        w.base = 1.0f;
        CHECK(chance_fires(w, c, kRollZero), "roll 0.0 vs 1.0 -> fires");
        CHECK(!chance_fires(w, c, kRollOne), "roll 1.0 vs 1.0 -> strict <, no fire");
    }

    std::printf("\n=== R6 index mapping (document order) ===\n");
    {
        const TacticDef* q = s.tactic("Quick");
        const TacticDef* e = s.tactic("EvadeT");
        CHECK(q && quick_attack_animation(*q, 0) == "Throw" &&
              quick_attack_animation(*q, 1) == "ShortAttack",
              "QuickAttack entry i -> i-th <QuickAttackChance> animation");
        CHECK(e && evade_animation(*e, 0) == "Throw",
              "Evade entry i -> i-th <EvadeChance> animation");
    }

    std::printf("\n=== stage order contract: full trace of an idle decision ===\n");
    {
        const TacticDef* nothing = s.tactic("Nothing");
        CHECK(nothing != nullptr, "Nothing tactic present");
        TacticMemory mem;
        TacticTableSet tables;  // empty — no table families needed
        auto lcg = make_lcg(42);
        RngSource rng = std::ref(lcg);
        DecisionTrace trace;
        TacticContext ctx = default_ctx();
        const TacticDecision d = decide(*nothing, ctx, mem, tables, rng, trace);

        CHECK(d.stage == DecisionStage::kIdle, "no stage fires -> idle decision");
        CHECK(d.animation.empty(), "idle decision has no animation");
        CHECK(d.wait_frames == 0, "Tabular idle wait is 0");

        const auto& lines = trace.lines();
        CHECK(lines.size() == 14, "full trace = 8 stage lines + 6 epilogue lines");
        if (lines.size() == 14) {
            bool order_ok = true;
            for (std::size_t i = 0; i < 14; ++i) {
                if (!starts_with(lines[i], kTracerOrder[i])) order_ok = false;
            }
            CHECK(order_ok, "stage line-groups and epilogue lines in tracer order");
            CHECK(starts_with(lines[0], "UseDefense:") &&
                  starts_with(lines[1], "UseSafeAttack:") &&
                  starts_with(lines[2], "TableAttack:") &&
                  starts_with(lines[3], "DodgeMissiles:") &&
                  starts_with(lines[4], "QuickAttack[1]:") &&
                  starts_with(lines[5], "QuickAttack[2]:") &&
                  starts_with(lines[6], "Evade[1]:") &&
                  starts_with(lines[7], "UseCautiousMovements:"),
                  "all seven stage prefixes present in order");
            CHECK(starts_with(lines[8], "DistanceError:") &&
                  starts_with(lines[9], "FrameError:") &&
                  starts_with(lines[10], "Intervals:") &&
                  starts_with(lines[11], "EnemyIntervals:") &&
                  starts_with(lines[12], "DecisionType:") &&
                  starts_with(lines[13], "Decision {Wait=0}"),
                  "epilogue prefixes in order, Tabular -> Wait=0");
        }
    }

    std::printf("\n=== first hit wins: UseDefense beats QuickAttack ===\n");
    {
        const TacticDef* agg = s.tactic("Aggressive");
        TacticMemory mem;
        TacticTableSet tables;
        auto lcg = make_lcg(7);
        RngSource rng = std::ref(lcg);
        DecisionTrace trace;
        const TacticDecision d = decide(*agg, default_ctx(), mem, tables, rng, trace);
        CHECK(d.stage == DecisionStage::kUseDefense, "stage 1 wins before stage 5");
        CHECK(d.animation == "Block", "BlockChance fired -> Block defense action");
        CHECK(!trace.lines().empty() &&
                  starts_with(trace.lines()[0], "UseDefense: Block / 0.0000 / 0.0000 / 2.0000"),
              "UseDefense line carries the three sub-chance scores");
        // Later stages still evaluate and trace after the win.
        CHECK(trace.lines().size() == 12 &&
                  starts_with(trace.lines()[1], "UseSafeAttack:") &&
                  starts_with(trace.lines()[11], "Decision {Wait="),
              "stages after the win still trace their lines");
    }

    std::printf("\n=== standalone chance stages fire ===\n");
    {
        TacticTableSet tables;

        const TacticDef* safe = s.tactic("SafeOnly");
        TacticMemory mem1;
        DecisionTrace t1;
        const TacticDecision d1 = decide(*safe, default_ctx(), mem1, tables,
                                         RngSource(kRollZero), t1);
        CHECK(d1.stage == DecisionStage::kUseSafeAttack && d1.animation == "Step",
              "UseSafeAttackChance fires -> safe attack picked from weights");

        const TacticDef* mis = s.tactic("MissilesOnly");
        TacticMemory mem2;
        DecisionTrace t2;
        const TacticDecision d2 = decide(*mis, default_ctx(), mem2, tables,
                                         RngSource(kRollZero), t2);
        CHECK(d2.stage == DecisionStage::kDodgeMissiles && d2.animation == "Dodge",
              "DodgeMissilesChance fires -> dodge action");

        const TacticDef* cau = s.tactic("CautiousOnly");
        TacticMemory mem3;
        DecisionTrace t3;
        const TacticDecision d3 = decide(*cau, default_ctx(), mem3, tables,
                                         RngSource(kRollZero), t3);
        CHECK(d3.stage == DecisionStage::kUseCautiousMovements && d3.animation == "Step",
              "CautiousMovementsChance fires -> cautious movement picked from weights");
    }

    std::printf("\n=== QuickAttack/Evade looped stages (R6) ===\n");
    {
        TacticTableSet tables;

        const TacticDef* q = s.tactic("Quick");
        TacticMemory mem1;
        DecisionTrace t1;
        const TacticDecision d1 = decide(*q, default_ctx(), mem1, tables,
                                         RngSource(kRollZero), t1);
        CHECK(d1.stage == DecisionStage::kQuickAttack && d1.animation == "Throw",
              "QuickAttack[1] fires -> document-order animation Throw");
        if (t1.lines().size() >= 6) {
            CHECK(t1.lines()[4] == "QuickAttack[1]: Throw / 2.0000" &&
                  t1.lines()[5] == "QuickAttack[2]: ShortAttack / 0.0000",
                  "QuickAttack lines are 1-based with per-entry animation and score");
        } else {
            CHECK(false, "QuickAttack lines present");
        }

        const TacticDef* e = s.tactic("EvadeT");
        TacticMemory mem2;
        DecisionTrace t2;
        const TacticDecision d2 = decide(*e, default_ctx(), mem2, tables,
                                         RngSource(kRollZero), t2);
        CHECK(d2.stage == DecisionStage::kEvade && d2.animation == "Throw",
              "Evade[1] fires -> document-order animation Throw");
    }

    std::printf("\n=== stage 3: attack_table when present, weights else ===\n");
    {
        TacticTableSet tables;
        CHECK(tables.load("assets"), "real .atf dump loads");
        const resf2::game::TacticTable* fists = tables.attack_table("Fists", "");
        CHECK(fists != nullptr && !fists->candidates.empty(),
              "real Fists v=2 table present with candidates");

        const TacticDef* tt = s.tactic("TableTactic");
        TacticMemory mem1;
        DecisionTrace t1;
        TacticContext ctx1 = default_ctx();
        ctx1.current_animation = "Fists";
        const TacticDecision d1 = decide(*tt, ctx1, mem1, tables,
                                         RngSource(kRollZero), t1);
        CHECK(d1.stage == DecisionStage::kTableAttack, "TableAttackChance fires");
        if (fists) {
            const bool in_table = std::find(fists->candidates.begin(),
                                            fists->candidates.end(), d1.animation) !=
                                  fists->candidates.end();
            CHECK(in_table, "table path picks from the attack table candidates");
        }

        TacticMemory mem2;
        DecisionTrace t2;
        TacticContext ctx2 = default_ctx();
        ctx2.current_animation = "NoSuchTable";
        const TacticDecision d2 = decide(*tt, ctx2, mem2, tables,
                                         RngSource(kRollZero), t2);
        CHECK(d2.stage == DecisionStage::kTableAttack && d2.animation == "Step",
              "no table -> picks from the tactic's animation_weights");
    }

    std::printf("\n=== epilogue: jitter ranges and determinism ===\n");
    {
        const TacticDef* r = s.tactic("Ranged");
        TacticTableSet tables;

        for (unsigned seed : {1u, 7u, 42u, 99u}) {
            auto lcg = make_lcg(seed);
            RngSource rng = std::ref(lcg);
            TacticMemory mem;
            DecisionTrace trace;
            const TacticDecision d = decide(*r, default_ctx(), mem, tables, rng, trace);
            CHECK(d.distance_error >= 10.0f && d.distance_error <= 20.0f,
                  "DistanceError jitter within [Min,Max]");
            CHECK(d.frame_error >= 30 && d.frame_error <= 40,
                  "FrameError jitter within [Min,Max]");
        }

        auto run = [&](unsigned seed) {
            auto lcg = make_lcg(seed);
            RngSource rng = std::ref(lcg);
            TacticMemory mem;
            DecisionTrace trace;
            return decide(*r, default_ctx(), mem, tables, rng, trace);
        };
        const TacticDecision a1 = run(42);
        const TacticDecision a2 = run(42);
        CHECK(a1.distance_error == a2.distance_error &&
              a1.frame_error == a2.frame_error,
              "same seed -> identical jitter");
        const TacticDecision b = run(7);
        CHECK(a1.distance_error != b.distance_error,
              "different seed -> different jitter");
    }

    std::printf("\n=== intervals epilogue reads TacticMemory ===\n");
    {
        const TacticDef* nothing = s.tactic("Nothing");
        TacticTableSet tables;
        TacticMemory mem;
        mem.tick();
        mem.tick();
        mem.tick();  // self and enemy both 3 frames
        DecisionTrace trace;
        const TacticDecision d1 = decide(*nothing, default_ctx(), mem, tables,
                                         RngSource(kRollZero), trace);
        const auto& lines = trace.lines();
        CHECK(d1.stage == DecisionStage::kIdle && lines.size() == 14 &&
              starts_with(lines[10], "Intervals: 3") &&
              starts_with(lines[11], "EnemyIntervals: 3"),
              "Intervals/EnemyIntervals trace the memory frame counters");

        mem.record_self("Step");  // self resets, enemy stays at 3
        DecisionTrace trace2;
        const TacticDecision d2 = decide(*nothing, default_ctx(), mem, tables,
                                         RngSource(kRollZero), trace2);
        CHECK(d2.stage == DecisionStage::kIdle && trace2.lines().size() == 14 &&
              starts_with(trace2.lines()[10], "Intervals: 0") &&
              starts_with(trace2.lines()[11], "EnemyIntervals: 3"),
              "record_self resets Intervals, not EnemyIntervals");
    }

    std::printf("\n=== ExpectedWait epilogue (R4) ===\n");
    {
        const TacticDef* w = s.tactic("Waiter");
        TacticTableSet tables;
        TacticMemory mem;
        DecisionTrace trace;
        const TacticDecision d = decide(*w, default_ctx(), mem, tables,
                                        RngSource(kRollZero), trace);
        CHECK(d.stage == DecisionStage::kIdle, "ExpectedWait with no stages -> idle");
        CHECK(d.wait_frames == 1000, "wait pick over <ExpectedWait> -> Step weight as frames");
        CHECK(d.type == "ExpectedWait", "decision type normalized");
        // No QuickAttack/Evade entries -> 5 stage lines + 6 epilogue lines.
        if (trace.lines().size() == 11) {
            CHECK(starts_with(trace.lines()[9], "DecisionType: ExpectedWait") &&
                  trace.lines()[10] == "Decision {Wait=1000}",
                  "epilogue prints the wait decision");
        } else {
            CHECK(false, "ExpectedWait trace complete");
        }

        const TacticDef* z = s.tactic("WaiterZero");
        TacticMemory mem2;
        DecisionTrace t2;
        const TacticDecision d2 = decide(*z, default_ctx(), mem2, tables,
                                         RngSource(kRollZero), t2);
        CHECK(d2.wait_frames == 0, "all-zero expected_wait list -> wait stays 0");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
