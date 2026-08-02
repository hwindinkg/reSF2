// tests/integration/test_soak_wave7b_defects.cpp
//
// Soak-fix Wave 7b (reverse/analysis/SOAK_TRIAGE.md §8 parser-level defects,
// re-soak 2): behavioral tests for P4, P5, P6, P8, P9, P11, P12 — written
// from the player's perspective, each asserting what the player SEES:
//
//   P4: battle HUD fighter names come from the active language pack
//       (eng.xml/rus.xml). stages.xml <Template FirstName="NAME_KENJI"> is
//       the localization KEY for the opponent's HUD name; a raw warrior
//       template ("Dojo_Disciple") must resolve to "NAME_KENJI" -> "KENJI"
//       in an English session, and the player is "NAME_SHADOW" -> "SHADOW".
//   P5: the fight HUD layout is pinned to the shipped atlas frames
//       (batchFightBars.plist: HealthBar_Empty 564x26, HealthBar_Full/Hit
//       1x43) and the reversed binary constants (inner gap 53 pt, fill
//       width 275 pt, bar centre 100 pt from the top). Names sit above the
//       bar's OUTER edge — never floating over the track. The framebuffer
//       is scanned for the orange fill at the expected rect.
//   P6: losing the training fight must NOT break the story: the tutorial
//       stays at FIRST_FIGHT (retry queued), the Results screen offers a
//       rematch back into Battle, victory advances to COMPLETE — and
//       nothing toggles the dojo's bag/fighter switch on its own (the soak
//       log showed "[DOJO] Switched to bag/enemy" cycling after the defeat).
//   P8: the dialogue scroll renders its textures at the sizes the source
//       frames dictate — Roll_* bar at native 74-atlas-px height (37 pt),
//       end caps at the 156x74 aspect, Paper_* sheet edges at the 116x1524
//       aspect — not the 2.5x/3.4x stretched eyeball proportions.
//   P9: the shop's left column renders the REAL fighter + equipped weapon
//       (the P1 weapon model), not the flat "FIGHTER" placeholder.
//   P11: the quest flow MOVE -> BAG -> FIRST_FIGHT -> (win | loss) matches
//       the authored tutorial chain, defeat included: MOVE shows the intro
//       dialog and advances to BAG; 3 bag hits advance to FIRST_FIGHT and
//       queue the Kenji fight; a LOSS keeps FIRST_FIGHT (retry); a WIN
//       advances to COMPLETE.
//   P12: the dialogue panel/portrait/text placement follows the authored
//       layout (parchment 0.53w centred at 0.235w, avatar 0.875 box_h at
//       the left inset, text after the avatar).
//
// RED on HEAD (5903504): P4 raw template names on the HUD; P5 names at
// cx±315 float over the bar; P6 tutorial COMPLETE before the fight + defeat
// drops to MainMenu; P8 roll/paper textures stretched; P9 placeholder
// silhouette; P11 state chain broken by the pre-fight COMPLETE.

#include "../headless_test_runner.hpp"
#include "../engine/game/ui_scale.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

#define CHECK_NEAR(a, b, eps, msg) do { \
    const float _va = (float)(a); \
    const float _vb = (float)(b); \
    const float _eps = (float)(eps); \
    if (std::fabs(_va - _vb) > _eps) { \
        std::fprintf(stderr, "  FAIL [line %d]: %s -- got %.3f, expected ~%.3f (eps %.3f)\n", \
                     __LINE__, msg, _va, _vb, _eps); \
        ++tests_failed; \
    } else { \
        std::printf("  PASS: %s\n", msg); \
        ++tests_passed; \
    } \
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
namespace ui = resf2::ui;
namespace game = resf2::game;

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

static resf2::test::HeadlessTestRunner make_battle_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 1280;
    config.height = 720;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = "battle";
    return resf2::test::HeadlessTestRunner(config);
}

static resf2::test::HeadlessTestRunner make_shop_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = "shop";
    return resf2::test::HeadlessTestRunner(config);
}

// Manual frame: poll, update, render, advance — the same pattern tap_key
// uses. poll_events() clears the just-pressed edges, so injected input must
// land after it and before on_update().
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

// Every scenario starts with the battle intro: the start-stance animation
// must run to completion before input lands.
static void warm_up(resf2::test::HeadlessTestRunner& r) {
    r.run_frames(330);              // intro stance animation runs to completion
    r.tap_key(plat::Key::D, 2);     // first input ends the A6 hold
    for (int i = 0; i < 80; ++i) {  // settle into stance_idle
        r.run_frames(1);
        if (!r.game().host_get_start_stance() &&
            r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
    r.run_frames(10);
}

static void walk_to_bag(resf2::test::HeadlessTestRunner& r) {
    for (int i = 0; i < 8; ++i) {
        const float bag_x = r.game().host_get_enemy_pos_x();
        const float px = r.game().host_get_player_pos_x();
        const float dist = std::fabs(bag_x - px);
        if (dist < 120.0f) return;
        edge_down(r, bag_x > px ? plat::Key::D : plat::Key::A);
        r.run_frames(40);
        edge_up(r, bag_x > px ? plat::Key::D : plat::Key::A);
    }
}

static void kick(resf2::test::HeadlessTestRunner& r) {
    edge_down(r, plat::Key::P);
    r.run_frames(2);
    edge_up(r, plat::Key::P);
    r.run_frames(50);
}

// ---------- P4: HUD fighter names come from the language pack ----------

static void test_p4_hud_names_localized() {
    std::printf("\n=== P4: battle HUD names resolve via the localization ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P4 init() failed\n"); ++tests_failed; return; }

    // The training fight's warrior is Template="Dojo_Disciple" with no
    // FirstName of its own; the template carries FirstName="NAME_KENJI".
    scn::SceneHost::BattleInfo info;
    info.enemy_name = "Dojo_Disciple";
    info.rounds = 1;
    info.round_time_s = 99;
    runner.game().host_set_battle_info(info);
    runner.run_frames(20);

    const auto names = runner.game().host_get_hud_fighter_names();
    std::fprintf(stderr, "  [P4] HUD names: player='%s' enemy='%s'\n",
                 names.player.c_str(), names.enemy.c_str());
    CHECK(names.enemy == "KENJI",
          "P4: the HUD enemy name is the localized 'KENJI' (eng.xml NAME_KENJI)");
    CHECK(names.player == "SHADOW",
          "P4: the HUD player name is the localized 'SHADOW' (NAME_SHADOW)");
}

// ---------- P5: fight HUD layout pinned to the atlas frames ----------

static void test_p5_hud_layout() {
    std::printf("\n=== P5: fight HUD layout == atlas frames + binary constants ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P5 init() failed\n"); ++tests_failed; return; }
    runner.run_frames(60);  // a few frames of fight HUD rendering

    const game::Game::FightHudLayout L = runner.game().host_get_fight_hud_layout();
    constexpr float kEps = 0.02f;
    const float pts = ui::points_scale(720.0f);
    const float cx = 640.0f;

    // Atlas frames: HealthBar_Empty 564x26, HealthBar_Full/Hit 1x43 at
    // content scale 2 -> 282x13 pt bar, 275 pt fill, 21.5 pt fill height.
    CHECK_NEAR(L.bar_w, 282.0f * pts, kEps, "P5: bar width = 564/2 pt (atlas frame)");
    CHECK_NEAR(L.bar_h, 13.0f * pts, kEps, "P5: bar height = 26/2 pt (atlas frame)");
    CHECK_NEAR(L.fill_w, 275.0f * pts, kEps, "P5: fill width = 275 pt (binary +0x150)");
    CHECK_NEAR(L.fill_h, 21.5f * pts, kEps, "P5: fill height = 43/2 pt (atlas frame)");
    // Bar centre 100 - h/2 pt below the top edge; inner edges at 53 pt.
    CHECK_NEAR(L.bar_cy, (100.0f - 6.5f) * pts, kEps, "P5: bar centre y = 93.5 pt");
    CHECK_NEAR(L.player_bar_x, cx - (53.0f + 282.0f) * pts, kEps,
               "P5: player bar outer edge at cx-335 pt");
    CHECK_NEAR(L.enemy_bar_x, cx + 53.0f * pts, kEps,
               "P5: enemy bar inner edge at cx+53 pt");
    CHECK_NEAR(L.bar_top_y, (100.0f - 13.0f) * pts, kEps, "P5: bar top y = 87 pt");

    // Names sit above the bar's OUTER edge (mirrored), not over the track.
    CHECK_NEAR(L.player_name_x, cx - (53.0f + 282.0f) * pts, kEps,
               "P5: player name left-aligned with the bar's outer edge");
    CHECK_NEAR(L.enemy_name_right, cx + (53.0f + 282.0f) * pts, kEps,
               "P5: enemy name right-aligned with the bar's outer edge");
    CHECK_NEAR(L.name_y, 45.0f * pts, kEps, "P5: name top y = 45 pt");

    // Round dots: 116 pt from the top, 77 pt from the centre (in the gap).
    CHECK_NEAR(L.dot_y, (116.0f - 12.0f) * pts, kEps, "P5: round-dot top y = 104 pt");
    CHECK_NEAR(L.player_dot_x, cx - (77.0f + 16.5f) * pts, kEps,
               "P5: first player dot left edge at cx-93.5 pt");

    // The framebuffer shows the orange fill inside the expected rects.
    const auto& soft = runner.renderer()->soft_renderer();
    const auto& fb = soft.framebuffer();
    const int fbw = 1280;
    const int fbh = 720;
    int orange = 0, orange_enemy = 0;
    int min_ox = 1 << 30, max_ox = -1;
    for (int y = (int)(70.0f * pts); y < (int)(100.0f * pts); ++y) {
        for (int x = (int)L.player_bar_x; x < (int)(640.0f - 40.0f * pts); ++x) {
            const size_t i = ((size_t)y * fbw + (size_t)x) * 4;
            const int fr = fb[i], fg = fb[i + 1], fbl = fb[i + 2];
            if (fr > 170 && fg > 60 && fg < 170 && fbl < 90) {  // HealthBar_Full orange
                ++orange;
                if (x < min_ox) min_ox = x;
                if (x > max_ox) max_ox = x;
            }
        }
    }
    for (int y = (int)(70.0f * pts); y < (int)(100.0f * pts); ++y) {
        for (int x = (int)(640.0f + 40.0f * pts); x < (int)(640.0f + 340.0f * pts); ++x) {
            const size_t i = ((size_t)y * fbw + (size_t)x) * 4;
            const int fr = fb[i], fg = fb[i + 1], fbl = fb[i + 2];
            if (fr > 170 && fg > 60 && fg < 170 && fbl < 90) ++orange_enemy;
        }
    }
    std::fprintf(stderr, "  [P5] orange fill px: player=%d enemy=%d (min_x=%d max_x=%d)\n",
                 orange, orange_enemy, min_ox == (1 << 30) ? -1 : min_ox, max_ox);
    // A full bar at ~265x20 px is ~5000 px; 20% of that must be orange.
    CHECK(orange > 800, "P5: the player's health fill is drawn in its region");
    CHECK(orange_enemy > 800, "P5: the enemy's health fill is drawn in its region");
}

// ---------- P6 + P11: quest/tutorial state chain, defeat included ----------
//
// P11 drives the whole authored chain MOVE -> BAG -> FIRST_FIGHT -> (loss
// keeps FIRST_FIGHT | win -> COMPLETE) and the P6 half asserts the scene
// flow after the loss: Results offers a rematch back into Battle, nothing
// toggles the dojo bag/fighter switch, and victory advances the story.

static void drive_bag_phase(resf2::test::HeadlessTestRunner& r) {
    r.game().host_set_tutorial_state("BAG");
    walk_to_bag(r);
    for (int i = 0; i < 3; ++i) {  // three registered bag hits -> FIRST_FIGHT
        kick(r);
        r.run_frames(20);
    }
}

// Tap through any queued dialogue (the tutorial hints) to land in the dojo.
static void skip_dialogue(resf2::test::HeadlessTestRunner& r, int lines) {
    for (int i = 0; i < lines; ++i) {
        bool in_dlg = false;
        for (int j = 0; j < 60; ++j) {
            r.run_frames(1);
            if (r.game().host_get_current_scene() == scn::SceneId::Dialogue) {
                in_dlg = true;
                break;
            }
        }
        if (!in_dlg) return;
        r.tap_key(plat::Key::Space, 1);
        r.run_frames(20);
    }
}

// Wait for a scene transition within budget; returns true when reached.
static bool wait_scene(resf2::test::HeadlessTestRunner& r, scn::SceneId want,
                       int max_frames) {
    for (int i = 0; i < max_frames; ++i) {
        r.run_frames(1);
        if (r.game().host_get_current_scene() == want) return true;
    }
    return false;
}

// One fight outcome via the battle runner already seated in Battle.
// Returns "victory" / "defeat" per who was killed.
static std::string finish_fight(resf2::test::HeadlessTestRunner& r,
                                bool player_dies) {
    r.run_frames(120);  // let the battle settle past the intro gate
    if (player_dies) {
        r.game().host_damage_player(1.0e9f);
    } else {
        r.game().host_damage_enemy(1.0e9f);
    }
    const bool went_results = wait_scene(r, scn::SceneId::Results, 120);
    std::fprintf(stderr, "  [fight] %s -> Results: %d\n",
                 player_dies ? "player killed" : "enemy killed", (int)went_results);
    return went_results ? r.game().host_get_battle_result() : std::string();
}

static void test_p6_defeat_retry_and_p11_chain() {
    std::printf("\n=== P6/P11: tutorial chain MOVE->BAG->FIRST_FIGHT, defeat retries ===\n");

    // ---- P11: MOVE -> BAG ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: P11 init() failed\n"); ++tests_failed; return; }
        warm_up(runner);
        runner.game().host_set_tutorial_state("MOVE");
        runner.game().host_run_tutorial_check();  // queues the Sensei intro dialog
        std::fprintf(stderr, "  [P11] after MOVE check: state='%s' dialog_lines=%zu\n",
                     runner.game().host_get_tutorial_state().c_str(),
                     runner.game().host_get_dialogue().size());
        CHECK(runner.game().host_get_tutorial_state() == "BAG",
              "P11: MOVE shows the intro dialog and advances to BAG");
        CHECK(runner.game().host_get_dialogue().size() >= 3,
              "P11: the Sensei intro dialog is queued (3 localized lines)");
    }

    // ---- P11 + P6: BAG -> FIRST_FIGHT -> defeat -> retry -> victory ----
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: P6 init() failed\n"); ++tests_failed; return; }
        warm_up(runner);
        const char* cap = "test_soak_wave7b_stdout.tmp";
        std::freopen(cap, "w", stdout);
        drive_bag_phase(runner);
        std::fflush(stdout);
        suppress_stdout();

        std::fprintf(stderr, "  [P11] after 3 bag hits: state='%s' hits=%d loc='%s'\n",
                     runner.game().host_get_tutorial_state().c_str(),
                     runner.game().host_get_tutorial_bag_hits(),
                     runner.game().host_get_battle_location().c_str());
        CHECK(runner.game().host_get_tutorial_bag_hits() >= 3,
              "P11: three hits land on the bag");
        CHECK(runner.game().host_get_tutorial_state() == "FIRST_FIGHT",
              "P11: three bag hits advance BAG -> FIRST_FIGHT (fight not yet won)");
        CHECK(runner.game().host_get_battle_location() == "dojo",
              "P11: the Kenji training fight is queued behind the dialog");

        // Dialog -> Battle (the fight vs Dojo_Disciple).
        skip_dialogue(runner, 1);
        CHECK(wait_scene(runner, scn::SceneId::Battle, 120),
              "P11: the training dialog hands over to the Battle scene");

        // ---- P6: LOSE the training fight ----
        const std::string outcome = finish_fight(runner, /*player_dies=*/true);
        CHECK(outcome == "defeat", "P6: the player loses the training fight");
        std::fprintf(stderr, "  [P6] after defeat: state='%s' battle_result='%s'\n",
                     runner.game().host_get_tutorial_state().c_str(),
                     runner.game().host_get_battle_result().c_str());
        CHECK(runner.game().host_get_tutorial_state() == "FIRST_FIGHT",
              "P6: a DEFEAT keeps the tutorial at FIRST_FIGHT (retry queued, "
              "never the broken pre-fight COMPLETE)");

        // The Results screen must offer a REMATCH back into the battle...
        std::string btn = runner.game().host_get_results_button_label();
        std::fprintf(stderr, "  [P6] Results button label: '%s'\n", btn.c_str());
        CHECK(!btn.empty() && btn != "BACK TO MENU",
              "P6: the defeat screen offers a rematch, not 'BACK TO MENU'");

        // ...and nothing may toggle the dojo bag/fighter switch on its own.
        const bool show_enemy_after = runner.game().host_get_show_enemy();
        for (int i = 0; i < 60; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_show_enemy() != show_enemy_after) break;
        }
        CHECK(runner.game().host_get_show_enemy() == show_enemy_after,
              "P6: no bag/fighter toggle loop while the retry is queued");

        // Press Continue -> back into Battle (retry), not the dojo.
        runner.run_frames(40);  // past the Results input guard
        runner.tap_key(plat::Key::Space, 1);
        CHECK(wait_scene(runner, scn::SceneId::Battle, 120),
              "P6: Continue on the defeat screen retries the training fight");

        // ---- WIN the retry ----
        const std::string outcome2 = finish_fight(runner, /*player_dies=*/false);
        CHECK(outcome2 == "victory", "P6: the rematch is won");
        std::fprintf(stderr, "  [P6] after victory: state='%s'\n",
                     runner.game().host_get_tutorial_state().c_str());
        CHECK(runner.game().host_get_tutorial_state() == "COMPLETE",
              "P11: a WIN advances FIRST_FIGHT -> COMPLETE");

        // Continue -> the story continues (Map).
        runner.run_frames(40);
        runner.tap_key(plat::Key::Space, 1);
        CHECK(wait_scene(runner, scn::SceneId::Map, 120),
              "P6: after the won training fight the story continues to the Map");

        // The whole flow must never have auto-toggled the dojo partner.
        int toggles = 0;
        {
            std::ifstream f(cap);
            std::string line;
            while (std::getline(f, line))
                if (line.find("[DOJO] Switched to") != std::string::npos) ++toggles;
        }
        std::remove(cap);
        std::fprintf(stderr, "  [P6] '[DOJO] Switched to' log lines: %d\n", toggles);
        CHECK(toggles == 0,
              "P6: zero bag/fighter toggle log lines across the whole defeat/retry flow");
    }
}

// ---------- P8 + P12: dialogue texture frames and placement ----------

static void test_p8_dialogue_textures_and_p12_placement() {
    std::printf("\n=== P8/P12: dialogue scroll textures + panel placement ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P8 init() failed\n"); ++tests_failed; return; }
    runner.run_frames(30);

    // P8: the scroll panel pieces are sized from their SOURCE frames.
    // Dialogue box at 1280x720: 0.53w x 0.20h.
    const float box_w = 1280.0f * 0.53f;
    const float box_h = 720.0f * 0.20f;
    const game::Game::ScrollPanelLayout SL =
        runner.game().host_get_scroll_panel_layout(box_w, box_h);
    const float pts = ui::points_scale(720.0f);
    CHECK_NEAR(SL.bar_h, 37.0f * pts, 0.02f,
               "P8: roll bar height = 74/2 pt (Roll_center 74 px frame)");
    CHECK_NEAR(SL.end_w, SL.bar_h * (156.0f / 74.0f), 0.02f,
               "P8: roll end caps keep the 156x74 source aspect");
    CHECK_NEAR(SL.edge_w, box_h * (116.0f / 1524.0f), 0.02f,
               "P8: paper edges keep the 116x1524 source aspect");

    // P12: the story dialogue panel geometry (JS-authored proportions).
    const scn::DialogueLayout D = runner.game().host_dialogue_layout(1280.0f, 720.0f);
    CHECK_NEAR(D.box_x, 1280.0f * 0.235f, 0.5f, "P12: parchment left edge at 0.235w");
    CHECK_NEAR(D.box_w, 1280.0f * 0.53f, 0.5f, "P12: parchment width 0.53w (900/1700)");
    CHECK_NEAR(D.box_h, 720.0f * 0.20f, 0.5f, "P12: parchment height 0.20h");
    CHECK_NEAR(D.box_y, (720.0f - 720.0f * 0.20f) * 0.5f, 0.5f,
               "P12: parchment vertically centred");
    CHECK_NEAR(D.portrait_size, D.box_h * 0.875f, 0.5f,
               "P12: avatar is 0.875 of the parchment height");
    CHECK_NEAR(D.portrait_x, D.box_x + D.box_w * 0.017f, 0.5f,
               "P12: avatar at the parchment's left inset (0.017w)");
    CHECK_NEAR(D.text_x, D.portrait_x + D.portrait_size + D.box_w * 0.02f, 0.5f,
               "P12: text starts after the avatar + gap");

    // The portrait texture itself is present (character_sensei).
    CHECK(runner.game().host_ui_texture_loaded("character_sensei"),
          "P12: the story-dialogue avatar texture is loaded");
    CHECK(runner.game().host_ui_texture_loaded("Roll_left") &&
          runner.game().host_ui_texture_loaded("Roll_center") &&
          runner.game().host_ui_texture_loaded("Roll_right") &&
          runner.game().host_ui_texture_loaded("Paper_left"),
          "P8: the dialogue scroll textures are loaded");
}

// ---------- P9: shop preview renders the fighter + equipped weapon ----------

static void test_p9_shop_preview() {
    std::printf("\n=== P9: shop preview shows the fighter + equipped weapon ===\n");
    resf2::test::HeadlessTestRunner runner = make_shop_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: P9 init() failed\n"); ++tests_failed; return; }
    runner.run_frames(20);

    // Equip the real weapon (P1 link: list.xml Model -> weapon_knives.xml).
    runner.game().host_add_item("WEAPON_KNIVES");
    const bool equipped = runner.game().host_equip_item("WEAPON_KNIVES");
    runner.run_frames(20);
    std::fprintf(stderr, "  [P9] equipped=%d weapon nodes=%zu\n",
                 (int)equipped, runner.game().host_get_player_weapon_node_count());
    CHECK(equipped, "P9: the weapon equips");
    CHECK(runner.game().host_get_player_weapon_node_count() > 0,
          "P9: the equipped weapon model is loaded (P1 link)");

    const game::Game::ShopPreviewGeometry G = runner.game().host_get_shop_preview_geometry();
    std::fprintf(stderr, "  [P9] shop preview: %zu body capsules, %zu weapon triangles\n",
                 G.body_capsules, G.weapon_triangles);
    CHECK(G.body_capsules > 0,
          "P9: the shop preview draws the fighter's body (capsules > 0)");
    CHECK(G.weapon_triangles > 0,
          "P9: the shop preview draws the equipped weapon model (> 0 triangles)");
}

// ---------- main ----------

int main() {
    test_p4_hud_names_localized();
    test_p5_hud_layout();
    test_p6_defeat_retry_and_p11_chain();
    test_p8_dialogue_textures_and_p12_placement();
    test_p9_shop_preview();

    std::fprintf(stderr, "\n=== Results: %d passed, %d failed ===\n",
                 tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
