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

static resf2::test::HeadlessTestRunner make_battle_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // deterministic empty inventory
    config.start_scene = "battle";
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

static float enemy_dist(resf2::test::HeadlessTestRunner& r) {
    return std::fabs(r.game().host_get_enemy_pos_x() -
                     r.game().host_get_player_pos_x());
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

// ---------- R4b: weapon attack edges resolve against the WEAPON model ----------
//
// Q2-B: weapon moves reference the WEAPON model's edges (WEAPON_SWORDS-
// Blade_2 ...). The probe resolves an attack edge with the SAME law the
// battle hit test uses: skeleton edge first, then the equipped weapon
// model's edge (MacroNode LCC law, radius from the weapon edge).
static void test_r4_weapon_edge_resolution() {
    std::printf("\n=== R4b: weapon attack edges resolve on the weapon model ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner(true);
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R4b init() failed\n"); ++tests_failed; return; }

    runner.game().host_add_item("WEAPON_MACHETE");
    const bool equipped = runner.game().host_equip_item("WEAPON_MACHETE");
    CHECK(equipped, "R4b: the machete equips (WEAPON_MACHETE -> SubType Machete)");
    warm_up(runner);

    // A body edge still resolves through the skeleton.
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, r = -1;
    const bool body_edge = runner.game().host_resolve_attack_edge(
        "EHand_2", x1, y1, x2, y2, r);
    std::fprintf(stderr, "  [R4b] EHand_2 resolved=%d r=%.1f span=%.1f\n",
                 (int)body_edge, r, std::hypot(x2 - x1, y2 - y1));
    CHECK(body_edge, "R4b: a skeleton attack edge resolves (EHand_2)");

    // The machete model's blade edge (SwordsSlash AttackingParts).
    float bx1 = 0, by1 = 0, bx2 = 0, by2 = 0, br = -1;
    const bool blade_edge = runner.game().host_resolve_attack_edge(
        "WEAPON_SWORDS-Blade_2", bx1, by1, bx2, by2, br);
    const float bspan = std::hypot(bx2 - bx1, by2 - by1);
    std::fprintf(stderr, "  [R4b] WEAPON_SWORDS-Blade_2 resolved=%d r=%.1f span=%.1f\n",
                 (int)blade_edge, br, bspan);
    CHECK(blade_edge, "R4b: a weapon attack edge resolves on the weapon model");
    CHECK(br > 3.0f, "R4b: the blade edge carries its authored radius (>3)");
    CHECK(bspan > 30.0f,
          "R4b: the blade segment is the full blade (not the wrist guess)");

    // Unmatched names (e.g. another weapon's edge) resolve to false.
    float ux1 = 0, uy1 = 0, ux2 = 0, uy2 = 0, ur = -1;
    const bool unknown = runner.game().host_resolve_attack_edge(
        "WEAPON_KNIVES-Edge17_1", ux1, uy1, ux2, uy2, ur);
    CHECK(!unknown, "R4b: a foreign weapon's edge does not resolve");
}

// ---------- R4b: the J/U cycle follows the OWNED weapons ----------
//
// HARDCODE_AUDIT H01: the hardcoded tactic cycle handed out weapons the
// save doesn't own — pressing U on a knives-only save produced Machete.
// The cycle must be Fists + the owned list.xml Type="Weapon" subtypes (in
// list.xml order), with NO magic/ranged names.
static void test_r4_weapon_cycle_owned_only() {
    std::printf("\n=== R4b: the J/U weapon cycle = owned weapons ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner(true);
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R4b cycle init() failed\n"); ++tests_failed; return; }

    runner.game().host_add_item("WEAPON_KNIVES");
    runner.game().host_add_item("WEAPON_MACHETE");
    const bool equipped = runner.game().host_equip_item("WEAPON_KNIVES");
    CHECK(equipped, "R4b: the knives equip (sync rebuilds the cycle)");

    const std::vector<std::string> cycle = runner.game().host_get_weapon_cycle();
    std::string joined;
    for (const auto& w : cycle) joined += (joined.empty() ? "" : ",") + w;
    std::fprintf(stderr, "  [R4b] cycle = [%s]\n", joined.c_str());
    bool has_fists = false, has_knives = false, has_machete = false, has_magic = false;
    for (const auto& w : cycle) {
        if (w == "Fists") has_fists = true;
        if (w == "Knives") has_knives = true;
        if (w == "Machete") has_machete = true;
        if (w == "FireBall" || w == "Energyball" || w == "Shuriken" ||
            w == "LightningArrow" || w == "Rifle")
            has_magic = true;
    }
    CHECK(has_fists && has_knives && has_machete,
          "R4b: the cycle lists Fists + the two OWNED weapons");
    CHECK(!has_magic,
          "R4b: no magic/ranged names in the melee weapon cycle");
}

// ---------- R4b/R4c: the machete deals damage at its authored reach ----------
//
// SwordsSlash (the machete's 1key punch) declares <Distance Max="350">;
// the R2 fallback was meant to use it but the nested <Tactics><Conditions>
// <Distance> never parsed — reach hardcoded 250, so a punch thrown during
// the approach (enemy at 250-350 units) missed while the headless fists
// test (reach 250) passed. The parse pins below are the deterministic RED;
// the battle probe pins the end-to-end weapon damage path (equip machete
// -> punch -> fallback/weapon-edge hit -> HP drop).
static void test_r4_machete_midrange_damage() {
    std::printf("\n=== R4b/R4c: the machete hits at its authored reach ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R4c init() failed\n"); ++tests_failed; return; }

    scn::SceneHost::BattleInfo info;
    info.enemy_name = "Dojo_Disciple";
    info.rounds = 1;
    info.round_time_s = 99;
    runner.game().host_set_battle_info(info);
    runner.game().host_set_battle_mode(true);
    runner.game().host_set_show_enemy(true);
    runner.game().host_add_item("WEAPON_MACHETE");
    runner.game().host_equip_item("WEAPON_MACHETE");
    runner.run_frames(190);  // battle intro: stance_2 plays (~162 frames at
                             // 20fps), then the A6 hold waits for input

    // R4c — the authored tactic reach must be parsed (RED on HEAD: 0).
    const float swords_reach = runner.game().host_get_move_distance_max("SwordsSlash");
    const float knives_reach = runner.game().host_get_move_distance_max("KnivesSlash");
    std::fprintf(stderr, "  [R4c] SwordsSlash distance_max=%.0f KnivesSlash=%.0f\n",
                 swords_reach, knives_reach);
    CHECK(swords_reach >= 349.0f,
          "R4c: SwordsSlash parses its authored tactic reach (350)");
    CHECK(knives_reach >= 299.0f,
          "R4c: KnivesSlash parses its authored tactic reach (300)");

    // The first A/D press ends the A6 stance hold; keep the fight in the
    // machete's mid-range band (the enemy closes to ~250 on its own).
    for (int i = 0; i < 6; ++i) {
        if (enemy_dist(runner) <= 330.0f) break;
        edge_down(runner, plat::Key::D);
        runner.run_frames(40);
        edge_up(runner, plat::Key::D);
    }
    const float d0 = enemy_dist(runner);
    const float hp0 = runner.enemy_health_frac();
    std::fprintf(stderr, "  [R4c] punching from dist=%.0f enemy_hp=%.3f\n", d0, hp0);

    // Punch: the machete swing (weapon move) must drop the enemy's HP.
    edge_down(runner, plat::Key::O);
    runner.run_frames(40);
    edge_up(runner, plat::Key::O);
    runner.run_frames(20);
    const float hp1 = runner.enemy_health_frac();
    const std::string anim = runner.game().host_get_player_anim();
    std::fprintf(stderr, "  [R4c] first punch: anim='%s' hp %.3f -> %.3f (dist=%.0f)\n",
                 anim.c_str(), hp0, hp1, enemy_dist(runner));
    CHECK(hp1 < hp0 - 0.005f,
          "R4c: the machete swing deals damage in battle (weapon move path)");
    CHECK(anim == "swords_slash" || anim == "swords_double_slash" ||
          anim == "swords_heavy_slash" || anim == "swords_spinning_slash",
          "R4c: the punch resolves to a WEAPON move (machete subtype)");
}

int main() {
    std::printf("=== Soak re-soak-4 regression probes (R4a render, R4b weapon, R4c reach) ===\n");
    std::fflush(stdout);

    suppress_stdout();

    test_r4_default_save_no_double_draw();
    test_r4_weapon_edge_resolution();
    test_r4_weapon_cycle_owned_only();
    test_r4_machete_midrange_damage();

    std::printf("\n=== re-soak-4 probes: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    std::fflush(stdout);
    return tests_failed == 0 ? 0 : 1;
}
