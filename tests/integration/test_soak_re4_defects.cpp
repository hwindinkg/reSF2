// tests/integration/test_soak_re4_defects.cpp
//
// Soak re-soak-4 regressions (after R1 render / R2 combat landed on HEAD):
//
//   R4a (render): the DEFAULT save (assets/user.xml Armor="Body"
//   Helm="Head") makes the armor pass re-render body.xml over the whole
//   fighter and the helm pass re-render head.xml — the player is drawn
//   TWICE ("голова вытянута, тело непойми как") while the enemy (no armor
//   pass) looks right. list.xml: <Item Name="Body" Model="body"
//   Type="Armor" ShopHide=1 Hidden=1> — the naked fighter, not armor.
//
// RED on HEAD: the default-save armor/helm overlay capsules drawn > 0.

#include "../headless_test_runner.hpp"
#include "../engine/game/ui_scale.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;
namespace scn = resf2::scene;

static resf2::test::HeadlessTestRunner make_dojo_runner(bool hermetic) {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = hermetic;
    return resf2::test::HeadlessTestRunner(config);
}

static void frame(resf2::test::HeadlessTestRunner& r) {
    r.game().on_update(r.platform(), 16);
    r.game().on_render(r.platform());
    r.platform().advance_time_ms(16);
}

static void edge_down(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_down(k);
    frame(r);
}

static void edge_up(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_up(k);
    frame(r);
}

// Same warm-up as the re-soak-3 suite: loading + location init, intro
// stance to completion, first input ends the A6 hold, settle into idle.
static void warm_up(resf2::test::HeadlessTestRunner& r) {
    r.run_frames(330);
    r.tap_key(plat::Key::D, 2);
    for (int i = 0; i < 80; ++i) {
        r.run_frames(1);
        if (!r.game().host_get_start_stance() &&
            r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
    r.run_frames(10);
}

// ---------- R4a: the default save must NOT double-draw the fighter ----------
//
// The shipped save equips Armor="Body" Helm="Head" — the list.xml hidden
// base items whose Model is body.xml/head.xml, i.e. the NAKED fighter the
// body pass already renders. The armor/helm overlay pass re-drew that same
// geometry in {34,31,27} (margins ignored) — "тело непойми как, голова
// вытянута" (re-soak-4). Probe: with the DEFAULT save equipment, zero
// armor/helm overlay capsules are drawn; equipping a REAL armor (ARMOR_ROBE
// -> armor_robe.xml) still draws its overlay (the R1 contract).
static void test_r4_default_save_no_double_draw() {
    std::printf("\n=== R4a: default save renders the fighter once ===\n");
    // Hermetic dojo (the fighter actually renders; the non-hermetic dojo
    // sits in the tutorial dialogue scene). Reproduce the shipped save's
    // equipment: the hidden base items Armor="Body" Helm="Head" (list.xml
    // Model="body"/"head" — the NAKED fighter).
    resf2::test::HeadlessTestRunner runner = make_dojo_runner(true);
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R4a init() failed\n"); ++tests_failed; return; }

    runner.game().host_add_item("Body");
    runner.game().host_add_item("Head");
    const bool body_equipped = runner.game().host_equip_item("Body");
    const bool head_equipped = runner.game().host_equip_item("Head");
    CHECK(body_equipped && head_equipped,
          "R4a: the default-save items equip (Armor=\"Body\" Helm=\"Head\")");

    warm_up(runner);

    const int armor_drawn = runner.game().host_get_armor_capsules_drawn();
    const int helm_drawn = runner.game().host_get_helm_capsules_drawn();
    std::fprintf(stderr, "  [R4a] default save: armor overlay capsules drawn=%d helm=%d\n",
                 armor_drawn, helm_drawn);
    CHECK(armor_drawn == 0,
          "R4a: Armor=\"Body\" draws NO armor overlay (body.xml is the naked fighter)");
    CHECK(helm_drawn == 0,
          "R4a: Helm=\"Head\" draws NO helm overlay (head.xml is the naked head)");

    // The real-armor contract survives: ARMOR_ROBE still overlays.
    runner.game().host_add_item("ARMOR_ROBE");
    const bool equipped = runner.game().host_equip_item("ARMOR_ROBE");
    CHECK(equipped, "R4a: the robe equips");
    runner.run_frames(5);
    const int robe_drawn = runner.game().host_get_armor_capsules_drawn();
    std::uint8_t cr = 0, cg = 0, cb = 0;
    runner.game().host_get_armor_render_color(cr, cg, cb);
    std::fprintf(stderr, "  [R4a] robe equipped: armor overlay capsules drawn=%d model=%zu color=(%d,%d,%d)\n",
                 robe_drawn, runner.game().host_get_armor_model_capsule_count(),
                 cr, cg, cb);
    CHECK(robe_drawn > 0,
          "R4a: equipping a REAL armor (ARMOR_ROBE) still draws its overlay");
}

int main() {
    std::printf("=== Soak re-soak-4 regression probes (R4a render) ===\n");
    std::fflush(stdout);

    suppress_stdout();

    test_r4_default_save_no_double_draw();

    std::printf("\n=== re-soak-4 probes: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    std::fflush(stdout);
    return tests_failed == 0 ? 0 : 1;
}
