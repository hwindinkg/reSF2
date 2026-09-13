// app/game/ — the playable shell (Phase 3.6a): main loop, screen manager,
// save/load (users.xml), the main menu, and the map.
//
// Boots the App -> GeneralMenu (screen 8). The user can click Fight -> Map
// (screen 5) -> a battle node -> the BattleResult placeholder. Real GLFW
// window, real mouse input (hit-test on the button rects). Screen
// transitions are logged; the save round-trip is verified by the
// SaveSystem (load users_default -> modify -> save -> reload).
//
// Usage:
//   game [res_root] [save_path]
//   game [res_root] [save_path] --headless N   run N frames then exit (log-only)
//   game [res_root] [save_path] --autoclick     click the Fight button once
//
// Defaults: res_root = reference/www/res, save = reference/saves/save.xml.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "app/app.hpp"
#include "app/save_system.hpp"
#include "app/screens.hpp"
#include "scene/fighter.hpp"

namespace {

using namespace sf2::app;  // kScreen* ids + the App/SaveSystem types

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [res_root] [save_path] [--headless N] [--autoclick] [--headless-loop]\n"
                  "                  [--fight] [--battle <name>] [--zone <name>]\n"
                  "                  [--dump-pose N] [--dump-clip <name>]\n"
                  "                  [--ui-tour] [--fidelity-tour]\n"
                  "                  [--replay [file]] [--verify-input]\n"
                 "  res_root  default reference/www/res\n"
                 "  save_path default reference/saves/save.xml\n"
                 "  --headless-loop  run the scripted playable loop, then exit\n"
                 "                   (dojo -> map -> BOSS_LYNX fight -> results -> shop\n"
                 "                    buy WEAPON_KNIVES -> profile viewer (equip OPEN)\n"
                 "                    -> map -> BOSS_LYNX fight -> results)\n"
                 "  --fight          boot DIRECTLY into the dojo fight (skip menu/map):\n"
                 "                   dojo, player Fists (keyboard) vs enemy Fists (AI)\n"
                 "  --dump-pose N    (with --fight) dump the first N fight frames as JSONL to\n"
                 "                   reference/traces/native_pose.jsonl (trace, no sim change)\n"
                 "  --dump-clip N    dump the anim archive clip <name> as 1/16 fixed-point\n"
                 "                   JSON to reference/traces/native_clip_<name>.json, exit 0\n",
                 argv0);
}

// One step of the headless-loop driver. Each step is two-phase:
//   wait for `wait_screen` (the screen the click targets), then click
//   (x,y); then wait for `expect_screen` (the screen the click navigates
//   to). `hold_frames` (>0) advances after that many frames post-click for
//   same-screen actions (buy/equip) where the screen id doesn't change.
// `capture` snapshots the expected screen on arrival.
struct LoopStep {
    float x = 0.0f;
    float y = 0.0f;
    const char* label = "";
    int wait_screen = -1;
    int min_delay = 0;     // min frames since step start before clicking
    int expect_screen = -1;
    int hold_frames = 0;   // same-screen steps: advance N frames after click
    const char* capture = nullptr;
    // Optional pre-click (0/0 = none). The JS map has no zone-tab strip
    // (PORT_AUDIT_UI §2.3); kept for drivers that need a settle click.
    float tab_x = 0.0f;
    float tab_y = 0.0f;
};

// The full scripted progression (Phase 3.6b). Runs in App::run_one_frame
// via HeadlessLoopDriver (below). Screen ids: 3=Dojo, 4=Shop, 5=Map,
// 6=Fight, 7=Profile(Equipment), 8=GeneralMenu, 10=Results.
//
// The navigation hub is the DOJO home screen (screen 3 — the screen the
// game boots into; the original starts in the Dojo, not the GeneralMenu).
// The Dojo buttons are FIGHT(training)/MAP/SHOP/PROFILE. The loop is:
//   dojo -> Map -> BOSS_LYNX fight -> Results -> Map -> BACK to dojo
//   dojo -> Shop -> BUY knives -> EQUIP knives -> BACK to dojo
//   dojo -> Profile -> BACK to dojo
//   dojo -> Map -> BOSS_LYNX fight -> Results -> Map
// (Equip lives in the shop detail `$o` (SHOP_STATIC §4): the detail-panel
//  action button buys while the item is unowned, then equips the owned item.
//  The loop exercises both, so the second fight's Locks move list reflects
//  the equipped weapon.)
//
// Layout math (matches the screen implementations in core/app/screens.cpp):
//   - dojo/shell nav: the shared `za` VERTICAL column (kZaNav, za_layout):
//     nav_cx = 184, rows y = 126/231/337/442/548 for
//     MAP/SHOP/PROFILE/SETTINGS (index 1..4; DOJO is index 0/self).
//   - map nodes (JS `qe.X0a` L2144): x = pos.x*uM + bg.w/2, y = -pos.y*uM
//     + bg.h/2 - 50, mapped through the 2046x854 backdrop -> the 1280x720
//     view (uM = 1.5003663). The map opens on the save's CurrentZone
//     (ZONE_1); there is NO zone-tab strip in the JS map (PORT_AUDIT_UI
//     §2.3) and the zone scroller is not ported, so only ZONE_1 nodes are
//     reachable. Its first node (BOSS_LYNX, X=-180 Y=-45) sits at ~(471,375).
//   - shop card grid: first card center (548.2, 218.4) at 1280x720 (the
//     responsive `Oa.layout` split, Wave H).
//   - equipment owned-item grid: first card (0.55*1280+110, 220+40).
//   - BACK buttons: top-left (64, 40) on Map/Shop/Equipment (pops back to
//     the Dojo hub).
static const LoopStep kLoopSteps[] = {
    // 0: Dojo -> Map (the MAP button). Capture loop_map.png on arrival.
    //    `tab_x/tab_y` = the collapsed `za` header tap (JS `gk.collapse(0)`
    //    default, L1978): the nav column is hidden until the header is
    //    tapped, so expand it 5 frames before the MAP click (header rect
    //    x64-176 y72-110; see za_header_rect in screens.cpp).
    {184.0f, 231.0f, "dojo->map (MAP)", kScreenDojo, 0, kScreenMap, 0,
     "loop_map.png", 120.0f, 90.0f},
    // 1: Map -> ZONE_1 boss fight (BOSS_LYNX, X=-180 Y=-45 -> ~471,375) —
    //    a money-bearing fight. The fight runs to KO (auto-attack) and
    //    pushes Results. Capture the fists fight (before-equip evidence).
    //    No zone tab: the map opens on the save's CurrentZone (ZONE_1) and
    //    the JS map has no tab strip (PORT_AUDIT_UI §2.3).
    {471.0f, 375.0f, "map->BOSS_LYNX fight", kScreenMap, 0, kScreenFight, 0,
     "loop_fight_fists.png"},
    // 2: Results -> Map (click anywhere pops; the results->map flow pops
    //    the dead Fight screen too). Capture loop_results.png on arrival.
    {1280 * 0.5f, 360.0f, "results->map", kScreenResults, 0, kScreenMap, 0, "loop_results.png"},
    // 3: Map -> BACK to the Dojo hub (top-left).
    {64.0f, 40.0f, "map->dojo (BACK)", kScreenMap, 0, kScreenDojo, 0, nullptr},
    // 4: Dojo -> Shop (the SHOP button). Capture loop_shop.png on arrival.
    {184.0f, 337.0f, "dojo->shop", kScreenDojo, 0, kScreenShop, 0,
     "loop_shop.png"},
    // 5: Shop -> BUY WEAPON_KNIVES (row 0, price 50). The grid click only
    //    SELECTS (JS `Oa.xA` L2296); the purchase is the `Up.Fhb` action
    //    button (`Oa.layout` L2295: left-slot top, `jP.C((b.J+b.N)*.5*.9)`,
    //    `jP.D(b.P+Up.qa())`). `sel_` already defaults to row 0 on entry, so
    //    the action click alone buys. Button centre from `shop_try_rect` =
    //    (305.4, 199.5) at 1280x720.
    {305.4f, 199.5f, "shop->buy WEAPON_KNIVES", kScreenShop, 0, -1, 12, nullptr},
    // 6: Shop -> EQUIP WEAPON_KNIVES (same action button; now owned ->
    //    `xa.$o`, L2300). The honest buy->equip path.
    {305.4f, 199.5f, "shop->equip WEAPON_KNIVES", kScreenShop, 0, -1, 12, nullptr},
    // 7: Shop -> BACK to the Dojo hub.
    {64.0f, 40.0f, "shop->dojo (BACK)", kScreenShop, 0, kScreenDojo, 0, nullptr},
    // 8: Dojo -> Equipment (the PROFILE button). Capture loop_equip.png on
    //    arrival. The old Profile MOVES-tab step is gone: equip now lives in
    //    the shop detail (`$o`), so the shop BUY/EQUIP steps are the honest
    //    path and the Profile visit is just the viewer capture.
    {184.0f, 442.0f, "dojo->equipment (PROFILE)", kScreenDojo, 0,
     kScreenProfile, 0, "loop_equip.png"},
    // 9: Equipment -> BACK to the Dojo hub.
    {64.0f, 40.0f, "equipment->dojo (BACK)", kScreenProfile, 0, kScreenDojo, 0, nullptr},
    // 10: Dojo -> Map again (MAP).
    {184.0f, 231.0f, "dojo->map (MAP)", kScreenDojo, 0, kScreenMap, 0,
     nullptr},
    // 11: Map -> ZONE_1 boss fight with the knives equipped (same reachable
    //     node as step 1). Capture loop_fight.png on arrival (after-equip).
    {471.0f, 375.0f, "map->BOSS_LYNX fight (knives)", kScreenMap, 0, kScreenFight, 0,
     "loop_fight.png"},
    // 12: Results -> Map (the loop end).
    {1280 * 0.5f, 360.0f, "results->map (loop end)", kScreenResults, 0, kScreenMap, 0, nullptr},
};
constexpr int kLoopStepCount = static_cast<int>(sizeof(kLoopSteps) / sizeof(kLoopSteps[0]));

// The headless-loop driver. Runs one tick per present frame (a small
// state machine that replaces the single auto_click path when enabled).
struct HeadlessLoopDriver {
    int step = 0;
    int step_frame = 0;
    int last_seen = -1;
    bool clicked = false;       // the current step's click has been sent
    bool tab_clicked = false;   // the current step's tab pre-click (if any)
    bool next_clicked = false;  // the between-rounds NEXT click has been sent
    bool captured_menu = false;
    int guard = 0;
    bool finished = false;
    int before_moves = 0;   // move-list size at the first fight (saved weapon)
    int after_moves = 0;    // move-list size at the second fight (post-buy)
    bool logged_before = false;
    bool logged_after = false;
    std::string last_capture;

    // Called once per present frame (before the fixed-step update).
    void frame_tick(sf2::app::App& app) {
        const LoopStep& s = kLoopSteps[step];
        const int cur = app.screens().current_id();
        if (cur != last_seen) {
            last_seen = cur;
            std::fprintf(stdout, "[loop] screen %d (step %d/%d)\n", cur, step + 1,
                         kLoopStepCount);
            std::fflush(stdout);
        }

        // Between-rounds NEXT: while the top screen is the FightScreen
        // waiting for the player (round_wait — a round ended; the next one
        // only starts on the HUD Next button), click the button center so
        // the fight can finish and reach the Results steps. Without this
        // the fight holds in EndStance forever and the loop stalls.
        sf2::app::Screen* top = app.screens().top();
        const bool fight_waiting =
            cur == kScreenFight && top != nullptr &&
            static_cast<sf2::app::FightScreen*>(top)->round_wait();
        if (fight_waiting && !next_clicked) {
            float cx = 0.0f, cy = 0.0f;
            static_cast<sf2::app::FightScreen*>(top)->next_button_center(cx, cy);
            app.inject_click(cx, cy);
            next_clicked = true;
            std::fprintf(stdout, "[loop] round_wait -> NEXT click (%.0f, %.0f)\n", cx, cy);
            std::fflush(stdout);
        } else if (!fight_waiting) {
            next_clicked = false;  // re-arm once the fight leaves round_wait
        }

        // Phase A: wait for the target screen, then click. Steps with a
        // zone tab click it first (5 frames before the main click so the
        // node list switches).
        if (!clicked) {
            if (cur == s.wait_screen && step_frame >= s.min_delay) {
                if (s.tab_x != 0.0f && !tab_clicked) {
                    std::fprintf(stdout, "[loop] step %d/%d %s -> tab click (%.0f, %.0f)\n",
                                 step + 1, kLoopStepCount, s.label, s.tab_x, s.tab_y);
                    std::fflush(stdout);
                    app.inject_click(s.tab_x, s.tab_y);
                    tab_clicked = true;
                    step_frame = s.min_delay - 5;
                    return;
                }
                if (step == 0 && !captured_menu) {
                    // The menu, after the first render (capture reads the
                    // back buffer).
                    captured_menu = true;
                    const std::string path = "reference/extracted/scene/loop_menu.png";
                    app.capture_png(path);
                    last_capture = path;
                    std::fprintf(stdout, "[loop] capture loop_menu.png\n");
                    std::fflush(stdout);
                }
                std::fprintf(stdout, "[loop] step %d/%d %s -> click (%.0f, %.0f)\n", step + 1,
                             kLoopStepCount, s.label, s.x, s.y);
                std::fflush(stdout);
                app.inject_click(s.x, s.y);
                clicked = true;
                ++step_frame;
            } else {
                ++step_frame;
            }
            return;
        }

        // Phase B: wait for the post-click screen (or hold for same-screen
        // actions), then advance.
        if (s.hold_frames > 0) {
            if (step_frame >= s.min_delay + s.hold_frames) {
                std::fprintf(stdout, "[loop] step %d/%d done (%s)\n", step + 1, kLoopStepCount,
                             s.label);
                std::fflush(stdout);
                advance();
                return;
            }
        } else if (cur == s.expect_screen) {
            if (s.expect_screen == kScreenFight) {
                // The fight is up (the MapScreen click pushed it): capture
                // the player's move-list size now — the FightScreen ctor
                // already logged the move names.
                const int size = app.screens().top() != nullptr
                                     ? static_cast<int>(static_cast<sf2::app::FightScreen*>(
                                                           app.screens().top())
                                                           ->move_list_size())
                                     : 0;
                if (step == 1) {
                    before_moves = size;
                    logged_before = true;
                    std::fprintf(stdout, "[loop] move list before buy: %d moves (saved weapon)\n",
                                 size);
                } else if (step == 11) {
                    after_moves = size;
                    logged_after = true;
                    // The shop BUY + EQUIP steps ran, so the Locks move list
                    // reflects the equipped WEAPON_KNIVES (JS `xa.$o`).
                    std::fprintf(stdout,
                                 "[loop] move list after buy+equip: %d moves (knives)\n",
                                 size);
                }
                std::fflush(stdout);
            }
            if (s.capture != nullptr) {
                const std::string path = std::string("reference/extracted/scene/") + s.capture;
                app.capture_png(path);
                last_capture = path;
                std::fprintf(stdout, "[loop] capture %s\n", s.capture);
                std::fflush(stdout);
            }
            std::fprintf(stdout, "[loop] step %d/%d done (%s)\n", step + 1, kLoopStepCount,
                         s.label);
            std::fflush(stdout);
            advance();
            return;
        }
        ++step_frame;
    }

    void advance() {
        ++step;
        step_frame = 0;
        clicked = false;
        tab_clicked = false;
        if (step >= kLoopStepCount) {
            finished = true;
            std::fprintf(stdout, "[loop] ALL %d STEPS DONE\n", kLoopStepCount);
            if (logged_before && logged_after) {
                std::fprintf(stdout,
                             "[loop] move-list before/after buy+equip: %d -> %d moves\n",
                             before_moves, after_moves);
            }
        }
    }
};

// UI tour driver (Dojo UI-diff wave): visits each screen and captures
// reference/traces/ui/port_<name>.png via App::capture_png. Key steps
// mirror the loop's proven coordinates (1280x720). `key` injects a
// GLFW key down/up instead of clicking (80 = P pause toggle).
// `no_click` = settle+capture only. Round_wait NEXT clicks reuse the
// loop's logic so tour fights run to Results.
struct UiTourStep {
    float x = 0.0f;
    float y = 0.0f;
    const char* label = "";
    int wait_screen = -1;
    int min_delay = 0;
    int expect_screen = -1;
    int hold_frames = 0;
    const char* capture = nullptr;
    int key = 0;
    bool no_click = false;
    // Optional pre-click (0/0 = none; the JS map has no zone-tab strip).
    float tab_x = 0.0f;
    float tab_y = 0.0f;
    // Optional per-step auto-attack override (-1 = leave unchanged). The
    // fidelity tour arms it on the map node-click step, before the
    // FightScreen is constructed, so the fight resolves to Results.
    int auto_attack = -1;
    // Optional boss-roster capture hook (the fidelity tour's `act_boss`
    // step): while set, MapScreen draws the JS `jk` boss-intro roster
    // instead of the map so the headless tour can capture it.
    bool force_boss_roster = false;
};

static const UiTourStep kUiTourSteps[] = {
    // 0: Dojo hub at boot (fresh save) - settle then capture.
    {0.0f, 0.0f, "dojo hub", 3, 150, -1, 60, "port_dojo.png", 0, true},
    // 1: Dojo -> Map (`za` vertical nav column, MAP = row 1 @184,231).
    //    `tab_x/tab_y` = the collapsed `za` header tap: expand the nav
    //    before the MAP click (step 0 captured the collapsed hub look).
    {184.0f, 231.0f, "dojo->map", 3, 10, 5, 60, "port_map.png", 0, false,
     120.0f, 90.0f},
    // 2: Map -> Dojo (BACK).
    {64.0f, 40.0f, "map->dojo", 5, 10, 3, 0, nullptr},
    // 3: Dojo -> Shop (nav row 2 @184,337).
    {184.0f, 337.0f, "dojo->shop", 3, 10, 4, 60, "port_shop.png"},
    // 4: Shop tab 2 (HELMS). The `ss`/`Eg` strip is the BOTTOM bar
    //    (`shop_tab_layout().cy = H - bar_h/2` ~= 673), so click the tab-2
    //    button there (the old (640,100) missed the strip and re-captured tab 0).
    {640.0f, 673.0f, "shop tab 2", 4, 10, -1, 40, "port_shop_tab2.png"},
    // 5: Shop -> Dojo (BACK).
    {64.0f, 40.0f, "shop->dojo", 4, 10, 3, 0, nullptr},
    // 6: Dojo -> Equipment (PROFILE, nav row 3 @184,442).
    {184.0f, 442.0f, "dojo->profile", 3, 10, 7, 60, "port_profile.png"},
    // 7: Equipment -> Dojo (BACK).
    {64.0f, 40.0f, "profile->dojo", 7, 10, 3, 0, nullptr},
    // 8: Dojo -> Map again. The JS hub has no direct Fight button (it is the
    //    `FightNone` viewer); fights launch from the map's zone nodes.
    {184.0f, 231.0f, "dojo->map (fight)", 3, 10, 5, 0, nullptr},
    // 9: Map -> ZONE_1 boss fight (BOSS_LYNX ~471,375). The map opens on the
    //    save's CurrentZone (ZONE_1) and has no zone-tab strip; its first
    //    node launches the fight. Hold 250 for the HUD.
    {471.0f, 375.0f, "map->BOSS_LYNX fight", 5, 10, 6, 250, "port_fight.png"},
    // 10: Pause via Esc, capture the pause menu. (P is the JS Magic key now
    //     — `Af.oUa` v[12]=80 — so the native pause alias is Escape only.)
    {0.0f, 0.0f, "pause.png", 6, 10, -1, 40, "port_pause.png", 256},
    // 11: Resume via Esc, run to KO -> Results captures on arrival.
    {0.0f, 0.0f, "resume->results", 6, 10, 10, 0, "port_results.png", 256},
    // 12: Results -> Map (the tour went Dojo->Map->Fight, so Results pops
    //     back to the map beneath the fight).
    {640.0f, 360.0f, "results->map", 10, 10, 5, 0, nullptr},
    // 13: Map -> Dojo (BACK).
    {64.0f, 40.0f, "map->dojo (post-fight)", 5, 10, 3, 0, nullptr},
    // 14: Dojo -> Settings (nav row 4 @184,547).
    {184.0f, 547.0f, "dojo->settings", 3, 10, 11, 0, "port_settings.png"},
};
constexpr int kUiTourStepCount = static_cast<int>(sizeof(kUiTourSteps) / sizeof(kUiTourSteps[0]));

// Fidelity tour (Phase 0 harness, phase1 step9): mirror the oracle's state
// matrix (reference/traces/oracle_matrix/<state>.png) into the port's
// reference/traces/port_matrix/<state>.png. The same proven navigate/settle/
// capture machine as the UI tour; `capture` is the raw state name. States the
// port cannot reach are captured from their closest reachable state (see
// reference/FIDELITY_MATRIX.md for the per-state owner notes). The two boot
// states (splash/loader) are captured before the driver (non-headless overlay).
//
// Coordinates (1280x720, matching core/app/screens.cpp layout math):
//   za header (collapsed nav expand) = (120, 90); nav rows MAP/SHOP/PROFILE/
//   SETTINGS = (184, 231/337/442/547); map node BOSS_LYNX = (471, 375);
//   BACK = (64, 40); results pop = (640, 360).
//   shop tab strip (shop_tab_layout): y=676, x = 418.7/529.3/640.0/750.7/861.3.
//   profile tab strip (profile_tab_layout): y=672.5, x = 461.1/580.4/699.7/818.9.
//   shop first card = (548.2, 218.4).
static const UiTourStep kFidelitySteps[] = {
    // --- Fresh-profile tutorial (JS StoryTutorialWelcome) --------------------
    // Approved `fresh/tutorial-from-0` boot: the Dojo plays the Sensei beats
    // (App::fresh_tutorial), then the `Punchbag|Bosses|1` training fight, then
    // the clean hub. Beats mirror tutorial_quests.xml StoryTutorialWelcome
    // (tutorial_move -> tutorial_punchbag -> the characterSensei Regular
    // dialog + dlgStoryBtnFight).
    // 0: beat 0 notification ("tutorial_move") -> tut_fight_stance.
    {0.0f, 0.0f, "tut stance (move notification)", 3, 150, -1, 90, "tut_fight_stance.png", 0, true},
    // 1: tap the notification banner -> beat 1 ("tutorial_punchbag").
    {1145.0f, 244.0f, "tut phase2 (punchbag notification)", 3, 10, -1, 50, "tut_fight_phase2.png", 0, false},
    // 2: tut_block (oracle CLOSEST: the same punchbag lesson).
    {0.0f, 0.0f, "tut block (punchbag notification)", 3, 0, -1, 30, "tut_block.png", 0, true},
    // 3: tap -> beat 2, the Regular Sensei training-fight dialog.
    {1145.0f, 244.0f, "dojo sensei (training dialog)", 3, 10, -1, 50, "dojo_sensei.png", 0, false},
    // 4: the dialog FIGHT button (`dlgStoryBtnFight`) -> the training fight
    //    (a real tutorial fight state for tut_win).
    {860.0f, 554.0f, "tut fight (Punchbag training)", 3, 10, 6, 520, "tut_win.png", 0, false,
     0.0f, 0.0f, 1},
    // 5: pause the training fight (Esc). The Training <Rules> carry no round
    //    end (the dummy never KOs), so the fight is exited, not won; the
    //    tutorial completes when the fight starts (screens.cpp
    //    start_tutorial_fight).
    {0.0f, 0.0f, "tut fight pause (Esc)", 6, 10, -1, 30, nullptr, 256, false},
    // 6: the pause dialog home ("QUIT") pops the fight -> the clean Dojo hub.
    //    `Dr` home button centre (L2066/L2068 layout: row y=396, x=403.75).
    {403.75f, 396.0f, "tut fight quit->dojo", 6, 10, 3, 0, nullptr},
    // 7: the clean Dojo hub (tutorial done; no banner).
    {0.0f, 0.0f, "dojo hub", 3, 20, -1, 80, "dojo_hub.png", 0, true},
    // 8: expand the collapsed `za` nav column (the header tap).
    {120.0f, 90.0f, "dojo menu open (za header)", 3, 10, -1, 40, "dojo_menu_open.png", 0, false},
    // --- Map ----------------------------------------------------------------
    {184.0f, 231.0f, "dojo->map", 3, 10, 5, 60, "map_zone1.png", 0, false},
    // node selection / info panels are not modelled (PORT_AUDIT_UI §2.3/2.4):
    // closest reachable = the ZONE_1 map itself.
    {0.0f, 0.0f, "map node sel (closest: map)", 5, 0, -1, 20, "map_node_sel.png", 0, true},
    {0.0f, 0.0f, "map panels (closest: map)", 5, 0, -1, 20, "map_panels.png", 0, true},
    // act_boss: the JS `jk` boss-intro roster (multi-fight boss battle start).
    // The port renders it headlessly via the `force_boss_roster` hook (the
    // boss act itself is bypassed in headless — MapScreen guards
    // `!app().headless()`), so the capture is the roster, not the map.
    {0.0f, 0.0f, "act_boss (boss roster)", 5, 10, -1, 30, "act_boss.png", 0, true, 0.0f, 0.0f, -1,
     true},
    // --- Fight (auto-attack ON so it resolves to Results) -------------------
    {471.0f, 375.0f, "map->fight", 5, 10, 6, 40, "fight_intro.png", 0, false, 0.0f, 0.0f, 1},
    // Pause early (phase 1, definitely live), capture, resume. Esc is the
    // native pause alias (P is the JS Magic key). `min_delay=90` lets the
    // ROUND intro banner clear first (the oracle `pause` capture has no
    // banner over the dialog).
    {0.0f, 0.0f, "pause (Esc)", 6, 90, -1, 40, "pause.png", 256, false},
    {0.0f, 0.0f, "resume (Esc)", 6, 0, -1, 30, nullptr, 256, false},
    // Phase 2 (>133 fight frames): idle stance.
    {0.0f, 0.0f, "fight stance", 6, 0, -1, 140, "fight_stance.png", 0, true},
    {0.0f, 0.0f, "fight attack (punch)", 6, 0, -1, 25, "fight_attack.png", 32, false},
    // Block is not a raw key in this game (on_key: it is a move interval);
    // closest reachable = the attack-recovery frame.
    {0.0f, 0.0f, "fight block (closest: recovery)", 6, 0, -1, 8, "fight_block.png", 0, true},
    {0.0f, 0.0f, "fight hit (closest: mid-fight)", 6, 0, -1, 40, "fight_hit.png", 0, true},
    {0.0f, 0.0f, "fight->results", 6, 0, 10, 0, "results_win.png", 0, true},
    // results_lose: no deterministic headless loss path (the enemy AI is
    // passive in the tested direct fight; the auto fight wins) — closest
    // reachable = the win Results.
    {0.0f, 0.0f, "results_lose (closest: results_win)", 10, 0, -1, 5, "results_lose.png", 0, true},
    {640.0f, 360.0f, "results->map", 10, 10, 5, 0, nullptr},
    {64.0f, 40.0f, "map->dojo", 5, 10, 3, 0, nullptr},
    // --- Shop ---------------------------------------------------------------
    {184.0f, 337.0f, "dojo->shop", 3, 10, 4, 60, "shop_tab1.png", 0, false},
    {548.2f, 218.4f, "shop detail (select row 0)", 4, 10, -1, 30, "shop_detail.png", 0, false},
    {529.3f, 676.0f, "shop tab 2", 4, 10, -1, 30, "shop_tab2.png", 0, false},
    {640.0f, 676.0f, "shop tab 3", 4, 10, -1, 30, "shop_tab3.png", 0, false},
    {750.7f, 676.0f, "shop tab 4", 4, 10, -1, 30, "shop_tab4.png", 0, false},
    {861.3f, 676.0f, "shop tab 5", 4, 10, -1, 30, "shop_tab5.png", 0, false},
    {64.0f, 40.0f, "shop->dojo", 4, 10, 3, 0, nullptr},
    // --- Profile (folded Moves = tab 1) -------------------------------------
    {184.0f, 442.0f, "dojo->profile", 3, 10, 7, 60, "profile_tab0.png", 0, false},
    {580.4f, 672.5f, "profile tab 1 (MOVES)", 7, 10, -1, 40, "profile_tab1.png", 0, false},
    {0.0f, 0.0f, "moves (folded into profile tab 1)", 7, 0, -1, 20, "moves.png", 0, true},
    {699.7f, 672.5f, "profile tab 2", 7, 10, -1, 40, "profile_tab2.png", 0, false},
    {818.9f, 672.5f, "profile tab 3", 7, 10, -1, 40, "profile_tab3.png", 0, false},
    {64.0f, 40.0f, "profile->dojo", 7, 10, 3, 0, nullptr},
    // --- Settings (last; no nav column) -------------------------------------
    {184.0f, 547.0f, "dojo->settings", 3, 10, 11, 60, "settings.png", 0, false},
};
constexpr int kFidelityStepCount =
    static_cast<int>(sizeof(kFidelitySteps) / sizeof(kFidelitySteps[0]));

// Generic tour driver — the proven navigate/settle/capture state machine,
// parameterized so BOTH the UI tour (reference/traces/ui/port_*.png) and the
// fidelity tour (reference/traces/port_matrix/<state>.png) reuse it. `steps`/
// `count` come from the caller, `out_dir` is the capture directory and `tag`
// the log prefix. Key steps inject a GLFW key down/up; `no_click` settles
// and captures only. Round_wait NEXT clicks run each fight to Results.
struct TourDriver {
    const UiTourStep* steps = nullptr;
    int count = 0;
    const char* out_dir = "reference/traces/ui";
    const char* tag = "[tour]";
    int step = 0;
    int step_frame = 0;
    int last_seen = -1;
    bool acted = false;
    bool key_up_done = false;
    bool next_clicked = false;
    bool tab_clicked = false;   // the step's zone-tab pre-click has been sent
    int guard = 0;
    bool finished = false;
    int applied_auto_attack = -1;  // last per-step auto-attack override applied
    int applied_force_roster = -1;  // last per-step boss-roster hook applied

    void frame_tick(sf2::app::App& app) {
        const UiTourStep& s = steps[step];
        const int cur = app.screens().current_id();
        // Per-step auto-attack override. The fidelity tour arms it on the
        // map node-click step, BEFORE the FightScreen is constructed, so the
        // fight it pushes picks up the mode.
        if (s.auto_attack != -1 && s.auto_attack != applied_auto_attack) {
            app.set_auto_attack(s.auto_attack != 0);
            applied_auto_attack = s.auto_attack;
            std::fprintf(stdout, "%s step %d/%d auto_attack=%d\n", tag, step + 1, count,
                         s.auto_attack);
            std::fflush(stdout);
        }
        // Per-step boss-roster capture hook (fidelity `act_boss`).
        if ((s.force_boss_roster ? 1 : 0) != applied_force_roster) {
            sf2::app::set_force_boss_roster(s.force_boss_roster);
            applied_force_roster = s.force_boss_roster ? 1 : 0;
            std::fprintf(stdout, "%s step %d/%d force_boss_roster=%d\n", tag, step + 1, count,
                         applied_force_roster);
            std::fflush(stdout);
        }
        if (cur != last_seen) {
            last_seen = cur;
            std::fprintf(stdout, "%s screen %d (step %d/%d %s)\n", tag, cur, step + 1,
                         count, s.label);
            std::fflush(stdout);
        }

        // Round-wait NEXT (copied from the loop driver so tour fights run
        // to Results instead of holding in EndStance forever).
        sf2::app::Screen* top = app.screens().top();
        const bool fight_waiting =
            cur == kScreenFight && top != nullptr &&
            static_cast<sf2::app::FightScreen*>(top)->round_wait();
        if (fight_waiting && !next_clicked) {
            float cx = 0.0f, cy = 0.0f;
            static_cast<sf2::app::FightScreen*>(top)->next_button_center(cx, cy);
            app.inject_click(cx, cy);
            next_clicked = true;
            std::fprintf(stdout, "%s round_wait -> NEXT click (%.0f, %.0f)\n", tag, cx, cy);
            std::fflush(stdout);
        } else if (!fight_waiting) {
            next_clicked = false;
        }

        if (!acted) {
            if (cur == s.wait_screen && step_frame >= s.min_delay) {
                if (s.tab_x != 0.0f && !tab_clicked) {
                    std::fprintf(stdout, "%s step %d/%d %s -> tab click (%.0f, %.0f)\n", tag,
                                 step + 1, count, s.label, s.tab_x, s.tab_y);
                    std::fflush(stdout);
                    app.inject_click(s.tab_x, s.tab_y);
                    tab_clicked = true;
                    step_frame = s.min_delay - 5;  // 5 frames for the list to switch
                    return;
                }
                if (s.key != 0) {
                    std::fprintf(stdout, "%s step %d/%d %s -> key %d\n", tag, step + 1,
                                 count, s.label, s.key);
                    std::fflush(stdout);
                    app.inject_key(s.key, true);
                } else if (!s.no_click) {
                    std::fprintf(stdout, "%s step %d/%d %s -> click (%.0f, %.0f)\n", tag,
                                 step + 1, count, s.label, s.x, s.y);
                    std::fflush(stdout);
                    app.inject_click(s.x, s.y);
                } else {
                    std::fprintf(stdout, "%s step %d/%d %s -> settle\n", tag, step + 1,
                                 count, s.label);
                    std::fflush(stdout);
                }
                acted = true;
                ++step_frame;
            } else {
                ++step_frame;
            }
            return;
        }

        // Key release shortly after the press (P/Esc toggle on down edge).
        if (s.key != 0 && !key_up_done && step_frame >= s.min_delay + 5) {
            app.inject_key(s.key, false);
            key_up_done = true;
        }

        if (s.hold_frames > 0) {
            if (step_frame >= s.min_delay + s.hold_frames) {
                snap(app, s);
                advance();
                return;
            }
        } else if (cur == s.expect_screen) {
            snap(app, s);
            advance();
            return;
        }
        ++step_frame;
    }

    void snap(sf2::app::App& app, const UiTourStep& s) {
        if (s.capture != nullptr) {
            const std::string path = std::string(out_dir) + "/" + s.capture;
            app.capture_png(path);
            std::fprintf(stdout, "%s capture %s\n", tag, path.c_str());
            std::fflush(stdout);
        }
        std::fprintf(stdout, "%s step done (%s)\n", tag, s.label);
        std::fflush(stdout);
    }

    void advance() {
        ++step;
        step_frame = 0;
        acted = false;
        key_up_done = false;
        tab_clicked = false;
        if (step >= count) {
            finished = true;
            std::fprintf(stdout, "%s ALL %d STEPS DONE\n", tag, count);
            std::fflush(stdout);
        }
    }
};

// ---------------------------------------------------------------------------
// Input replay / scripted verification (phase1 step9)
// ---------------------------------------------------------------------------
// A reconstructed key edge for the recorded input stream
// (`atframe <n> press <control> <player> <value>`): a down/up of a game
// control id (JS `sa.$h`, 1..14). The stream lists a control once per frame
// it is held, so a contiguous run of frames is one down..up edge; duplicate
// rows on the SAME frame are extra taps (the multi-tap `2key`/`3key` inputs
// that a single tap can never satisfy).
struct ReplayEdge {
    int frame = 0;
    int control = 0;   // game key id, or GLFW key code when `glfw`
    bool down = false;
    bool glfw = false;  // route through FightScreen::on_key (key-map test)
};

struct VerifyProbe {
    int frame = 0;
    const char* label = "";
    const char* expect = "";  // "<..." = expect NO move
    int window = 4;           // frames after `frame` to observe the move start
    bool substring = false;   // expect is a substring of the move name
};

// The `--verify-input` tape. Control ids are JS `sa.$h`
// (1=Up,3=Forward,5=Down,7=Back,9=Punch,10=Kick,11=Ranged,12=Magic,
// 13=RaidCharge,14=Super). The double-taps are two same-frame taps: JS
// `zl.Sgb` (L798) appends every keydown to the 2-slot `zg.sh`, so two taps
// that never get consumed by a lower-priority move combine into the `2key`
// move — exactly what the debug audit flagged as missing.
static const VerifyProbe kVerifyProbes[] = {
    {180, "Back Tap x2 (spawn gap 283)", "DashBackwards", 4, false},
    {300, "Forward Tap x2", "DoubleStepForward", 4, false},
    {420, "Punch Tap x2 + Forward Hold", "DoublePunch", 4, false},
    {520, "Forward Tap x1 (1key)", "StaffStepForward", 4, false},
    {550, "Forward Tap x1 (+30f)", "StaffStepForward", 4, false},
    {620, "K key -> Punch-key move", "ShortUpwardElbowStrike", 4, false},
    {700, "B key -> dropped (no move)", "<none>", 12, false},
};
constexpr int kVerifyProbeCount =
    static_cast<int>(sizeof(kVerifyProbes) / sizeof(kVerifyProbes[0]));

std::vector<ReplayEdge> build_verify_edges() {
    std::vector<ReplayEdge> e;
    auto down = [&](int f, int c) { e.push_back(ReplayEdge{f, c, true}); };
    auto up = [&](int f, int c) { e.push_back(ReplayEdge{f, c, false}); };
    // DashBackwards = Back Tap x2 (moves.xml L358527) FIRST, at the spawn gap
    // (dist 283): once the higher-priority `WallDashForward_50` (Back Tap x2
    // + a `Max=100` gap) is out of range.
    down(180, 7);
    down(180, 7);
    up(220, 7);
    // DoubleStepForward = Forward Tap x2 (moves.xml L499745).
    down(300, 3);
    down(300, 3);
    up(340, 3);
    // DoublePunch = Punch Tap x2 + Forward Hold (moves.xml L609376).
    down(400, 3);
    down(420, 9);
    down(420, 9);
    up(470, 3);
    // Single Forward taps: 1key StepForward; two lone taps 30 frames apart
    // are two single steps — never DoubleStepForward (the first tap is
    // consumed by the 1key move).
    down(520, 3);
    up(530, 3);
    down(550, 3);
    up(560, 3);
    // Key-map fixes: K (GLFW 75) must be Punch, not Super; the non-JS B
    // (GLFW 66) is dropped (no binding -> no move).
    e.push_back(ReplayEdge{620, 75, true, true});
    e.push_back(ReplayEdge{632, 75, false, true});
    e.push_back(ReplayEdge{700, 66, true, true});
    e.push_back(ReplayEdge{712, 66, false, true});
    std::stable_sort(e.begin(), e.end(),
                     [](const ReplayEdge& a, const ReplayEdge& b) { return a.frame < b.frame; });
    return e;
}

// Parses the recorded input stream into down/up edges.
std::vector<ReplayEdge> parse_replay_file(const std::string& path) {
    std::ifstream in(path);
    std::map<int, std::map<int, int>> by_frame;  // frame -> control -> count
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag, press;
        int frame = 0, control = 0, player = 0, value = 0;
        if (!(ss >> tag >> frame >> press >> control >> player >> value)) continue;
        if (tag != "atframe" || press != "press") continue;
        by_frame[frame][control] += 1;
    }
    std::set<int> controls;
    for (const auto& f : by_frame) {
        for (const auto& c : f.second) controls.insert(c.first);
    }
    std::vector<ReplayEdge> edges;
    for (const int c : controls) {
        bool prev_down = false;
        int prev_frame = -1000;
        for (const auto& f : by_frame) {
            const int frame = f.first;
            const auto it = f.second.find(c);
            const int count = it == f.second.end() ? 0 : it->second;
            const bool active = count > 0;
            const bool contiguous = frame == prev_frame + 1;
            if (active && (!prev_down || !contiguous)) {
                edges.push_back(ReplayEdge{frame, c, true});
                for (int x = 1; x < count; ++x) edges.push_back(ReplayEdge{frame, c, true});
            } else if (active && prev_down && contiguous && count > 1) {
                for (int x = 0; x < count - 1; ++x) edges.push_back(ReplayEdge{frame, c, true});
            } else if (!active && prev_down) {
                edges.push_back(ReplayEdge{frame, c, false});
            }
            prev_down = active;
            prev_frame = frame;
        }
        if (prev_down) edges.push_back(ReplayEdge{prev_frame + 1, c, false});
    }
    std::stable_sort(edges.begin(), edges.end(),
                     [](const ReplayEdge& a, const ReplayEdge& b) { return a.frame < b.frame; });
    return edges;
}

} // namespace

int main(int argc, char** argv) {
    std::string res_root = "reference/www/res";
    std::string save_path = "reference/saves/save.xml";
    int headless = 0;
    bool auto_click = false;
    bool headless_loop = false;
    bool ui_tour = false;
    bool fidelity_tour = false;
    bool replay_mode = false;
    bool verify_input = false;
    std::string replay_file = "reference/traces/recorded_inputs.txt";
    bool debug_ui = false;
    bool capture_fight = false;
    bool capture_idle_fight = false;  // --capture-idle-fight-at N: boot direct + no input, capture at fight frame N
    bool auto_attack = false;
    bool fight_mode = false;  // --fight: boot DIRECTLY into the dojo fight
    int capture_fight_frame = 300;  // fight frames after the Fight screen appears
    std::string capture_dir;  // when set, capture screens to this dir
    std::string dump_clip;    // --dump-clip <name>: dump one anim clip as JSON, exit
    int dump_pose_frames = 0;  // --dump-pose N: dump the first N fight frames (0 = off)
    // `--battle <name>` / `--zone <name>` (used with --fight): direct-boot a
    // named stages.xml battle resolved in a named zone — the verification
    // path for the stage-rule feeder (e.g. `Duel` in `ZONE_1`). Defaults to
    // the Training dojo battle with no zone (legacy first-match scan).
    std::string fight_battle;
    std::string fight_zone;

    // Positional args (res_root, save_path) are assigned by slot, not by
    // value: a user passing the default res_root explicitly used to collide
    // with the value-dependent check and overwrite res_root with the second
    // positional (breaking asset loading). Count the positionals instead.
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless" && i + 1 < argc) {
            headless = std::atoi(argv[++i]);
        } else if (arg == "--autoclick") {
            auto_click = true;
        } else if (arg == "--headless-loop") {
            headless_loop = true;
        } else if (arg == "--ui-tour") {
            ui_tour = true;
        } else if (arg == "--fidelity-tour") {
            fidelity_tour = true;
        } else if (arg == "--replay") {
            replay_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                replay_file = argv[++i];
            }
        } else if (arg == "--verify-input") {
            verify_input = true;
        } else if (arg == "--debug-ui") {
            debug_ui = true;
        } else if (arg == "--capture" && i + 1 < argc) {
            capture_dir = argv[++i];
        } else if (arg == "--capture-fight") {
            capture_fight = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                capture_fight_frame = std::atoi(argv[++i]);
            }
        } else if (arg == "--capture-idle-fight-at" && i + 1 < argc) {
            capture_idle_fight = true;
            capture_fight_frame = std::atoi(argv[++i]);
        } else if (arg == "--auto-attack") {
            auto_attack = true;
        } else if (arg == "--dump-clip" && i + 1 < argc) {
            dump_clip = argv[++i];
        } else if (arg == "--dump-pose") {
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                std::fprintf(stderr, "game: --dump-pose requires an explicit frame count N\n");
                return 1;
            }
            dump_pose_frames = std::atoi(argv[++i]);
            if (dump_pose_frames <= 0) {
                std::fprintf(stderr, "game: --dump-pose needs a positive frame count N\n");
                return 1;
            }
        } else if (arg == "--fight") {
            fight_mode = true;
        } else if (arg == "--battle" && i + 1 < argc) {
            fight_battle = argv[++i];
        } else if (arg == "--zone" && i + 1 < argc) {
            fight_zone = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (positional == 0) {
            res_root = arg;
            ++positional;
        } else if (positional == 1) {
            save_path = arg;
            ++positional;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    // Verify the save round-trip before opening the window (the same
    // SaveSystem the shell uses): load users_default -> bump money ->
    // save -> reload -> money persists. Uses a throwaway path so the real
    // save file starts from the template.
    if (!headless_loop) {
        std::string default_save = res_root + "/users_default.xml";
        if (!std::filesystem::exists(default_save)) {
            const std::string hashed = res_root + "/users_default.b7da2019.xml";
            if (std::filesystem::exists(hashed)) {
                default_save = hashed;
            } else {
                const std::string extracted = "reference/extracted/xml/res/users_default.xml";
                if (std::filesystem::exists(extracted)) {
                    default_save = extracted;
                }
            }
        }
        const std::string test_save = "reference/saves/roundtrip_test.xml";
        try {
            sf2::app::SaveSystem ss(test_save, default_save);
            sf2::app::WarriorSave w = ss.load();
            std::fprintf(stdout, "[save] round-trip: loaded default money=%d level=%d weapon=%s\n",
                         w.money, w.level, w.weapon.c_str());
            w.money += 250;
            w.level = 2;
            ss.save(w);
            sf2::app::WarriorSave reloaded = ss.load();
            const bool ok = reloaded.money == w.money && reloaded.level == w.level;
            std::fprintf(stdout, "[save] round-trip: after save reload money=%d level=%d -> %s\n",
                         reloaded.money, reloaded.level, ok ? "PASS" : "FAIL");
            if (!ok) {
                std::fprintf(stderr, "save round-trip FAILED\n");
                return 1;
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "save round-trip error: %s\n", e.what());
            return 1;
        }
    }

    sf2::app::App app;
    if (!app.init(res_root, save_path)) {
        std::fprintf(stderr, "game: app init failed\n");
        return 1;
    }
    std::fprintf(stdout, "[game] booted: window %dx%d, save '%s'\n", app.view_w(), app.view_h(),
                 save_path.c_str());

    if (!dump_clip.empty()) {
        // [trace, Phase 0] Clip dump: find the named clip in the loaded anim
        // archive and write it as 1/16 fixed-point ints (the source i16/16
        // format — multiplying the parsed floats back by 16 recovers the
        // exact ints) to reference/traces/native_clip_<name>.json.
        // The ints use JS Math.round parity (floor(x*16+0.5): half toward
        // +inf). No fight runs; exit 0 after the file is written.
        const auto& clips = app.fight_assets().clips;
        const auto it = clips.find(dump_clip);
        if (it == clips.end()) {
            std::fprintf(stderr, "game: clip '%s' not found in the anim archive (%zu clips)\n",
                         dump_clip.c_str(), clips.size());
            app.shutdown();
            return 1;
        }
        const sf2::data::anim_clip& clip = it->second;
        std::filesystem::create_directories("reference/traces");
        const std::string path = "reference/traces/native_clip_" + dump_clip + ".json";
        std::FILE* out = nullptr;
        if (fopen_s(&out, path.c_str(), "wb") != 0 || out == nullptr) {
            std::fprintf(stderr, "game: cannot open %s for writing\n", path.c_str());
            app.shutdown();
            return 1;
        }
        std::fprintf(out, "{\"t\":\"clip\",\"name\":\"%s\",\"frames\":%zu,\"bones\":%zu,\"data\":[",
                     clip.name.c_str(), clip.frames.size(), clip.bone_count());
        for (std::size_t fi = 0; fi < clip.frames.size(); ++fi) {
            std::fprintf(out, "%s[", fi == 0 ? "" : ",");
            const std::vector<sf2::data::anim_keyframe>& bones = clip.frames[fi].bones;
            for (std::size_t bi = 0; bi < bones.size(); ++bi) {
                const sf2::data::anim_keyframe& k = bones[bi];
                std::fprintf(out, "%s[%d,%d,%d]", bi == 0 ? "" : ",",
                             static_cast<int>(std::floor(k.x * 16.0f + 0.5f)),
                             static_cast<int>(std::floor(k.y * 16.0f + 0.5f)),
                             static_cast<int>(std::floor(k.z * 16.0f + 0.5f)));
            }
            std::fprintf(out, "]");
        }
        std::fprintf(out, "]}\n");
        std::fclose(out);
        std::fprintf(stdout, "[dump] clip '%s' (%zu frames, %zu bones) -> %s\n",
                     dump_clip.c_str(), clip.frames.size(), clip.bone_count(), path.c_str());
        app.shutdown();
        return 0;
    }

    if (headless_loop) {
        // The scripted playable loop: run the driver until all steps land
        // (with a frame guard), then dump the final save state and exit.
        HeadlessLoopDriver driver;
        const std::string default_save = res_root + "/users_default.xml";
        const std::string hashed_save = res_root + "/users_default.b7da2019.xml";
        const std::string extracted_save = "reference/extracted/xml/res/users_default.xml";
        std::string def = std::filesystem::exists(default_save)   ? default_save
                          : std::filesystem::exists(hashed_save) ? hashed_save
                                                                 : extracted_save;
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();
            std::fprintf(stdout, "[loop] start save: money=%d exp=%d level=%d weapon=%s items=%zu\n",
                         w.money, w.experience, w.level, w.weapon.c_str(), w.items.size());
            std::fprintf(stdout, "[loop] start move list (%zu):\n", w.weapon == "Fists" ? 0 : 0);
            for (const auto& m : w.items) {
                std::fprintf(stdout, "  %s x%d%s\n", m.name.c_str(), m.count,
                             m.equipped ? " [EQ]" : "");
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[loop] save read failed: %s\n", e.what());
        }
        // The driver needs the FightScreen's move-list size to log the
        // before/after equip diff.
        app.set_auto_attack(true);
        // Uncapped deterministic frames (the loop runs at fixed 1/60 steps,
        // but headless_frames_ = 0 would use real dt and the loop would run
        // in real time). Force the headless stepping.
        app.set_headless_frames(1);
        while (!driver.finished && driver.guard < 40000) {
            glfwPollEvents();
            driver.frame_tick(app);
            app.run_one_frame();
            ++driver.guard;
        }
        if (!driver.finished) {
            std::fprintf(stderr, "[loop] did not finish after %d frames (step %d) — see the log\n",
                         driver.guard, driver.step);
        }
        // Final state + the save/load-after-loop verification.
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();
            std::fprintf(stdout, "[loop] END save: money=%d exp=%d level=%d weapon=%s items=%zu\n",
                         w.money, w.experience, w.level, w.weapon.c_str(), w.items.size());
            for (const auto& m : w.items) {
                std::fprintf(stdout, "  %s x%d%s\n", m.name.c_str(), m.count,
                             m.equipped ? " [EQ]" : "");
            }
            // Reload once more — the round-trip proof (money/items/equipment
            // persist across a fresh SaveSystem).
            sf2::app::SaveSystem ss2(save_path, def);
            sf2::app::WarriorSave w2 = ss2.load();
            const bool persist = w2.money == w.money && w2.items.size() == w.items.size() &&
                                 w2.weapon == w.weapon;
            std::fprintf(stdout, "[loop] save/load after loop: money=%d items=%zu weapon=%s -> %s\n",
                         w2.money, w2.items.size(), w2.weapon.c_str(),
                         persist ? "PASS" : "FAIL");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[loop] end save read failed: %s\n", e.what());
        }
        if (!driver.finished) {
            return 1;
        }
    } else if (ui_tour) {
        // UI screenshot tour: visit each screen, capture ui/port_*.png.
        TourDriver driver;
        driver.steps = kUiTourSteps;
        driver.count = kUiTourStepCount;
        driver.out_dir = "reference/traces/ui";
        driver.tag = "[tour]";
        app.set_auto_attack(false);
        app.set_headless_frames(1);
        if (debug_ui) app.set_debug_ui(true);
        std::filesystem::create_directories("reference/traces/ui");
        while (!driver.finished && driver.guard < 60000) {
            glfwPollEvents();
            driver.frame_tick(app);
            app.run_one_frame();
            ++driver.guard;
        }
        if (!driver.finished) {
            std::fprintf(stderr, "[tour] did not finish after %d frames (step %d)\n",
                         driver.guard, driver.step + 1);
            return 1;
        }
        app.shutdown();
        return 0;
    } else if (fidelity_tour) {
        // [Phase 0 harness, phase1 step9] Fidelity tour: mirror the oracle's
        // reference/traces/oracle_matrix/<state>.png state list into
        // reference/traces/port_matrix/<state>.png (App::capture_png).
        // The boot overlay (splash/loader) draws ONLY in non-headless mode
        // (App::render_frame), so capture those two first with the overlay
        // live, then run the deterministic headless step tour.
        std::filesystem::create_directories("reference/traces/port_matrix");
        // Fresh-profile tutorial (JS StoryTutorialWelcome): the Dojo plays the
        // Sensei beats then the Punchbag training fight before the clean hub.
        app.set_fresh_tutorial(true);
        // splash: right after boot (Preloader counts down from 75).
        app.set_headless_frames(0);
        app.run_one_frame();
        app.capture_png("reference/traces/port_matrix/splash.png");
        // loader: advance the fixed-step countdown into the loader window
        // (<= kBootLoaderFrames = 30), then render one non-headless frame so
        // the overlay draws and capture it.
        app.set_headless_frames(1);
        for (int i = 0; i < 46; ++i) {
            app.run_one_frame();
        }
        app.set_headless_frames(0);
        app.run_one_frame();
        app.capture_png("reference/traces/port_matrix/loader.png");
        app.set_headless_frames(1);
        TourDriver driver;
        driver.steps = kFidelitySteps;
        driver.count = kFidelityStepCount;
        driver.out_dir = "reference/traces/port_matrix";
        driver.tag = "[fidelity]";
        app.set_auto_attack(false);
        while (!driver.finished && driver.guard < 80000) {
            glfwPollEvents();
            driver.frame_tick(app);
            app.run_one_frame();
            ++driver.guard;
        }
        if (!driver.finished) {
            std::fprintf(stderr, "[fidelity] did not finish after %d frames (step %d)\n",
                         driver.guard, driver.step + 1);
            app.shutdown();
            return 1;
        }
        app.shutdown();
        return 0;
    } else if (replay_mode || verify_input) {
        // ---- Input replay / scripted verification (phase1 step9) ----------
        // Boots the direct dojo fight (same as --fight) and feeds a game
        // control stream into the fight input. `--replay [file]` reads
        // `reference/traces/recorded_inputs.txt` (control = the JS `sa.$h`
        // key id); `--verify-input` runs the embedded tape that exercises the
        // double-tap / window / key-map fixes.
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name = "Training";
            pb.zone.clear();
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned.clear();
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));

        // Unit-level buffer check (JS `zl.Sgb`/`ia` L798): 2-slot Tap
        // sequence, no same-key replacement, holds rebuilt from the down
        // keys, and the 15-frame tap window (present through +14, gone +15).
        if (verify_input) {
            sf2::scene::Fighter f;
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            const bool two = f.buffered_tap_count() == 2;
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            const bool cap = f.buffered_tap_count() == 2;  // 3rd evicts the oldest
            const bool hold = f.buffered_hold_count() == 1;
            for (int i = 0; i < 15; ++i) f.age_keys();
            const bool alive = f.buffered_tap_count() == 2;  // still there at +14
            f.age_keys();
            const bool gone = f.buffered_tap_count() == 0;  // cleared at +15
            std::fprintf(stdout,
                         "[verify] buffer: 2-tap=%d cap2=%d hold=%d alive@+14=%d "
                         "empty@+15=%d -> %s\n",
                         two, cap, hold, alive, gone,
                         (two && cap && hold && alive && gone) ? "PASS" : "FAIL");
            std::fflush(stdout);
        }

        const std::vector<ReplayEdge> edges =
            verify_input ? build_verify_edges() : parse_replay_file(replay_file);
        std::fprintf(stdout, "[replay] %s: %zu edges\n",
                     verify_input ? "verify tape" : replay_file.c_str(), edges.size());
        std::fflush(stdout);

        if (verify_input) {
            // Key-map assertion (JS `sc.OD` `Af.oUa` L2472): K->Punch(9) not
            // Super(14), Q->Super(14), P->Magic(12), and the non-JS B is
            // unbound (0).
            const bool map_ok =
                FightScreen::key_type_for_glfw(75) == 9 &&
                FightScreen::key_type_for_glfw(81) == 14 &&
                FightScreen::key_type_for_glfw(80) == 12 &&
                FightScreen::key_type_for_glfw(79) == 11 &&
                FightScreen::key_type_for_glfw(74) == 13 &&
                FightScreen::key_type_for_glfw(76) == 10 &&
                FightScreen::key_type_for_glfw(66) == 0 &&
                FightScreen::key_type_for_glfw(65) == 7 &&
                FightScreen::key_type_for_glfw(68) == 3 &&
                FightScreen::key_type_for_glfw(87) == 1 &&
                FightScreen::key_type_for_glfw(83) == 5;
            std::fprintf(stdout,
                         "[verify] key map: K75->%d Q81->%d P80->%d O79->%d J74->%d "
                         "L76->%d B66->%d A65->%d D68->%d W87->%d S83->%d -> %s\n",
                         FightScreen::key_type_for_glfw(75),
                         FightScreen::key_type_for_glfw(81),
                         FightScreen::key_type_for_glfw(80),
                         FightScreen::key_type_for_glfw(79),
                         FightScreen::key_type_for_glfw(74),
                         FightScreen::key_type_for_glfw(76),
                         FightScreen::key_type_for_glfw(66),
                         FightScreen::key_type_for_glfw(65),
                         FightScreen::key_type_for_glfw(68),
                         FightScreen::key_type_for_glfw(87),
                         FightScreen::key_type_for_glfw(83),
                         map_ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        }

        app.set_headless_frames(1);
        bool fight_seen = false;
        int fight_frames = 0;
        std::size_t ei = 0;
        int last_started = 0;
        const VerifyProbe* pending = nullptr;
        int guard = 0;
        const int last_frame = edges.empty() ? 0 : edges.back().frame;
        while (guard < 6000) {
            glfwPollEvents();
            sf2::app::FightScreen* fs =
                fight_seen ? static_cast<sf2::app::FightScreen*>(app.screens().top()) : nullptr;
            if (fight_seen && fs != nullptr) {
                while (ei < edges.size() && edges[ei].frame <= fight_frames) {
                    if (edges[ei].glfw) {
                        fs->on_key(edges[ei].control, edges[ei].down);
                    } else {
                        fs->inject_game_key(edges[ei].control, edges[ei].down);
                    }
                    ++ei;
                }
            }
            if (fight_seen && verify_input) {
                for (int p = 0; p < kVerifyProbeCount; ++p) {
                    if (kVerifyProbes[p].frame == fight_frames) pending = &kVerifyProbes[p];
                }
            }
            app.run_one_frame();
            ++guard;
            if (!fight_seen && app.screens().current_id() == kScreenFight) {
                fight_seen = true;
                fight_frames = 0;
                std::fprintf(stdout, "[replay] fight screen up\n");
                std::fflush(stdout);
            } else if (fight_seen) {
                ++fight_frames;
                fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
                const int started = fs != nullptr ? fs->player_moves_started() : 0;
                if (pending != nullptr && started > last_started) {
                    const std::string dec = fs->player_last_decision();
                    const bool pass =
                        pending->substring
                            ? dec.find(pending->expect) != std::string::npos
                            : dec == std::string("input:") + pending->expect;
                    std::fprintf(stdout, "[verify] %s -> %s (F%d) expect=%s %s\n",
                                 pending->label, dec.c_str(), fight_frames, pending->expect,
                                 pass ? "PASS" : "FAIL");
                    std::fflush(stdout);
                    pending = nullptr;
                } else if (pending != nullptr && pending->window > 0 &&
                           fight_frames > pending->frame + pending->window) {
                    const bool expect_none = pending->expect[0] == '<';
                    std::fprintf(stdout, "[verify] %s -> (no move) (F%d) expect=%s %s\n",
                                 pending->label, fight_frames, pending->expect,
                                 expect_none ? "PASS" : "FAIL");
                    std::fflush(stdout);
                    pending = nullptr;
                }
                last_started = started;
            }
            if (fight_seen && fight_frames > last_frame + 40) break;
        }
        // Escape closes the Settings dialog (JS `od.aa` L1895 `Db(156)`
        // applied to `un extends od` L1916). Push Settings over the fight,
        // inject Escape, confirm it popped.
        {
            app.screens().push(make_screen(app.screens(), kScreenSettings));
            const int before = app.screens().current_id();
            app.inject_key(256, true);
            const int after = app.screens().current_id();
            const bool ok = before == kScreenSettings && after != kScreenSettings;
            std::fprintf(stdout, "[verify] Escape closes Settings: id %d -> %d %s\n", before,
                         after, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        }
        app.shutdown();
        return 0;
    } else if (fight_mode) {
        // [Phase 4c] --fight: boot DIRECTLY into the dojo fight, bypassing
        // the menu/map (which are flat-rectangle placeholders). The user is
        // in the dojo with the player keyboard-controllable (manual input
        // path: on_key -> player_input -> try_select_move) and the enemy on
        // AI. The battle is the default Training fight (dojo, player Fists
        // vs enemy Fists/AI) — the same one the map's Training node starts.
        //   game --fight                 windowed, keyboard-controlled
        //   game --fight --headless N    run N frames then exit (verify)
        // The fight push mirrors the MapScreen node click: carry the battle
        // (name/location/reward) into pending_battle, then push
        // kScreenFight. The player's owned list is EMPTY on purpose: the
        // Fists fallback in FightController::make_fighter builds the player
        // move list from the "Fists" TacticWeapon, so the direct boot is
        // ALWAYS the fists fight (player Fists vs enemy Fists/AI) regardless
        // of the user's save state (a save with WEAPON_KNIVES equipped would
        // otherwise pull the knives moves into the player's list).
        if (auto_attack) {
            app.set_auto_attack(true);
        }
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name = fight_battle.empty() ? "Training" : fight_battle;
            pb.zone = fight_zone;
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned.clear();
            std::fprintf(stdout, "[fight] direct boot: battle=%s zone=%s location=%s owned=%zu\n",
                         pb.battle_name.c_str(), pb.zone.c_str(), pb.location.c_str(),
                         pb.owned.size());
            std::fflush(stdout);
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        if (dump_pose_frames > 0) {
            // [trace, Phase 0] Arm the FightController's per-frame pose dump
            // (reference/traces/native_pose.jsonl). Pure trace — the fight
            // simulation is unaffected.
            std::filesystem::create_directories("reference/traces");
            if (app.screens().top() != nullptr) {
                static_cast<sf2::app::FightScreen*>(app.screens().top())
                    ->enable_pose_dump("reference/traces/native_pose.jsonl", dump_pose_frames);
            }
            std::fprintf(stdout,
                         "[dump] pose trace armed: first %d fight frames -> "
                         "reference/traces/native_pose.jsonl\n",
                         dump_pose_frames);
            std::fflush(stdout);
        }
        if (headless > 0) {
            // Deterministic headless verification: force one fixed step per
            // frame (like the other drivers) so the fight advances frame-
            // for-frame. The fight starts in phase 1 (the start-stance
            // intro, 133 fight frames); phase 2 (live fighting) begins at
            // fight frame 133+. Inject a Punch (Space) at fight frame 170
            // so the player's manual input path starts a punch and the log
            // proves the keyboard control; capture at fight frame 400 (the
            // task's mid-fight snapshot).
            app.set_headless_frames(1);
            int guard = 0;
            bool fight_seen = false;
            int fight_frames = 0;
            bool punch_sent = false;
            while (guard < headless) {
                glfwPollEvents();
                if (fight_seen && !punch_sent && fight_frames >= 170) {
                    app.inject_key(32, true);
                    app.inject_key(32, false);
                    punch_sent = true;
                    std::fprintf(stdout, "[fight] injected Punch at fight frame %d\n",
                                 fight_frames);
                    std::fflush(stdout);
                }
                app.run_one_frame();
                ++guard;
                if (!fight_seen && app.screens().current_id() == kScreenFight) {
                    fight_seen = true;
                    fight_frames = 0;
                } else if (fight_seen) {
                    ++fight_frames;
                }
            }
            std::filesystem::create_directories("reference/extracted/scene");
            app.capture_png("reference/extracted/scene/direct_fight.png");
            std::fprintf(stdout,
                         "[fight] captured reference/extracted/scene/direct_fight.png "
                         "(fight frame ~%d, guard %d)\n",
                         fight_frames, guard);
            if (app.screens().top() != nullptr) {
                static_cast<sf2::app::FightScreen*>(app.screens().top())->verify_fight();
            }
        } else {
            // Windowed: run until the window closes (the user plays).
            app.run(0, false);
        }
    } else if (capture_idle_fight) {
        // [Phase 4d] Boot DIRECTLY into the dojo fight with NO input and NO
        // auto-attack, run to fight frame `capture_fight_frame`, then capture
        // a PNG. The enemy AI still runs, but the deterministic seed makes
        // the run reproducible; the player stays in its stance idle (no key
        // injected). Used for the pixel-diff vs the oracle.
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name = "Training";
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned.clear();
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        app.set_headless_frames(1);  // uncapped deterministic stepping
        int guard = 0;
        bool fight_seen = false;
        int fight_frames = 0;
        while (guard < 20000) {
            glfwPollEvents();
            app.run_one_frame();
            ++guard;
            if (!fight_seen && app.screens().current_id() == kScreenFight) {
                fight_seen = true;
                fight_frames = 0;
            } else if (fight_seen) {
                ++fight_frames;
                if (fight_frames >= capture_fight_frame) {
                    break;
                }
            }
        }
        std::filesystem::create_directories("reference/extracted/scene");
        const std::string path = "reference/extracted/scene/diff_native.png";
        app.capture_png(path);
        std::fprintf(stdout, "[game] captured %s (fight frame ~%d, guard %d)\n", path.c_str(),
                     fight_frames, guard);
        if (app.screens().top() != nullptr) {
            static_cast<sf2::app::FightScreen*>(app.screens().top())->verify_fight();
        }
    } else if (capture_fight) {
        // [FIX Phase 4a/4b verification] Navigate menu -> map -> Training
        // fight and capture the fight at a fixed fight frame (the intro + a
        // few phase-2 frames). The auto-click drives the menu/map; then the
        // capture waits until the Fight screen is up and
        // `capture_fight_frame` frames have elapsed.
        // [FIX Phase 4b] The capture runs the MANUAL (playable) path: no
        // auto-attack, so the fighters stay at their spawn stances (the
        // oracle's silhouettes). `--auto-attack` re-enables the demo
        // auto-attack for the old style captures.
        if (auto_attack) {
            app.set_auto_attack(true);
        }
        app.set_headless_frames(1);  // uncapped deterministic stepping
        int guard = 0;
        bool fight_seen = false;
        int fight_frames = 0;
        bool punch_sent = false;
        while (guard < 20000) {
            glfwPollEvents();
            if (!fight_seen) {
                // Auto-click: menu FIGHT at frame 30, Training node at 60.
                if (guard == 30) {
                    app.inject_click(app.view_w() * 0.28, app.view_h() * 0.72);
                } else if (guard == 60 && app.screens().current_id() == kScreenMap) {
                    app.inject_click(app.view_w() / 2.0 + 158.0, app.view_h() / 2.0 - 145.0);
                }
            } else if (!punch_sent && fight_frames >= 170) {
                // [FIX Phase 4b control verification] Phase 2 is live at
                // fight frame 133+; inject a Punch (Space) so the player's
                // manual input path (on_key -> player_input -> try_select_move)
                // starts a punch and the log proves it.
                app.inject_key(32, true);
                app.inject_key(32, false);
                punch_sent = true;
                std::fprintf(stdout, "[game] injected Punch at fight frame %d\n", fight_frames);
                std::fflush(stdout);
            }
            app.run_one_frame();
            ++guard;
            if (!fight_seen && app.screens().current_id() == kScreenFight) {
                fight_seen = true;
                fight_frames = 0;
            } else if (fight_seen) {
                ++fight_frames;
                if (fight_frames >= capture_fight_frame) {
                    break;
                }
            }
        }
        std::filesystem::create_directories(capture_dir);
        const std::string path = capture_dir + "/fix_dojo.png";
        app.capture_png(path);
        std::fprintf(stdout, "[game] captured %s (fight frame ~%d, guard %d)\n", path.c_str(),
                     fight_frames, guard);
        // [FIX Phase 4a verification] The bone-sample + bbox dump.
        if (app.screens().top() != nullptr) {
            static_cast<sf2::app::FightScreen*>(app.screens().top())->verify_fight();
        }
    } else if (!capture_dir.empty()) {
        std::filesystem::create_directories(capture_dir);
        // Run long enough for the auto-click flow (menu -> map), then
        // capture the current frame. Without --autoclick this captures the
        // menu; with it, the map.
        app.run(headless > 0 ? headless : 90, auto_click);
        app.capture_png(capture_dir + "/screen.png");
        std::fprintf(stdout, "[game] captured %s/screen.png\n", capture_dir.c_str());
        } else {
        app.run(headless, auto_click);
    }
    app.shutdown();
    std::fprintf(stdout, "[game] shutdown\n");
    return 0;
}
