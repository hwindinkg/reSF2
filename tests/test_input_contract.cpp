// tests/test_input_contract.cpp
//
// Input-path contract test: pins that the Win32 GetAsyncKeyState poll path
// (glfw_platform.cpp poll_events) produces the SAME InputState timeline as
// the TestPlatform injection path (NullPlatform::inject_key_down/up) that
// every integration test drives.
//
// Why this exists (see reverse/analysis/INPUT_PATH_AUDIT.md):
//   - The user plays through run.bat: GlfwPlatform::poll_events() bypasses
//     GLFW key events on Windows (spurious RELEASE/PRESS pairs on Win10
//     19044) and samples GetAsyncKeyState once per frame.
//   - Tests inject Key events directly via NullPlatform::inject_key_*.
//   - Both write the same InputState, but the Win32 edge computation was
//     never exercised by any test — the diagnostic --input-script path
//     bypasses the poll loop entirely, and the physical key reads cannot
//     be driven headlessly. The seam is poll_key_frame(): the production
//     edge-decision function the Win32 loop calls. The test drives the
//     REAL function with scripted OS state.
//
// Scenarios: the four user-visible behaviors fixed under test (M1 roll
// both key orders, P7 held duck no auto-repeat, D4 dialogue advance on any
// key, J/U weapon cycle) are driven as press/release timelines through
// both producers; the per-frame InputState must match exactly. The one
// DOCUMENTED divergence — a tap that starts and ends between two polls is
// invisible to the poll path but visible to injection — is pinned
// explicitly (DIV-1), not hidden.
//
// Run: ctest -R test_input_contract (fast: pure platform layer, no assets).

#include "../engine/platform/input_edges.hpp"
#include "../engine/platform/platform.hpp"

#include <array>
#include <cstdio>
#include <utility>
#include <vector>

namespace plat = resf2::platform;

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);     \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// A single frame's physical change: list of (Key index, down?).
using Transition = std::pair<int, bool>;

// Apply the same physical timeline to both producers and compare the full
// InputState contract (keys_down / just_pressed / just_released for every
// key in the enum).
//
//   ws/os_down/prev : Win32 side — os_down is the OS sample the poll would
//                     read this frame; edges come from poll_key_frame().
//   tp              : TestPlatform side — poll clears edges, then the
//                     changes are injected exactly like integration tests.
static void frame_both(plat::InputState& ws,
                       std::array<bool, plat::kMaxKeys>& prev,
                       std::array<bool, plat::kMaxKeys>& os_down,
                       plat::NullPlatform& tp,
                       const std::vector<Transition>& transitions,
                       int frame_no) {
    // --- Win32 side: clear edges, sample OS state, run the poll contract.
    ws.keys_just_pressed.fill(false);
    ws.keys_just_released.fill(false);
    for (const auto& [k, down] : transitions) os_down[k] = down;
    plat::poll_key_frame(ws, prev, os_down);

    // --- TestPlatform side: poll_events() clears edges, then inject.
    (void)tp.poll_events();
    for (const auto& [k, down] : transitions) {
        if (down) tp.inject_key_down(static_cast<plat::Key>(k));
        else tp.inject_key_up(static_cast<plat::Key>(k));
    }

    // --- Compare every key the enum covers.
    const auto& ti = tp.input();
    for (int i = 0; i <= static_cast<int>(plat::Key::AltRight); ++i) {
        if (ws.keys_down[i] != ti.keys_down[i] ||
            ws.keys_just_pressed[i] != ti.keys_just_pressed[i] ||
            ws.keys_just_released[i] != ti.keys_just_released[i]) {
            std::fprintf(stderr,
                "FAIL: frame %d key[%d]: Win32(down=%d jp=%d jr=%d) vs "
                "TestPlatform(down=%d jp=%d jr=%d)\n",
                frame_no, i,
                (int)ws.keys_down[i], (int)ws.keys_just_pressed[i],
                (int)ws.keys_just_released[i],
                (int)ti.keys_down[i], (int)ti.keys_just_pressed[i],
                (int)ti.keys_just_released[i]);
            ++g_failures;
        }
    }
}

// Count just_pressed edges for one key over the recorded Win32-side frames.
static int count_edges(const std::vector<plat::InputState>& frames, int key) {
    int n = 0;
    for (const auto& f : frames)
        if (f.keys_just_pressed[key]) ++n;
    return n;
}

// ---------- M1: back roll fires for both key orders ----------
// The roll trigger is just_pressed on the SECOND key + held state on the
// FIRST (game.cpp M1 selector 3001-3094, hardcoded roll block 3194-3224).
// The contract: the second key's edge fires exactly once, on the frame its
// OS state flips — identically through both producers.
static void scenario_m1_key_orders() {
    std::printf("  [M1] roll key orders: A-then-S / S-then-A / same-frame\n");
    plat::NullPlatform tp;
    plat::InputState ws;
    std::array<bool, plat::kMaxKeys> prev{};
    std::array<bool, plat::kMaxKeys> os_down{};
    std::vector<plat::InputState> frames;
    const int A = (int)plat::Key::A, S = (int)plat::Key::S;
    int f = 0;
    auto step = [&](std::vector<Transition> t) {
        ++f;
        frame_both(ws, prev, os_down, tp, t, f);
        frames.push_back(ws);
    };

    // (a) A-then-S: A down f1..f8, S down f4..f8, both up f9.
    step({{A, true}});                              // f1
    step({}); step({});                             // f2,f3 A held
    step({{S, true}});                              // f4 — roll trigger edge
    step({}); step({}); step({}); step({});         // f5..f8 both held
    step({{A, false}, {S, false}});                 // f9
    step({});                                       // f10 idle
    CHECK(frames[0].keys_just_pressed[A], "M1a: A edge on f1");
    CHECK(frames[3].keys_just_pressed[S], "M1a: S edge on f4");
    CHECK(frames[3].keys_down[A] && frames[3].keys_down[S],
          "M1a: both held on the trigger frame");
    CHECK(frames[8].keys_just_released[A] && frames[8].keys_just_released[S],
          "M1a: release edges on f9");
    CHECK(!frames[9].keys_down[A] && !frames[9].keys_down[S],
          "M1a: idle on f10");
    CHECK(count_edges(frames, S) == 1, "M1a: S edge fires exactly once");

    // (b) S-then-A: S down f1..f6, A down f4..f6, both up f7.
    frames.clear();
    f = 0;
    step({{S, true}});                              // f1
    step({}); step({});                             // f2,f3 S held
    step({{A, true}});                              // f4 — roll trigger edge
    step({}); step({});                             // f5,f6 both held
    step({{S, false}, {A, false}});                 // f7
    CHECK(frames[3].keys_just_pressed[A], "M1b: A edge on f4");
    CHECK(frames[3].keys_down[A] && frames[3].keys_down[S],
          "M1b: both held on the trigger frame");
    CHECK(count_edges(frames, A) == 1, "M1b: A edge fires exactly once");

    // (c) S+A in the same frame: both edges on f1, held f2, up f3.
    frames.clear();
    f = 0;
    step({{S, true}, {A, true}});                   // f1
    step({});                                       // f2 held
    step({{S, false}, {A, false}});                 // f3
    CHECK(frames[0].keys_just_pressed[S] && frames[0].keys_just_pressed[A],
          "M1c: same-frame edges both fire on f1");
    CHECK(count_edges(frames, S) == 1 && count_edges(frames, A) == 1,
          "M1c: each key edges exactly once");
}

// ---------- P7: held key produces exactly one edge, no auto-repeat ----------
// The held-duck fix (game.cpp 3099-3158, 3173-3192) depends on a held key
// keeping keys_down true while just_pressed fires ONCE. The Win32 poll must
// never re-edge a held key — exactly like injection.
static void scenario_p7_held_duck() {
    std::printf("  [P7] held S: one edge over 151 frames, held throughout\n");
    plat::NullPlatform tp;
    plat::InputState ws;
    std::array<bool, plat::kMaxKeys> prev{};
    std::array<bool, plat::kMaxKeys> os_down{};
    std::vector<plat::InputState> frames;
    const int S = (int)plat::Key::S;
    int f = 0;
    auto step = [&](std::vector<Transition> t) {
        ++f;
        frame_both(ws, prev, os_down, tp, t, f);
        frames.push_back(ws);
    };

    step({{S, true}});                              // f1
    for (int i = 0; i < 149; ++i) step({});         // f2..f150 held
    step({{S, false}});                             // f151
    CHECK(count_edges(frames, S) == 1,
          "P7: 151 frames of hold produce exactly one just_pressed");
    for (int i = 0; i < 150; ++i)
        CHECK(frames[i].keys_down[S], "P7: S held through f150");
    CHECK(!frames[150].keys_down[S], "P7: S up on f151");
    CHECK(frames[150].keys_just_released[S], "P7: release edge on f151");
}

// ---------- D4: dialogue advance on any key ----------
// DialogueScene::on_update (scenes.cpp 868-877) scans keys_just_pressed for
// ANY key. The contract: a single tap exposes just_pressed for exactly one
// frame — identical through both producers.
static void scenario_d4_dialogue_advance() {
    std::printf("  [D4] dialogue advance: single P tap = one edge\n");
    plat::NullPlatform tp;
    plat::InputState ws;
    std::array<bool, plat::kMaxKeys> prev{};
    std::array<bool, plat::kMaxKeys> os_down{};
    std::vector<plat::InputState> frames;
    const int P = (int)plat::Key::P;
    int f = 0;
    auto step = [&](std::vector<Transition> t) {
        ++f;
        frame_both(ws, prev, os_down, tp, t, f);
        frames.push_back(ws);
    };

    step({{P, true}});                              // f1
    step({{P, false}});                             // f2
    step({});                                       // f3 idle
    CHECK(frames[0].keys_just_pressed[P], "D4: P edge on f1");
    CHECK(count_edges(frames, P) == 1,
          "D4: one tap = exactly one advance edge");
    CHECK(frames[0].keys_down[P] && !frames[1].keys_down[P],
          "D4: held for one frame, then released");
}

// ---------- R4b: J/U weapon cycle ----------
// game.cpp 2379-2390 cycles on just_pressed of J (next) / U (previous).
static void scenario_j_u_weapon_cycle() {
    std::printf("  [R4b] J/U cycle: each tap edges exactly once\n");
    plat::NullPlatform tp;
    plat::InputState ws;
    std::array<bool, plat::kMaxKeys> prev{};
    std::array<bool, plat::kMaxKeys> os_down{};
    std::vector<plat::InputState> frames;
    const int J = (int)plat::Key::J, U = (int)plat::Key::U;
    int f = 0;
    auto step = [&](std::vector<Transition> t) {
        ++f;
        frame_both(ws, prev, os_down, tp, t, f);
        frames.push_back(ws);
    };

    step({{J, true}}); step({{J, false}});          // f1,f2 — next
    step({});                                       // f3 idle between taps
    step({{U, true}}); step({{U, false}});          // f4,f5 — previous
    step({});                                       // f6 idle
    CHECK(frames[0].keys_just_pressed[J], "R4b: J edge on f1");
    CHECK(frames[3].keys_just_pressed[U], "R4b: U edge on f4");
    CHECK(count_edges(frames, J) == 1 && count_edges(frames, U) == 1,
          "R4b: each cycle key edges exactly once");
}

// ---------- DIV-1: sub-frame tap (the documented physical gap) ----------
// A press+release that both land between two polls: the OS sampler never
// sees the key down, so the poll path emits NO edges, while TestPlatform
// injection (which can express sub-frame events) emits both. This is the
// one deliberate, documented divergence between the paths — pinned here so
// it stays explicit instead of silently drifting.
static void scenario_sub_frame_tap_gap() {
    std::printf("  [DIV-1] sub-frame tap: TestPlatform sees it, the poll path cannot\n");
    plat::NullPlatform tp;
    plat::InputState ws;
    std::array<bool, plat::kMaxKeys> prev{};
    std::array<bool, plat::kMaxKeys> os_down{};     // all false: no poll ever saw the key

    // TestPlatform: down+up injected between polls.
    (void)tp.poll_events();
    tp.inject_key_down(plat::Key::J);
    tp.inject_key_up(plat::Key::J);

    // Win32 side: same frame, no observable OS state change.
    ws.keys_just_pressed.fill(false);
    ws.keys_just_released.fill(false);
    plat::poll_key_frame(ws, prev, os_down);

    const auto& ti = tp.input();
    CHECK(ti.keys_just_pressed[(int)plat::Key::J] &&
          ti.keys_just_released[(int)plat::Key::J],
          "DIV-1: TestPlatform expresses the sub-frame tap");
    CHECK(!ws.keys_just_pressed[(int)plat::Key::J] &&
          !ws.keys_just_released[(int)plat::Key::J],
          "DIV-1: Win32 poll cannot see a tap that never spans a poll (documented)");
}

int main() {
    std::printf("=== Input Path Contract Test (Win32 poll vs TestPlatform) ===\n");
    scenario_m1_key_orders();
    scenario_p7_held_duck();
    scenario_d4_dialogue_advance();
    scenario_j_u_weapon_cycle();
    scenario_sub_frame_tap_gap();

    if (g_failures == 0) {
        std::printf("PASS: both input producers agree on the tested contract\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL: %d contract violation(s)\n", g_failures);
    return 1;
}
