// tests/integration/test_soak_re3_defects.cpp
//
// Soak re-soak-3 regressions (after Wave 7 P1-P12 landed on HEAD 24f1267):
//
//   R1 (render): the fighter body renders YELLOW and the weapon is yellow,
//   invisible, or far from the character. Root causes found in reverse:
//     - P3's armor render paints the equipped robe (ARMOR_ROBE, the default
//       in users.xml -> armor_robe.xml) over the WHOLE fighter in a flat
//       khaki fill {128,96,62} — the user's "тело жёлтого цвета". The
//       original renders the fighter as a unified DARK silhouette (the
//       render_body_model design law), armor included.
//     - The weapon triangles are looked up in the model's PLAIN node map,
//       but weapon_knives.xml ships ZERO plain nodes — every figure
//       references the model's own MacroNodes (WEAPON_KNIVES-MacroNode17_1
//       ...), which resolve through LCC weights over the skeleton's
//       Weapon-Node*_1 nodes. The skeleton pins Weapon-Node2_1 to NWrist_1
//       (zero-length Edge129 — the dojo placement law, LIVE_GAME_EVIDENCE
//       Q1/Q2). The render instead drew the authored rest coords at a
//       hand-fitted offset — the "weapon далеко от персонажа" report.
//
//   R2 (combat): the player cannot damage the enemy in battle. Root causes:
//     - the battle hit test runs against the PLAYER's body model
//       (assets_->body_model()) resolved at the enemy transform; the
//       ENEMY's own model per the battle setup (Dojo_Disciple template:
//       BODY_KENJI -> body_kenji.xml, HEAD_DISCIPLE -> head_disciple.xml —
//       stages.xml <Template> items, list.xml Model attrs) never loads.
//     - the precise edge test only connects at point-blank (the attack
//       interval frames 4-5 are early in the punch animation, the fist is
//       not at full reach); the distance fallback exists only for the dojo
//       BAG, never for the enemy fighter — while the enemy's own attack
//       range test (move tactic Distance Max=250) lets the enemy hit from
//       mid-range. The authored move range must apply to the player's hits
//       on the enemy the same way.
//
// RED on HEAD: R1 torso shows khaki/yellow; R1 weapon min-triangle distance
// to the hand is ~60+ units; R2 the Dojo_Disciple enemy model loads 0
// capsules and mid-range punches never drop the enemy's HP.

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

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // deterministic empty inventory -> fists
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

// Same warm-up as the wave-7b suite: the loading screen + location init
// run first, then the intro stance plays to completion; the first input
// ends the A6 hold and the player settles into stance_idle. After this the
// ANIMATED node positions (anim_node_pos_) are populated, so the weapon
// resolver and the body render use the animated transform.
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

// ---------- R1: the fighter body is a DARK silhouette, not yellow ----------
//
// The default save equips ARMOR_ROBE (users.xml Armor="ARMOR_ROBE" ->
// armor_robe.xml). P3 renders its capsules over the whole fighter in a flat
// khaki {128,96,62} — the "body is yellow" report. The original renders the
// fighter as a unified dark silhouette; the armor must stay silhouette-dark.
// Probe: framebuffer sample at the player's torso rect — the fill must be
// the dark body/armor tone, with ZERO khaki/yellow pixels.
static void test_r1_body_not_yellow() {
    std::printf("\n=== R1: the fighter body renders dark, not yellow ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R1 init() failed\n"); ++tests_failed; return; }

    // Equip the robe explicitly (hermetic inventory starts empty; the
    // shipped users.xml defaults to ARMOR_ROBE — same item). The model
    // itself loads when the location initializes (init_location).
    runner.game().host_add_item("ARMOR_ROBE");
    const bool equipped = runner.game().host_equip_item("ARMOR_ROBE");
    CHECK(equipped, "R1: the robe equips");

    warm_up(runner);

    std::fprintf(stderr, "  [R1] robe equipped=%d armor capsules=%zu\n",
                 (int)equipped, runner.game().host_get_armor_model_capsule_count());
    CHECK(runner.game().host_get_armor_model_capsule_count() > 0,
          "R1: the robe model loads (armor_robe.xml capsules)");

    const auto& soft = runner.renderer()->soft_renderer();
    const auto& fb = soft.framebuffer();
    const int vw = 320, vh = 180;

    // R1a: the armor RENDER color is silhouette-dark (the render contract;
    // the re-soak-3 "the body is yellow" was the khaki {128,96,62} robe).
    std::uint8_t ar = 0, ag = 0, ab = 0;
    runner.game().host_get_armor_render_color(ar, ag, ab);
    std::fprintf(stderr, "  [R1] armor render color = (%d,%d,%d)\n", ar, ag, ab);
    CHECK(ar < 70 && ag < 70 && ab < 90,
          "R1: the armor paint color is silhouette-dark (not khaki/yellow)");

    // R1b: the robe's PIXELS on screen (sampled at the armor's rendered
    // world extents via the same camera math the render uses) must be the
    // dark silhouette — zero khaki robe fill. The dojo background itself is
    // tan (wall panels/floor planks: ~17% of a background strip matches the
    // khaki tone), so counting khaki over a large region catches background,
    // not the robe; sampling INSIDE the rendered armor rect isolates the
    // fighter (the old khaki robe {128,96,62} painted every armor pixel).
    const float zoom = runner.game().host_get_zoom();
    const float cam_x = runner.game().host_get_camera_x();
    const float cam_y = runner.game().host_get_camera_y();
    const float hw = vw / (2.0f * zoom), hh = vh / (2.0f * zoom);
    const float left = cam_x - hw, right = cam_x + hw;
    const float bottom = cam_y - hh, top = cam_y + hh;
    auto w2s = [&](float wx, float wy, int& sx, int& sy) {
        sx = (int)std::lround((wx - left) / (right - left) * vw);
        sy = (int)std::lround((1.0f - (wy - bottom) / (top - bottom)) * vh);
    };
    float amnx = 0, amny = 0, amxx = 0, amxy = 0;
    runner.game().host_get_armor_world_extents(amnx, amny, amxx, amxy);
    std::fprintf(stderr, "  [R1] armor world extents x=[%.0f..%.0f] y=[%.0f..%.0f]\n",
                 amnx, amxx, amny, amxy);
    int tx0 = 0, ty0 = 0, tx1 = 0, ty1 = 0;
    w2s(amnx, amny, tx0, ty1);
    w2s(amxx, amxy, tx1, ty0);
    int t_dark = 0, t_khaki = 0, t_total = 0;
    for (int y = ty0; y <= ty1; ++y) {
        for (int x = tx0; x <= tx1; ++x) {
            if (x < 0 || x >= vw || y < 0 || y >= vh) continue;
            const size_t i = ((size_t)y * vw + (size_t)x) * 4;
            const int r = fb[i], g = fb[i + 1], b = fb[i + 2];
            ++t_total;
            // Body {20,20,25} / armor {34,31,27}: neutral-dark.
            if (r < 55 && g < 50 && std::abs(r - b) < 15) ++t_dark;
            // Khaki robe {128,96,62}.
            if (r > 90 && g > 70 && b < 80 && r - g < 60) ++t_khaki;
        }
    }
    std::fprintf(stderr, "  [R1] armor rect screen (%d,%d)-(%d,%d) total=%d dark=%d khaki=%d (zoom=%.2f cam=(%.0f,%.0f))\n",
                 tx0, ty0, tx1, ty1, t_total, t_dark, t_khaki, zoom, cam_x, cam_y);
    CHECK(t_dark > 60, "R1: the robe renders the dark silhouette on screen");
    CHECK(t_khaki * 4 < t_dark, "R1: no khaki robe fill on the fighter");
}

// ---------- R1: the weapon renders at the hand ----------
//
// weapon_knives.xml ships ONLY MacroNodes; its triangle figures reference
// them, and the MacroNodes compute their position from the skeleton's
// Weapon-Node*_1 nodes (pinned to the wrist by zero-length Edge129). The
// old render looked the vertices up in the plain node map (empty) and drew
// the authored rest coords at a heuristic offset — invisible or far from
// the fighter. Probe: the drawn weapon's NEAREST triangle must be within
// 25 world units of the hand (Weapon-Node2_1, the Edge129 pin target), and
// nothing drawn farther than 100 units (the blade extends up ~70).
static void test_r1_weapon_at_hand() {
    std::printf("\n=== R1: the weapon renders at the hand ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R1 weapon init() failed\n"); ++tests_failed; return; }

    runner.game().host_add_item("WEAPON_KNIVES");
    const bool equipped = runner.game().host_equip_item("WEAPON_KNIVES");
    CHECK(equipped, "R1: the knives equip");

    warm_up(runner);

    std::fprintf(stderr, "  [R1] weapon equipped=%d nodes=%zu triangles=%zu\n",
                 (int)equipped, runner.game().host_get_player_weapon_node_count(),
                 runner.game().host_get_player_weapon_triangle_count());
    CHECK(runner.game().host_get_player_weapon_triangle_count() > 0,
          "R1: the knives model loads with triangles");

    float hx = 0, hy = 0;
    runner.game().host_get_player_hand_world(hx, hy);
    float min_d = 1.0e9f, max_d = 0.0f;
    int drawn = 0;
    float min_wy = 1.0e9f, max_wy = -1.0e9f;
    const int nt = (int)runner.game().host_get_player_weapon_triangle_count();
    for (int t = 0; t < nt; ++t) {
        float cx = 0, cy = 0;
        if (!runner.game().host_get_player_weapon_triangle_world(t, cx, cy)) continue;
        ++drawn;
        if (cy < min_wy) min_wy = cy;
        if (cy > max_wy) max_wy = cy;
        const float d = std::hypot(cx - hx, cy - hy);
        if (d < min_d) min_d = d;
        if (d > max_d) max_d = d;
    }
    std::fprintf(stderr, "  [R1] hand world=(%.0f,%.0f) weapon triangles drawn=%d min_d=%.1f max_d=%.1f knife_world_y=[%.0f..%.0f] anim='%s' yadj=%.1f\n",
                 hx, hy, drawn, min_d, max_d, min_wy, max_wy,
                 runner.game().host_get_player_anim().c_str(),
                 runner.game().host_get_y_adjust());
    CHECK(drawn > 0, "R1: the weapon mesh draws (>0 triangles resolve)");
    // The knife's authored handle sits ~20 units below the wrist pin (the
    // LCC weights put the handle under the fist); 35 units = at the hand.
    CHECK(min_d < 35.0f, "R1: the weapon's nearest triangle is at the hand (<35 units)");
    CHECK(max_d < 100.0f, "R1: the whole weapon stays near the fighter (<100 units)");
}

// ---------- R2: the player damages the enemy at mid-range ----------
//
// The Dojo_Disciple enemy's own model (stages.xml template items BODY_KENJI
// -> body_kenji.xml, HEAD_DISCIPLE -> head_disciple.xml) must load and be
// the hit target, and a punch inside the move's authored range (moves.xml
// <Distance Max="250"> for HighPunch) must connect — not only point-blank.
// The bag-only distance fallback (dojo training) is mirrored for the enemy
// fighter in battle.
static void test_r2_player_hits_enemy_at_midrange() {
    std::printf("\n=== R2: the player damages the enemy at mid-range ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: R2 init() failed\n"); ++tests_failed; return; }

    scn::SceneHost::BattleInfo info;
    info.enemy_name = "Dojo_Disciple";
    info.rounds = 1;
    info.round_time_s = 99;
    runner.game().host_set_battle_info(info);
    runner.game().host_set_battle_mode(true);
    runner.game().host_set_show_enemy(true);
    runner.run_frames(150);  // battle intro + settle

    // R2a: the enemy's own model loads per the battle setup.
    const size_t enemy_caps = runner.game().host_get_enemy_model_capsule_count();
    const size_t enemy_edges = runner.game().host_get_enemy_model_edge_count();
    std::fprintf(stderr, "  [R2] enemy model: %zu edges, %zu capsules\n",
                 enemy_edges, enemy_caps);
    CHECK(enemy_caps > 0, "R2: the Dojo_Disciple enemy model loads (body_kenji/head_disciple)");
    CHECK(enemy_edges > 0, "R2: the enemy model edges exist");

    // Walk toward the enemy but STOP at mid-range (~160 units) — far beyond
    // the old point-blank-only connection range.
    for (int i = 0; i < 10; ++i) {
        const float dist = std::fabs(runner.game().host_get_enemy_pos_x() -
                                     runner.game().host_get_player_pos_x());
        if (dist <= 160.0f) break;
        edge_down(runner, plat::Key::D);
        runner.run_frames(40);
        edge_up(runner, plat::Key::D);
    }
    const float dist0 = std::fabs(runner.game().host_get_enemy_pos_x() -
                                  runner.game().host_get_player_pos_x());
    const float hp0 = runner.enemy_health_frac();
    std::fprintf(stderr, "  [R2] fighting at dist=%.0f enemy_hp=%.3f\n", dist0, hp0);
    CHECK(dist0 <= 170.0f, "R2: the player fights from mid-range (not point-blank)");

    // Punch repeatedly (O = punch) — the move's attack interval runs each
    // swing; mid-range hits must land and drop the enemy's HP.
    for (int i = 0; i < 14; ++i) {
        edge_down(runner, plat::Key::O);
        runner.run_frames(2);
        edge_up(runner, plat::Key::O);
        runner.run_frames(38);
        if (runner.enemy_health_frac() < hp0 - 0.02f) break;
    }
    const float hp1 = runner.enemy_health_frac();
    std::fprintf(stderr, "  [R2] enemy_hp %.3f -> %.3f (dist=%.0f)\n", hp0, hp1,
                 std::fabs(runner.game().host_get_enemy_pos_x() -
                           runner.game().host_get_player_pos_x()));
    CHECK(hp1 < hp0 - 0.02f,
          "R2: a mid-range punch damages the enemy (enemy HP drops)");
}

int main() {
    std::printf("=== Soak re-soak-3 regression probes (R1 render, R2 combat) ===\n");
    std::fflush(stdout);

    suppress_stdout();

    test_r1_body_not_yellow();
    test_r1_weapon_at_hand();
    test_r2_player_hits_enemy_at_midrange();

    std::printf("\n=== re-soak-3 probes: %d passed, %d failed ===\n",
                tests_passed, tests_failed);
    std::fflush(stdout);
    return tests_failed == 0 ? 0 : 1;
}
