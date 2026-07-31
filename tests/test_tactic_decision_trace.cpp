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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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

    // Golden fight-state scenario: chance curves driven by the scripted
    // distance / health / bullet state (GATE G1). Extreme values on purpose:
    // every curve reads >= 1.0 (fires for ANY roll) or 0 (never), so an R3
    // comparator re-pin changes scores, never which stages fire.
    "<Tactic Name=\"StateGated\" Type=\"Tabular\">"
    "<QuickAttacks>"
    "<QuickAttackChance Animation=\"Punch\" Base=\"0\" Limit=\"3\" DistanceFactor=\"1\"/>"
    "<QuickAttackChance Animation=\"Kick\" Base=\"0\" Limit=\"3\" HealthFactor=\"1\"/>"
    "</QuickAttacks>"
    "<DodgeMissilesChance Base=\"0\" Limit=\"2\" MagicBulletFactor=\"1\" MissileBulletFactor=\"1\"/>"
    "<DistanceError><Min Base=\"0\"/><Max Base=\"0\"/></DistanceError>"
    "<FrameError><Min Base=\"0\"/><Max Base=\"0\"/></FrameError>"
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

// ===========================================================================
// GATE G1 (ADR C4): the decision-trace golden contract.
//
// Pins each scripted scenario's DecisionTrace output to
// tests/golden/tactic_decision_trace.golden.txt, line-group-by-line-group in
// tracer order — any stage reorder/merge/skip or value drift fails the byte
// comparison. Golden provenance: the ARM tracer's format strings at
// 0x8F798090..0x8F79834C (PORT_GAPS.md:171-178, MEMORY_INDEXING_R56.md §3.1,
// GOLDEN_TESTS.md §2). `*` in a golden line means "any digits" (format-only
// pin — the R4-unpinned Wait value).
// ===========================================================================

static const char* kEpilogueOrder[] = {
    "DistanceError:", "FrameError:", "Intervals:", "EnemyIntervals:",
    "DecisionType:", "Decision {Wait=",
};

using GoldenSections = std::map<std::string, std::vector<std::string>>;

// Parses the golden file: `#` lines are comments, `=== key ===` starts a
// section, everything else is an expected trace line. Tolerates CRLF (the
// repo checks out with core.autocrlf).
static bool load_golden(const std::string& path, GoldenSections& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line, cur;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("=== ", 0) == 0 && line.size() >= 8 &&
            line.compare(line.size() - 4, 4, " ===") == 0) {
            cur = line.substr(4, line.size() - 8);
            out[cur] = {};
        } else if (!cur.empty()) {
            out[cur].push_back(line);
        }
    }
    return !out.empty();
}

// Exact match, except `*` in the golden line = "any run of digits" (used
// only for the R4-unpinned Wait value).
static bool golden_line_matches(const std::string& actual,
                                const std::string& golden) {
    const std::size_t star = golden.find('*');
    if (star == std::string::npos) return actual == golden;
    if (actual.size() < golden.size() - 1) return false;
    if (actual.compare(0, star, golden, 0, star) != 0) return false;
    std::size_t i = star;
    while (i < actual.size() &&
           std::isdigit(static_cast<unsigned char>(actual[i]))) {
        ++i;
    }
    return actual.compare(i, std::string::npos, golden, star + 1,
                          std::string::npos) == 0;
}

struct GoldenScenario {
    const char* name;    // golden section key
    const char* tactic;  // tactic name in kTacticXml
    unsigned seed;       // make_lcg seed
    void (*setup_ctx)(TacticContext&);  // optional scripted fight state
    void (*setup_mem)(TacticMemory&);   // optional memory state
};

static void no_ctx(TacticContext&) {}
static void no_mem(TacticMemory&) {}

// Runs one scripted golden scenario: fresh LCG/memory/context, decide(),
// returns the trace lines. No scenario reaches the .atf table path (every
// TableAttackChance here is zero), so an empty TacticTableSet suffices.
static bool run_golden(const GoldenScenario& sc, const TacticSettings& s,
                       TacticDecision& out, std::vector<std::string>& lines) {
    const TacticDef* def = s.tactic(sc.tactic);
    if (!def) return false;
    TacticTableSet tables;
    auto lcg = make_lcg(sc.seed);
    RngSource rng = std::ref(lcg);
    TacticMemory mem;
    sc.setup_mem(mem);
    TacticContext ctx;
    sc.setup_ctx(ctx);
    DecisionTrace trace;
    out = decide(*def, ctx, mem, tables, rng, trace);
    lines = trace.lines();
    return true;
}

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

    std::printf("\n=== GATE G1 golden: decision-trace contract ===\n");
    {
        const char* kGoldenPath = "tests/golden/tactic_decision_trace.golden.txt";
        GoldenSections golden;
        CHECK(load_golden(kGoldenPath, golden), "golden file loads (tests/golden/)");
        CHECK(golden.size() == 6, "golden file has all 6 scenario sections");
        if (!golden.empty()) {
            CHECK(golden.count("all_stages_idle") && golden.count("first_hit_wins") &&
                  golden.count("state_far") && golden.count("state_close") &&
                  golden.count("epilogue_memory") && golden.count("expected_wait"),
                  "golden section names match the scenarios");
        }

        const GoldenScenario scenarios[] = {
            {"all_stages_idle", "Nothing", 42, no_ctx, no_mem},
            {"first_hit_wins", "Aggressive", 7, no_ctx, no_mem},
            {"state_far", "StateGated", 42,
             [](TacticContext& c) {
                 c.distance = 0.75f;   // far
                 c.health = 1.0f;      // healthy
                 c.magic_bullets = 0;  // no bullets inbound
                 c.missile_bullets = 0;
             },
             no_mem},
            {"state_close", "StateGated", 42,
             [](TacticContext& c) {
                 c.distance = 0.0f;    // close
                 c.health = 0.5f;      // wounded
                 c.magic_bullets = 1;  // bullets inbound
                 c.missile_bullets = 1;
             },
             no_mem},
            {"epilogue_memory", "Ranged", 99, no_ctx,
             [](TacticMemory& m) { m.tick(); m.tick(); m.tick(); }},
            {"expected_wait", "Waiter", 1, no_ctx, no_mem},
        };

        // Byte-compare each scenario's trace to its golden section.
        for (const GoldenScenario& sc : scenarios) {
            TacticDecision d;
            std::vector<std::string> lines;
            const bool ran = run_golden(sc, s, d, lines);
            const auto it = golden.find(sc.name);
            bool ok = ran && it != golden.end() && lines.size() == it->second.size();
            if (ok) {
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (!golden_line_matches(lines[i], it->second[i])) {
                        ok = false;
                        break;
                    }
                }
            }
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "golden '%s': trace == golden, line-group by line-group",
                          sc.name);
            CHECK(ok, msg);
            if (!ok && ran && it != golden.end()) {
                const std::size_t n = std::max(lines.size(), it->second.size());
                for (std::size_t i = 0; i < n; ++i) {
                    std::fprintf(stderr, "    [%zu] trace:  %s\n"
                                         "    [%zu] golden: %s\n",
                                 i, i < lines.size() ? lines[i].c_str() : "<missing>",
                                 i, i < it->second.size() ? it->second[i].c_str()
                                                          : "<missing>");
                }
            }
        }

        // Two QuickAttack entries -> the [1] and [2] line-groups in order.
        {
            TacticDecision d;
            std::vector<std::string> lines;
            const bool ran = run_golden(scenarios[0], s, d, lines);
            CHECK(ran && lines.size() >= 6 && starts_with(lines[4], "QuickAttack[1]:") &&
                  starts_with(lines[5], "QuickAttack[2]:"),
                  "two QuickAttack entries -> QuickAttack[1] and QuickAttack[2] groups");
        }

        // Every scenario: the last 6 lines are the epilogue block in order.
        {
            bool epilogue_ok = true;
            for (const GoldenScenario& sc : scenarios) {
                TacticDecision d;
                std::vector<std::string> lines;
                if (!run_golden(sc, s, d, lines) || lines.size() < 6) {
                    epilogue_ok = false;
                    break;
                }
                const std::size_t base = lines.size() - 6;
                for (int i = 0; i < 6; ++i) {
                    if (!starts_with(lines[base + i], kEpilogueOrder[i])) {
                        epilogue_ok = false;
                    }
                }
            }
            CHECK(epilogue_ok, "every scenario: 6 epilogue lines in tracer order");
        }

        // Scripted fight state gates the decision (distance / health / bullets).
        {
            TacticDecision far, close;
            std::vector<std::string> lf, lc;
            CHECK(run_golden(scenarios[2], s, far, lf) &&
                      far.stage == DecisionStage::kQuickAttack &&
                      far.animation == "Punch",
                  "state_far: distance-gated QuickAttack[1] fires");
            CHECK(run_golden(scenarios[3], s, close, lc) &&
                      close.stage == DecisionStage::kDodgeMissiles,
                  "state_close: bullet-gated DodgeMissiles fires first (stage 4 < stage 5)");
        }

        // Jitter inside [Min,Max] and the memory deltas on the epilogue lines.
        {
            TacticDecision d;
            std::vector<std::string> lines;
            const bool ran = run_golden(scenarios[4], s, d, lines);
            CHECK(ran && d.distance_error >= 10.0f && d.distance_error <= 20.0f &&
                      d.frame_error >= 30 && d.frame_error <= 40,
                  "Ranged jitter inside [10,20] / [30,40]");
            CHECK(ran && lines.size() == 11 && lines[7] == "Intervals: 3" &&
                      lines[8] == "EnemyIntervals: 3",
                  "Intervals/EnemyIntervals trace the TacticMemory deltas (3 ticks)");
        }

        // ExpectedWait: the Decision {Wait=…} line is present; its VALUE is
        // unpinned ([HEURISTIC-TODO R4] — the golden pins format only).
        {
            TacticDecision d;
            std::vector<std::string> lines;
            const bool ran = run_golden(scenarios[5], s, d, lines);
            const bool wait_format = ran && !lines.empty() &&
                                     starts_with(lines.back(), "Decision {Wait=");
            bool wait_ok = false;
            if (wait_format) {
                const std::string v = lines.back().substr(15);  // after "Decision {Wait="
                wait_ok = v.size() >= 2 && v.back() == '}';
                for (std::size_t i = 0; wait_ok && i + 1 < v.size(); ++i) {
                    wait_ok = std::isdigit(static_cast<unsigned char>(v[i]));
                }
            }
            CHECK(wait_format && wait_ok,
                  "expected_wait: 'Decision {Wait=<digits>}' line present (R4 value unpinned)");
            CHECK(ran && d.type == "ExpectedWait", "expected_wait: decision type ExpectedWait");
        }

        // Same seed twice -> byte-identical traces (whole trace, not fields).
        {
            TacticDecision a, b;
            std::vector<std::string> la, lb;
            CHECK(run_golden(scenarios[2], s, a, la) &&
                      run_golden(scenarios[2], s, b, lb) && la == lb &&
                      a.stage == b.stage && a.animation == b.animation &&
                      a.wait_frames == b.wait_frames &&
                      a.distance_error == b.distance_error &&
                      a.frame_error == b.frame_error,
                  "same seed -> byte-identical DecisionTrace (and decision)");
        }
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
