// tests/integration/test_soak_movement_defects.cpp
//
// Soak-fix Wave 2 (reverse/analysis/SOAK_TRIAGE.md §2): behavioral tests for
// the movement/input defects M1-M5, written from the player's perspective —
// each asserts what the player SEES, not what the pipeline traces.
//
//   M1: back roll fires for BOTH key orders — A-then-S (A held, then S) and
//       S-then-A. The soak showed only the near-simultaneous S+A working:
//       A first started a back step and the later S press never produced a
//       roll.
//   M2: back roll / back handflip displacement must match the authored
//       NPivot root motion in the .bin data (back_roll net -350, forward_roll
//       net +404) — not the ~10px crawl the soak showed. Pins the ratio back
//       vs forward at >= 0.5 of the authored ratio (350/404 ~ 0.87).
//   M3: jump (W alone) must not drift horizontally — its authored NPivot X
//       wobbles ±6 units (net 0), which the soak rendered as a visible
//       left/right drift (~0.10 units/frame). Peak in-jump horizontal drift
//       must stay under 4 units.
//   M4: d+d within the 300 ms window fires DoubleStepForward AND the dash
//       actually travels (authored +220); spaced single D taps must each
//       complete a full step and never walk the fighter backward (the old
//       align-snap bug gained 11.6 and lost 19.5 per tap).
//   M5: when a move ends BEHIND the opponent, facing does NOT snap around
//       at move end — and it does NOT turn on a movement input either
//       (VERIFY_W11 Q2 GREEN: StepForward/StepBack carry no SetDirection, a
//       back-walk keeps facing). The mirror changes ONLY at a Controlled
//       move start (an attack), rendered as a short rotation (the blend
//       sweeps, it does not jump ±1 in one frame).

#include "../headless_test_runner.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Suppress noisy stdout from the game's internal logging so the test
// doesn't time out from I/O overhead. FAIL diagnostics go to stderr.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;

// ---------- deterministic key driving ----------
// run_frames() calls poll_events() FIRST, which clears keys_just_pressed, so
// an edge injected between run_frames() calls is wiped before on_update().
// Edges must therefore be delivered on a manually-driven frame (poll, inject,
// update, render, advance) — the same pattern tap_key() uses. The manual
// frame must NOT poll again, or it would wipe the edge it was built to carry.

static void frame(resf2::test::HeadlessTestRunner& r) {
    r.game().on_update(r.platform(), 16);
    r.game().on_render(r.platform());
    r.platform().advance_time_ms(16);
}

// Press edge + one update frame with the key held.
static void edge_down(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_down(k);
    frame(r);
}

// Release edge + one update frame with the key released.
static void edge_up(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_up(k);
    frame(r);
}

// Press several keys in the SAME frame (S+A roll input), hold `hold` frames,
// then release them all.
static void press_combo(resf2::test::HeadlessTestRunner& r,
                        std::initializer_list<plat::Key> keys, int hold) {
    r.platform().poll_events();
    for (auto k : keys) r.platform().inject_key_down(k);
    for (int i = 0; i < hold; ++i) frame(r);
    r.platform().poll_events();
    for (auto k : keys) r.platform().inject_key_up(k);
    frame(r);
}

static void run_until_idle_settled(resf2::test::HeadlessTestRunner& r) {
    // After the intro stance breaks, wait for the resulting step to finish
    // its cycle and the fighter to settle in stance_idle.
    for (int i = 0; i < 80; ++i) {
        r.run_frames(1);
        if (!r.game().host_get_start_stance() &&
            r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
    r.run_frames(10);
}

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

// Every scenario starts with the battle intro: the start-stance animation
// must run to completion before the A6 hold can be broken. In the dojo the
// stance plays after a 10 fps animation, so its hit_anim_ countdown runs to
// ~325 engine frames; the break-input's own step then plays out.
static void warm_up(resf2::test::HeadlessTestRunner& r) {
    r.run_frames(330);              // intro stance animation runs to completion
    r.tap_key(plat::Key::D, 2);     // first input ends the A6 hold
    run_until_idle_settled(r);
}

int main() {
    std::printf("=== Soak Movement Defects Test (M1-M5) ===\n");
    std::fflush(stdout);
    suppress_stdout();

    // ---- M1: back roll fires for either key order ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M1 init() returned false\n"); return 1; }
        warm_up(runner);

        // (a) A pressed first (back step starts), then S — the soak's broken
        //     order. The roll must fire on the S press.
        edge_down(runner, plat::Key::A);
        for (int i = 0; i < 12 && runner.game().host_get_player_move_state() != 1; ++i)
            runner.run_frames(1);
        CHECK(runner.game().host_get_player_move_state() == 1,
              "M1a: A starts the back step");
        edge_down(runner, plat::Key::S);  // a+s
        bool rolled = false;
        for (int i = 0; i < 12; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "back_roll") { rolled = true; break; }
        }
        CHECK(rolled, "M1a: A-then-S fires the back roll");
        runner.platform().poll_events();
        runner.platform().inject_key_up(plat::Key::A);
        runner.platform().inject_key_up(plat::Key::S);
        frame(runner);
        // Let the roll play out before the next scenario.
        for (int i = 0; i < 130 && runner.game().host_get_player_move_state() == 10; ++i)
            runner.run_frames(1);
        run_until_idle_settled(runner);

        // (b) S pressed first (duck), then A — the other order must also fire.
        edge_down(runner, plat::Key::S);
        for (int i = 0; i < 6; ++i) runner.run_frames(1);
        edge_down(runner, plat::Key::A);  // s+a
        rolled = false;
        for (int i = 0; i < 12; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "back_roll") { rolled = true; break; }
        }
        CHECK(rolled, "M1b: S-then-A fires the back roll");
        runner.platform().poll_events();
        runner.platform().inject_key_up(plat::Key::S);
        runner.platform().inject_key_up(plat::Key::A);
        frame(runner);

        // (c) S+A in the same frame (the order that already worked) must not
        //     regress.
        for (int i = 0; i < 130 && runner.game().host_get_player_move_state() == 10; ++i)
            runner.run_frames(1);
        run_until_idle_settled(runner);
        press_combo(runner, {plat::Key::S, plat::Key::A}, 4);
        rolled = false;
        for (int i = 0; i < 12; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "back_roll") { rolled = true; break; }
        }
        CHECK(rolled, "M1c: simultaneous S+A still fires the back roll");
    }

    // ---- M2: rolls travel the authored root-motion distance ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M2 init() returned false\n"); return 1; }
        warm_up(runner);

        // Forward roll (S+D): authored NPivot net +404 over 26 frames.
        press_combo(runner, {plat::Key::S, plat::Key::D}, 6);
        float fwd_x0 = 0.0f;
        bool fwd_started = false;
        for (int i = 0; i < 10; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "forward_roll") {
                fwd_x0 = runner.game().host_get_player_pos_x();
                fwd_started = true;
                break;
            }
        }
        CHECK(fwd_started, "M2a: forward roll fires on S+D");
        for (int i = 0; i < 130 && runner.game().host_get_player_move_state() == 10; ++i)
            runner.run_frames(1);
        for (int i = 0; i < 10; ++i) runner.run_frames(1);
        const float dx_fwd = runner.game().host_get_player_pos_x() - fwd_x0;
        std::fprintf(stderr, "  [M2a] forward_roll displacement = %.1f (authored +404)\n", dx_fwd);
        CHECK(dx_fwd >= 150.0f, "M2a: forward roll travels >= 150 units");

        // Back roll (S+A): authored NPivot net -350 over 31 frames. Run in a
        // FRESH runner: the forward roll above crosses the bag, which flips
        // the facing — S+A would then read as S+forward. The back-roll
        // scenario needs the pre-crossing orientation (bag to the right).
        {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M2b init() returned false\n"); return 1; }
        warm_up(runner);
        press_combo(runner, {plat::Key::S, plat::Key::A}, 6);
        float back_x0 = 0.0f;
        bool back_started = false;
        for (int i = 0; i < 10; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "back_roll") {
                back_x0 = runner.game().host_get_player_pos_x();
                back_started = true;
                break;
            }
        }
        CHECK(back_started, "M2b: back roll fires on S+A");
        for (int i = 0; i < 130 && runner.game().host_get_player_move_state() == 10; ++i)
            runner.run_frames(1);
        for (int i = 0; i < 10; ++i) runner.run_frames(1);
        const float dx_back = runner.game().host_get_player_pos_x() - back_x0;
        std::fprintf(stderr, "  [M2b] back_roll displacement = %.1f (authored -350)\n", dx_back);
        CHECK(-dx_back >= 150.0f, "M2b: back roll travels >= 150 units");
        CHECK(dx_fwd >= 1.0f && (-dx_back) / dx_fwd >= 0.5f,
              "M2c: back roll travels at least half the forward roll's distance");
        }
    }

    // ---- M3: jump does not drift horizontally ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M3 init() returned false\n"); return 1; }
        warm_up(runner);

        // Jump from a live forward step (the soak's drift case): the jump
        // follows the non-aligned step, so the authored NPivot X wobble
        // (±6 units) is applied raw. W while step_forward is playing — the
        // direction key is RELEASED first so W resolves to "Up" (JumpUp),
        // not "UpForward" (front flip).
        edge_down(runner, plat::Key::D);
        for (int i = 0; i < 5; ++i) runner.run_frames(1);
        edge_up(runner, plat::Key::D);
        for (int i = 0; i < 2; ++i) runner.run_frames(1);  // step still playing
        const float jump_x0 = runner.game().host_get_player_pos_x();
        edge_down(runner, plat::Key::W);  // jump mid-step
        bool jumped = false;
        float peak = 0.0f;
        for (int i = 0; i < 140; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "jump") {
                jumped = true;
                const float d = std::fabs(runner.game().host_get_player_pos_x() - jump_x0);
                if (d > peak) peak = d;
            } else if (i > 5) {
                break;  // jump finished
            }
        }
        std::fprintf(stderr, "  [M3] jump fired=%d peak X drift = %.2f units (authored wobble ~6, soak ~0.10/frame)\n",
                     (int)jumped, peak);
        CHECK(jumped, "M3: W mid-step fires the jump");
        CHECK(peak < 4.0f, "M3: jump horizontal drift stays under 4 units");
        runner.platform().poll_events();
        runner.platform().inject_key_up(plat::Key::D);
        runner.platform().inject_key_up(plat::Key::W);
        frame(runner);
    }

    // ---- M4: double-tap dashes and spaced-tap stepping ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M4 init() returned false\n"); return 1; }
        warm_up(runner);

        // (a) d+d within the 300 ms window -> DoubleStepForward, and the
        //     dash travels (authored +220 over 17 frames).
        edge_down(runner, plat::Key::D);
        runner.run_frames(2);
        edge_up(runner, plat::Key::D);
        runner.run_frames(3);
        edge_down(runner, plat::Key::D);  // 7 frames (~112 ms) after the first tap
        runner.run_frames(2);
        edge_up(runner, plat::Key::D);
        bool dashed = false;
        float dash_x0 = 0.0f;
        for (int i = 0; i < 12; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_anim() == "double_step_forward") {
                dashed = true;
                dash_x0 = runner.game().host_get_player_pos_x();
                break;
            }
        }
        CHECK(dashed, "M4a: d+d within 300 ms fires the dash");
        for (int i = 0; i < 90 && runner.game().host_get_player_move_state() == 10; ++i)
            runner.run_frames(1);
        for (int i = 0; i < 8; ++i) runner.run_frames(1);
        const float dx_dash = runner.game().host_get_player_pos_x() - dash_x0;
        std::fprintf(stderr, "  [M4a] dash displacement = %.1f (authored +220)\n", dx_dash);
        CHECK(dx_dash >= 100.0f, "M4a: the dash actually travels >= 100 units");

        // (b) spaced single taps (outside the dash window) must each complete
        //     a full forward step and never walk the fighter backward (the
        //     old align-snap regression was a net -7.9 per tap). Fresh runner:
        //     the M4a dash crosses the bag, which flips the facing — D would
        //     then read as "back" and the taps would walk toward the bag.
        {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M4b init() returned false\n"); return 1; }
        warm_up(runner);

        // One tap, then wait for the step to end on its own: the walk must
        // play out its authored cycle (step_forward: 16 frames at 20 fps =
        // 800 ms, NPivot +66 units) instead of being cut at 16 ENGINE frames
        // (~5 animation frames, ~25 units — the soak's "small steps without
        // waiting for walk animation end").
        const float x0 = runner.game().host_get_player_pos_x();
        edge_down(runner, plat::Key::D);
        runner.run_frames(2);
        edge_up(runner, plat::Key::D);
        for (int i = 0; i < 60; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_player_move_state() == 0 &&
                runner.game().host_get_player_anim() == "stance_idle")
                break;
        }
        const float dx_tap = runner.game().host_get_player_pos_x() - x0;
        std::fprintf(stderr, "  [M4b] single tap walked %.1f of the authored %.0f-unit cycle\n", dx_tap, 66.0);
        CHECK(dx_tap >= 50.0f, "M4b: a single tap walks the full step cycle (>= 50 units)");

        // 4 spaced taps (outside the dash window) must keep advancing and
        // never walk the fighter backward.
        float prev_x = runner.game().host_get_player_pos_x();
        float min_advance = 1e9f;
        for (int t = 0; t < 4; ++t) {
            edge_down(runner, plat::Key::D);
            runner.run_frames(2);
            edge_up(runner, plat::Key::D);
            runner.run_frames(18);  // ~336 ms period -> outside the dash window
            const float x = runner.game().host_get_player_pos_x();
            min_advance = std::min(min_advance, x - prev_x);
            prev_x = x;
        }
        std::fprintf(stderr, "  [M4b] 4 spaced taps advanced %.1f total, min step %.1f\n",
                     prev_x - x0, min_advance);
        // [Soak-fix Wave 9A] F3: the stance idle is a PLANTED stance (the
        // idle's authored NPivot wander no longer translates the fighter).
        // A sampling window that spans a step boundary (step tail + idle +
        // next step head) therefore advances ~10 units — the old 10.0 floor
        // was only reachable because the idle wander padded it to ~10.7.
        // 8.0 keeps a margin over the actual regression this check guards
        // (the pre-M4 cancel/back-walk net was NEGATIVE, -7.9 per tap).
        CHECK(min_advance >= 8.0f, "M4b: every single tap completes a forward step (no cancel/back-walk)");
        CHECK(prev_x - x0 >= 80.0f, "M4b: 4 spaced taps advance >= 80 units total");
        }
    }

    // ---- M5: deferred turn + smooth mirror sweep ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: M5 init() returned false\n"); return 1; }
        warm_up(runner);

        const float bag_x = runner.game().host_get_enemy_pos_x();
        const bool bag_right = bag_x > runner.game().host_get_player_pos_x();
        const plat::Key dash_key = bag_right ? plat::Key::D : plat::Key::A;
        std::fprintf(stderr, "  [M5] bag_x=%.1f player_x=%.1f bag_to_the_right=%d\n",
                     bag_x, runner.game().host_get_player_pos_x(), (int)bag_right);

        // Dash toward the bag until the fighter is past it.
        bool crossed = false;
        for (int dash = 0; dash < 3 && !crossed; ++dash) {
            edge_down(runner, dash_key);
            runner.run_frames(2);
            edge_up(runner, dash_key);
            runner.run_frames(3);
            edge_down(runner, dash_key);  // second tap -> dash
            runner.run_frames(2);
            edge_up(runner, dash_key);
            for (int i = 0; i < 90 && runner.game().host_get_player_move_state() == 10; ++i)
                runner.run_frames(1);
            for (int i = 0; i < 10; ++i) runner.run_frames(1);
            const float px = runner.game().host_get_player_pos_x();
            // [Soak-fix Wave 9A] F3: the idle is planted, so the dash must
            // cross on its own root motion — it lands at ~bag_x+30 (measured
            // +22.9 vs the bag at -7). The old +30 threshold was only
            // reachable because the idle's authored wander added ~3 free
            // units during the approach (the drift F3 removes). +20 = past
            // the bag's body with margin, still "behind the bag".
            crossed = bag_right ? (px > bag_x + 20.0f) : (px < bag_x - 20.0f);
        }
        CHECK(crossed, "M5: the fighter crosses behind the bag via a dash");
        if (crossed) {
            // The move ended behind the opponent: facing must NOT have
            // snapped around at move end — it stays the stale pre-crossing
            // direction until the player gives a movement input.
            const bool stale_facing = runner.game().host_get_player_facing();
            runner.run_frames(20);
            CHECK(runner.game().host_get_player_facing() == stale_facing,
                  "M5a: no auto-turn during the idle frames after the move");
            CHECK(stale_facing == bag_right,
                  "M5a: facing still points the pre-crossing direction (no snap)");

            // Next movement input: the toward-key is the OPPOSITE absolute
            // key now (the bag sits on the other side), and the fighter
            // faces AWAY from it — so this press is a BACK-WALK. Back-walks
            // NEVER turn (VERIFY_W11 Q2 GREEN: StepForward/StepBack carry
            // no SetDirection — walking keeps facing; the old
            // turn-on-fresh-input turned the fighter on a back-walk press,
            // exactly the reported "turns away from the enemy" bug). The
            // step must still cover ground toward the bag.
            const plat::Key toward_key = bag_right ? plat::Key::A : plat::Key::D;
            edge_down(runner, toward_key);
            bool back_walk_turned = false;
            for (int i = 0; i < 6; ++i) {
                runner.run_frames(1);
                if (runner.game().host_get_player_facing() != bag_right) { back_walk_turned = true; break; }
            }
            CHECK(!back_walk_turned,
                  "M5b: the back-walk NEVER turns the fighter (facing only at "
                  "Controlled move start)");

            const float x_walk = runner.game().host_get_player_pos_x();
            bool walked_back = false;
            for (int i = 0; i < 40; ++i) {
                runner.run_frames(1);
                const float px = runner.game().host_get_player_pos_x();
                const bool toward = bag_right ? (px < x_walk - 10.0f) : (px > x_walk + 10.0f);
                if (toward) { walked_back = true; break; }
            }
            CHECK(walked_back,
                  "M5b: the back-walk still steps toward the opponent (no turn, "
                  "but the ground is covered)");
            edge_up(runner, toward_key);

            // Attack start = SetDirection: the next CONTROLLED move (an O
            // punch) turns the fighter to face the enemy at its start — the
            // ONE place the mirror may change (VERIFY_W11 Q2 GREEN).
            edge_down(runner, plat::Key::O);
            bool attack_turned = false;
            for (int i = 0; i < 6; ++i) {
                runner.run_frames(1);
                if (runner.game().host_get_player_facing() != bag_right) { attack_turned = true; break; }
            }
            CHECK(attack_turned,
                  "M5c: the attack (Controlled move start) turns the fighter "
                  "to face the enemy");

            // The turn renders as a short rotation: the mirror blend must be
            // mid-sweep a few frames in, not a one-frame ±1 jump.
            const float blend3 = runner.game().host_get_player_turn_blend();
            runner.run_frames(2);
            const float blend5 = runner.game().host_get_player_turn_blend();
            std::fprintf(stderr, "  [M5c] turn blend at +1/+3 frames: %.2f / %.2f\n", blend3, blend5);
            CHECK(std::fabs(blend3) < 0.99f || std::fabs(blend5) < 0.99f,
                  "M5c: the turn sweeps through intermediate mirror positions");
            edge_up(runner, plat::Key::O);
        }
    }

    // ---- Final verdict ----
    if (tests_failed > 0) {
        std::fprintf(stderr, "\n=== SOAK MOVEMENT DEFECTS TEST FAILED (%d failures) ===\n",
                     tests_failed);
        return 1;
    }
    std::fprintf(stderr, "\n=== SOAK MOVEMENT DEFECTS TEST PASSED ===\n");
    return 0;
}
