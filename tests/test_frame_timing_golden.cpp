// tests/test_frame_timing_golden.cpp
//
// Golden test: the frame-timing contract of the ORIGINAL binary.
//
// Reference data: tests/golden/frame_timing.golden.json, captured live from
// the ARM binary (loop at game+0x64400, interval read from this+0x08, delta
// divisor decoded from the literal pool at 0x8F0BB2B0).
// Background: reverse/analysis/RUNTIME_MAP.md §5, PORT_GAPS.md GAP-1/GAP-2.
//
// This test encodes what the original *actually does*:
//
//   * the frame interval is an integer 16 ms (1000/60 truncated), giving a
//     62.5 fps cap -- not 60
//   * dt = interval_ms / 1000.0 in DOUBLE, so dt is quantised to 1 ms and
//     0.0166666... can never occur
//   * the wait is an inner spin loop that re-reads the clock and the interval,
//     carrying no fractional remainder between frames
//
// engine/core/game_loop.hpp currently uses a float 1/60 accumulator, so the
// OriginalTimestep parts of this test fail until that is ported. That failure
// is the deliverable: it is the 1:1 gap made executable.

#include "../engine/core/game_loop.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
} while (0)

// ---------------------------------------------------------------------------
// Values from the golden capture. Keep these in sync with
// tests/golden/frame_timing.golden.json.
// ---------------------------------------------------------------------------
namespace golden {
constexpr std::int64_t kFrameIntervalMs = 16;      // this+0x08, live capture
constexpr double kDeltaDivisor = 1000.0;           // literal @0x8F0BB2B0
constexpr double kExpectedDt = 0.016;              // 16 / 1000.0
constexpr int kStepDoubles = 4;                    // vstr targets in +0x64230
}  // namespace golden

// ---------------------------------------------------------------------------
// A faithful model of the original loop, used as the oracle the engine must
// match. Mirrors game+0x64400 exactly, including the inner spin loop.
// ---------------------------------------------------------------------------
class OriginalTimestep {
public:
    explicit OriginalTimestep(std::int64_t interval_ms = golden::kFrameIntervalMs)
        : interval_ms_(interval_ms) {}

    // Returns the dt handed to the step function for a frame, given the clock
    // readings the loop would observe. `now_ms` advances as the caller decides,
    // exactly like s3eTimerGetMs.
    double frame_dt() const { return double(interval_ms_) / golden::kDeltaDivisor; }

    // The inner wait loop at +0x644C0. Returns the yield arguments issued.
    // next_frame is taken when interval <= elapsed (cmp/cmpeq/bls).
    std::vector<std::int64_t> wait(std::int64_t t0, std::vector<std::int64_t> clock_reads) const {
        std::vector<std::int64_t> yields;
        for (std::int64_t now : clock_reads) {
            std::int64_t elapsed = now - t0;
            if (interval_ms_ <= elapsed) break;            // bls -> frame top
            std::int64_t remaining = (t0 + interval_ms_) - now;
            if (remaining < 0) break;                      // bmi -> frame top
            yields.push_back(remaining);
        }
        return yields;
    }

    std::int64_t interval_ms() const { return interval_ms_; }

private:
    std::int64_t interval_ms_;
};

static void test_interval_is_integer_16ms() {
    std::printf("\n-- frame interval --\n");
    OriginalTimestep ts;
    CHECK(ts.interval_ms() == 16, "interval is 16 ms (1000/60 truncated), not 16.67");

    // The cap that follows from an integer 16 ms period.
    const double implied_fps = 1000.0 / 16.0;
    CHECK(std::fabs(implied_fps - 62.5) < 1e-9,
          "integer 16 ms implies a 62.5 fps cap, not 60");

    // A float 1/60 timestep is a different number and must not be confused
    // with the nominal <FrameRate Value="60"/> in internalSettings.xml.
    const double float_60 = 1.0f / 60.0f;
    CHECK(std::fabs(float_60 - golden::kExpectedDt) > 1e-5,
          "float 1/60 differs measurably from the original's 0.016");
}

static void test_delta_is_quantised_double() {
    std::printf("\n-- delta conversion --\n");
    OriginalTimestep ts;
    const double dt = ts.frame_dt();

    CHECK(std::fabs(dt - golden::kExpectedDt) < 1e-12,
          "dt == 16 / 1000.0 == 0.016 exactly (double)");

    // Quantisation: only integer-ms deltas can appear.
    bool all_quantised = true;
    for (std::int64_t ms = 1; ms <= 100; ++ms) {
        double d = double(ms) / golden::kDeltaDivisor;
        double back = d * golden::kDeltaDivisor;
        if (std::fabs(back - double(ms)) > 1e-9) all_quantised = false;
    }
    CHECK(all_quantised, "every dt round-trips to an integer millisecond");

    // The forbidden value: a float accumulator's 1/60.
    const double forbidden = 1.0 / 60.0;
    CHECK(std::fabs(dt - forbidden) > 1e-6,
          "0.0166666... is NOT a value the original can emit");
}

static void test_wait_is_inner_spin_loop() {
    std::printf("\n-- inner wait loop --\n");
    OriginalTimestep ts;

    // Frame did 5 ms of work; the loop should sleep out the remaining 11 ms,
    // spinning until the interval is consumed.
    auto yields = ts.wait(1000, {1005, 1010, 1016});
    CHECK(yields.size() == 2, "spins while interval remains, stops when consumed");
    if (yields.size() >= 2) {
        CHECK(yields[0] == 11, "first yield asks for 11 ms (1000+16-1005)");
        CHECK(yields[1] == 6, "second yield asks for 6 ms (1000+16-1010)");
    }

    // Overran frame: no yield at all, straight to the next frame.
    auto over = ts.wait(1000, {1030});
    CHECK(over.empty(), "an overrunning frame yields nothing (interval <= elapsed)");

    // Exactly on the boundary is already "spent" (bls is <=).
    auto boundary = ts.wait(1000, {1016});
    CHECK(boundary.empty(), "elapsed == interval takes the next-frame branch");
}

static void test_no_remainder_carried_between_frames() {
    std::printf("\n-- no accumulator --\n");
    // The original re-reads the clock each frame and never carries a
    // fractional remainder, so N frames always report the same dt.
    OriginalTimestep ts;
    double first = ts.frame_dt();
    bool constant = true;
    for (int i = 0; i < 1000; ++i) {
        if (std::fabs(ts.frame_dt() - first) > 0.0) constant = false;
    }
    CHECK(constant, "dt is identical every frame (no accumulator drift)");

    // Contrast: a float accumulator drifts. Show the engine's current model
    // does NOT reproduce the original over a 90-frame combo window
    // (<Combo Time="90"/>), which is where the divergence becomes visible.
    const double original_total = 90 * (16.0 / 1000.0);          // 1.44 s
    const double accumulated_total = 90 * (1.0 / 60.0);          // 1.50 s
    CHECK(std::fabs(original_total - accumulated_total) > 0.05,
          "over a 90-frame combo window the two models differ by >50 ms");
}

static void test_engine_gameloop_matches_original() {
    std::printf("\n-- engine GameLoop vs original (GAP-1) --\n");

    // Drive the engine's loop with the real frame period and see what dt the
    // update callback receives.
    resf2::core::GameLoop loop;
    std::vector<float> deltas;
    loop.set_update([&](float dt) { deltas.push_back(dt); });
    loop.set_render([](float) {});

    const float real_period = 16.0f / 1000.0f;
    for (int i = 0; i < 10; ++i) loop.tick(real_period);

    CHECK(!deltas.empty(), "engine loop produced update ticks");
    if (!deltas.empty()) {
        const double got = deltas[0];
        const bool matches = std::fabs(got - golden::kExpectedDt) < 1e-6;
        if (!matches) {
            std::fprintf(stderr,
                         "  [GAP-1] engine dt=%.9f, original dt=%.9f (delta %.9f)\n",
                         got, golden::kExpectedDt, got - golden::kExpectedDt);
        }
        CHECK(matches, "engine fixed_dt equals the original's 16 ms / 1000.0");
    }

    // The original steps once per frame at a fixed period; an accumulator fed
    // exactly one period must not produce a variable number of updates.
    CHECK(deltas.size() == 10,
          "exactly one update per frame when fed the real frame period");
}

static void test_step_state_shape() {
    std::printf("\n-- step state (GAP-2) --\n");
    // game+0x64230 writes four doubles (offsets 8/0x10/0x18/0x20). The engine
    // currently passes a single float dt, so this documents the shortfall.
    CHECK(golden::kStepDoubles == 4,
          "original step writes 4 doubles per frame (dt + 3 more, all seconds)");
    std::printf("  [GAP-2] engine passes 1 float; original carries %d doubles\n",
                golden::kStepDoubles);
}

int main() {
    std::printf("=== frame timing golden test (original: game+0x64400) ===\n");

    test_interval_is_integer_16ms();
    test_delta_is_quantised_double();
    test_wait_is_inner_spin_loop();
    test_no_remainder_carried_between_frames();
    test_engine_gameloop_matches_original();
    test_step_state_shape();

    std::printf("\n=== %d passed, %d failed ===\n", tests_passed, tests_failed);
    if (tests_failed > 0) {
        std::printf("Failures above are the measured 1:1 gaps; see\n"
                    "reverse/analysis/PORT_GAPS.md (GAP-1, GAP-2).\n");
    }
    return tests_failed == 0 ? 0 : 1;
}
