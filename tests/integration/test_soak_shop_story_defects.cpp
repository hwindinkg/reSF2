// tests/integration/test_soak_shop_story_defects.cpp
//
// Soak-fix Wave 9B (re-soak-5): SHOP + STORY regressions. Track A (combat /
// hit-feedback) is out of scope — shop/quest/scene files only.
//
//   S1: shop item icons resolve from image/ut_items/icon (list.xml Image=).
//       re-soak-5: "магазин ... отображает только названия предметов, но не
//       их иконки" — the ut_items loose PNGs were never loaded into the HUD
//       texture table, so host_render_ui_texture(item.image) fell back to a
//       tinted square.
//   S2: the shop's centre column renders the CURRENT CATEGORY's item list
//       (scrollable), not the three equipped-slot rows ("Weapon"/"Consumable"/
//       "Ranged") that showed the same first weapon in every tab. Category
//       switch filters the rows, S-key/wheel scroll moves the window, and a
//       row click selects THAT row (the hit-test already indexed
//       scroll_offset_+i — the render simply never showed the items).
//   S3: the BUY button hit-test at the desktop resolution (1280x720) spends
//       gold; and a failed buy (gold < price) is LOUD — the can_buy gate
//       silently swallowed the click ("при нажатии купить ничего не
//       происходит" while the handler itself works per the purchase log).
//   S4: the shop preview draws the knife at the hand, not above it. The
//       preview resolved the BODY with the (stale) animation pose but the
//       WEAPON with the skeleton rest pose (use_anim=false) — mixed pose
//       sources. The probe below measures the hand-anchor gap under the
//       preview's own resolve law.
//   S5: the story continues after the first win: FIRST_FIGHT -> COMPLETE
//       queues the Sensei tutorial_shop dialogue (shown on the Map, returns
//       to the Map); buying WEAPON_KNIVES queues tutorial_buy_knives +
//       tutorial_map; winning ZONE_1|BOSS_LYNX|1 fires the quests.xml
//       "FirstGuardBeaten" set (Lynx taunt + May intro + ShowBattle
//       ZONE_1|Tournament). "после битвы ничего не произошло, меня просто
//       кинуло на карту" — host_trigger_quest_event was a logging stub.
//
// RED on HEAD (2026-08-04): S1 (no weapon_knives texture), S2 (rows are
// slot labels, not catalog items), S3-fail (no cannot-buy diagnostic), S4
// (gap large with the stale dojo pose), S5 (no dialogue queued after the
// Kenji win / guard win; tournament never shown). S3-buy and S2-click were
// already functional on HEAD — they lock the behavior in.

#include "../headless_test_runner.hpp"
#include "../engine/game/ui_scale.hpp"
#include "../engine/scene/scene_system.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

namespace plat = resf2::platform;
namespace scn = resf2::scene;
namespace ui = resf2::ui;

static resf2::test::HeadlessTestRunner make_runner(int w, int h,
                                                   const std::string& scene) {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = w;
    config.height = h;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    if (!scene.empty()) config.start_scene = scene;
    return resf2::test::HeadlessTestRunner(config);
}

// Manual frame (poll, update, render, advance) — same pattern as the
// soak_ui_defects helpers: injected input must land after poll_events().
static void frame(resf2::test::HeadlessTestRunner& r) {
    r.game().on_update(r.platform(), 16);
    r.game().on_render(r.platform());
    r.platform().advance_time_ms(16);
}

static void click(resf2::test::HeadlessTestRunner& r, float x, float y) {
    r.platform().poll_events();
    r.inject_pointer_down(x, y, 0);
    frame(r);
    r.platform().poll_events();
    r.inject_pointer_up(0);
    frame(r);
}

static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

// Run frames until the current scene is `id` (or the budget runs out).
static bool run_until_scene(resf2::test::HeadlessTestRunner& r, scn::SceneId id,
                            int budget = 120) {
    for (int i = 0; i < budget; ++i) {
        if (r.game().host_get_current_scene() == id) return true;
        r.run_frames(1);
    }
    return r.game().host_get_current_scene() == id;
}

// ---------- S1: item icons resolve ----------

static void test_s1_item_icons() {
    std::printf("\n=== S1: shop item icons resolve (ut_items) ===\n");
    resf2::test::HeadlessTestRunner runner = make_runner(320, 180, "shop");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S1 init() failed\n"); ++tests_failed; return; }
    suppress_stdout();
    runner.run_frames(10);

    std::fprintf(stderr, "  [S1] weapon_knives=%d armor_robe=%d helm_conical_hat=%d\n",
                 (int)runner.game().host_ui_texture_loaded("weapon_knives"),
                 (int)runner.game().host_ui_texture_loaded("armor_robe"),
                 (int)runner.game().host_ui_texture_loaded("helm_conical_hat"));
    CHECK(runner.game().host_ui_texture_loaded("weapon_knives"),
          "S1: the WEAPON_KNIVES icon texture resolves (ut_items/icon)");
    CHECK(runner.game().host_ui_texture_loaded("armor_robe"),
          "S1: the ARMOR_ROBE icon texture resolves");
}

// ---------- S2: centre column shows the category item list ----------

// Look up a catalog item's Type by name.
static std::string item_type(resf2::test::HeadlessTestRunner& r, const std::string& name) {
    auto* ld = r.game().host_get_list_data();
    if (!ld) return {};
    for (const auto& it : ld->items)
        if (it.name == name) return it.type;
    return {};
}

// The rows the shop's centre column renders right now.
static std::vector<std::string> shop_rows(resf2::test::HeadlessTestRunner& r) {
    return r.game().host_shop_visible_rows();
}

// Row-click geometry — the scene's own layout law (scenes.cpp shop_layout).
struct ShopRowGeom {
    float inner_x, inner_w, first_y, row_h;
};

static ShopRowGeom shop_row_geom(float w, float h) {
    const float s = ui::points_scale(h);
    const float body_y = 192.0f * s + 56.0f * s;
    const float body_h = (h - 80.0f * s) - body_y;
    const float scroll_x = w * 0.28f;
    const float scroll_w = w * 0.40f;
    const float pad = scroll_w * 0.08f;
    ShopRowGeom g;
    g.inner_x = scroll_x + pad;
    g.inner_w = scroll_w - 2.0f * pad;
    g.first_y = body_y + body_h * 0.06f + 26.0f * s * 1.4f;
    g.row_h = body_h * 0.22f;
    return g;
}

static void test_s2_shop_list() {
    std::printf("\n=== S2: shop centre column shows the category item list ===\n");
    resf2::test::HeadlessTestRunner runner = make_runner(320, 180, "shop");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S2 init() failed\n"); ++tests_failed; return; }
    suppress_stdout();
    runner.run_frames(10);

    const float w = (float)runner.platform().window_width();
    const float h = (float)runner.platform().window_height();
    const ShopRowGeom G = shop_row_geom(w, h);

    // --- (a) Weapon tab: the rows are catalog items, not slot labels ---
    auto rows = shop_rows(runner);
    std::fprintf(stderr, "  [S2] weapon rows: %zu [", rows.size());
    for (const auto& row : rows) std::fprintf(stderr, "%s, ", row.c_str());
    std::fprintf(stderr, "]\n");
    CHECK(rows.size() >= 3, "S2: the centre column renders at least 3 rows");
    bool all_catalog = !rows.empty();
    for (const auto& row : rows) {
        if (row.empty() || item_type(runner, row) != "Weapon") { all_catalog = false; break; }
    }
    CHECK(all_catalog && rows[0] == "WEAPON_KNIVES",
          "S2: Weapon rows are catalog weapons (WEAPON_KNIVES first)");

    // --- (d) Clicking a row selects THAT row (no offset) ---
    // Runs BEFORE the category/scroll tests: the window must still be at 0
    // and the Weapon tab active so row i maps to item i.
    auto* ld = runner.game().host_get_list_data();
    std::string expect;
    if (ld) {
        int count = 0;
        for (const auto& it : ld->items) {
            if (it.type != "Weapon" || it.shop_hide || it.hidden || it.price <= 0) continue;
            if (count == 2) { expect = it.name; break; }
            ++count;
        }
    }
    const float row2_y = G.first_y + 2.0f * G.row_h;
    click(runner, G.inner_x + G.inner_w * 0.5f, row2_y + G.row_h * 0.5f);
    runner.run_frames(5);
    const std::string selected = runner.game().host_shop_selected_item();
    std::fprintf(stderr, "  [S2] click row 2: selected='%s' expected='%s'\n",
                 selected.c_str(), expect.c_str());
    CHECK(!expect.empty() && selected == expect,
          "S2: clicking the third row selects the third catalog item");

    // --- (b) Category switch filters the rows ---
    const float s = ui::points_scale(h);
    const float bottom_y = h - 80.0f * s;
    const float cat_icon_w = 52.0f * s;
    const float icon_gap = 12.0f * s;
    const float start_x = (w - (5.0f * cat_icon_w + 4.0f * icon_gap)) * 0.5f;
    const float cat_icon_y = bottom_y + (80.0f * s - cat_icon_w) * 0.5f;
    click(runner, start_x + cat_icon_w + icon_gap + cat_icon_w * 0.5f,
          cat_icon_y + cat_icon_w * 0.5f);  // Armor tab
    runner.run_frames(5);
    rows = shop_rows(runner);
    std::fprintf(stderr, "  [S2] armor rows: %zu [", rows.size());
    for (const auto& row : rows) std::fprintf(stderr, "%s, ", row.c_str());
    std::fprintf(stderr, "]\n");
    bool all_armor = !rows.empty();
    for (const auto& row : rows)
        if (row.empty() || item_type(runner, row) != "Armor") { all_armor = false; break; }
    CHECK(all_armor, "S2: switching to the Armor tab shows armor items only");

    // --- (c) Scroll moves the window (S key) ---
    click(runner, start_x + cat_icon_w * 0.5f, cat_icon_y + cat_icon_w * 0.5f);  // back to Weapon
    runner.run_frames(5);
    rows = shop_rows(runner);
    const std::string row0_before = rows.empty() ? std::string{} : rows[0];
    runner.tap_key(plat::Key::S);
    runner.tap_key(plat::Key::S);
    runner.tap_key(plat::Key::S);
    runner.run_frames(3);
    rows = shop_rows(runner);
    const std::string row0_after = rows.empty() ? std::string{} : rows[0];
    std::fprintf(stderr, "  [S2] scroll: row0 '%s' -> '%s'\n",
                 row0_before.c_str(), row0_after.c_str());
    CHECK(!row0_after.empty() && row0_after != row0_before,
          "S2: pressing S scrolls the list window (row 0 changes)");
}

// ---------- S3: BUY button at desktop resolution + loud failure ----------

static void test_s3_buy_button() {
    std::printf("\n=== S3: BUY hit-test at 1280x720 + failed-buy diagnostics ===\n");
    resf2::test::HeadlessTestRunner runner = make_runner(1280, 720, "shop");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S3 init() failed\n"); ++tests_failed; return; }
    runner.run_frames(10);

    const float w = (float)runner.platform().window_width();
    const float h = (float)runner.platform().window_height();
    const float s = ui::points_scale(h);
    const float bottom_y = h - 80.0f * s;
    const float detail_w = w * 0.32f;
    const float buy_btn_w = detail_w * 0.72f;
    const float buy_btn_h = 44.0f * s;
    const float buy_btn_x = w * 0.68f + (detail_w - buy_btn_w) * 0.5f;
    const float buy_btn_y = bottom_y - buy_btn_h - 14.0f * s;

    // --- (a) failed buy is LOUD: no gold -> the click must log why ---
    // (host_spend_currency refuses to go below zero, so spend exactly the
    // starting 1000 to reach 0.)
    runner.game().host_spend_currency(1000);
    const int gold0 = runner.game().host_get_currency();
    const char* cap = "test_soak_shop_story_s3.tmp";
    std::freopen(cap, "w", stdout);
    click(runner, buy_btn_x + buy_btn_w * 0.5f, buy_btn_y + buy_btn_h * 0.5f);
    runner.run_frames(5);
    std::fflush(stdout);
    suppress_stdout();
    std::string diag;
    {
        std::ifstream f(cap);
        std::string line;
        while (std::getline(f, line))
            if (line.find("[SHOP] cannot buy") != std::string::npos) diag = line;
    }
    std::remove(cap);
    std::fprintf(stderr, "  [S3] gold=%d click -> %s\n", gold0,
                 diag.empty() ? "<silent>" : diag.c_str());
    CHECK(gold0 < 50 && !diag.empty(),
          "S3: a failed BUY click logs the reason (gold/level breakdown)");

    // --- (b) affordable buy at the desktop resolution spends exactly ---
    runner.game().host_add_currency(1000);
    const int gold1 = runner.game().host_get_currency();
    click(runner, buy_btn_x + buy_btn_w * 0.5f, buy_btn_y + buy_btn_h * 0.5f);
    runner.run_frames(5);
    const int gold2 = runner.game().host_get_currency();
    std::fprintf(stderr, "  [S3] buy click: gold %d -> %d, owns WEAPON_KNIVES=%d\n",
                 gold1, gold2, (int)runner.game().host_has_item("WEAPON_KNIVES"));
    CHECK(gold2 == gold1 - 50 && runner.game().host_has_item("WEAPON_KNIVES"),
          "S3: clicking BUY at 1280x720 spends the price and lands the item");
}

// ---------- S4: shop preview weapon at the hand ----------

static void test_s4_preview_weapon() {
    std::printf("\n=== S4: shop preview places the knife at the hand ===\n");
    resf2::test::HeadlessTestRunner runner = make_runner(320, 180, "");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S4 init() failed\n"); ++tests_failed; return; }
    suppress_stdout();
    // Let the dojo settle: the intro stance (stance_2) is non-interruptible
    // for ~156 frames, so pre-roll past it into the idle stance (populates
    // anim_node_pos_ — the pose the preview would otherwise mix with the
    // weapon's skeleton-rest pins).
    runner.run_frames(260);
    // The hermetic profile owns nothing: give the fighter the knives so the
    // preview has a weapon model to draw (the real game always has one).
    runner.game().host_add_item("WEAPON_KNIVES");
    CHECK(runner.game().host_equip_item("WEAPON_KNIVES"),
          "S4: the knives equip (weapon model loads)");
    // The idle stance coincides with the skeleton rest pose (gap ~0 either
    // way), so freeze a STEP pose — the player walks into the shop in the
    // real game, and that is when the knife visibly hangs above the hand.
    runner.tap_key(plat::Key::A, 30);
    runner.run_frames(30);
    runner.tap_key(plat::Key::A, 30);
    runner.run_frames(30);
    runner.game().request_scene_transition(scn::SceneId::Shop);
    if (!run_until_scene(runner, scn::SceneId::Shop)) {
        std::fprintf(stderr, "FAIL: S4 never reached the Shop scene\n"); ++tests_failed; return;
    }
    runner.run_frames(5);  // the preview renders each shop frame
    const float gap = runner.game().host_get_shop_preview_hand_gap();
    std::fprintf(stderr, "  [S4] preview hand<->weapon-anchor gap = %.2f world units\n", gap);
    CHECK(gap < 12.0f,
          "S4: the preview weapon anchor sits at the body hand (gap < 12 units)");
}

// ---------- S5: story continuation after the first win ----------

static void test_s5_story_continuation() {
    std::printf("\n=== S5: story continues after the first win ===\n");
    resf2::test::HeadlessTestRunner runner = make_runner(320, 180, "");
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S5 init() failed\n"); ++tests_failed; return; }
    suppress_stdout();
    runner.run_frames(10);

    // --- (a) winning the Kenji (FIRST_FIGHT) fight queues the next beat ---
    runner.game().host_set_tutorial_state("FIRST_FIGHT");
    runner.game().host_set_battle_result("victory");
    runner.game().request_scene_transition(scn::SceneId::Results);
    if (!run_until_scene(runner, scn::SceneId::Results)) {
        std::fprintf(stderr, "FAIL: S5 never reached the Results scene\n"); ++tests_failed; return;
    }
    CHECK(runner.game().host_get_tutorial_state() == "COMPLETE",
          "S5: FIRST_FIGHT victory advances the tutorial to COMPLETE");
    CHECK(!runner.game().host_get_dialogue().empty(),
          "S5: a follow-up dialogue is queued after the Kenji win");
    CHECK(runner.game().host_has_pending_story_dialogue(),
          "S5: the queued story dialogue is pending");

    // --- (b) the queued dialogue plays over the Map and returns to it ---
    runner.game().request_scene_transition(scn::SceneId::Map);
    if (!run_until_scene(runner, scn::SceneId::Map)) {
        std::fprintf(stderr, "FAIL: S5 never reached the Map\n"); ++tests_failed; return;
    }
    CHECK(run_until_scene(runner, scn::SceneId::Dialogue, 60),
          "S5: the pending story dialogue opens over the Map");
    runner.tap_key(plat::Key::Space);  // advance through the (single) line
    runner.run_frames(3);
    CHECK(run_until_scene(runner, scn::SceneId::Map, 60),
          "S5: the story dialogue returns to the Map when finished");

    // --- (c) buying the knives advances the tutorial chain ---
    runner.game().host_set_tutorial_state("COMPLETE");
    CHECK(runner.game().host_buy_item("WEAPON_KNIVES"),
          "S5: the knives are buyable");
    CHECK(runner.game().host_has_pending_story_dialogue(),
          "S5: buying the knives queues the Lynx-challenge dialogue");

    // --- (d) winning ZONE_1|BOSS_LYNX|1 fires FirstGuardBeaten ---
    runner.game().host_trigger_quest_event("FightEnd", "ZONE_1|BOSS_LYNX|1");
    CHECK(!runner.game().host_get_dialogue().empty(),
          "S5: the first-guard win queues the Lynx/May story dialogue");
    CHECK(runner.game().host_quest_battle_unlocked("ZONE_1|Tournament"),
          "S5: the first-guard win shows the ZONE_1 tournament battle");
}

int main() {
    std::printf("=== Soak Wave 9B: SHOP + STORY defects ===\n");

    test_s1_item_icons();
    test_s2_shop_list();
    test_s3_buy_button();
    test_s4_preview_weapon();
    test_s5_story_continuation();

    std::fflush(stdout);
    if (tests_failed > 0) {
        std::fprintf(stderr, "\n=== SOAK SHOP/STORY TEST FAILED (%d failures) ===\n",
                     tests_failed);
        return 1;
    }
    std::printf("\n=== SOAK SHOP/STORY TEST PASSED (%d checks) ===\n", tests_passed);
    return 0;
}
