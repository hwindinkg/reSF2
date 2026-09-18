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

#define NOMINMAX  // before any <windows.h> pull-in (glfw3native.h includes it)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <GLFW/glfw3.h>

#include "app/app.hpp"
#include "app/quest_engine.hpp"
#include "app/save_system.hpp"
#include "app/screens.hpp"
#include "scene/fighter.hpp"
#include "scene/renderer.hpp"

namespace {

using namespace sf2::app;  // kScreen* ids + the App/SaveSystem types

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [res_root] [save_path] [--headless N] [--autoclick] [--headless-loop]\n"
                  "                  [--fight] [--battle <name>] [--zone <name>]\n"
                  "                  [--dump-pose N] [--dump-clip <name>]\n"
                  "                  [--ui-tour] [--fidelity-tour] [--quest-verify]\n"
                  "                  [--dialog-verify] [--replay [file]] [--verify-input]\n"
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
    // Battle-start step: click the Map's `Rr` FIGHT button (`tj`, JS L2099)
    // instead of a literal coordinate. JS starts a fight ONLY on that button
    // (a node tap just re-targets the panel), so the driver resolves the
    // button centre at click time (`MapScreen::fight_button_center`).
    bool map_fight = false;
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
    {0.0f, 0.0f, "map->BOSS_LYNX fight", kScreenMap, 0, kScreenFight, 0,
     "loop_fight_fists.png", 0.0f, 0.0f, true},
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
    {0.0f, 0.0f, "map->BOSS_LYNX fight (knives)", kScreenMap, 0, kScreenFight, 0,
     "loop_fight.png", 0.0f, 0.0f, true},
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
                float ccx = s.x, ccy = s.y;
                if (s.map_fight && cur == kScreenMap && top != nullptr) {
                    static_cast<sf2::app::MapScreen*>(top)->fight_button_center(ccx, ccy);
                }
                std::fprintf(stdout, "[loop] step %d/%d %s -> click (%.0f, %.0f)\n", step + 1,
                             kLoopStepCount, s.label, ccx, ccy);
                std::fflush(stdout);
                app.inject_click(ccx, ccy);
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
                // D2: a shop step must NOT print a bare "done" for a failed
                // purchase — report the REAL save outcome of the click.
                if (step == 5 || step == 6) {
                    bool owns = false;
                    std::string weapon;
                    try {
                        const sf2::app::WarriorSave w = app.save().load();
                        weapon = w.weapon;
                        for (const auto& mi : w.items) {
                            if (mi.name == "WEAPON_KNIVES") owns = true;
                        }
                    } catch (const std::exception&) {
                    }
                    std::fprintf(stdout, "[loop] step %d/%d %s -> owns_knives=%d weapon=%s\n",
                                 step + 1, kLoopStepCount, s.label, owns ? 1 : 0,
                                 weapon.c_str());
                    std::fflush(stdout);
                }
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
    // [fidelity fight-frame gate] Optional capture frame for fight states:
    // while the Fight screen is current, hold until the FightController's
    // frame counter (JS `ca.frame`) reaches this port frame, then capture.
    // -1 = use the generic hold_frames timing. The fidelity tour's targets are
    // the oracle `fight.frame` values mapped to the port's phase-local frame.
    int fight_frame = -1;
    // Battle-start step: click the Map's `Rr` FIGHT button (`tj`, JS L2099)
    // resolved at click time (`MapScreen::fight_button_center`) instead of a
    // literal coordinate. JS starts a fight ONLY on that button.
    bool map_fight = false;
    // APPENDED LAST so the existing positional initializers above keep their
    // meaning. The frozen `jk` pose for the roster hook: 3/4 = the `act_boss`
    // resting selection, 1 = a mid horizontal scroll-in (`act_boss_scroll`).
    int boss_state = 3;
    // [fidelity VS-intro gate] The `ik` overlay (screens.cpp kVsTotal, JS
    // `ik.yY` L2071 = 3.4 s) covers the fight scene while the native sim runs
    // underneath, so a fixed frame no longer identifies a visible fight state.
    // 0 = none; 1 = capture inside the composed hold (the `ik` roster, the
    // oracle `fight_intro`); 2 = wait for the overlay to end, then honour
    // `hold_frames` (the visible-fight captures: stance/block).
    int vs_wait = 0;
    // [fidelity tutorial-beat gate] The `StoryTutorialWelcome` chain is
    // serialized by its two lesson actions (`Do`/`Eo`, quest_engine
    // `tutorial_gate_beat`), so each tutorial capture waits on the app's OWN
    // beat instead of a frame count: 0 = none, 1 = the move lesson bar (the
    // oracle `tut_fight_stance`), 2 = the punchbag lesson bar (the oracle
    // `tut_fight_phase2`), 3 = the `Regular characterSensei` training modal is
    // up (the oracle `tut_block` / `dojo_sensei`).
    int wait_gate = 0;
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
    // 9: Map -> ZONE_1 boss fight (BOSS_LYNX, JS `Rr` FIGHT button). The map
    //    opens on the save's CurrentZone (ZONE_1) and has no zone-tab strip;
    //    the node tap only re-targets the panel, so the tour clicks the
    //    FIGHT button centre (resolved at click time). Hold 250 for the HUD.
    {0.0f, 0.0f, "map->BOSS_LYNX fight", 5, 10, 6, 250, "port_fight.png", 0, false, 0.0f,
     0.0f, -1, false, -1, true},
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
    // 14: Dojo -> Settings (nav row 4 @184,547). D13: nav #5 opens the `un`
    //     dialog OVER the Dojo (`Vfb` L1981 -> `Xc.Shb` L931) — it does NOT
    //     navigate, so the step expects to STAY on the Dojo (id 3).
    {184.0f, 547.0f, "dojo->settings", 3, 10, 3, 0, "port_settings.png"},
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
// [fidelity VS-intro gate] The `ik` overlay's COMPOSED-hold lower bound: the
// JS composes the roster at `kd7` (names in, ~1.7 s; `kVsNameT` 0.45 s in the
// port's stage fold) and starts fading at `kd9` (`kVsFadeT` 2.90 s). 1.0 s sits
// inside that window, so the `fight_intro` capture is the full roster and can
// never catch the slide-in or the fade-out.
static constexpr float kVsComposedT = 1.0f;

static const UiTourStep kFidelitySteps[] = {
    // --- Fresh-profile tutorial (JS StoryTutorialWelcome) --------------------
    // Approved `fresh/tutorial-from-0` boot: the Dojo plays the Sensei beats
    // (App::fresh_tutorial), then the `Punchbag|Bosses|1` training fight, then
    // the clean hub. Beats mirror tutorial_quests.xml StoryTutorialWelcome
    // (tutorial_move -> tutorial_punchbag -> the characterSensei Regular
    // dialog + dlgStoryBtnFight).
    // 0: beat 0 notification ("tutorial_move") -> tut_fight_stance.
    //    The beat notifications carry `ReadTime="5.0"` (tutorial_quests.xml
    //    L26/L31), so the capture must land inside that window — the settle
    //    below does. The `StoryTutorialMove` lesson then parks the chain
    //    (`wait_gate=1`, quest_engine `tutorial_gate_beat`), so the `Regular`
    //    modal is NOT queued yet and the bar is the only dialog: the oracle
    //    frame (`oracle_tutorial_stance.png` = the move banner over the dojo,
    //    no modal) is what the port now shows.
    {0.0f, 0.0f, "tut stance (move notification)", 3, 60, -1, 30, "tut_fight_stance.png", 0, true,
     0.0f, 0.0f, -1, false, -1, false, 3, 0, 1},
    // 1: beat 1 ("tutorial_punchbag") — the chain resumes from the move
    //    lesson after its `TutorialStepTimeout` (the oracle's own driver waited
    //    the same 15 s: `oracle_tutorial_move.png`). Wait on the beat, never on
    //    a frame count.
    {0.0f, 0.0f, "tut phase2 (punchbag notification)", 3, 10, -1, 30, "tut_fight_phase2.png", 0,
     true, 0.0f, 0.0f, -1, false, -1, false, 3, 0, 2},
    // 2: the `Regular` sensei training-fight dialog, queued once the punchbag
    //    lesson completes. `tut_block`'s oracle frame
    //    (`oracle_matrix/tut_block.png`, sourced from
    //    oracle_tutorial_punchbag) IS that modal (СЭНСЭЙ portrait + В БОЙ).
    {0.0f, 0.0f, "tut block (sensei training dialog)", 3, 10, -1, 50, "tut_block.png", 0, true,
     0.0f, 0.0f, -1, false, -1, false, 3, 0, 3},
    // 3: the same training dialog (`oracle_tutorial_modal`) -> dojo_sensei.
    {0.0f, 0.0f, "dojo sensei (training dialog)", 3, 10, -1, 30, "dojo_sensei.png", 0, true, 0.0f,
     0.0f, -1, false, -1, false, 3, 0, 3},
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
    // act_boss_scroll: the SAME `jk` machine frozen mid scroll-in (state 1) —
    // `scrollX = Pp + (512 - Jq[index].node.ya - Pp)*ed(index==last?1:2)`
    // (L2064). This is the "add a mid-scroll capture for state 1" beat.
    {0.0f, 0.0f, "act_boss_scroll (jk state 1)", 5, 10, -1, 30, "act_boss_scroll.png", 0, true,
     0.0f, 0.0f, -1, true, -1, false, 1},
    // --- Fight (passive player so round 0 survives, like the oracle) --------
    // [fidelity fight-frame alignment] The oracle `fight_*` captures are
    // pinned to `fight.frame` (each shot is row-adjacent to its `oracle`
    // record in the oracle per-frame trace — `console.big.bak.log` for the
    // oracle_matrix set): fight_intro f=0 (phase 0, VS), fight_stance f=251
    // (phase 1), fight_block f=347 (phase 2), pause f=588, fight_attack f=591,
    // fight_hit f=835. The port's phase 1 starts at frame 0 (the oracle's
    // phase 1 at f=131) and its phase 2 at frame 133 (the oracle's at f=334),
    // so each state's port frame = oracle phase-local frame mapped onto the
    // port's phase-local frame:
    //   stance 251-131=120 ; block 347-334+133=146 ; pause 588-334+133=387 ;
    //   attack 591-334+133=390 ; hit 835-334+133=634.
    // The port's BOSS_LYNX round 0 now survives the full 99 s timer (the
    // 8cb8a65e JS-exact fight), so the earlier ~F181 K.O. clamp is gone and
    // every mapped phase-2 frame is reachable. The oracle's ONLY fight input
    // is the punch (control 9 = K/Space): press at oracle `fight.frame` 561
    // (phase-2 local 227), release at 566 — `reference/traces/_residual_fight.log`
    // [INPUT-REC] {"f":561,"i":1,"m":"N0a","c":9} / {"f":566,...,"O0a"}; the
    // tour's `tap 1135 540` punch button. Phase-2 local 227 -> port frame
    // 133+227=360, so the punch is pressed there and the attack/hit captures
    // take the oracle's own offsets from the press (attack 257=press+30 ->
    // 390, hit 501 -> 634). `auto_attack=0` keeps the player otherwise idle,
    // like the oracle's passive opponent.
    // [VS-intro gate] `fight_intro` is the `ik` roster itself (the oracle's own
    // shot: `MANIFEST.md` — "map -> В БОЙ -> VS (ТЕНЬ / ШИН)"), so it captures
    // inside the composed hold (`vs_wait=1`). `fight_stance`/`fight_block` are
    // VISIBLE-fight frames and wait for the overlay to end (`vs_wait=2`) — the
    // pre-3.4 s driver's F120/F146 landed under the overlay once `ik.yY` grew.
    {0.0f, 0.0f, "map->fight", 5, 10, 6, 40, "fight_intro.png", 0, false, 0.0f, 0.0f, 0, false,
     -1, true, 3, 1},
    {0.0f, 0.0f, "fight stance", 6, 0, -1, 0, "fight_stance.png", 0, true, 0.0f, 0.0f, -1, false,
     -1, false, 3, 2},
    {0.0f, 0.0f, "fight block", 6, 0, -1, 35, "fight_block.png", 0, true, 0.0f, 0.0f, -1, false,
     -1, false, 3, 2},
    // The oracle's single fight input: punch (control 9 = K/Space) pressed at
    // phase-2 local 227 (oracle f=561 -> port frame 360).
    {0.0f, 0.0f, "fight punch (oracle control 9)", 6, 0, -1, 0, nullptr, 32, true, 0.0f, 0.0f,
     -1, false, 360},
    // Pause at the oracle pause shot's frame f=588 (phase-2 local 254 -> port
    // frame 387), so the frozen backdrop matches the oracle `pause.png`
    // backdrop (opened by the HUD pause disc 627,125; native Esc alias). The
    // pause/resume freeze does not skip the move clock, so the attack capture
    // below still lands at move-frame 30 as in the oracle.
    {0.0f, 0.0f, "pause (Esc)", 6, 0, -1, 2, "pause.png", 256, true, 0.0f, 0.0f, -1, false, 387},
    {0.0f, 0.0f, "resume (Esc)", 6, 0, -1, 2, nullptr, 256, false},
    {0.0f, 0.0f, "fight attack", 6, 0, -1, 0, "fight_attack.png", 0, true, 0.0f, 0.0f, -1, false,
     390},
    {0.0f, 0.0f, "fight hit", 6, 0, -1, 0, "fight_hit.png", 0, true, 0.0f, 0.0f, -1, false,
     634},
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
    // D13: nav #5 opens the `un` Settings dialog OVER the Dojo (`Vfb` L1981 ->
    // `Xc.Shb` L931) — it does NOT navigate, so the step STAYS on the Dojo
    // (id 3); the old `expect_screen=11` was unreachable and only worked
    // because the hold branch ignored it.
    {184.0f, 547.0f, "dojo->settings", 3, 10, 3, 60, "settings.png", 0, false},
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
    int applied_boss_state = -1;    // last per-step frozen `jk` state applied
    // The step_frame at which a `wait_gate` beat first appeared (so the
    // tutorial settle counts from the beat, not from the stale step delay).
    int gate_open_frame = -1;

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
        // Per-step boss-roster capture hook (fidelity `act_boss`). The frozen
        // pose (`boss_state`) must be re-applied when it CHANGES even though
        // the roster flag stays set: `act_boss_scroll` follows `act_boss`
        // (`force_boss_roster` already 1), and gating on the flag alone left
        // both captures on the same state — the state-1 mid-scroll capture was
        // byte-identical to the state-3/4 resting pose.
        if ((s.force_boss_roster ? 1 : 0) != applied_force_roster ||
            s.boss_state != applied_boss_state) {
            sf2::app::set_force_boss_roster(s.force_boss_roster);
            sf2::app::set_force_boss_state(s.boss_state);
            applied_force_roster = s.force_boss_roster ? 1 : 0;
            applied_boss_state = s.boss_state;
            std::fprintf(stdout, "%s step %d/%d force_boss_roster=%d boss_state=%d\n", tag,
                         step + 1, count, applied_force_roster, applied_boss_state);
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

        // [fidelity fight-frame gate] Hold a fight capture until the
        // FightController's frame counter (JS `ca.frame`) reaches the step's
        // port frame, then capture. The oracle fight captures are pinned to
        // `fight.frame`: stance f=251 (phase 1 +120), block f=347 (phase 2
        // +13), pause f=588 (+254), attack f=591 (+257), hit f=835 (+501).
        // The port's phase 1 starts at frame 0 (the oracle's phase 1 starts
        // at f=131; its phase 2 at f=334) and its phase 2 at frame 133, so the
        // targets are the oracle phase-local frame mapped onto the port's
        // phase-local frame. A keyed step presses `key` once at the target and
        // waits `hold_frames` so the move / pause dialog is drawn at capture.
        //
        // [VS-intro gate] The frame counter keeps running under the `ik`
        // overlay (`vs_wait == 2`), so an absolute frame no longer identifies
        // a VISIBLE fight state: the pre-`ik.yY`-3.4 s driver captured F120/F146
        // while the overlay still covered the scene. The JS creates the fight
        // only after `ik.kg` (L2071), so a visible-fight capture waits for
        // `!vs_active()` first; `hold_frames` then counts the visible frames.
        if (s.fight_frame >= 0 || s.vs_wait == 2) {
            const bool on_fight = (cur == kScreenFight);
            sf2::app::Screen* ftop = app.screens().top();
            sf2::app::FightScreen* fs =
                (on_fight && ftop != nullptr) ? static_cast<sf2::app::FightScreen*>(ftop)
                                              : nullptr;
            const int ff = fs != nullptr ? fs->fight_frame() : -1;
            const bool vs_over = fs != nullptr && !fs->vs_active();
            const bool ready =
                !on_fight || (s.vs_wait == 2 ? vs_over : ff >= s.fight_frame);
            if (ready) {
                if (on_fight && s.key != 0 && !key_up_done) {
                    app.inject_key(s.key, true);
                    app.inject_key(s.key, false);
                    key_up_done = true;
                    step_frame = 0;
                    std::fprintf(stdout, "%s step %d/%d %s -> key %d @F%d\n", tag,
                                 step + 1, count, s.label, s.key, ff);
                    std::fflush(stdout);
                    return;
                }
                if (step_frame < s.hold_frames) {
                    ++step_frame;
                    return;
                }
                if (s.vs_wait == 2) {
                    std::fprintf(stdout, "%s fight frame F%d (gate vs-over) capture %s\n", tag,
                                 ff, s.capture != nullptr ? s.capture : "-");
                } else {
                    std::fprintf(stdout, "%s fight frame F%d (gate %d) capture %s\n", tag, ff,
                                 s.fight_frame, s.capture != nullptr ? s.capture : "-");
                }
                std::fflush(stdout);
                snap(app, s);
                advance();
                return;
            }
            ++step_frame;
            return;
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
                    float ccx = s.x, ccy = s.y;
                    sf2::app::Screen* top_s = app.screens().top();
                    if (s.map_fight && cur == kScreenMap && top_s != nullptr) {
                        static_cast<sf2::app::MapScreen*>(top_s)->fight_button_center(ccx, ccy);
                    }
                    std::fprintf(stdout, "%s step %d/%d %s -> click (%.0f, %.0f)\n", tag,
                                 step + 1, count, s.label, ccx, ccy);
                    std::fflush(stdout);
                    app.inject_click(ccx, ccy);
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
            // Wait for the destination screen BEFORE the hold (a same-screen
            // step has expect_screen == -1). The boss-intro `jk` roster
            // (4.6 s) now delays the Fight push, so a capture must not fire
            // before the screen it belongs to has arrived.
            const bool arrived = s.expect_screen < 0 || cur == s.expect_screen;
            bool ready = arrived && step_frame >= s.min_delay + s.hold_frames;
            // [fidelity tutorial-beat gate] Wait on the quest chain's own beat
            // (quest_engine `tutorial_gate_beat`) instead of a frame count tied
            // to the lesson timeout: 1/2 = the bar beat the chain is parked at,
            // 3 = the `Regular` modal is queued. The oracle tutorial frames are
            // exactly these beats (oracle_matrix/MANIFEST.md).
            if (s.wait_gate != 0) {
                const bool open = (s.wait_gate == 3)
                                      ? app.quest_engine().has_modal()
                                      : app.quest_engine().tutorial_gate_beat() == s.wait_gate;
                if (!open) {
                    ready = false;
                } else if (gate_open_frame < 0) {
                    // The beat has only just been queued — the bar rolls in and
                    // the modal opens over `od.Ge`, so `hold_frames` counts from
                    // the OPEN, not from the (already elapsed) step delay.
                    gate_open_frame = step_frame;
                    ready = false;
                } else if (step_frame < gate_open_frame + s.hold_frames) {
                    ready = false;
                }
            }
            // [VS-intro gate] `fight_intro` is the `ik` ROSTER frame (the
            // oracle's own capture), not a bare fight frame: hold until the
            // overlay has composed (portraits slid in + strokes wiped + names,
            // `kVsNameT`..`kVsFadeT`) so the capture cannot land in the
            // slide-in or the fade-out window.
            if (ready && s.vs_wait == 1) {
                sf2::app::Screen* ftop = app.screens().top();
                sf2::app::FightScreen* fs =
                    (cur == kScreenFight && ftop != nullptr)
                        ? static_cast<sf2::app::FightScreen*>(ftop)
                        : nullptr;
                if (fs != nullptr) {
                    ready = fs->vs_active() && fs->vs_time() >= kVsComposedT;
                }
            }
            if (ready) {
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
        gate_open_frame = -1;
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
    // The JS-exact player decision this probe must reproduce: the
    // `FightScreen::player_decision()` line — `cands=<name>@<prio>,...`
    // (the `hb_`/JS `ra.Lk` order candidate set), `f=<name>,...` (the `Aua`
    // max-`priority` non-`Rha` group), `draw=<v>|-`, `idx=<i>`, the picked
    // move and the optional `ukb=<name>` (`Rha` group). It pins the whole
    // `Gc.DK` `c == false` branch (L673-674), so a probe can never pass on a
    // hard-coded move name alone. "" = nothing to assert (the no-move probe).
    const char* decision = "";
    int window = 4;           // frames after `frame` to observe the move start
    bool substring = false;   // expect is a substring of the move name
};

// The `--verify-input` tape. Control ids are JS `sa.$h`
// (1=Up,3=Forward,5=Down,7=Back,9=Punch,10=Kick,11=Ranged,12=Magic,
// 13=RaidCharge,14=Super). A double-tap is press-RELEASE-press: JS
// `zl.Sgb` (L798) is guarded by `!a.sl` (the key must not already be
// down), so two same-frame downs of one key produce ONE tap, not two.
// The earlier tape's `down;down` pairs were exactly that bug, plus Punch
// was never released, which made the later K press (`sl` still set) a
// no-op - both fixed here.
//
// The expected picks are asserted through the JS-exact PLAYER decision, not a
// single move name: each probe carries the `player_decision()` line it must
// reproduce — the candidate set (`cands=<name>@<priority>,...`, the `hb_` /
// JS `ra.Lk` document order `Gc.EZa` L676 walks), the `Aua` max-`priority`
// non-`Rha` group (`f=...`), the `uf.sja` draw (`draw=`), the drawn index and
// the pick.
//
// Which branch of `Gc.DK` (L673-674) the human lands in is decided entirely by
// the `eb` flag of the event that produced the candidates:
//   * The HUMAN key press enters as `Gc.mS(a){this.Ih(2,a)}` (L672) <- `wd.BHa`
//     (L507) <- `zl.rwa` (L799) <- `zl.Sgb` (L798) <- `wd.yJa` (L501) <-
//     `ca.N0a` (L426 `this.eu==2 && b.yJa(a)`). `Ih` leaves `eb` FALSE.
//   * `eb` is set TRUE only by `Gc.Vkb(a){this.Ih(2,a,!0)}` (L673), which is
//     reached only from `ca.Vgb` (L388 `this.Bg.Vkb(a)`) <- `wd.hJa` (L500)
//     <- `wd.Anb` (L499, gated `this.parameters.Fj||P.fP` = AI/BothBot) and
//     only for a `type==1` (Random) tactic.
// `Gc.DK(a,b,c)` with `c == false` (the per-frame `dxa` L678 call) evaluates
// `c||!h.eb||h.animation.Rha||d.push(h)`: the `d.push` sits at the END of an
// `||` chain, so it runs only when `c`, `!h.eb` and `h.Rha` are all falsy —
// i.e. `d` = {eb && !Rha}. On the HUMAN path (`eb == false`) `d` is therefore
// EMPTY, `d.length>0` is false, and `Gc.Pkb` (L674-676) — its `M7.Wcb`
// mirror-compat filter, its `va.Ts` <Tactics><Conditions> filter and its
// `this.jL(a,d)` -> `de.jL` (L597) -> `Md.jL` (L640) + `iCa` WEIGHTED
// ROULETTE — is NEVER REACHED. The `Tactics` gate is an AI-tactic gate.
//
// The human's pick is the `DK` else branch (L674):
//   e = f[uf.sja(f.length)]     the `Aua` (L673) max-`priority` group of the
//                               non-`Rha` candidates, picked UNIFORMLY from
//                               `Math.random` (`uf.sja` L115 =
//                               `floor(uf.OKa.RGa()*(n-0))+0`; `uf.OKa.RGa() =
//                               Math.random`, `at.Nlb` L114, `uf.OKa=new at`
//                               L2471) — an UNSHARED stream, NOT `Da.pg`;
//   g.length>0 && a.Ukb(...)    the `Rha` group only parks a name in `wd.P9`
//                               (`wd.Mnk` L507 clears it) — no clip starts;
//   then `e.animation.MS ? a.jJa(e.animation,e.R1)
//                        : Gc.Nsb(a, this.Ek[e.index], e.animation, e.sign)`
//                               -> `wd.fJa` (L506) -> `Ml` -> `wd.Bnb` (L507)
//                               -> `wd.NS` (L505) -> `Te.Skb` (L550).
// `Fighter::try_select_move` is exactly that branch (`set_math_random`
// installs the pinned `math_random01()` for the `uf.sja` draw).
//
// The candidates are the JS `ru.iQ` set for the shipped Fists loadout: a move
// whose `<Events>` contains `<KeyPressed/>` (the `1key` Template chains to
// `Controlled`, moves.xml, which owns `<KeyPressed/>`) AND whose own
// `<Conditions>` (the `<Keys>` Tap requirement lives there) pass — for the
// boot fighter that means the `<Locks>` pass too (Skeleton + Weapon/Fists +
// Body/Head, fight.cpp `make_fighter`; `ra.Hza` L684-685 admits a move only
// when its `<Locks>` hold). `ra.Lk` document order is the candidate order.
static const VerifyProbe kVerifyProbes[] = {
    // F180: Back Tap x2 at the spawn gap (dist 283). Candidate order is the
    // JS `ra.Lk` DOCUMENT order: StepBack then BackHandflip. `Gc.DK` L673
    // splits them with `Aua` (`animation.Rha` false for both -> the `f`
    // group) and keeps the max-`<Priority>` group only: BackHandflip
    // (Priority 20) beats StepBack (10), so `f` is a SINGLETON and L674's
    // `e = f[uf.sja(f.length)]` needs no draw (`floor(r*1) == 0`). No `Pkb`.
    {180, "Back Tap x2 (spawn gap 283)", "BackHandflip",
     "cands=StepBack@10,BackHandflip@20 f=BackHandflip draw=- idx=0 BackHandflip",
     4, false},
    // F300: Forward Tap x2. Document order: StepForward then
    // DoubleStepForward. `Aua` keeps DoubleStepForward (Priority 20 > 10),
    // a singleton `f` -> no draw, no `Pkb`. The 2key double step wins on
    // PRIORITY, not on a weight.
    {300, "Forward Tap x2", "DoubleStepForward",
     "cands=StepForward@10,DoubleStepForward@20 f=DoubleStepForward draw=- idx=0 DoubleStepForward",
     4, false},
    // F420: Punch Tap x2 + Forward Hold. PROOF that the `va.Ts`
    // <Tactics><Conditions> filter of `Gc.Pkb` (L675) is NOT on this path:
    // HighPunch (`<Tactics>` Distance Max=250) and HeavyPunch (Min=50
    // Max=350) are both still candidates at a gap well past 350 — the old
    // `Pkb` run dropped them here. `Aua` (L673) then keeps the unique max-
    // `<Priority>` group: DoublePunch (130) over HeavyPunch (120), HighPunch
    // (110) and StepForward (10).
    {420, "Punch Tap x2 + Forward Hold", "DoublePunch",
     "cands=StepForward@10,HighPunch@110,HeavyPunch@120,DoublePunch@130 f=DoublePunch draw=- idx=0 DoublePunch",
     4, false},
    // F520: single Forward tap -> the 1key StepForward (singleton `Aua`).
    {520, "Forward Tap x1 (1key)", "StepForward",
     "cands=StepForward@10 f=StepForward draw=- idx=0 StepForward",
     4, false},
    // F550: single Forward tap 30 frames later (a fresh 1key step).
    {550, "Forward Tap x1 (+30f)", "StepForward",
     "cands=StepForward@10 f=StepForward draw=- idx=0 StepForward",
     4, false},
    // F620: K (GLFW 75) maps to Punch (id 9) -> the Punch-key candidate set.
    // THE FIX PROBE: a SINGLE Punch tap cannot satisfy the `2key` Punch-x2
    // moves, so the 1key candidates are HighPunch (`<Tactics>` Distance
    // Max=250) and ShortUpwardElbowStrike. `Gc.Pkb`'s `va.Ts` gate (L675)
    // lives on the AI's `eb=true` (`Gc.Vkb`) branch only, so the human CAN
    // punch here; `Aua` then keeps ShortUpwardElbowStrike (Priority 150 >
    // HighPunch 110) and it starts. Its OWN `<Conditions>` (the `f.Yz` L677
    // test, `<Keys>` Punch Tap + `<Distance Max="130">`) do pass at this
    // frame — the player has walked in from the spawn gap 283. Under the
    // old `Pkb` path this probe expected "<none>": every attack key was
    // dead at fight start.
    {620, "K key -> Punch-key move (no Tactics gate)", "ShortUpwardElbowStrike",
     "cands=HighPunch@110,ShortUpwardElbowStrike@150 f=ShortUpwardElbowStrike draw=- idx=0 ShortUpwardElbowStrike",
     14, false},
    // F700: B (GLFW 66) is unbound -> no tap -> no decision, no move.
    {700, "B key -> dropped (no move)", "<none>", "", 12, false},
};
constexpr int kVerifyProbeCount =
    static_cast<int>(sizeof(kVerifyProbes) / sizeof(kVerifyProbes[0]));

std::vector<ReplayEdge> build_verify_edges() {
    std::vector<ReplayEdge> e;
    auto down = [&](int f, int c) { e.push_back(ReplayEdge{f, c, true}); };
    auto up = [&](int f, int c) { e.push_back(ReplayEdge{f, c, false}); };
    // BackHandflip = Back Tap x2 (moves.xml L358527) FIRST, at the spawn gap
    // (dist 283): the same Priority-20 Back Tap x2 slot DashBackwards used to
    // win only because the lock-ignoring move list admitted its
    // `Armor BODY_GATEKEEPER` lock. Tap = down;up;down (JS `!a.sl`).
    down(180, 7);
    up(180, 7);
    down(180, 7);
    up(220, 7);
    // DoubleStepForward = Forward Tap x2 (moves.xml L499745).
    down(300, 3);
    up(300, 3);
    down(300, 3);
    up(340, 3);
    // DoublePunch = Punch Tap x2 + Forward Hold (moves.xml L609376). Punch is
    // released at 470 with Forward so the K press at 620 is a fresh down.
    down(400, 3);
    down(420, 9);
    up(420, 9);
    down(420, 9);
    up(470, 9);
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

// --- `--quest-verify`: interactive quest-action verification ---------------
// A live app (headless_frames_ == 0, so the engine EXECUTES actions instead
// of recording them) driven ONLY through the app's own input injection
// (`App::inject_click` — the JS `ma.Bd`/pointer tap primitive the scripted
// drivers already use). No OS input is generated: the window is hidden and
// never foregrounded, so the user's cursor and desktop are untouched. Drives
// the shipped `StoryTutorial*` chain (fresh profile) and asserts the live
// behaviours: the chain fires, `ChangeScene` executes, `MenuBtnFlashing`
// resolves the `_NextScene` nav target, `OpenShop` opens the Shop at the
// tutorial tab/item, and the Lynx (Shin) fight launches.
struct QuestVerifyDriver {
    int frame = 0;
    int cooldown = 0;  // frames before the next injected click
    bool nav_toggle = false;
    int last_screen = -1;
    // Assertions (PASS/FAIL logged at the end).
    bool saw_chain = false;       // the fresh tutorial chain fired
    bool saw_nav_flash = false;   // `MenuBtnFlashing` resolved a nav target
    bool saw_shop = false;        // the Shop screen became current
    bool saw_lynx_dialog = false; // the Lynx boss dialog queued
    bool saw_shin = false;        // the boss fight carries BOSS_LYNX
    bool saw_fight = false;       // the Fight screen launched
    bool go_map = false;          // post-OpenShop: navigate to the Map

    // Queue an internal click at the view coordinate (no OS input).
    void tap(sf2::app::App& app, int x, int y) {
        cooldown = 8;
        std::fprintf(stdout, "[qverify] injected click (%d, %d)\n", x, y);
        std::fflush(stdout);
        app.inject_click(x, y);
    }

    void tick(sf2::app::App& app) {
        ++frame;
        if (cooldown > 0) --cooldown;
        const int cur = app.screens().current_id();
        if (cur != last_screen) {
            last_screen = cur;
            std::fprintf(stdout, "[qverify] screen -> %d\n", cur);
            std::fflush(stdout);
        }
        if (app.quest_engine().scene_actions() > 0) saw_chain = true;
        if (app.quest_engine().shop_actions() > 0) go_map = true;
        if (cur == kScreenShop) saw_shop = true;
        if (cur == kScreenFight) {
            saw_fight = true;
            const std::string nm = app.pending_battle().battle_name;
            if (nm.find("BOSS_LYNX") != std::string::npos) saw_shin = true;
        }
        sf2::app::QuestEngine& q = app.quest_engine();
        // 1. A queued dialog owns the input (advance / fire the plate).
        if (q.has_dialog()) {
            const sf2::app::EngineDialog& d = q.dialog();
            if (d.image.find("boss_lynx") != std::string::npos ||
                d.title.find("Lynx") != std::string::npos) {
                saw_lynx_dialog = true;
            }
            if (cooldown == 0) {
                if (d.type == "Notification")
                    tap(app, 640, 400);  // any tap advances
                else
                    tap(app, 860, 549);  // tutorial_dialog_layout action plate
            }
            return;
        }
        // 2. Between-rounds Next button (the fight holds until it is pressed).
        sf2::app::Screen* top = app.screens().top();
        if (cur == kScreenFight && top != nullptr &&
            static_cast<sf2::app::FightScreen*>(top)->round_wait()) {
            if (cooldown == 0) {
                float cx = 0.0f, cy = 0.0f;
                static_cast<sf2::app::FightScreen*>(top)->next_button_center(cx, cy);
                tap(app, static_cast<int>(cx), static_cast<int>(cy));
            }
            return;
        }
        // 3. Results -> back (pops to the caller).
        if (cur == kScreenResults) {
            if (cooldown == 0) tap(app, 640, 360);
            return;
        }
        // 4. `MenuBtnFlashing` guidance: expand the collapsed `za` column,
        //    then click the flashed row (the JS `eo` collapse + row flash).
        const std::string flash = q.nav_flash();
        if (!flash.empty()) {
            saw_nav_flash = true;
            int idx = -1;
            if (flash == "Dojo") idx = 0;
            else if (flash == "Map") idx = 1;
            else if (flash == "Shop") idx = 2;
            else if (flash == "Profile") idx = 3;
            else if (flash == "Settings") idx = 4;
            if (idx >= 0 && cooldown == 0) {
                // The `za` nav column is interactive on the Dojo hub (the
                // shared chrome's `za_update`); from a sub-screen (Shop/Map/
                // Profile) press BACK first, then use the column.
                if (cur != kScreenDojo) {
                    tap(app, 64, 40);
                    return;
                }
                static const int kRowY[5] = {126, 231, 337, 442, 548};
                if (nav_toggle) {
                    tap(app, 184, kRowY[idx]);
                } else {
                    tap(app, 184, 92);  // the collapsed `gk` header (za_header_rect)
                }
                nav_toggle = !nav_toggle;
            }
            return;
        }
        // 5. After `OpenShop` executed: head to the Map for the Lynx beat.
        //    (`StoryTutorialOpenScene` is latched after its first fire, so the
        //    second `Activate` does not re-arm a nav flash; the driver closes
        //    the loop by navigating to the Map itself.)
        if (go_map && !saw_lynx_dialog) {
            if (cooldown == 0) {
                if (cur == kScreenMap) {
                    return;  // wait for `StoryTutorialBossFight`'s Lynx dialog
                }
                if (cur != kScreenDojo) {
                    tap(app, 64, 40);  // BACK to the hub
                } else {
                    static const int kRowY[5] = {126, 231, 337, 442, 548};
                    if (nav_toggle) {
                        tap(app, 184, kRowY[1]);  // Map
                    } else {
                        tap(app, 184, 92);  // header
                    }
                    nav_toggle = !nav_toggle;
                }
            }
            return;
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    std::string res_root = "reference/www/res";
    std::string save_path = "reference/saves/save.xml";
    int headless = 0;
    bool auto_click = false;
    bool headless_loop = false;
    bool ui_tour = false;
    bool fidelity_tour = false;
    bool quest_verify = false;  // --quest-verify: interactive action check
    bool quest_verify_buy = false;  // --quest-verify-buy: seeded STEP_BUY_ITEM
    bool dialog_verify = false;     // --dialog-verify: headless dialog harness
    bool observe_dialogs = false;   // --observe-dialogs: keep the queue observable
    bool replay_mode = false;
    bool verify_input = false;
    // --input-tape [js|desktop]: the scripted key/pointer tape fed through the
    // REAL input consumer (FightScreen::on_key, the function App::poll_input
    // calls for every GLFW key edge). `js` forces the byte-exact `Af.oUa`
    // table (set_desktop_key_aliases(false)); `desktop` (default) is the
    // shipped desktop map. Prints per key: GLFW code -> key_type -> selected
    // move -> the per-frame player move name (the move -> idle flip), then the
    // dojo hub section.
    bool input_tape = false;
    bool input_tape_js_table = false;
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
        } else if (arg == "--quest-verify") {
            quest_verify = true;
        } else if (arg == "--quest-verify-buy") {
            quest_verify = true;
            quest_verify_buy = true;
        } else if (arg == "--dialog-verify") {
            dialog_verify = true;
        } else if (arg == "--observe-dialogs") {
            observe_dialogs = true;
        } else if (arg == "--replay") {
            replay_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                replay_file = argv[++i];
            }
        } else if (arg == "--verify-input") {
            verify_input = true;
        } else if (arg == "--input-tape") {
            input_tape = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const std::string m = argv[++i];
                if (m == "js") {
                    input_tape_js_table = true;
                } else if (m != "desktop") {
                    std::fprintf(stderr,
                                 "game: --input-tape map must be 'js' or 'desktop'\n");
                    return 1;
                }
            }
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

    // `--fidelity-tour` captures the stock FRESH tutorial beats, so start the
    // story at NotStarted BEFORE boot: the JS loads `quests.xml` with the
    // `tutorial_quests.xml` include gated on `_$StoryTutorialStep != END`
    // (L2478) and the port's `QuestEngine::ensure_loaded` short-circuits on an
    // END step (L200) — once short-circuited the chain cannot be armed, so a
    // stale completed local save would make the tutorial steps stall. The
    // oracle harness seeds the same fresh state.
    if (fidelity_tour || quest_verify || observe_dialogs) {
        std::string def = res_root + "/users_default.xml";
        if (!std::filesystem::exists(def)) {
            const std::string hashed = res_root + "/users_default.b7da2019.xml";
            if (std::filesystem::exists(hashed)) {
                def = hashed;
            } else {
                const std::string extracted = "reference/extracted/xml/res/users_default.xml";
                if (std::filesystem::exists(extracted)) def = extracted;
            }
        }
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();
            if (quest_verify_buy) {
                // Harness seed for the `OpenShop` verification: the tutorial
                // chain only advances to STEP_BUY_ITEM on a training-fight WIN
                // (tutorial_quests.xml L77-80 — the port's shipped fight is
                // decided by the enemy AI and is a loss, a pre-existing
                // gameplay gap outside this task). Seeding the step (the same
                // device the fidelity tour uses for its END handoff) makes
                // `StoryTutorialShop` arm the Shop nav flash at boot, so the
                // driver verifies the LIVE `OpenShop` execution.
                w.set_story_step("STEP_BUY_ITEM");
                ss.save(w);
                std::fprintf(stdout, "[qverify] seeded story step -> STEP_BUY_ITEM\n");
                std::fflush(stdout);
            } else if (!w.story_step().empty()) {
                w.set_story_step("");
                ss.save(w);
                std::fprintf(stdout, "[quest] story step reset -> NotStarted (fresh profile)\n");
                std::fflush(stdout);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[qverify] story-step reset failed: %s\n", e.what());
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
        // D1/D2: the shop steps click the TRY plate on row 0 (WEAPON_KNIVES,
        // price 50), but `users_default.xml` carries `Money="0"` — so the buy
        // was a VACUOUS no-op that still printed "step done". Seed the
        // purchase money so the scripted loop exercises the REAL `Pa.iwa`
        // gate (`p.o.Tb >= a.jp()`, L1228), and assert the outcome at the end.
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();
            const int before = w.money;
            if (w.money < 50) {
                w.money = 200;
                ss.save(w);
                std::fprintf(stdout, "[loop] seeded purchase money: money=%d (was %d)\n",
                             w.money, before);
                std::fflush(stdout);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[loop] save seed failed: %s\n", e.what());
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
            // D2: the shop BUY + EQUIP steps must have REALLY landed (the
            // driver used to print "step done" for a failed purchase).
            bool owns_knives = false;
            for (const auto& mi : w.items) {
                if (mi.name == "WEAPON_KNIVES") owns_knives = true;
            }
            const bool shop_ok = owns_knives && w.weapon == "WEAPON_KNIVES";
            std::fprintf(stdout,
                         "[loop] shop BUY/EQUIP outcome: owns_knives=%d weapon=%s -> %s\n",
                         owns_knives ? 1 : 0, w.weapon.c_str(), shop_ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            if (!shop_ok) {
                std::fprintf(stderr, "[loop] shop steps did not buy+equip WEAPON_KNIVES\n");
                return 1;
            }
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
        // The save was seeded fresh before boot (see the `--fidelity-tour`
        // pre-init reset) so the engine loaded the tutorial include; replay the
        // Loader->Dojo ChangeTab to arm the chain. The END seed is re-landed
        // by `App::finish_tutorial_handoff` at the training-fight launch.
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
    } else if (observe_dialogs) {
        // --- dialog-observation harness (`--observe-dialogs`) ---------------
        // The headless probe normally makes `quest_modal_top` silently DRAIN
        // the queue (screens.cpp), which hides the queue ORDER from every
        // other harness. `App::set_dialog_observe` stops that drain, so this
        // mode reports the FRESH-profile queue exactly as `He.S` (L1050)
        // builds it: the `Ib` bar (last `Notification` wins, `Ib.Qhb` L1907)
        // and the `Wb` top (the first non-Notification — `Wb.Xob` L927 is
        // only reached from the `Regular` path, L931). No OS input.
        app.set_dialog_observe(true);
        app.set_fresh_tutorial(true);  // arm StoryTutorialWelcome
        app.set_auto_attack(false);
        app.set_headless_frames(1);
        std::size_t last_q = static_cast<std::size_t>(-1);
        std::string last_wb = "\x01", last_bar = "\x01";
        for (int f = 0; f < 300; ++f) {
            app.run_one_frame();
            const sf2::app::EngineDialog* m = app.quest_engine().modal_top();
            const sf2::app::EngineDialog* n = app.quest_engine().notification_top();
            const std::size_t q = app.quest_engine().dialog_count();
            const std::string wb = m != nullptr ? (m->type + "/" + m->title) : std::string("-");
            const std::string bar = n != nullptr ? (n->type + "/" + n->title) : std::string("-");
            if (q != last_q || wb != last_wb || bar != last_bar) {
                std::fprintf(stdout,
                             "[observe] f=%d queue=%zu wb_top=%s bar=%s\n", f, q,
                             wb.c_str(), bar.c_str());
                std::fflush(stdout);
                last_q = q;
                last_wb = wb;
                last_bar = bar;
            }
        }
        app.shutdown();
        return 0;
    } else if (dialog_verify) {
        // --- headless dialog harness (`--dialog-verify`) -------------------
        // No OS input at all: the window is hidden (never foregrounded) and
        // the harness uses only the app's internal primitives. Asserts the
        // `He` dialog contracts — D1 (a Left button renders BOTH plates and
        // the Left press dispatches the Left action), D2 (color -> button
        // frame), D7 (last-page caption precedence) and a dialog queued on
        // the Fight screen — then the shipped quest-tree `<Button Type>`
        // census. `headless_frames_ == 0` keeps the modal live (the headless
        // path drains the queue silently, screens.cpp `quest_modal_top`).
        glfwHideWindow(app.renderer().window());
        app.set_headless_frames(0);
        app.set_auto_attack(false);
        const bool selfcheck_ok = run_quest_dialog_selfcheck(app);

        const QuestButtonCensus census = census_quest_tree();
        // Parsed slots = the `<Button>` ELEMENTS the parser sees (XML comments
        // are not elements). The raw text additionally carries commented-out
        // quests (zone_4..zone_7 ship 3 each), which is where the 680/159
        // inventory comes from.
        std::fprintf(stdout,
                     "[dlgverify] parsed slots: %zu files, %zu dialogs, %zu buttons "
                     "(Right %zu / Left %zu / Middle %zu / Close %zu)\n",
                     census.files, census.dialogs, census.typed(), census.right, census.left,
                     census.middle, census.close);
        std::fprintf(stdout,
                     "[dlgverify] raw XML text (incl. %zu commented-out dialogs): %zu dialogs, "
                     "%zu buttons (Right %zu / Left %zu / Middle %zu / Close %zu)\n",
                     census.text_dialogs - census.dialogs, census.text_dialogs,
                     census.text_typed(), census.text_right, census.text_left,
                     census.text_middle, census.text_close);
        const bool elements_ok = census.right == 668 && census.left == 153 &&
                                 census.middle == 16 && census.close == 1;
        const bool raw_ok = census.text_right == 680 && census.text_left == 159 &&
                            census.text_middle == 16 && census.text_close == 1;
        const bool census_ok = elements_ok && raw_ok;
        std::fprintf(stdout,
                     "[dlgverify] %s parse census (parsed elements Right 668 / Left 153 / "
                     "Middle 16 / Close 1; raw text Right 680 / Left 159 / Middle 16 / "
                     "Close 1 - the +12/+6 are commented-out quests)\n",
                     census_ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return (selfcheck_ok && census_ok) ? 0 : 1;
    } else if (quest_verify || quest_verify_buy) {
        // --- interactive quest-action verification (internal injection) -----
        // A live app (headless_frames_ == 0, so the engine EXECUTES actions
        // rather than recording them) driven by `App::inject_click`. The
        // window is HIDDEN and never foregrounded — no OS input is generated,
        // so the user's cursor and desktop are untouched.
        //   `--quest-verify`     fresh profile: the shipped StoryTutorial*
        //                        chain fires, `ChangeScene` executes, the
        //                        training fight launches.
        //   `--quest-verify-buy` seeded STEP_BUY_ITEM: `OpenShop` opens the
        //                        Shop at the tutorial tab/item, then the Lynx
        //                        (Shin) boss dialog/fight.
        glfwHideWindow(app.renderer().window());
        if (quest_verify && !quest_verify_buy) app.set_fresh_tutorial(true);
        app.set_auto_attack(true);
        QuestVerifyDriver drv;
        std::fprintf(stdout, "[qverify] engine live (headless=0, hidden window), buy=%d\n",
                     quest_verify_buy ? 1 : 0);
        std::fflush(stdout);
        const std::size_t quests_loaded = app.quest_engine().quest_count();
        while (!glfwWindowShouldClose(app.renderer().window()) && drv.frame < 24000) {
            drv.tick(app);
            app.run_one_frame();
            if (drv.saw_lynx_dialog && drv.saw_shin && drv.saw_shop) break;
            if (!quest_verify_buy && drv.saw_fight && drv.frame > 600) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(6));  // pace the 60 Hz steps
        }
        sf2::app::QuestEngine& q = app.quest_engine();
        const bool ok_chain = drv.saw_chain && quests_loaded > 0;
        const bool ok_change = q.scene_actions() > 0;
        const bool ok_flash = drv.saw_nav_flash;
        const bool ok_shop = drv.saw_shop;
        const bool ok_open = q.shop_actions() > 0;
        const bool ok_lynx = drv.saw_lynx_dialog;
        const bool ok_shin = drv.saw_shin;
        const bool ok_fight = drv.saw_fight;
        std::fprintf(stdout,
                     "[qverify] chain=%d ChangeScene executed=%zu navflash=%d Shop=%d "
                     "OpenShop executed=%zu LynxDialog=%d ShinFight=%d trainingFight=%d\n",
                     ok_chain ? 1 : 0, q.scene_actions(), ok_flash ? 1 : 0, ok_shop ? 1 : 0,
                     q.shop_actions(), ok_lynx ? 1 : 0, ok_shin ? 1 : 0, ok_fight ? 1 : 0);
        const bool all = quest_verify_buy
                             ? (ok_chain && ok_change && ok_flash && ok_shop && ok_open &&
                                ok_lynx && ok_shin)
                             : (ok_chain && ok_change && ok_fight);
        std::fprintf(stdout,
                     "[qverify] RESULT chain=%s ChangeScene=%s navflash=%s Shop=%s "
                     "OpenShop=%s LynxDialog=%s ShinFight=%s trainingFight=%s -> %s\n",
                     ok_chain ? "PASS" : "FAIL", ok_change ? "PASS" : "FAIL",
                     ok_flash ? "PASS" : "FAIL", ok_shop ? "PASS" : "FAIL",
                     ok_open ? "PASS" : "FAIL", ok_lynx ? "PASS" : "FAIL",
                     ok_shin ? "PASS" : "FAIL", ok_fight ? "PASS" : "FAIL",
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
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

        // Unit-level buffer check (JS `zl.Sgb`/`ia` L798): the `!a.sl`
        // guard means a second down of a key that is still held is IGNORED
        // (one Tap row, not two) — two taps require a release between them.
        // Then the 2-slot Tap cap (a 3rd evicts the oldest), holds rebuilt
        // from the down keys, and the 15-frame tap window (present through
        // +14, gone +15).
        if (verify_input) {
            sf2::scene::Fighter f;
            // JS-exact: a same-frame double-down of ONE key = 1 tap.
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            const bool one = f.buffered_tap_count() == 1;
            // Press-release-press = two taps.
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::release);
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            const bool two = f.buffered_tap_count() == 2;
            // A third press again evicts the oldest (2-slot cap).
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::release);
            f.input(sf2::scene::key_type::forward, sf2::scene::press_type::tap);
            const bool cap = f.buffered_tap_count() == 2;
            const bool hold = f.buffered_hold_count() == 1;
            for (int i = 0; i < 15; ++i) f.age_keys();
            const bool alive = f.buffered_tap_count() == 2;  // still there at +14
            f.age_keys();
            const bool gone = f.buffered_tap_count() == 0;  // cleared at +15
            std::fprintf(stdout,
                         "[verify] buffer: dbl-down=1tap(%d) 2-tap=%d cap2=%d hold=%d "
                         "alive@+14=%d empty@+15=%d -> %s\n",
                         one, two, cap, hold, alive, gone,
                         (one && two && cap && hold && alive && gone) ? "PASS" : "FAIL");
            std::fflush(stdout);
        }

        // Damage-parse assertion (JS `Ul.qjb` L777-778): EVERY sub-`<Damage>`
        // child lands in `SZ` and every `<Defense>` child in `KP`. The old
        // parser read `.child("Damage")` (first only) and ignored
        // `<Defense>`. Counts below are the authoritative XML-tree scan of
        // the shipped moves.xml (`617` outer `<Damage Value=..>` blocks, 568
        // of them with 2 sub-`<Damage>`, 120 with a `<Defense>`, 1182
        // sub-`<Damage>` entries, and 3 outer blocks with NO sub-entry —
        // those carry no `SZ`, so they are excluded from the `outer` count).
        if (verify_input) {
            int outer = 0, two_sub = 0, with_defense = 0, sub_total = 0, no_sz = 0;
            for (const auto& kv : app.fight_assets().moves) {
                for (const sf2::scene::Interval& iv : kv.second.intervals) {
                    if (iv.type != 4 || !iv.has_damage) continue;
                    if (iv.attack_attrs.empty()) {
                        ++no_sz;
                        continue;
                    }
                    ++outer;
                    sub_total += static_cast<int>(iv.attack_attrs.size());
                    if (iv.attack_attrs.size() >= 2) ++two_sub;
                    if (!iv.defense_names.empty()) ++with_defense;
                }
            }
            const bool parse_ok = outer == 614 && two_sub == 568 &&
                                  with_defense == 120 && sub_total == 1182 &&
                                  no_sz == 3;
            std::fprintf(stdout,
                         "[verify] damage parse: outer=%d/614 2sub=%d/568 "
                         "defense=%d/120 subTotal=%d/1182 noSZ=%d/3 -> %s\n",
                         outer, two_sub, with_defense, sub_total, no_sz,
                         parse_ok ? "PASS" : "FAIL");
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
        int probe_failures = 0;
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
                    const std::string rr = fs->player_decision();
                    const bool move_ok =
                        pending->substring
                            ? dec.find(pending->expect) != std::string::npos
                            : dec == std::string("input:") + pending->expect;
                    // The JS-exact gate: the recorded `Gc.DK` `c == false`
                    // decision must reproduce the expected candidate set +
                    // `Aua` group + draw + index + pick. An empty expectation
                    // is not asserted (the no-move probe).
                    const bool decision_ok =
                        pending->decision[0] == '\0' || rr == pending->decision;
                    const bool pass = move_ok && decision_ok;
                    if (!pass) ++probe_failures;
                    std::fprintf(stdout,
                                 "[verify] %s -> %s (F%d) expect=%s\n"
                                 "[verify]   decision got = %s\n"
                                 "[verify]   decision exp = %s -> %s\n",
                                 pending->label, dec.c_str(), fight_frames,
                                 pending->expect, rr.c_str(),
                                 pending->decision[0] == '\0' ? "(none)"
                                                              : pending->decision,
                                 pass ? "PASS" : "FAIL");
                    std::fflush(stdout);
                    pending = nullptr;
                } else if (pending != nullptr && pending->window > 0 &&
                           fight_frames > pending->frame + pending->window) {
                    const bool expect_none = pending->expect[0] == '<';
                    const std::string rr = fs->player_decision();
                    if (!expect_none) ++probe_failures;
                    std::fprintf(stdout,
                                 "[verify] %s -> (no move) (F%d) expect=%s "
                                 "decision=%s %s\n",
                                 pending->label, fight_frames, pending->expect,
                                 rr.empty() ? "(none)" : rr.c_str(),
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
        if (verify_input) {
            std::fprintf(stdout, "[verify] probes: %d/%d PASS\n",
                         kVerifyProbeCount - probe_failures, kVerifyProbeCount);
            std::fflush(stdout);
        }
        return probe_failures == 0 ? 0 : 1;
    } else if (input_tape) {
        // ------------------------------------------------------------------
        // `--input-tape [js|desktop]`: the scripted key/pointer tape driven
        // through the REAL input consumers. Every key edge goes through
        // `FightScreen::on_key` - the exact function `App::poll_input`
        // (app.cpp `kFightKeys` loop) calls for a GLFW edge - and every
        // pointer edge through `App::inject_click` (the JS `ma.Bd` tap
        // primitive the other drivers use). No OS input is generated.
        // ------------------------------------------------------------------
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
        app.set_headless_frames(1);
        auto* fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
        if (fs == nullptr) {
            std::fprintf(stderr, "[tape] no fight screen\n");
            return 1;
        }
        fs->set_desktop_key_aliases(!input_tape_js_table);
        std::fprintf(stdout, "[tape] key map = %s (desktop_key_aliases=%d)\n",
                     input_tape_js_table ? "Af.oUa (js)" : "desktop",
                     fs->desktop_key_aliases() ? 1 : 0);
        std::fflush(stdout);

        // The scripted tape. Each control is a press/release pair (a Tap in
        // the JS `zl.Sgb` sense: `!a.sl` needs a release before a second
        // press), spaced so the selected move's clip plays out and the
        // per-frame log shows the flip back to the stance idle.
        //   150/250/350 = D/A/W (Forward/Back/Up sectors 3/7/1)
        //   450..896    = K/L/O/P J/Q (Punch/Kick/Ranged/Magic/RaidCharge/Super)
        //   1000        = Left  (a desktop alias: sector 7)
        //   1090        = Space (a desktop alias: Punch)
        //   1180        = B     (bound by NOTHING - the dropped-key control)
        struct TapeEdge { int frame; int glfw; bool down; };
        static const TapeEdge kTape[] = {
            {150, 68, true},  {156, 68, false},    // D -> Forward
            {250, 65, true},  {256, 65, false},    // A -> Back
            {350, 87, true},  {356, 87, false},    // W -> Up
            {450, 75, true},  {456, 75, false},    // K -> Punch
            {560, 76, true},  {566, 76, false},    // L -> Kick
            {670, 79, true},  {676, 79, false},    // O -> Ranged
            {780, 74, true},  {786, 74, false},    // J -> RaidCharge
            {890, 81, true},  {896, 81, false},    // Q -> Super
            {1000, 263, true}, {1006, 263, false}, // Left -> Back (alias)
            {1090, 32, true}, {1096, 32, false},   // Space -> Punch (alias)
            {1180, 66, true}, {1186, 66, false},   // B -> unbound
        };
        constexpr int kTapeCount = static_cast<int>(sizeof(kTape) / sizeof(kTape[0]));
        const int last_frame = kTape[kTapeCount - 1].frame;

        std::size_t ei = 0;
        bool fight_seen = false;
        int fight_frames = 0;
        int guard = 0;
        int started_seen = 0;
        std::string last_move;
        // Per-tape-key result: the move the key selected ("" = none).
        std::map<int, std::string> selected_by_glfw;
        while (guard < 9000) {
            glfwPollEvents();
            if (fight_seen) {
                while (ei < static_cast<std::size_t>(kTapeCount) &&
                       kTape[ei].frame <= fight_frames) {
                    // The REAL consumer: App::poll_input calls exactly this.
                    fs->on_key(kTape[ei].glfw, kTape[ei].down);
                    std::fprintf(stdout,
                                 "[tape] input F%d glfw=%d down=%d -> key_type=%d\n",
                                 fight_frames, kTape[ei].glfw, kTape[ei].down ? 1 : 0,
                                 fs->last_input_key_type());
                    std::fflush(stdout);
                    if (kTape[ei].down) {
                        selected_by_glfw[kTape[ei].glfw];  // ensure a row
                    }
                    ++ei;
                }
            }
            app.run_one_frame();
            ++guard;
            if (!fight_seen && app.screens().current_id() == kScreenFight) {
                fight_seen = true;
                fight_frames = 0;
                std::fprintf(stdout, "[tape] fight screen up\n");
                std::fflush(stdout);
            } else if (fight_seen) {
                ++fight_frames;
                fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
                if (fs == nullptr) break;
                // The move-selection edge: attribute the new move to the
                // most recent pressed tape key.
                const int started = fs->player_moves_started();
                if (started > started_seen) {
                    started_seen = started;
                    const std::string dec = fs->player_last_decision();
                    if (!dec.empty() && dec[0] == 'i') {
                        const std::string mv = dec.substr(std::string("input:").size());
                        for (int t = kTapeCount - 1; t >= 0; --t) {
                            if (kTape[t].down && kTape[t].frame <= fight_frames) {
                                selected_by_glfw[kTape[t].glfw] = mv;
                                break;
                            }
                        }
                    }
                    std::fprintf(stdout, "[tape] F%d selected %s\n", fight_frames,
                                 dec.c_str());
                    std::fflush(stdout);
                }
                // The per-frame move name: printed only on a change so the
                // move -> idle flip is visible without a 1300-line dump.
                const std::string mv = fs->player_current_move();
                if (mv != last_move) {
                    last_move = mv;
                    std::fprintf(stdout, "[tape] F%d player_move=%s\n", fight_frames,
                                 mv.empty() ? "<none>" : mv.c_str());
                    std::fflush(stdout);
                }
            }
            if (fight_seen && fight_frames > last_frame + 60) break;
        }
        std::fprintf(stdout, "[tape] tape complete: %d fight frames\n", fight_frames);

        // The accepted-key report LAST (the probes feed real taps, so they
        // must not disturb the tape above).
        struct KeyProbe { int glfw; const char* name; };
        static const KeyProbe kProbes[] = {
            {87, "W"}, {68, "D"}, {83, "S"}, {65, "A"},
            {75, "K"}, {76, "L"}, {79, "O"}, {80, "P"}, {74, "J"}, {81, "Q"},
            {263, "Left"}, {262, "Right"}, {265, "Up"}, {264, "Down"},
            {32, "Space"}, {256, "Esc"}, {257, "Enter"}, {66, "B"},
        };
        int accepted = 0, accepted_js = 0, accepted_alias = 0;
        std::fprintf(stdout, "[tape] accepted-key report:\n");
        for (const KeyProbe& kp : kProbes) {
            const int js_kt = sf2::app::FightScreen::key_type_for_glfw(kp.glfw);
            fs->on_key(kp.glfw, true);
            const int kt = fs->last_input_key_type();
            fs->on_key(kp.glfw, false);
            if (kt != 0) {
                ++accepted;
                if (js_kt != 0) ++accepted_js; else ++accepted_alias;
            }
            std::fprintf(stdout,
                         "[tape]   %-6s glfw=%-4d Af.oUa=%-2d on_key->key_type=%-2d %s\n",
                         kp.name, kp.glfw, js_kt, kt, kt == 0 ? "DROPPED" : "accept");
        }
        std::fprintf(stdout, "[tape] accepted=%d of %zu (Af.oUa=%d desktop-alias=%d)\n",
                     accepted, sizeof(kProbes) / sizeof(kProbes[0]), accepted_js,
                     accepted_alias);
        std::fprintf(stdout, "[tape] per-key selected move:\n");
        for (const auto& kv : selected_by_glfw) {
            std::fprintf(stdout, "[tape]   glfw=%-4d -> %s\n", kv.first,
                         kv.second.empty() ? "<no move>" : kv.second.c_str());
        }
        std::fflush(stdout);

        // ------------------------------------------------------------------
        // The dojo hub (JS `Tf` L1969-1972). The JS hub runs a REAL `FightNone`
        // `ca` (`this.Ig=v.m1a(a)`, `aa(): this.YL(Ig,a)`), and the gamepad it
        // draws (`Za.F()`) is WIRED to it: `Za.hS` L453 -> `ca.Ka()` ->
        // `ca.N0a` L426 -> the controlled fighter. Prove it end to end: a
        // joystick sector press must start the move / displace the hub
        // fighter, and a punch press must start the punch move.
        // ------------------------------------------------------------------
        app.screens().pop();  // the fight -> back to the Dojo hub
        auto* ds = static_cast<sf2::app::DojoScreen*>(app.screens().top());
        const int dojo_id = app.screens().current_id();
        for (int i = 0; i < 4; ++i) app.run_one_frame();  // let the pad settle
        const bool dojo_ready = ds != nullptr && ds->dojo_fight_ready();
        std::fprintf(stdout, "[tape] dojo hub up: screen id %d fight_ready=%d frame=%d\n",
                     dojo_id, dojo_ready ? 1 : 0,
                     ds != nullptr ? ds->dojo_fight_frame() : -1);
        // (a) the drawn joystick (bottom-left, centre (216,554.4)). A press at
        //     (316,554) is the forward sector (3): it must start the forward
        //     move and displace the fighter in +x. kViewH=720, kPadSizeE=288,
        //     kPadMarginC=72, kPadMarginD=21.6, joy_r=144 (dead 72, grab 216).
        const float x0 = ds != nullptr ? ds->dojo_player_x() : 0.0f;
        const std::string m0 = ds != nullptr ? ds->dojo_player_move() : std::string();
        app.inject_click(316.0, 554.0, 3);
        for (int i = 0; i < 8; ++i) app.run_one_frame();
        const float x1 = ds != nullptr ? ds->dojo_player_x() : 0.0f;
        const std::string m1 = ds != nullptr ? ds->dojo_player_move() : std::string();
        std::fprintf(stdout,
                     "[tape] dojo joystick forward (sector 3): move '%s'->'%s' x %.2f->%.2f "
                     "dx=%.2f %s\n",
                     m0.c_str(), m1.c_str(), x0, x1, x1 - x0,
                     (x1 != x0 || m1 != m0) ? "PASS" : "FAIL");
        for (int i = 0; i < 4; ++i) app.run_one_frame();  // release the stick
        // (b) the drawn punch button (right cluster, centre ~(1142.5,521.7)).
        //     The press must start the Punch move.
        const std::string m2 = ds != nullptr ? ds->dojo_player_move() : std::string();
        app.inject_click(1142.5, 521.7, 3);
        for (int i = 0; i < 6; ++i) app.run_one_frame();
        const std::string m3 = ds != nullptr ? ds->dojo_player_move() : std::string();
        std::fprintf(stdout, "[tape] dojo pad punch (1142.5,521.7): move '%s'->'%s' %s\n",
                     m2.c_str(), m3.c_str(), m3 != m2 ? "PASS" : "FAIL");
        std::fflush(stdout);
        // (c) the real dojo navigation: the `za` nav column. Expand the
        //     collapsed header (x64-176 y72-110) then tap the MAP row (184,231).
        app.inject_click(120.0, 90.0);
        for (int i = 0; i < 4; ++i) app.run_one_frame();
        app.inject_click(184.0, 231.0);
        for (int i = 0; i < 8; ++i) app.run_one_frame();
        std::fprintf(stdout, "[tape] dojo nav MAP tap (184,231): screen id %d %s\n",
                     app.screens().current_id(),
                     app.screens().current_id() == kScreenMap ? "(-> Map) PASS"
                                                               : "(expected Map) FAIL");
        std::fflush(stdout);
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
        // Plain interactive boot (no harness). A FRESH profile (empty
        // `_$StoryTutorialStep` == the JS `NotStarted` default, quest_engine
        // `resolve_token` L267) arms the shipped tutorial chain so the Dojo
        // plays the Sensei beats and the training fight starts ONLY on the
        // player's `dlgStoryBtnFight` press (JS `He.dhb(1)` L1061). Seeded
        // saves (step END) stay chain-silent.
        if (headless == 0 && !auto_click) {
            bool fresh = false;
            try {
                fresh = app.save().load().story_step().empty();
            } catch (const std::exception&) {
            }
            if (fresh) {
                std::fprintf(stdout,
                             "[game] fresh profile: arming StoryTutorialWelcome (player-driven)\n");
                std::fflush(stdout);
                app.set_fresh_tutorial(true);
            }
        }
        app.run(headless, auto_click);
    }
    app.shutdown();
    std::fprintf(stdout, "[game] shutdown\n");
    return 0;
}
