// tests/integration/test_soak_ui_defects.cpp
//
// Soak-fix Wave 5 (reverse/analysis/SOAK_TRIAGE.md §5 UI): behavioral tests
// for U1-U6, written from the player's perspective — each asserts what the
// player SEES, not what the internals trace.
//
//   U1: the enemy weapon model must load real geometry. The soak showed
//       "Enemy weapon 'weapon_knuckles.xml': 0 nodes, 0 edges, 0 capsules"
//       and a yellow placeholder at the enemy's hand. weapon_knuckles.xml
//       ships 138 MacroNodes and 276 Triangle figures — the loader only
//       accepts Type="Node"/"CenterOfMass" and Type="Capsule", so it parses
//       NOTHING. The loaded model must carry >0 nodes and >0 triangles.
//   U2: the shop must actually work — the BUY flow must spend gold and put
//       the item in the inventory when the player clicks the green button,
//       and the screen must render its layout (MENU roll, parchment column,
//       bottom bar) rather than an empty background.
//   U3: the settings screen must render the real settings layout (sound/
//       music/graphics rows with sliders, language buttons) from the shipped
//       atlases — not the flat navy placeholder panel.
//   U4: the Profile entry of the expanded menu must be clickable: a click on
//       its icon must navigate to the Profile scene.
//   U5: after navigating to a submenu (Shop/Map/…) the menu panel must NOT
//       stay visible over the submenu — the overlay must close.
//   U6: opening the menu must play the scroll unfold (panel height animates
//       over several frames), not appear in a single frame.
//
// RED on HEAD (2026-08-02): U1 0 nodes (loader type filter), U2 empty shop
// catalog (list.xml fallback path never runs), U3 placeholder settings,
// U5 menu stays open over the submenu, U6 menu snaps open in one frame
// (inverted tween-speed division). U4 (Profile click) was ALREADY functional
// on HEAD — the menu hit-test works; the probe locks the behavior in.
// Fixes are implemented test-first: no fix before these RED tests.

#include "../headless_test_runner.hpp"
#include "../engine/game/ui_scale.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
namespace scn = resf2::scene;
namespace ui = resf2::ui;

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

static resf2::test::HeadlessTestRunner make_scene_runner(const std::string& scene) {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = scene;
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

// One pointer click (down+up across two frames, edge survives into update).
// NOTE: use the RUNNER's wrapper — it takes (x, y, id); the platform's own
// inject_pointer_down takes (id, x, y), and passing (x, y, id) into it turns
// the coordinates into a garbage pointer (this exact bug broke the first
// revision of this file's click helper).
static void click(resf2::test::HeadlessTestRunner& r, float x, float y) {
    r.platform().poll_events();
    r.inject_pointer_down(x, y, 0);
    frame(r);
    r.platform().poll_events();
    r.inject_pointer_up(0);
    frame(r);
}

// Wait until the menu expand/collapse animation settles.
static void settle_menu(resf2::test::HeadlessTestRunner& r, bool open) {
    const float target = open ? 1.0f : 0.0f;
    for (int i = 0; i < 90; ++i) {
        r.run_frames(1);
        const float p = r.game().host_get_menu_anim_progress();
        if (open ? (p >= 0.999f) : (p <= 0.001f)) return;
        (void)target;
    }
}

// The expanded menu's icon geometry — the same layout law the scene uses
// (ui::atlas_scale, roll under the top panel, 176px icons, 25px gaps).
struct MenuIconRect { float x = 0, y = 0, size = 0; };

static MenuIconRect menu_icon_rect(int idx, float win_h) {
    MenuIconRect r;
    const float s = ui::atlas_scale(win_h);
    const float roll_y = ui::top_panel_h(win_h);
    const float roll_h = 114.0f * s;
    const float paper_y = roll_y + roll_h - 3.0f;
    const float paper_padding = 44.0f * s;
    r.size = 176.0f * s;
    const float icon_spacing = 25.0f * s;
    r.x = 32.0f * s + paper_padding + 31.0f * s;
    r.y = paper_y + paper_padding + idx * (r.size + icon_spacing);
    return r;
}

// Sample one RGBA pixel from the software framebuffer (row-major, top-down).
static bool pixel(resf2::test::HeadlessTestRunner& r, int x, int y,
                  int& pr, int& pg, int& pb) {
    const auto& fb = r.renderer()->soft_renderer().framebuffer();
    const int w = r.platform().window_width();
    const int h = r.platform().window_height();
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    if (fb.size() < (std::size_t)(y * w + x + 1) * 4) return false;
    const std::size_t i = ((std::size_t)y * w + x) * 4;
    pr = fb[i]; pg = fb[i + 1]; pb = fb[i + 2];
    return true;
}

// ---------- U1: enemy weapon geometry ----------

static void test_u1_weapon() {
    std::printf("\n=== U1: enemy weapon model loads real geometry ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    // The enemy weapon is loaded by init_location(), which the Loading scene
    // triggers during the first frames — capture that window.
    const char* cap = "test_soak_ui_u1_stdout.tmp";
    std::freopen(cap, "w", stdout);
    const bool ok = runner.init();
    if (ok) runner.run_frames(40);
    std::fflush(stdout);
    suppress_stdout();
    if (!ok) { std::fprintf(stderr, "FAIL: U1 init() returned false\n"); ++tests_failed; return; }

    std::string weapon_line;
    {
        std::ifstream f(cap);
        std::string line;
        while (std::getline(f, line))
            if (line.find("Enemy weapon 'weapon_knuckles.xml'") != std::string::npos)
                weapon_line = line;
    }
    std::remove(cap);
    std::fprintf(stderr, "  [U1] %s\n",
                 weapon_line.empty() ? "<no Enemy weapon log line>" : weapon_line.c_str());
    CHECK(weapon_line.find("0 nodes") == std::string::npos,
          "U1: the enemy weapon log no longer reports 0 nodes");

    const std::size_t nodes = runner.game().host_get_enemy_weapon_node_count();
    const std::size_t tris = runner.game().host_get_enemy_weapon_triangle_count();
    std::fprintf(stderr, "  [U1] enemy weapon: %zu nodes, %zu triangles\n", nodes, tris);
    CHECK(nodes > 0, "U1: the enemy weapon model loads with >0 nodes");
    CHECK(tris > 0, "U1: the enemy weapon model loads with >0 triangles");
}

// ---------- U2: shop purchase flow + layout ----------

static void test_u2_shop() {
    std::printf("\n=== U2: shop renders its layout and the BUY flow works ===\n");
    resf2::test::HeadlessTestRunner runner = make_scene_runner("shop");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: U2 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    runner.run_frames(10);

    const float w = (float)runner.platform().window_width();
    const float h = (float)runner.platform().window_height();
    const float s = ui::points_scale(h);

    // --- layout probes (the scene's own ShopLayout law) ---
    const float menu_roll_y = 192.0f * s, menu_roll_h = 56.0f * s;
    const float body_y = menu_roll_y + menu_roll_h;
    const float bottom_h = 80.0f * s, bottom_y = h - bottom_h;
    const float scroll_x = w * 0.28f, scroll_w = w * 0.40f;
    const float detail_w = w * 0.32f, detail_x = w * 0.68f;
    const float buy_btn_w = detail_w * 0.72f, buy_btn_h = 44.0f * s;
    const float buy_btn_x = detail_x + (detail_w - buy_btn_w) * 0.5f;
    const float buy_btn_y = bottom_y - buy_btn_h - 14.0f * s;

    // --- render probes: MENU roll, parchment column, bottom bar ---
    int r1 = 0, g1 = 0, b1 = 0;
    if (pixel(runner, (int)(w * 0.5f), (int)(menu_roll_y + menu_roll_h * 0.5f), r1, g1, b1))
        std::fprintf(stderr, "  [U2] MENU roll pixel at (%.0f,%.0f): rgb(%d,%d,%d)\n",
                     w * 0.5f, menu_roll_y + menu_roll_h * 0.5f, r1, g1, b1);
    CHECK(r1 > 20 && r1 < 90 && g1 > 12 && g1 < 60,
          "U2: the MENU scroll roll is rendered below the top panel");

    int r2 = 0, g2 = 0, b2 = 0;
    if (pixel(runner, (int)(scroll_x + scroll_w * 0.5f), (int)(body_y + 30.0f * s), r2, g2, b2))
        std::fprintf(stderr, "  [U2] parchment pixel: rgb(%d,%d,%d)\n", r2, g2, b2);
    CHECK(r2 > 170 && g2 > 150,
          "U2: the centre parchment column is rendered (light parchment)");

    int r3 = 0, g3 = 0, b3 = 0;
    // Sample right of the centred category icons (which now draw on the bar).
    if (pixel(runner, (int)(w * 0.95f), (int)(bottom_y + bottom_h * 0.5f), r3, g3, b3))
        std::fprintf(stderr, "  [U2] bottom bar pixel: rgb(%d,%d,%d)\n", r3, g3, b3);
    CHECK(r3 < 60 && g3 < 45,
          "U2: the bottom currency/category bar is rendered (dark brown)");

    // --- purchase flow: give gold, click BUY, item lands in inventory ---
    if (const auto* ld = runner.game().host_get_list_data()) {
        int weapon_items = 0;
        for (const auto& it : ld->items)
            if (it.type == "Weapon" && !it.shop_hide && !it.hidden && it.price > 0)
                ++weapon_items;
        std::fprintf(stderr, "  [U2] catalog: %zu items, %d buyable Weapon items\n",
                     ld->items.size(), weapon_items);
    }
    runner.game().host_add_currency(1000);
    const int gold0 = runner.game().host_get_currency();
    click(runner, buy_btn_x + buy_btn_w * 0.5f, buy_btn_y + buy_btn_h * 0.5f);
    runner.run_frames(5);
    const int gold1 = runner.game().host_get_currency();
    std::fprintf(stderr, "  [U2] buy click at (%.0f,%.0f): gold %d -> %d, owns WEAPON_KNIVES=%d\n",
                 buy_btn_x + buy_btn_w * 0.5f, buy_btn_y + buy_btn_h * 0.5f,
                 gold0, gold1, (int)runner.game().host_has_item("WEAPON_KNIVES"));
    CHECK(gold1 < gold0 && gold1 == gold0 - 50,
          "U2: clicking BUY spends the item price (50 gold)");
    CHECK(runner.game().host_has_item("WEAPON_KNIVES"),
          "U2: the purchased item lands in the inventory");
}

// ---------- U3: settings renders the real layout ----------

static void test_u3_settings() {
    std::printf("\n=== U3: settings renders the real layout ===\n");
    resf2::test::HeadlessTestRunner runner = make_scene_runner("settings");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: U3 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    runner.run_frames(10);

    CHECK(runner.game().host_ui_texture_loaded("sound"),
          "U3: the settings atlas loads the sound row icon");
    CHECK(runner.game().host_ui_texture_loaded("slider") &&
          runner.game().host_ui_texture_loaded("SettingsEmpty"),
          "U3: the slider atlas loads (track/fill/knob)");
    CHECK(runner.game().host_ui_texture_loaded("usbr") ||
          runner.game().host_ui_texture_loaded("rus"),
          "U3: the language button textures load");

    // The settings body must not be the flat placeholder (clear colour
    // {8,8,16} or the navy stub panel {25,25,40}): a real row (icon +
    // slider) has to be drawn there. Sample the first row's slider fill —
    // the layout law the scene uses (roll at 192pt, rows below, slider at
    // 52% width, 34% of the window).
    const float h = (float)runner.platform().window_height();
    const float w = (float)runner.platform().window_width();
    const float s = ui::points_scale(h);
    const float roll_y = 192.0f * s;
    const float body_y = roll_y + 56.0f * s + 10.0f * s;
    const float row_h = 54.0f * s;
    const float slider_x = w * 0.52f, slider_w = w * 0.34f;
    const float slider_h = 20.0f * s;
    const float sample_x = slider_x + slider_w * 0.30f;
    const float sample_y = body_y + (row_h - slider_h) * 0.5f + slider_h * 0.5f;
    int pr = 0, pg = 0, pb = 0;
    if (pixel(runner, (int)sample_x, (int)sample_y, pr, pg, pb))
        std::fprintf(stderr, "  [U3] first slider pixel at (%.0f,%.0f): rgb(%d,%d,%d)\n",
                     sample_x, sample_y, pr, pg, pb);
    const bool flat_placeholder =
        (std::abs(pr - 8) <= 10 && std::abs(pg - 8) <= 10 && std::abs(pb - 16) <= 10) ||
        (std::abs(pr - 23) <= 12 && std::abs(pg - 23) <= 12 && std::abs(pb - 37) <= 12);
    CHECK(!flat_placeholder,
          "U3: the settings body renders real rows, not a flat placeholder");
}

// Run until the dojo (MainMenu scene) is up. Boot (0.5s) + Loading (0.9s)
// take ~88 frames at 16ms; the menu interactions below need the dojo.
static void warm_to_dojo(resf2::test::HeadlessTestRunner& r) {
    for (int i = 0; i < 200; ++i) {
        if (r.game().host_get_current_scene() == scn::SceneId::MainMenu) break;
        r.run_frames(1);
    }
}

// ---------- U4: Profile entry navigates ----------

static void test_u4_profile_clickable() {
    std::printf("\n=== U4: Profile entry is clickable ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: U4 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    warm_to_dojo(runner);
    CHECK(runner.game().host_get_current_scene() == scn::SceneId::MainMenu,
          "U4: the dojo scene is up before the menu test");

    // Open the menu and let the unfold finish.
    runner.tap_key(plat::Key::M, 1);
    settle_menu(runner, true);
    CHECK(runner.game().host_get_menu_open(),
          "U4: the menu overlay is open");
    std::fprintf(stderr, "  [U4] menu anim progress = %.3f\n",
                 runner.game().host_get_menu_anim_progress());

    const float win_h = (float)runner.platform().window_height();
    const MenuIconRect ic = menu_icon_rect(3, win_h);  // Profile = 4th icon
    std::fprintf(stderr, "  [U4] Profile icon rect: (%.0f,%.0f) size %.0f\n",
                 ic.x, ic.y, ic.size);
    click(runner, ic.x + ic.size * 0.5f, ic.y + ic.size * 0.5f);

    bool profile = false;
    for (int i = 0; i < 60; ++i) {
        runner.run_frames(1);
        if (runner.game().host_get_current_scene() == scn::SceneId::Profile) {
            profile = true;
            break;
        }
    }
    std::fprintf(stderr, "  [U4] scene after Profile click: %s\n",
                 scn::scene_name(runner.game().host_get_current_scene()));
    CHECK(profile, "U4: clicking the Profile icon navigates to the Profile scene");
}

// ---------- U5: menu closes after navigating to a submenu ----------

static void test_u5_menu_hides() {
    std::printf("\n=== U5: menu hides after navigating to a submenu ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: U5 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    warm_to_dojo(runner);

    runner.tap_key(plat::Key::M, 1);
    settle_menu(runner, true);
    CHECK(runner.game().host_get_menu_open(),
          "U5: the menu overlay is open before navigating");

    const float win_h = (float)runner.platform().window_height();
    const MenuIconRect ic = menu_icon_rect(2, win_h);  // Shop = 3rd icon
    click(runner, ic.x + ic.size * 0.5f, ic.y + ic.size * 0.5f);

    bool shop = false;
    for (int i = 0; i < 60; ++i) {
        runner.run_frames(1);
        if (runner.game().host_get_current_scene() == scn::SceneId::Shop) {
            shop = true;
            break;
        }
    }
    CHECK(shop, "U5: clicking the Shop icon navigates to the Shop scene");
    std::fprintf(stderr, "  [U5] after nav: menu_open=%d anim=%.3f\n",
                 (int)runner.game().host_get_menu_open(),
                 runner.game().host_get_menu_anim_progress());
    CHECK(!runner.game().host_get_menu_open(),
          "U5: the menu overlay is closed inside the submenu");
    runner.run_frames(30);
    std::fprintf(stderr, "  [U5] anim after 30 more frames: %.3f\n",
                 runner.game().host_get_menu_anim_progress());
    CHECK(runner.game().host_get_menu_anim_progress() < 0.10f,
          "U5: the menu panel collapses instead of staying over the submenu");
}

// ---------- U6: menu unfolds over several frames ----------

static void test_u6_menu_animation() {
    std::printf("\n=== U6: menu unfold is animated ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: U6 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    warm_to_dojo(runner);
    CHECK(runner.game().host_get_menu_anim_progress() <= 0.001f,
          "U6: the menu starts fully collapsed");

    // Open with an edge-driven M press (manual frame, like tap_key).
    runner.platform().poll_events();
    runner.platform().inject_key_down(plat::Key::M);
    frame(runner);
    runner.platform().poll_events();
    runner.platform().inject_key_up(plat::Key::M);
    frame(runner);

    const float p_early = runner.game().host_get_menu_anim_progress();
    std::fprintf(stderr, "  [U6] anim progress 2 frames after open: %.3f\n", p_early);
    CHECK(p_early > 0.01f && p_early < 0.99f,
          "U6: the menu does not snap open in one frame (progress mid-transition)");

    runner.run_frames(10);
    const float p_mid = runner.game().host_get_menu_anim_progress();
    std::fprintf(stderr, "  [U6] anim progress after 12 frames: %.3f\n", p_mid);
    CHECK(p_mid < 0.95f,
          "U6: the unfold is still in progress ~200 ms in (not instant)");

    settle_menu(runner, true);
    CHECK(runner.game().host_get_menu_anim_progress() >= 0.999f,
          "U6: the unfold completes to the fully expanded state");
}

int main() {
    std::printf("=== Soak UI Defects Test (U1-U6) ===\n");
    std::fflush(stdout);

    test_u1_weapon();
    test_u2_shop();
    test_u3_settings();
    test_u4_profile_clickable();
    test_u5_menu_hides();
    test_u6_menu_animation();

    if (tests_failed > 0) {
        std::fprintf(stderr, "\n=== SOAK UI DEFECTS TEST FAILED (%d failures) ===\n",
                     tests_failed);
        return 1;
    }
    std::fprintf(stderr, "\n=== SOAK UI DEFECTS TEST PASSED ===\n");
    return 0;
}
