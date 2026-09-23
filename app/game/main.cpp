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
//   game [res_root] [save_path]                 HIDDEN by default (no window)
//   game [res_root] [save_path] --windowed      the ONLY visible window
//   game [res_root] [save_path] --headless N   run N frames then exit (log-only)
//   game [res_root] [save_path] --autoclick     click the Fight button once
//
// Defaults: res_root = reference/www/res, save = reference/saves/save.xml.

#define NOMINMAX  // before any <windows.h> pull-in (glfw3native.h includes it)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
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
#include "atlas.hpp"
#include "audio/audio.hpp"
#include "scene/fighter.hpp"
#include "scene/fight.hpp"
#include "scene/magic_effects.hpp"
#include "scene/renderer.hpp"
#include "xml_archive.hpp"
#include "zstd_stream.hpp"

namespace {

using namespace sf2::app;  // kScreen* ids + the App/SaveSystem types

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [res_root] [save_path] [--headless N] [--autoclick] [--headless-loop] [--windowed|--hidden]\n"
                  "                  [--fight] [--battle <name>] [--zone <name>]\n"
                  "                  [--dump-pose N] [--dump-clip <name>]\n"
                  "                  [--ui-tour] [--fidelity-tour] [--quest-verify]\n"
                  "                  [--quest-query-probe] [--quest-action-probe]\n"
                  "                  [--dialog-verify] [--replay [file]] [--verify-input]\n"
                  "                  [--round-log] [--fx-probe] [--hit-audit]\n"
                  "  --watchdog N     RULE 0: force-exit a driver run after N seconds\n"
                   "                   (0 disables; default 900)\n"
                   "  --windowed       open the VISIBLE interactive window (the ONLY\n"
                   "                   way to get a window; every launch is hidden by\n"
                   "                   default, flagless included)\n"
                   "  --hidden         force the hidden + watchdog driver path\n"
                   "                   (the default; wins over --windowed)\n"
                  "  res_root  default reference/www/res\n"
                 "  save_path default reference/saves/save.xml\n"
                 "  --headless-loop  run the scripted playable loop, then exit\n"
                 "                   (dojo -> map -> BOSS_LYNX fight -> results -> shop\n"
                 "                    buy+equip WEAPON_KNIVES -> profile viewer\n"
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
// The `za` nav header tap (120,90) opens the column with `expand(.3)` (JS `gk`
// L2000) = 18 fixed steps, and `gk.aa` (L1998) swallows input until the run
// ends, so a follow-up row click must wait it out. Non-nav tabs (e.g. the shop
// TRY plate) keep the old 5-frame lead.
constexpr int kZaNavOpenLockFrames = 18;
static int za_tab_lead_frames(float tab_y) {
    return (tab_y > 0.0f && tab_y <= 100.0f) ? kZaNavOpenLockFrames + 6 : 5;
}

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
// (Equip lives in the shop detail: the TRY plate (`Oa.Fhb` L2300) wears an
//  unowned item on the `Pi` model + plays `TryOn` (JS `Ex(a,7)` L2301) and
//  arms the `Pi` panel; the `M8` price plate then buys AND equips (`Pa.iwa`
//  L1228 + `$o(b,!0)`, `ZYa` L2251). An owned+equipped item's TRY/EQUIP plate
//  UNEQUIPS. The loop exercises buy+equip, so the second fight's Locks move
//  list reflects the equipped weapon.)
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
    // 5: Shop -> WEAPON_KNIVES: the `Oa.Fhb` L2300 unowned path (JS
    //    `this.Ex(a,7)` L2301) wears the item on the `Pi` model + plays its
    //    `TryOn` clip and arms the `Pi` panel; the `M8` GoldButton (the price
    //    plate, `Pa.iwa` L1228 + `p.o.xa.$o(b,!0)` equip, `ZYa` L2251) then
    //    buys AND equips. `tab_x/tab_y` = the TRY plate press (JS `Up`, opens
    //    the panel ~5 frames early); the main click is the price-plate centre
    //    (`shop_price_rect` at 1280x720 = 934.6, 460.1).
    {934.6f, 460.1f, "shop->buy+equip WEAPON_KNIVES (M8)", kScreenShop, 0, -1, 12,
     nullptr, 305.4f, 199.5f},
    // 6: No re-toggle. After step 5 the item is owned+equipped, so the TRY/
    //    EQUIP plate (`Up`) would UNEQUIP (`xa.Qxb` L2300). The `Pi` panel is
    //    closed, so this click at the price-plate position is inert; the step
    //    only reports the save state.
    {934.6f, 460.1f, "shop->no re-toggle (owned)", kScreenShop, 0, -1, 12, nullptr},
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
    int probe_hold_ = -1;   // SF2_REVEAL_PROBE frames to hold on Results
    int probe_ticks_ = 0;

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

        // PROBE (temporary, `SF2_REVEAL_PROBE=N`): hold the Results screen for
        // N present frames so the reveal timeline can be sampled (0 = off).
        if (probe_hold_ == -1) {
            const char* env = std::getenv("SF2_REVEAL_PROBE");
            probe_hold_ = env != nullptr ? std::atoi(env) : 0;
        }
        if (probe_hold_ > 0 && cur == kScreenResults) {
            if (probe_ticks_++ < probe_hold_) return;
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
                    step_frame = s.min_delay - za_tab_lead_frames(s.tab_y);
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
    // VISIBLE-fight frames and gate on the SIM frame (`fight_frame` 120 / 146,
    // phase-local: phase 1 = frame 0, phase 2 = frame 133).
    {0.0f, 0.0f, "map->fight", 5, 10, 6, 40, "fight_intro.png", 0, false, 0.0f, 0.0f, 0, false,
     -1, true, 3, 1},
    {0.0f, 0.0f, "fight stance", 6, 0, -1, 0, "fight_stance.png", 0, true, 0.0f, 0.0f, -1, false,
     120, false, 3, 0},
    {0.0f, 0.0f, "fight block", 6, 0, -1, 0, "fight_block.png", 0, true, 0.0f, 0.0f, -1, false,
     146, false, 3, 0},
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
        // [fight-frame gate] The step waits on the SIM frame (JS `ca.frame`):
        // the ROUND-plate lead-in runs the intro at frame 0 (`init_locks` ->
        // plate -> `enter_start_stance`), so phase 1 is frame 0 and phase 2 is
        // frame 133 again. `fight_stance`/`fight_block` gate at their
        // phase-local oracle frames (120 / 146); the old `vs_wait == 2`
        // overlay workaround is gone.
        if (s.fight_frame >= 0) {
            const bool on_fight = (cur == kScreenFight);
            sf2::app::Screen* ftop = app.screens().top();
            sf2::app::FightScreen* fs =
                (on_fight && ftop != nullptr) ? static_cast<sf2::app::FightScreen*>(ftop)
                                              : nullptr;
            const int ff = fs != nullptr ? fs->fight_frame() : -1;
            const bool ready = !on_fight || ff >= s.fight_frame;
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
                std::fprintf(stdout, "%s fight frame F%d (gate %d) capture %s\n", tag, ff,
                             s.fight_frame, s.capture != nullptr ? s.capture : "-");
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
                    // The `za` header tap needs the open run to finish (the lock
                    // swallows input); other tabs keep the old 5-frame lead.
                    step_frame = s.min_delay - za_tab_lead_frames(s.tab_y);
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
//
// The explicit direct-boot loadout for `--loadout <WeaponSubType>`: the
// equipped slots in the JS `xc.hk` order (`Of`/`Hd`/`hg`/`Lg` = Skeleton /
// Weapon / Armor / Helm), each row the `Hm.he` (L758) Type/SubType/Name
// triple. Empty string -> empty list -> the app resolves the save
// (`owned_items`). The Weapon row's SubType is what the scene layer derives
// the move-list subtype from (`Fd` L808, `I.vg` L2473).
std::vector<sf2::scene::OwnedItem> loadout_owned(const std::string& weapon) {
    if (weapon.empty()) return {};
    return {
        {"Skeleton", "Skeleton", "Skeleton"},
        {"Weapon", weapon, weapon},
        {"Armor", "Body", "Body"},
        {"Helm", "Head", "Head"},
    };
}

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
    // `<Priority>` group: DoublePunch (130) over HeavyPunch (120) and
    // HighPunch (110). [item-1 fix] `StepForward` is NOT a candidate here:
    // the fighter is still inside its own `SelfUninterrupt [0,13]` window, and
    // with `ctx.anims_me` now carrying the move's transitive `<Template>` chain
    // (`StepForward -> ForwardStep -> Step`, the JS `jc.xl`/`lg.vQ` `XH`), the
    // `Step` template's `<CurrentAnimation Name="Step"/>` half is TRUE — so the
    // `<And Not="1"><CurrentInterval SelfUninterrupt/><Or>Step/DoubleStep</Or>`
    // guard correctly rejects the restart (the pre-fix expectation listed it
    // only because `anims_me` held the move NAME and the guard never matched).
    {420, "Punch Tap x2 + Forward Hold", "DoublePunch",
     "cands=HighPunch@110,HeavyPunch@120,DoublePunch@130 f=DoublePunch draw=- idx=0 DoublePunch",
     4, false},
    // F520: single Forward tap -> the 1key StepForward (singleton `Aua`).
    {520, "Forward Tap x1 (1key)", "StepForward",
     "cands=StepForward@10 f=StepForward draw=- idx=0 StepForward",
     4, false},
    // F550: single Forward tap 30 frames later. [item-1 fix] The tap lands
    // while the previous `StepForward` is still inside `SelfUninterrupt [0,13]`
    // (the trace shows the fighter at `StepForward@12`), so the `Step`
    // template's `<CurrentAnimation Name="Step"/>` guard — now reachable
    // because `ctx.anims_me` carries the transitive template chain — rejects
    // the restart: no NEW move starts (`decision=(none)`). The pre-fix
    // expectation of a fresh `StepForward` encoded the inert guard.
    {550, "Forward Tap x1 (+30f) [item-1 guard]", "<none>", "", 4, false},
    // F620: K (GLFW 75) maps to Punch (id 9) -> the Punch-key candidate set.
    // THE FIX PROBE: a SINGLE Punch tap cannot satisfy the `2key` Punch-x2
    // moves, so the 1key candidates are HighPunch and
    // ShortUpwardElbowStrike. `Gc.Pkb`'s `va.Ts` gate (L675) lives on the
    // AI's `eb=true` (`Gc.Vkb`) branch only, so the human CAN punch here.
    // [stale-expectation fix] The old expectation was `HighPunch` on the
    // belief that ShortUpwardElbowStrike's `<Conditions>` `<Distance
    // Max="130">` (moves.xml:15156, From Me/Pivot -> To Enemy/NPivot) fails
    // at this frame ("trace: dist=208"). That `dist` predates the current
    // tape/step-guard behaviour. The LIVE geometry at the F620 tap is
    // `me=891.8 en=973.0 dist=81.2` (`SF2_TRACE_COND=1` dump), so the
    // Distance gate PASSES, and `Gc.Aua` (L673) keeps the max-`<Priority>`
    // group — ShortUpwardElbowStrike Priority 150 (moves.xml:15147) beats
    // HighPunch 110. ShortUpwardElbowStrike is therefore the JS-exact pick
    // at this distance; the port's selection is correct and only the
    // authored expectation was stale.
    {620, "K key -> Punch-key move (no Tactics gate)", "ShortUpwardElbowStrike",
     "cands=HighPunch@110,ShortUpwardElbowStrike@150 f=ShortUpwardElbowStrike draw=- idx=0 ShortUpwardElbowStrike",
     14, false},
    // F700: B (GLFW 66) is unbound -> no tap -> no decision, no move.
    {700, "B key -> dropped (no move)", "<none>", "", 12, false},
};
constexpr int kVerifyProbeCount =
    static_cast<int>(sizeof(kVerifyProbes) / sizeof(kVerifyProbes[0]));

// The `--verify-input --loadout Knives` probe set: the SAME tape frames, but
// the weapon-subtype derivation (`Fd` L808) admits the Knives move set
// (`ra.Hza` L684-685 over `TacticWeapon="Knives|Keris"`), so the Punch key
// resolves `KnivesSlash` instead of `HighPunch`, and the idle is
// `KnivesStartStanceIdle` (`knives_stance_idle`). No Kick-key knives move
// exists (all 20 knives-keyed moves in moves.xml are `<Key Type="Punch"/>`),
// so the Kick tap keeps the universal `HighKick` on both loadouts.
static const VerifyProbe kVerifyProbesKnives[] = {
    {180, "Back Tap x2 (spawn gap 283) [Knives]", "BackHandflip",
     "cands=StepBack@10,BackHandflip@20 f=BackHandflip draw=- idx=0 BackHandflip",
     4, false},
    {300, "Forward Tap x2 [Knives]", "DoubleStepForward",
     "cands=StepForward@10,DoubleStepForward@20 f=DoubleStepForward draw=- idx=0 DoubleStepForward",
     4, false},
    // [item-1 fix] `StepForward` absent here for the same reason as the Fists
    // F420 probe (the `Step` guard now rejects the mid-`SelfUninterrupt`
    // restart; see the note there).
    {420, "Punch Tap x2 + Forward Hold [Knives]", "KnivesSuperSlash",
     "cands=KnivesSlash@110,KnivesDoubleSlash@115,KnivesHeavySlash@120,KnivesSuperSlash@130 f=KnivesSuperSlash draw=- idx=0 KnivesSuperSlash",
     4, false},
    {520, "Forward Tap x1 (1key) [Knives]", "StepForward",
     "cands=StepForward@10 f=StepForward draw=- idx=0 StepForward",
     4, false},
    // [item-1 fix] same blocked mid-window restart as the Fists F550 probe.
    {550, "Forward Tap x1 (+30f) [Knives]", "<none>", "", 4, false},
    {620, "K key -> Punch-key move (Knives) [Knives]", "KnivesSlash",
     "cands=KnivesSlash@110 f=KnivesSlash draw=- idx=0 KnivesSlash",
     14, false},
    {700, "B key -> dropped (no move) [Knives]", "<none>", "", 12, false},
};
constexpr int kVerifyProbesKnivesCount =
    static_cast<int>(sizeof(kVerifyProbesKnives) / sizeof(kVerifyProbesKnives[0]));

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
    std::string last_dialog_key;  // frame-stamped dialog census (one per top)
    // Assertions (PASS/FAIL logged at the end).
    bool saw_chain = false;       // the fresh tutorial chain fired
    bool saw_nav_flash = false;   // `MenuBtnFlashing` resolved a nav target
    bool saw_shop = false;        // the Shop screen became current
    bool saw_lynx_dialog = false; // the Lynx boss dialog queued
    bool saw_shin = false;        // the boss fight carries BOSS_LYNX
    bool saw_fight = false;       // the Fight screen launched
    bool go_map = false;          // post-OpenShop: navigate to the Map

    // Queue an internal click at the view coordinate (no OS input).
    void tap(sf2::app::App& app, int x, int y, int cd = 8) {
        cooldown = cd;
        std::fprintf(stdout, "[qverify] f%d injected click (%d, %d)\n", frame, x, y);
        std::fflush(stdout);
        app.inject_click(x, y);
    }

    void tick(sf2::app::App& app) {
        ++frame;
        if (cooldown > 0) --cooldown;
        const int cur = app.screens().current_id();
        if (cur != last_screen) {
            last_screen = cur;
            std::fprintf(stdout, "[qverify] f%d screen -> %d\n", frame, cur);
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
            // Frame-anchored dialog census (one line per distinct top dialog):
            // brackets the engine's `[quest] dialog queued`/dismiss logs so the
            // before/after chain order is provable.
            const std::string dkey = d.type + "|" + d.title;
            if (dkey != last_dialog_key) {
                last_dialog_key = dkey;
                std::fprintf(stdout, "[qverify] f%d dialog up: %s\n", frame,
                             dkey.c_str());
                std::fflush(stdout);
            }
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
                    tap(app, 184, 92, 90);  // `gk` header; wait out the 18-step run
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
                        tap(app, 184, 92, 90);  // header; wait out the run
                    }
                    nav_toggle = !nav_toggle;
                }
            }
            return;
        }
    }
};

} // namespace

// RULE 0: the hard watchdog. Every driver/tour/probe run must be incapable of
// blocking forever on a modal/settle/wait loop, so a detached thread force-
// exits the process after `seconds` with a printed reason. Only installed for
// driver modes (never the plain interactive launch). `--watchdog <sec>`
// overrides the default; `<= 0` disables it.
constexpr int kDefaultWatchdogSeconds = 900;
static void install_watchdog(int seconds) {
    if (seconds <= 0) {
        return;
    }
    std::thread([seconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stdout, "[watchdog] timeout after %ds\n", seconds);
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(7);
    }).detach();
}

int main(int argc, char** argv) {
    std::string res_root = "reference/www/res";
    std::string save_path = "reference/saves/save.xml";
    int headless = 0;
    bool auto_click = false;
    bool headless_loop = false;
    bool flow_verify = false;  // --flow-verify: the repaired map/menu/ladder flows
    bool za_nav_verify = false;  // --za-nav-verify: the per-screen `za` open/close proof
    bool ui_tour = false;
    bool fidelity_tour = false;
    bool quest_verify = false;  // --quest-verify: interactive action check
    bool quest_verify_buy = false;  // --quest-verify-buy: seeded STEP_BUY_ITEM
    bool changetab_probe = false;   // --changetab-probe: synthetic `Hn` action
    bool quest_query_probe = false;  // --quest-query-probe: Foreach query proof
    bool map_button_probe = false;   // --map-button-probe: Vb map-button proof
    // --quest-action-probe: fire the shipped ToggleItems/Discount actions
    // through the engine path and log the save/price before/after.
    bool quest_action_probe = false;
    bool dialog_verify = false;     // --dialog-verify: headless dialog harness
    bool observe_dialogs = false;   // --observe-dialogs: keep the queue observable
    // --tutorial-real-verify: boot the REAL path (NO `fresh_tutorial` arm, NO
    // tutorial-END seed), hidden, and assert the engine's own `_$StoryTutorialStep`
    // default starts StoryTutorialWelcome + the lesson gate serializes to the modal.
    bool tutorial_real_verify = false;
    bool replay_mode = false;
    bool verify_input = false;
    bool fx_probe = false;  // --fx-probe: targeted FX-bus self-check (no OS input)
    // --input-tape [js|desktop]: the scripted key/pointer tape fed through the
    // REAL input consumer (FightScreen::on_key, the function App::poll_input
    // calls for every GLFW key edge). `js` forces the byte-exact `Af.oUa`
    // table (set_desktop_key_aliases(false)); `desktop` (default) is the
    // shipped desktop map. Prints per key: GLFW code -> key_type -> selected
    // move -> the per-frame player move name (the move -> idle flip), then the
    // dojo hub section.
    bool input_tape = false;
    bool input_tape_js_table = false;
    // `--verify-place <me_x> <enemy_x>`: boot the direct fight, park the two
    // fighters at explicit world x, and run the mirror / throw / interval
    // proof probes. The caller must pass the enemy on the player's LEFT
    // (`me_x > enemy_x`) and a gap <= 100 (the throw `Distance Max="100"`).
    bool verify_place = false;
    float place_me_x = 0.0f;
    float place_enemy_x = 0.0f;
    std::string replay_file = "reference/traces/recorded_inputs.txt";
    bool debug_ui = false;
    bool capture_fight = false;
    bool capture_idle_fight = false;  // --capture-idle-fight-at N: boot direct + no input, capture at fight frame N
    bool round_log = false;  // --round-log: per-frame scene_visible/x/camera around a round transition
    // --boss-hit-probe: boot a BOSS fight, drive the player into range via the
    // internal `player_input` path (no OS input), land an attack, and log the
    // boss's per-frame hit reaction (move/ragdoll/world_x).
    bool boss_hit_probe = false;
    // --boss-loss-probe: boot a BOSS fight and drive a COMPETENT scripted
    // player (approach + punch/kick via `inject_game_key`, NO OS input) until
    // the battle ends, so the per-second `[fight]` HP log, the `[hit]` damage
    // lines and the `[fight] summary` decide whether the JS would let the
    // player win or the port mis-resolves the loss.
    bool boss_loss_probe = false;
    // --d3-probe: force a named move on the player and print the attacker's
    // part set the OLD way (yD(4) only) vs the NEW way (the xqb union over
    // every active type-4 interval), then the hit_test result. Proves the D3
    // union fix on a shipped two-interval pair (default FansSuperSlash).
    bool d3_probe = false;
    std::string d3_probe_move = "FansSuperSlash";
    int d3_probe_frame = 36;
    float d3_probe_dist = 500.0f;  // enemy x offset from the player
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
    // `--loadout <WeaponSubType>`: pins the player's EQUIPPED Weapon slot for
    // the direct-boot paths (`--fight` / `--verify-input` / `--input-tape`).
    // Empty = resolve from the save (the shipped JS behaviour: `owned_items`
    // reads the save's equipped slots, each row a Type/SubType/Name triple).
    // A non-empty value seeds the same triple set explicitly, so a run is
    // deterministic regardless of the ambient save — required by the probe
    // harness, whose expectations are authored per loadout.
    std::string loadout;
    // RULE 0: the hard watchdog bound (seconds). See install_watchdog().
    int watchdog_secs = kDefaultWatchdogSeconds;

    // Positional args (res_root, save_path) are assigned by slot, not by
    // value: a user passing the default res_root explicitly used to collide
    // with the value-dependent check and overwrite res_root with the second
    // positional (breaking asset loading). Count the positionals instead.
    int positional = 0;
    // The safety gate (see the `driver_mode` block after the loop): the window
    // is VISIBLE only for a plain interactive launch. ANY flag at all — known
    // or unknown — forces the hidden + watchdog driver path, so a forgotten or
    // typo'd flag can never open a visible, hanging window. `--windowed`
    // forces the visible window; `--hidden` forces hidden (and wins).
    bool saw_flag = false;
    bool force_windowed = false;
    bool force_hidden = false;
    bool unknown_flag = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // Any `-`-prefixed token is a flag. A flag's *value* is consumed with
        // `argv[++i]` and never reaches this line, so this cannot misfire on a
        // value; only a genuine flag token flips the gate.
        if (arg.size() > 1 && arg[0] == '-') saw_flag = true;
        if (arg == "--headless" && i + 1 < argc) {
            headless = std::atoi(argv[++i]);
        } else if (arg == "--watchdog" && i + 1 < argc) {
            watchdog_secs = std::atoi(argv[++i]);
        } else if (arg == "--autoclick") {
            auto_click = true;
        } else if (arg == "--flow-verify") {
            flow_verify = true;
        } else if (arg == "--za-nav-verify") {
            za_nav_verify = true;
        } else if (arg == "--headless-loop") {
            headless_loop = true;
        } else if (arg == "--ui-tour") {
            ui_tour = true;
        } else if (arg == "--tutorial-real-verify") {
            tutorial_real_verify = true;
        } else if (arg == "--fidelity-tour") {
            fidelity_tour = true;
        } else if (arg == "--quest-verify") {
            quest_verify = true;
        } else if (arg == "--changetab-probe") {
            changetab_probe = true;
        } else if (arg == "--quest-query-probe") {
            quest_query_probe = true;
        } else if (arg == "--map-button-probe") {
            map_button_probe = true;
        } else if (arg == "--quest-action-probe") {
            quest_action_probe = true;
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
        } else if (arg == "--fx-probe") {
            // Targeted FX-bus self-check (no OS input, no sim): exercises the
            // three kinds end to end — spawn (`Yl`/`lwb`), the follow update
            // (`bv.update`), a draw-list/frame read (`cv.WL` state),
            // StopEffect (`gm`/`Dwb`/`LNa` destroy) and StopFollowEffect
            // (`hm`/`Hwb`/`Gwb` `Yla` latch, animation runs to completion).
            fx_probe = true;
            sf2::scene::MagicEffects fx;
            fx.add_default_descs();
            sf2::scene::EffectAnchor anchors[2] = {
                {100.0f, -10.0f, 1}, {-100.0f, -8.0f, -1}};
            const bool s0 = fx.spawn("hit_flash", anchors[0].x, anchors[0].y, 1,
                                     0, /*follow=*/true);
            const bool s1 = fx.spawn("hit_flash", anchors[1].x, anchors[1].y, -1,
                                     1, /*follow=*/true);
            const std::size_t spawned = fx.live().size();
            // Follow update: move owner 0 and tick once.
            anchors[0].x = 250.0f;
            anchors[0].y = -30.0f;
            fx.update(1.0f, anchors, 2);
            bool follow_ok = false, draw_ok = false;
            for (const sf2::scene::MagicInstance& in : fx.live()) {
                if (in.owner == 0 && std::fabs(in.x - 250.0f) < 0.01f) {
                    follow_ok = true;
                }
                if (!fx.frame_for(in).empty()) draw_ok = true;
            }
            // StopEffect: destroy the owner-0 instance only.
            fx.stop("hit_flash", 0);
            bool destroyed = true;
            for (const sf2::scene::MagicInstance& in : fx.live()) {
                if (in.owner == 0) destroyed = false;
            }
            // StopFollowEffect: latch the owner-1 instance (keeps animating).
            fx.stop_follow("hit_flash", 1);
            bool latch_alive = false, latch_detached = false;
            for (const sf2::scene::MagicInstance& in : fx.live()) {
                if (in.owner == 1) {
                    latch_alive = true;
                    latch_detached = in.detached;
                }
            }
            // The latched one-shot (29 frames) must run out and be removed.
            for (int f = 0; f < 40; ++f) fx.update(1.0f, anchors, 2);
            const bool emptied = fx.empty();
            // --- real shipped descriptor check -----------------------------
            // Load the packed `magic/*.json` registry (the 79
            // `res/magic/mgc_*.json` atlases inside the zstd archive) and
            // build one real `<Effect Sequence="mgc_effect_fall">` descriptor.
            // Asserts: the atlas resolves (frame count > 0) and a real frame
            // name is produced by the `ni` cursor.
            std::map<std::string, std::vector<std::string>> atlas_frames;
            std::size_t magic_atlas_count = 0, magic_frame_count = 0;
            try {
                std::ifstream in(res_root + "/magic_ktx.72456186.dat",
                                 std::ios::binary);
                std::vector<std::uint8_t> cz((std::istreambuf_iterator<char>(in)),
                                             std::istreambuf_iterator<char>());
                const std::vector<std::uint8_t> dc = sf2::data::zstd_decompress(cz);
                const std::vector<sf2::data::archive_entry> ar =
                    sf2::data::xml_archive_parse(dc.data(), dc.size());
                for (const auto& e : ar) {
                    if (e.name.size() <= 5 ||
                        e.name.compare(e.name.size() - 5, 5, ".json") != 0) {
                        continue;
                    }
                    const std::string stem = e.name.substr(0, e.name.size() - 5);
                    const std::string leaf =
                        stem.substr(stem.find_last_of('/') + 1);
                    const sf2::data::atlas a =
                        sf2::data::atlas_parse(e.data.data(), e.data.size());
                    std::vector<std::string> nm;
                    nm.reserve(a.frames.size());
                    for (const auto& fr : a.frames) nm.push_back(fr.name);
                    magic_frame_count += nm.size();
                    atlas_frames[leaf] = std::move(nm);
                    ++magic_atlas_count;
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "[fxprobe] magic archive load failed: %s\n",
                             ex.what());
            }
            sf2::scene::set_magic_atlas_frames(atlas_frames);
            sf2::scene::MagicEffects real_fx;
            bool real_spawn = false;
            std::string real_frame;
            const auto fit = atlas_frames.find("mgc_effect_fall");
            if (fit != atlas_frames.end() && !fit->second.empty()) {
                sf2::scene::MagicEffectDesc d;
                d.name = "mgc_effect_fall";
                d.frames = fit->second;   // atlas order
                d.draw_source_size = true;  // TexturePacker sourceSize path
                real_fx.load({d});
                real_spawn = real_fx.spawn("mgc_effect_fall", 10.0f, -5.0f, 1, 0, false);
                if (real_spawn) {
                    const std::vector<sf2::scene::MagicInstance> live = real_fx.live();
                    if (!live.empty()) real_frame = real_fx.frame_for(live.front());
                }
            }
            const bool real_ok =
                real_spawn && !real_frame.empty() && magic_frame_count > 0;
            std::fprintf(stdout,
                         "[fxprobe] magic atlases=%zu frames=%zu real_spawn=%d "
                         "real_frame=%s -> %s\n",
                         magic_atlas_count, magic_frame_count, real_spawn ? 1 : 0,
                         real_frame.empty() ? "<none>" : real_frame.c_str(),
                         real_ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            const bool pass = s0 && s1 && spawned == 2 && follow_ok && draw_ok &&
                              destroyed && latch_alive && latch_detached && emptied &&
                              real_ok;
            std::fprintf(stdout,
                         "[fxprobe] spawn=%zu/%d follow=%d draw=%d "
                         "stopeffect_destroyed=%d stopfollow_alive=%d "
                         "detached=%d finished_empty=%d -> %s\n",
                         spawned, (s0 && s1) ? 1 : 0, follow_ok ? 1 : 0,
                         draw_ok ? 1 : 0, destroyed ? 1 : 0, latch_alive ? 1 : 0,
                         latch_detached ? 1 : 0, emptied ? 1 : 0,
                         pass ? "PASS" : "FAIL");
            (void)fx_probe;
            return 0;
        } else if (arg == "--loadout" && i + 1 < argc) {
            loadout = argv[++i];
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
        } else if (arg == "--verify-place" && i + 2 < argc) {
            verify_place = true;
            place_me_x = static_cast<float>(std::atof(argv[++i]));
            place_enemy_x = static_cast<float>(std::atof(argv[++i]));
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
        } else if (arg == "--round-log") {
            // Per-frame round-transition log (scene_visible + fighter x +
            // camera x); no input, no capture, no OS input.
            round_log = true;
        } else if (arg == "--boss-hit-probe") {
            boss_hit_probe = true;
        } else if (arg == "--boss-loss-probe") {
            boss_loss_probe = true;
        } else if (arg == "--d3-probe") {
            d3_probe = true;
        } else if (arg == "--d3-probe-move" && i + 1 < argc) {
            d3_probe_move = argv[++i];
        } else if (arg == "--d3-probe-frame" && i + 1 < argc) {
            d3_probe_frame = std::atoi(argv[++i]);
        } else if (arg == "--d3-probe-dist" && i + 1 < argc) {
            d3_probe_dist = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--hit-audit") {
            // Probe: arm the per-frame `[hitaudit]` hit-row log (the
            // attacker's active Window / active parts / overlap / `<Hit>`
            // window / outcome). Simulation-neutral.
            sf2::scene::set_hit_audit_global(true);
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
        } else if (arg == "--windowed") {
            // Explicit override: a human wants the real, visible window.
            force_windowed = true;
        } else if (arg == "--hidden") {
            // Explicit override: force the hidden + watchdog driver path.
            force_hidden = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg.size() > 1 && arg[0] == '-') {
            // Unknown/typo'd flag. NEVER let it fall through to the positional
            // res_root slot — that used to look like a plain launch and open a
            // visible, hanging window. Name the offender and force hidden.
            unknown_flag = true;
            std::fprintf(stderr,
                         "game: unknown flag '%s' -> forcing hidden+watchdog "
                         "mode (no window)\n",
                         arg.c_str());
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

    // RULE 0 (hidden-by-default): the window is HIDDEN for EVERY launch —
    // flagless included. The visible interactive window requires an EXPLICIT
    // `--windowed`; a flagless run (or any driver/tour/probe flag) is hidden
    // + hard-watchdogged. `--hidden` forces hidden and wins over `--windowed`.
    // So a forgotten flag, a typo'd flag, or a bare launch can never pop a
    // visible (possibly empty) window.
    const bool flagless = !saw_flag;
    bool driver_mode = true;                  // hidden unless --windowed says otherwise
    if (force_windowed) driver_mode = false;  // the ONLY way to a visible window
    if (force_hidden) driver_mode = true;     // --hidden wins over --windowed
    std::fprintf(stdout,
                 "[gate] hidden=%d flagless=%d unknown_flag=%d windowed=%d "
                 "hidden_override=%d\n",
                 driver_mode ? 1 : 0, flagless ? 1 : 0,
                 unknown_flag ? 1 : 0, force_windowed ? 1 : 0,
                 force_hidden ? 1 : 0);
    std::fflush(stdout);
    // A human launching with no flag would otherwise stare at nothing (the run
    // is hidden by default) — say so in one line, so the invisible run is never
    // a mystery. `--windowed` is the way to see the game.
    if (flagless && driver_mode) {
        std::fprintf(stdout,
                     "game: no --windowed flag -> running hidden (add --windowed to play)\n");
        std::fflush(stdout);
    }

    // Asset guard: validate the res_root BEFORE anything opens a window. An
    // asset-less/missing root used to reach App::init, which creates the GLFW
    // window first and only then finds nothing to draw — a visible (or hidden)
    // empty shell. Fail fast instead, so no window is ever created for a bad
    // root.
    {
        const std::filesystem::path rr(res_root);
        std::error_code ec;
        const bool is_dir = std::filesystem::is_directory(rr, ec);
        // The shipped res carries the packed XML archive + the default save;
        // any of these proves the root is a real game asset tree.
        const bool has_assets = std::filesystem::exists(rr / "xml.9e0b4b10.dat", ec) ||
                                std::filesystem::exists(rr / "users_default.b7da2019.xml", ec) ||
                                std::filesystem::exists(rr / "ui", ec);
        if (!is_dir || !has_assets) {
            std::fprintf(stderr,
                         "game: res_root '%s' has no game assets -> aborting before any "
                         "window is created\n",
                         res_root.c_str());
            std::fprintf(stderr,
                         "      expected the packed res tree (e.g. reference/www/res "
                         "with xml.9e0b4b10.dat)\n");
            std::fflush(stderr);
            return 1;
        }
    }

    if (driver_mode) {
        install_watchdog(watchdog_secs);
    }

    // The `--verify-input` tape + probe expectations are authored on the
    // shipped Fists loadout (`reference/tools/input_phase1.txt`), so an
    // unspecified verify run pins Fists. `--loadout <WeaponSubType>` runs the
    // same tape against another weapon (the knives probe set), so the
    // weapon-subtype derivation is exercised for both.
    if (verify_input && loadout.empty()) loadout = "Fists";

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

    // --- Save hygiene (bug 2) ----------------------------------------------
    // Every driver/tour/probe starts from a well-defined profile: the ambient
    // `save_path` (gitignored) is mutated by each run (money, items, battles,
    // fights, story step), so a gate asserting a FRESH state (flow-verify's
    // BOSS_LYNX ladder, the shop BUY/EQUIP) failed depending on the previous
    // run. Reset to the shipped template before boot; the per-mode seeds
    // (fidelity/quest-verify story step, headless-loop END+money) apply on top.
    if (driver_mode) {
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
            std::filesystem::remove(save_path);  // drop the ambient save
        } catch (const std::exception&) {
            // no ambient save is the normal first-run case
        }
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();  // template (save was removed)
            // The driver baseline emulates a POST-tutorial profile: the JS
            // `zt.Pla()` (`zi`, bundle idx 156971) finishes the story by
            // writing `kU[kU.length-1]` = END. The shipped `users_default`
            // carries `Tutorial="MOVE"`, which `zt.parse` normalizes to
            // `kU[0]` = NotStarted (a FRESH profile), so without this seed
            // every driver gate would arm the tutorial chain. The
            // fresh-tutorial modes reset it just below.
            w.set_story_step("END");
            ss.save(w);                           // well-defined baseline
            std::fprintf(stdout, "[save] driver baseline: %s -> '%s'\n", def.c_str(),
                         save_path.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[save] driver baseline failed: %s\n", e.what());
        }
    }

    // `--fidelity-tour` captures the stock FRESH tutorial beats, so start the
    // story at NotStarted BEFORE boot: the JS loads `quests.xml` with the
    // `tutorial_quests.xml` include gated on `_$StoryTutorialStep != END`
    // (L2478) and the port's `QuestEngine::ensure_loaded` short-circuits on an
    // END step (L200) — once short-circuited the chain cannot be armed, so a
    // stale completed local save would make the tutorial steps stall. The
    // oracle harness seeds the same fresh state.
    if (fidelity_tour || quest_verify || observe_dialogs || tutorial_real_verify) {
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

    // `--headless-loop`: reset the run save from the shipped template so the
    // 13-step gate is deterministic regardless of the ambient (gitignored)
    // save. Bug (2): the ambient save is mutated by every gate, so a prior run
    // already owns WEAPON_KNIVES and the shop TRY/price plate toggles the
    // equipped weapon instead of buying — the BUY/EQUIP gate then FAILs
    // (`owns_knives=1 weapon=Fists`). The well-defined baseline is the shipped
    // `users_default.b7da2019.xml` with `_$StoryTutorialStep=END` (the loop's
    // steps assume the clean post-tutorial hub, not the StoryTutorial chain)
    // and the 200-coin purchase float (`users_default` ships Money="0", so the
    // JS `Pa.iwa` price gate would otherwise void the buy).
    if (headless_loop) {
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
            std::filesystem::remove(save_path);  // drop the ambient save
        } catch (const std::exception&) {
            // no ambient save is the normal first-run case
        }
        try {
            sf2::app::SaveSystem ss(save_path, def);
            sf2::app::WarriorSave w = ss.load();  // template (save was removed)
            w.set_story_step("END");
            w.money = 200;
            ss.save(w);  // well-defined baseline on disk
            std::fprintf(stdout,
                         "[loop] seeded baseline save '%s' from %s (money=200, tutorial=END)\n",
                         save_path.c_str(), def.c_str());
            std::fflush(stdout);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[loop] baseline seed failed: %s\n", e.what());
        }
    }

    sf2::app::App app;
    if (!app.init(res_root, save_path, std::string(), /*hidden=*/driver_mode)) {
        std::fprintf(stderr, "game: app init failed\n");
        return 1;
    }
    std::fprintf(stdout, "[game] booted: window %dx%d, save '%s'\n", app.view_w(), app.view_h(),
                 save_path.c_str());

    // JS `sc` ctor `ckb` (L113759): `<CurrentUser><Sounds>/<Sound|Music>@Mute`
    // restore the bus mutes at save-parse. Apply right after boot so the menu
    // music/SFX obey the persisted state (`ta.WT`/`ta.VT`, L1265).
    {
        sf2::audio::AudioEngine& boot_au = sf2::audio::AudioEngine::instance();
        try {
            const sf2::app::WarriorSave boot_w = app.save().load();
            if (boot_au.sfx_muted() != boot_w.sound_muted) boot_au.set_sfx_muted(boot_w.sound_muted);
            if (boot_au.music_muted() != boot_w.music_muted) boot_au.set_music_muted(boot_w.music_muted);
        } catch (const std::exception&) {
        }
    }

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
    } else if (tutorial_real_verify) {
        // --- REAL-PATH tutorial gate (permanent, NO harness arm) -------------
        // Boots the way the REAL app does: NO `fresh_tutorial` arm and NO
        // tutorial-END seed — only the fresh save. The shipped default's
        // `Tutorial="MOVE"` is not a valid step, so the engine's own
        // `resolve_token` falls back to `kU[0]` = NotStarted (`zt.parse`). This
        // proves the ENGINE default starts `StoryTutorialWelcome` and that the
        // lesson gate (`Do`/`Eo`) serializes the chain to the sensei modal with
        // no flag. Live app (headless_frames_ == 0 -> no silent dialog drain),
        // HIDDEN window (RULE 0), no OS input.
        glfwHideWindow(app.renderer().window());
        app.set_auto_attack(false);
        app.set_headless_frames(0);
        std::fprintf(stdout, "[tutreal] engine live: no fresh_tutorial, no END seed\n");
        std::fflush(stdout);
        bool saw_welcome = false, saw_beat1 = false, saw_beat2 = false, saw_sensei = false;
        std::string sensei_title;
        int last_beat = -1;
        for (int f = 0; f < 4000; ++f) {
            if (glfwWindowShouldClose(app.renderer().window())) break;
            app.run_one_frame();
            sf2::app::QuestEngine& q = app.quest_engine();
            if (!saw_welcome && q.dialog_count() > 0) saw_welcome = true;
            const int beat = q.tutorial_gate_beat();
            if (beat != last_beat) {
                std::fprintf(stdout, "[tutreal] f=%d lesson gate beat=%d\n", f, beat);
                std::fflush(stdout);
                last_beat = beat;
            }
            if (beat == 1) saw_beat1 = true;
            if (beat == 2) saw_beat2 = true;
            if (const sf2::app::EngineDialog* m = q.modal_top()) {
                if (m->title == "characterSensei") {
                    saw_sensei = true;
                    sensei_title = m->title;
                }
            }
            if (saw_beat1 && saw_beat2 && saw_sensei) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));  // pace 60 Hz
        }
        const bool ok = saw_beat1 && saw_beat2 && saw_sensei;
        std::fprintf(stdout, "[tutreal] %s welcome=%d beat1=%d beat2=%d sensei=%d title=%s\n",
                     ok ? "PASS" : "FAIL", saw_welcome ? 1 : 0, saw_beat1 ? 1 : 0,
                     saw_beat2 ? 1 : 0, saw_sensei ? 1 : 0, sensei_title.c_str());
        std::fflush(stdout);
        app.shutdown();
        return ok ? 0 : 1;
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
        // --- task 3 (win marking) assertion --------------------------------
        // The live fight can LOSE to Shin (the BeginnerCheat bot), so the
        // shipped win path is asserted deterministically by replaying the
        // exact `FightEnd` journal a win publishes. The data: quests.xml
        // L260-263 `FirstGuardBeaten` — `_$Fight==ZONE_1|BOSS_LYNX|1` AND
        // `_$FightResult==Win` -> `SetStoryTutorialStep=LEARN_PERK` +
        // `ClearQuestQueue StoryTutorialBossFight`. No OS input, no screen.
        bool ok_story_advance = false;
        if (drv.saw_shin) {
            std::string pre_step;
            try {
                pre_step = app.save().load().story_step();
            } catch (const std::exception&) {
            }
            sf2::app::QuestJournal j;
            j.fight = "ZONE_1|BOSS_LYNX|1";
            j.fight_result = "Win";
            q.note_fight(j.fight, j.fight_result);
            q.fire(app, "FightEnd", j);
            // `Actions Place="Map"` (JS `be.Gib` L1007): the set is parked
            // while the Fight screen is mounted and runs once the Map is
            // (re)entered. Leave the fight/result screen the way the shipped
            // flow does — the pop fires the ChangeTab/SceneLoaded edge the
            // gate waits on.
            for (int k = 0; k < 4 && !ok_story_advance; ++k) {
                const int c = app.screens().current_id();
                if (c != kScreenFight && c != kScreenResults) break;
                app.screens().pop();
            }
            try {
                ok_story_advance = app.save().load().story_step() == "LEARN_PERK";
            } catch (const std::exception&) {
            }
            std::fprintf(stdout, "[qverify] FirstGuardBeaten win -> step=%s (%s)\n",
                         ok_story_advance ? "LEARN_PERK" : "?", ok_story_advance ? "PASS" : "FAIL");
            // The verifier must not advance the SHARED save past the step the
            // loop/tour drivers expect, so put it back after asserting.
            try {
                sf2::app::WarriorSave w = app.save().load();
                w.set_story_step(pre_step);
                app.save().save(w);
            } catch (const std::exception&) {
            }
            std::fflush(stdout);
        }
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
                                ok_lynx && ok_shin && ok_story_advance)
                             : (ok_chain && ok_change && ok_fight);
        std::fprintf(stdout,
                     "[qverify] RESULT chain=%s ChangeScene=%s navflash=%s Shop=%s "
                     "OpenShop=%s LynxDialog=%s ShinFight=%s trainingFight=%s "
                     "StoryAdvance=%s -> %s\n",
                     ok_chain ? "PASS" : "FAIL", ok_change ? "PASS" : "FAIL",
                     ok_flash ? "PASS" : "FAIL", ok_shop ? "PASS" : "FAIL",
                     ok_open ? "PASS" : "FAIL", ok_lynx ? "PASS" : "FAIL",
                     ok_shin ? "PASS" : "FAIL", ok_fight ? "PASS" : "FAIL",
                     ok_story_advance ? "PASS" : "FAIL",
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
    } else if (quest_query_probe) {
        // --- `--quest-query-probe`: the query engine's Foreach proof ---------
        // Runs the SHIPPED sub-quest `FindLastAvailableFight` (utils.xml) via
        // a real `<Foreach Type="Battles" Name="FindLastAvailableFight"
        // OnlyActiveBattles="1"/>` action. Its conditions read
        // `?Battle[_$Iterator].Available/Name` and its action writes
        // `CurrentZone` from `?Battle[_$Iterator].Zone` — all previously logged
        // UNKNOWN. A battle record is seeded into a COPY of the save so the
        // `Available` bit can be true; the original save is restored after.
        // Hidden window + RULE 0 watchdog (driver_mode). NO OS input.
        glfwHideWindow(app.renderer().window());
        sf2::app::WarriorSave original;
        bool have_original = false;
        try {
            original = app.save().load();
            have_original = true;
            // Fight-record round-trip proof (`il` L141476 / `?Fight.*` L498367).
            // BEFORE: the shipped profile has no `<Fight>` for this triple.
            {
                const sf2::app::QuestJournal bj;
                const char* const fe[] = {
                    "?Fight[ZONE_1|BOSS_LYNX|1].Level",
                    "?Fight[ZONE_1|BOSS_LYNX|1].LossCount",
                    "?Fight[ZONE_1|BOSS_LYNX|1].Timestamp",
                    "?Fight[ZONE_1|BOSS_LYNX|1].WinCount",
                };
                for (const char* e : fe) {
                    std::fprintf(stdout, "[qquery] fight BEFORE %-38s = '%s'\n", e,
                                 app.quest_engine().resolve_for_test(app, e, bj).c_str());
                }
            }
            sf2::app::WarriorSave seeded = original;
            seeded.battle_unlock("ZONE_1", "Survival");  // make one Available
            {
                // Seed a `<Fight IDS="ZONE_1|BOSS_LYNX|1">` with the JS `il`
                // ctor attrs so the `?Fight.*` fields resolve from the save.
                sf2::app::WarriorSave::FightWins& fr =
                    seeded.fight_record_or_create("ZONE_1|BOSS_LYNX|1");
                fr.wins = 2;
                fr.losses = 1;
                fr.level = 7;
                fr.time_left = 1234;
                fr.randomize_time_left = 9;
                fr.completed_time = 55;
            }
            seeded.variables.erase("CurrentZone");       // prove the write
            seeded.variables.erase("_CurrentZone");
            app.save().save(seeded);
            const sf2::app::WarriorSave chk = app.save().load();
            std::fprintf(stdout,
                         "[qquery] seeded: battles=%zu records=%zu hasSurvival=%d\n",
                         chk.battles.size(), chk.battle_records.size(),
                         chk.has_battle("Survival") ? 1 : 0);
            {
                const sf2::app::QuestJournal aj;
                const char* const fe[] = {
                    "?Fight[ZONE_1|BOSS_LYNX|1].Level",
                    "?Fight[ZONE_1|BOSS_LYNX|1].LossCount",
                    "?Fight[ZONE_1|BOSS_LYNX|1].Timestamp",
                    "?Fight[ZONE_1|BOSS_LYNX|1].WinCount",
                };
                for (const char* e : fe) {
                    std::fprintf(stdout, "[qquery] fight AFTER  %-38s = '%s'\n", e,
                                 app.quest_engine().resolve_for_test(app, e, aj).c_str());
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stdout, "[qquery] seed failed: %s\n", e.what());
        }
        const auto has_battle_unknown = [&]() {
            for (const std::string& s : app.quest_engine().unanswerable_queries()) {
                if (s.find("?Battle[_$Iterator]") != std::string::npos) return true;
            }
            return false;
        };
        const bool battle_unknown_before = has_battle_unknown();
        sf2::app::QuestAction act;
        act.tag = "Foreach";
        act.attrs["Type"] = "Battles";
        act.attrs["Name"] = "FindLastAvailableFight";
        act.attrs["OnlyActiveBattles"] = "1";
        sf2::app::QuestJournal j;
        const std::size_t before_matches = app.quest_engine().foreach_matches();
        app.quest_engine().run_action_probe(app, {act}, j);
        const bool fired = app.quest_engine().foreach_matches() > before_matches;
        std::string after_zone;
        try {
            const sf2::app::WarriorSave after = app.save().load();
            const auto it = after.variables.find("CurrentZone");
            if (it != after.variables.end()) after_zone = it->second;
        } catch (const std::exception&) {
        }
        // Read-back through the engine's own `f5a` path (Local -> Global ->
        // Users `rv`); after the restore below the profile is pristine again,
        // so this must run first.
        std::string after_readback;
        {
            const sf2::app::QuestJournal qj;
            after_readback =
                app.quest_engine().resolve_for_test(app, "_CurrentZone", qj);
        }
        const bool battle_unknown_after = has_battle_unknown();
        // Per-query resolution table (the AFTER values; UNKNOWN -> "").
        {
            const sf2::app::QuestJournal qj;
            const char* const exprs[] = {
                "?Purchase[WEAPON_KNIVES].Type",
                "?Purchase[WEAPON_KNIVES].Name",
                "?Purchase[WEAPON_KNIVES].UpgradeLevel",
                "?Item[WEAPON_KNIVES].Quantity",
                "?Item[WEAPON_KNIVES].SubType",
                "?Item[WEAPON_KNIVES].Type",
                "?Item[WEAPON_KNIVES].Price",
                "?Item[WEAPON_KNIVES].Level",
                "?Item[WEAPON_KNIVES].Availability",
                "?Item[WEAPON_KNIVES].BonusPrice",
                "?Battle[ZONE_1|BOSS_LYNX].Available",
                "?Battle[ZONE_1|BOSS_LYNX].Name",
                "?Battle[ZONE_1|BOSS_LYNX].Zone",
                "?Battle[ZONE_1|BOSS_LYNX].Type",        // nYa L982 -> BOSSES
                "?Fight[ZONE_1|BOSS_LYNX|BOSS_LYNX].Type",  // X3a L972 -> BOSSES
                "?Battle[BOSS_LYNX].Available",
                "?Sum[?Multi[100,?Player[].Level],30]",
                "?Multi[7,6]",
                "?Sub[10,3]",
                "?NDiv[10,3]",
                "?Mod[10,3]",
                "?UniformIntRandom[1,1]",
                // `QNa` L956-957: Concat (c=1) vs Slice (c=2, INCLUSIVE
                // [start,end]; <3 args returns the first arg untouched).
                "?Concat[A,B,C]",
                "?Concat[ITEM|,?Sum[?Multi[100,?Player[].Level],230]]",
                "?Slice[ABCDEF,1,3]",
                "?Slice[ABCDEF,2]",
                // The named remainder fixes (JS cite per row):
                "?Item[WEAPON_KNIVES].Level",         // cdb L977 (has Level)
                "?Item[Pile_Gems].Level",             // cdb L977 -> "null"
                "?Purchase[WEAPON_KNIVES].PaidItem",  // IJa L980 -> "None"
                "?Purchase[WEAPON_KNIVES].Timeout",   // IJa L980 -> 0
                "_$BestAcquiredArmorLevel",           // Bj L960 -> "0"
                "_$BestAcquiredWeaponLevel",          // Bj L960 -> "0"
                // --- Slice: DataVersion/VersionController, SysInfo extras,
                // Item Recieve*, ItemsOfType, Pack, Player Bonus/Power/CoinIcon,
                // Enchantment (JS cites in the handlers).
                "?DataVersion().DataVersion",         // yzb L982 -> "0"
                "?DataVersion().Major",               // yzb L982 -> "0"
                "?DataVersion().Minor",               // yzb L982 -> "13"
                "?DataVersion().Production",          // yzb L982 -> "1"
                "?DataVersion().Version",             // yzb L982 -> "1.0.13"
                "?VersionController().Production",    // Czb L983 -> "1"
                "?VersionController().Minor",         // Czb L983 -> "13"
                "?SysInfo().OsName",                  // $wb L983 -> "Windows"
                "?SysInfo().NBO",                     // $wb L983 -> "1"
                "?SysInfo().StarterPacksAvailable",   // $wb L987 -> "0"
                "?Item[WEAPON_KNIVES].RecieveGold",   // cdb L978 -> 0
                "?Item[Casket_Gems].RecieveBonus",    // cdb L978 -> 1000
                "?ItemsOfType[Weapon].Quantity",      // edb L981 -> owned count
                "?Pack[ZONE_1].IsAvailable",          // zib L979 -> "0"
                "?Player().Bonus",                    // blb L973 -> w.bonus
                "?Player().Power",                    // blb L975 -> w.power
                "?Player().CoinIcon",                 // blb L973 -> "gold"
                "?Enchantment[ENCH_A|RECIPE_B].Item",       // z3a L967 -> ENCH_A
                "?Enchantment[ENCH_A|RECIPE_B].Recipe",     // z3a L967 -> RECIPE_B
                "?Enchantment[ENCH_A|RECIPE_B|30].DeliveryTime",  // z3a L967 -> 30
                "?Enchantment[ONLYONE].Item",         // z3a L967 -> UNKNOWN (<2 parts)
                // --- shop offers (`p.Cw.It`, `hh`/`pl` L180...): the model +
                // the `?Offer(s)` queries (JS `wfb` L508754 / `yfb` L509394).
                // The 30 shipped defs are list.xml `SubType="DailyOffer"` items.
                "?Offer[DailyOffer_3_4].Exists",          // wfb -> "1"
                "?Offer[DailyOffer_3_4].State",           // -> "NotStarted"
                "?Offer[DailyOffer_3_4].Title",           // Text -> "dailyOfferTitel"
                "?Offer[DailyOffer_3_4].Description",     // -> "dailyOfferDescr"
                "?Offer[DailyOffer_3_4].Image",           // Image attr -> ""
                "?Offer[DailyOffer_3_4].ProfitImage",     // -> ""
                "?Offer[DailyOffer_3_4].RealPrice",       // -> Ela markup
                "?Item[Casket_Gems].RealPrice",           // -> "" (no xr)
                "?Offer[DailyOffer_3_4].FocusOnBuy",      // -> WEAPON_VAL20_SAI
                "?Offer[DailyOffer_3_4].ShowLastChance",  // -> "0"
                "?Offer[DailyOffer_3_4].Type",            // lp() -> "1"
                "?Offer[DailyOffer_3_4].TimerActive",     // QEa() -> "0"
                "?Offer[DailyOffer_3_4].TimerName",       // oJ() -> OfferTimer_DailyOffer_3_4
                "?Offer[DailyOffer_3_4].AllItemsRecieved",// rc.UH -> "0"
                "?Offer[NOPE].Exists",                    // -> "0"
                "?Offers[].First",                        // -> DailyOffer_3_4
                "?Offers[].FirstNotStarted",              // -> DailyOffer_3_4
                "?Offers[].FirstJustStarted",             // -> "0" (none start)
                "?Offers[].FirstLastChance",              // -> "0"
                "?Offers[].FirstPurchased",               // -> "0"
            };
            for (const char* e : exprs) {
                const std::string v =
                    app.quest_engine().resolve_for_test(app, e, qj);
                std::fprintf(stdout, "[qquery]   %-42s = '%s'\n", e, v.c_str());
            }
            // --- shop-offer state machine (`a_a` -> `En` -> `tlb`) ---------
            // `CheckOffersStart` (`Jn` L531140) runs `a_a`; every shipped offer
            // carries `<Equal Value1="?Pack[CLANS].IsAvailable" Value2="1"/>`
            // (list.xml) which is "0" in this build -> none starts (`Ti` false).
            {
                sf2::app::QuestAction oa;
                oa.tag = "CheckOffersStart";
                sf2::app::QuestJournal oj;
                const std::size_t before = app.quest_engine().unanswerable_count();
                app.quest_engine().run_action_probe(app, {oa}, oj);
                std::fprintf(stdout,
                             "[qquery] CheckOffersStart ran (unanswerable %zu->%zu)\n",
                             before, app.quest_engine().unanswerable_count());
                const std::string js =
                    app.quest_engine().resolve_for_test(app, "?Offers[].FirstJustStarted", oj);
                std::fprintf(stdout, "[qquery]   after a_a FirstJustStarted='%s'\n", js.c_str());
            }
            // `En` L528761 (`ChangeOfferState`) then re-read the `?Offers` list.
            app.quest_engine().offer_change_state(app, "DailyOffer_3_4", "JustStarted");
            {
                const sf2::app::QuestJournal oj;
                const std::string a =
                    app.quest_engine().resolve_for_test(app, "?Offers[].FirstJustStarted", oj);
                const std::string b =
                    app.quest_engine().resolve_for_test(app, "?Offer[DailyOffer_3_4].State", oj);
                std::fprintf(stdout, "[qquery]   En JustStarted: FirstJustStarted='%s' State='%s'\n",
                             a.c_str(), b.c_str());
            }
            // `tlb` L180xxx (`offer_purchase`): state=Purchased + `n4`.
            app.quest_engine().offer_purchase(app, "DailyOffer_3_4");
            {
                const sf2::app::QuestJournal oj;
                const std::string a =
                    app.quest_engine().resolve_for_test(app, "?Offers[].FirstPurchased", oj);
                const std::string b =
                    app.quest_engine().resolve_for_test(app, "?Offer[DailyOffer_3_4].State", oj);
                std::fprintf(stdout, "[qquery]   tlb Purchased: FirstPurchased='%s' State='%s'\n",
                             a.c_str(), b.c_str());
            }
            // The purchase journal (`Pa.Wz` L1234 / `Pa.Bv` L1211): the tokens
            // the shipped `<Purchase/>`/`<PurchaseUnsuccessful/>` quests read,
            // incl. the PAREN arg form (`Bj.vNa` L965 strips `(`/`)`).
            sf2::app::QuestJournal pj;
            pj.item = "WEAPON_KNIVES";
            pj.purchase_failure = "Coins";
            const char* const pexprs[] = {
                "_$Purchase",                             // Bj L963 -> name
                "_$PurchaseUnsuccessful",                 // Bj v8a L989 -> name|reason
                "?Purchase(_$Purchase).Type",             // quests.xml L1845 -> Weapon
                "?Purchase(_$Purchase).UpgradeLevel",     // L9829/L1846 -> 0
                "?Item(_$Purchase).Quantity",             // L1390 -> owned count
                "?Purchase(_$PurchaseUnsuccessful).Failure",  // L9501 -> Coins
                "?Sub(?Multi(100,?Player().Level),?Purchase(_$Purchase).UpgradeLevel)",
            };
            for (const char* e : pexprs) {
                const std::string v =
                    app.quest_engine().resolve_for_test(app, e, pj);
                std::fprintf(stdout, "[qquery]   %-42s = '%s'\n", e, v.c_str());
            }
        }
        std::fprintf(stdout, "[qquery] quests=%zu\n", app.quest_engine().quest_count());
        try {
            const sf2::app::WarriorSave chk2 = app.save().load();
            std::fprintf(stdout, "[qquery] after run: records=%zu hasSurvival=%d\n",
                         chk2.battle_records.size(), chk2.has_battle("Survival") ? 1 : 0);
        } catch (const std::exception&) {
        }
        if (have_original) {
            try {
                app.save().save(original);  // leave the profile as we found it
            } catch (const std::exception&) {
            }
        }
        std::fprintf(stdout,
                     "[qquery] BEFORE: ?Battle[_$Iterator] unanswerable=%d, "
                     "sub-quest FindLastAvailableFight fired=0 (conditions UNKNOWN)\n",
                     battle_unknown_before ? 1 : 0);
        std::fprintf(stdout,
                     "[qquery] AFTER:  ?Battle[_$Iterator] unanswerable=%d, "
                     "sub-quest fired=%d, CurrentZone='%s' readback='%s'\n",
                     battle_unknown_after ? 1 : 0, fired ? 1 : 0, after_zone.c_str(),
                     after_readback.c_str());
        // --- `Ct` timer registry proof (JS L291-292) ----------------------
        // The SHIPPED `<ActivateTimer Name="Timer_StarterPack" Value="86400"/>`
        // (quests.xml L2154) sets a 24 h deadline (`p.Dc` is in seconds);
        // `?Timer[].Value` (L988) reads the remaining seconds; the tick (`t_a`
        // L292) fires `QUEST_EVENT_TIMER_END` and removes the timer.
        bool timer_ok = false;
        {
            sf2::app::QuestAction ta;
            ta.tag = "ActivateTimer";
            ta.attrs["Name"] = "Timer_StarterPack";
            ta.attrs["Value"] = "86400";
            const sf2::app::QuestJournal tj;
            const std::size_t fires0 = app.quest_engine().timer_end_fires();
            app.quest_engine().run_action_probe(app, {ta}, tj);
            const bool present =
                app.quest_engine().timer_present("Timer_StarterPack");
            const double rem =
                app.quest_engine().timer_remaining("Timer_StarterPack");
            const std::string qv = app.quest_engine().resolve_for_test(
                app, "?Timer[Timer_StarterPack].Value", tj);
            std::fprintf(stdout,
                         "[qtimer] BEFORE: ActivateTimer Timer_StarterPack=86400 "
                         "present=%d remaining=%.0f ?Timer[].Value='%s'\n",
                         present ? 1 : 0, rem, qv.c_str());
            const std::size_t expired =
                app.quest_engine().run_timer_tick_for_test(app, 1.0e9);
            const std::size_t fires1 = app.quest_engine().timer_end_fires();
            const bool gone =
                !app.quest_engine().timer_present("Timer_StarterPack");
            std::fprintf(stdout,
                         "[qtimer] AFTER:  tick(1e9) expired=%zu fires=%zu "
                         "present=%d\n",
                         expired, fires1, gone ? 0 : 1);
            timer_ok = present && rem > 86390.0 && qv == "86400" &&
                       expired >= 1 && fires1 > fires0 && gone;
        }
        int checks = 0, passed = 0;
        const auto check = [&](bool ok, const char* what) {
            ++checks;
            if (ok) ++passed;
            std::fprintf(stdout, "[qquery] %-50s %s\n", what, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        };
        check(fired, "FindLastAvailableFight MATCHED + ran");
        check(!battle_unknown_after, "?Battle[_$Iterator].* answered (not UNKNOWN)");
        check(after_zone == "ZONE_1",
              "SetVariable Users persisted CurrentZone='ZONE_1'");
        check(after_readback == "ZONE_1",
              "_CurrentZone reads back via f5a (save round-trip)");
        {
            const sf2::app::QuestJournal cqj;
            const auto rq = [&](const char* e) {
                return app.quest_engine().resolve_for_test(app, e, cqj);
            };
            const std::string cat_sum = rq("?Sum[?Multi[100,?Player[].Level],230]");
            check(rq("?Concat[A,B,C]") == "ABC",
                  "?Concat[A,B,C] -> 'ABC' (QNa c=1)");
            check(rq("?Concat[ITEM|,?Sum[?Multi[100,?Player[].Level],230]]") ==
                      "ITEM|" + cat_sum,
                  "?Concat[ITEM|,?Sum[?Multi[100,Level],230]] -> shipped form");
            check(rq("?Slice[ABCDEF,1,3]") == "BCD",
                  "?Slice[ABCDEF,1,3] -> 'BCD' (inclusive [1,3])");
            check(rq("?Slice[ABCDEF,2]") == "ABCDEF",
                  "?Slice[ABCDEF,2] -> first arg (needs 3 args to slice)");
        }
        // Local (CH2) must NOT persist: JS `ha.F().Cja`/`q0` (L517259) is a
        // session/scoped map, never the save. The probe's run-local map is
        // discarded, so a Local write must never land in the save.
        {
            sf2::app::QuestAction la;
            la.tag = "SetVariable";
            la.attrs["Scope"] = "Local";
            la.attrs["Name"] = "ProbeLocalOnly";
            la.attrs["Value"] = "LocalVal";
            const sf2::app::QuestJournal lj;
            app.quest_engine().run_action_probe(app, {la}, lj);
        }
        bool local_in_save = false;
        try {
            const sf2::app::WarriorSave s2 = app.save().load();
            local_in_save = s2.variables.find("ProbeLocalOnly") != s2.variables.end();
        } catch (const std::exception&) {
        }
        check(!local_in_save, "SetVariable Local NOT persisted (in-memory only)");
        check(timer_ok, "shipped ActivateTimer -> ?Timer[] -> TimerEnd fired");
        const bool all = checks == passed;
        std::fprintf(stdout, "[qquery] RESULT %d/%d -> %s\n", passed, checks,
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
    } else if (map_button_probe) {
        // --- `--map-button-probe`: the `Vb` map-button manager, JS-exact ------
        // Hidden window + RULE 0 watchdog (driver_mode). NO OS input.
        // 1. `wo` L1100 (`EShowMapButton`) -> `Vb.F().Lua` (L2167) adds an `hg`
        //    to `ny` (deduped by name).
        // 2. `bo` L1086 (`EHideMapButton`) -> `Vb.F().oKa` removes it.
        // 3. `Qg` L2173 (`MapButtonPress`) -> `ta.Av=name` +
        //    `Sf("QUEST_EVENT_MAP_BUTTON_PRESS")`; the shipped `StarterPackPress`
        //    quest (quests.xml L2123-2133) matches on `_$ButtonName` (L960).
        glfwHideWindow(app.renderer().window());
        int checks = 0, passed = 0;
        const auto check = [&](bool ok, const char* what) {
            ++checks;
            if (ok) ++passed;
            std::fprintf(stdout, "[mapbtn] %-52s %s\n", what, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        };
        const auto count_named = [&](const char* name) {
            std::size_t n = 0;
            for (const sf2::app::EngineMapButton& b : app.quest_engine().map_buttons()) {
                if (b.name == name) ++n;
            }
            return n;
        };
        sf2::app::QuestAction show;
        show.tag = "ShowMapButton";
        show.attrs["Name"] = "Button_StarterPack";
        show.attrs["Image"] = "starter_pack";
        show.attrs["Timer"] = "Timer_StarterPack";
        show.attrs["ShowType"] = "Story";
        const sf2::app::QuestJournal sj;
        app.quest_engine().run_action_probe(app, {show}, sj);
        std::string img;
        for (const sf2::app::EngineMapButton& b : app.quest_engine().map_buttons()) {
            if (b.name == "Button_StarterPack") img = b.image;
        }
        std::fprintf(stdout,
                     "[mapbtn] BEFORE: Vb.ny=%zu Button_StarterPack x%zu image='%s'\n",
                     app.quest_engine().map_buttons().size(),
                     count_named("Button_StarterPack"), img.c_str());
        check(app.quest_engine().map_buttons().size() == 1 &&
                  count_named("Button_StarterPack") == 1 && img == "starter_pack",
              "ShowMapButton -> Vb.ny has the button (image resolved)");
        // Dedup (`Lua` L2167: `m.find(ny, d=>d.name==a.name)==null`).
        app.quest_engine().run_action_probe(app, {show}, sj);
        check(app.quest_engine().map_buttons().size() == 1,
              "ShowMapButton repeat is a no-op (Lua dedup by name)");
        // Press: the shipped `StarterPackPress` quest must fire.
        const std::vector<std::string> fired =
            app.quest_engine().press_map_button(app, "Button_StarterPack");
        bool press_fired = false;
        for (const std::string& f : fired) {
            if (f == "StarterPackPress") press_fired = true;
        }
        std::string bn;
        try {
            sf2::app::QuestJournal jj;
            jj.button_name = "Button_StarterPack";
            bn = app.quest_engine().resolve_for_test(app, "_$ButtonName", jj);
        } catch (const std::exception&) {
        }
        std::fprintf(stdout,
                     "[mapbtn] PRESS: fired=%zu StarterPackPress=%d _$ButtonName='%s'\n",
                     fired.size(), press_fired ? 1 : 0, bn.c_str());
        check(press_fired, "MapButtonPress fired shipped quest StarterPackPress");
        check(bn == "Button_StarterPack", "_$ButtonName reads Bj.Av (L960)");
        sf2::app::QuestAction hide;
        hide.tag = "HideMapButton";
        hide.attrs["Name"] = "Button_StarterPack";
        app.quest_engine().run_action_probe(app, {hide}, sj);
        std::fprintf(stdout, "[mapbtn] AFTER:  Vb.ny=%zu Button_StarterPack x%zu\n",
                     app.quest_engine().map_buttons().size(),
                     count_named("Button_StarterPack"));
        check(app.quest_engine().map_buttons().empty(),
              "HideMapButton -> Vb.ny empty (oKa removed it)");
        const bool all = checks == passed;
        std::fprintf(stdout, "[mapbtn] RESULT %d/%d -> %s\n", passed, checks,
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
    } else if (changetab_probe) {
        // --- `--changetab-probe`: the `Hn` ChangeTab action, JS-exact --------
        // Hidden window + RULE 0 watchdog (driver_mode). Mounts the Shop/Profile
        // (so `wa.F().Td.Tf==this.CX`), fires a synthetic `<ChangeTab .../>`
        // through the engine's own parse path (`run_action_probe`) and logs the
        // resulting screen/tab. NO OS input, no visible window.
        glfwHideWindow(app.renderer().window());
        int checks = 0, passed = 0;
        const auto check = [&](bool ok, const char* what) {
            ++checks;
            if (ok) ++passed;
            std::fprintf(stdout, "[changetab] %-44s %s\n", what, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        };
        const auto fire = [&](const char* tab, const char* focus,
                              const std::string& tab_to) {
            sf2::app::QuestAction act;
            act.tag = "ChangeTab";
            act.attrs["Tab"] = tab;
            act.attrs["Focus"] = focus;
            sf2::app::QuestJournal j;
            j.tab_from = "Default";
            j.tab_to = tab_to;
            app.quest_engine().run_action_probe(app, {act}, j);
        };
        // 1. Shop (screen 4): open on Weapon (tab 0), then `Tab="Armor"` (vj 2
        //    -> Cj.l6 1).
        app.screens().push(sf2::app::make_screen(app.screens(), sf2::app::kScreenShop));
        app.run_one_frame();
        sf2::app::ShopScreen* shop =
            dynamic_cast<sf2::app::ShopScreen*>(app.screens().top());
        const int shop_before = shop != nullptr ? shop->tab() : -1;
        fire("Armor", "Helm_Test", "");
        const int shop_armor = shop != nullptr ? shop->tab() : -1;
        std::fprintf(stdout, "[changetab] shop tab before=%d after(Armor)=%d\n",
                     shop_before, shop_armor);
        check(shop_before == 0 && shop_armor == 1, "Shop: Tab=Armor selects tab 1");
        // 2. The `_$TabTo` form (the shipped FreeReminderOnTabLeave action): the
        //    journal's YNa resolves through `vj.E0` to the owning screen.
        fire("_$TabTo", "Tapjoy", "Magic");
        const int shop_magic = shop != nullptr ? shop->tab() : -1;
        std::fprintf(stdout, "[changetab] shop tab after(_$TabTo=Magic)=%d\n",
                     shop_magic);
        check(shop_magic == 4, "Shop: Tab=_$TabTo=Magic selects tab 4");
        // 3. A bare `<ChangeTab/>` (Tab="" -> vj.E0 0 -> vj.ifa 11 != 4) is a
        //    no-op on the Shop, exactly the JS `Td.Tf==CX` guard.
        const std::size_t ta0 = app.quest_engine().tab_actions();
        fire("", "", "");
        check(app.quest_engine().tab_actions() == ta0,
              "Shop: bare ChangeTab is a no-op");
        // 4. Profile (screen 7): `Tab="Moves"` (vj 11 -> To.hOa 1) then
        //    `Tab="Perks"` (vj 10 -> To.hOa 0) via `vb.rF`/`hla`.
        app.screens().pop();
        app.screens().push(sf2::app::make_screen(app.screens(), sf2::app::kScreenProfile));
        app.run_one_frame();
        sf2::app::EquipmentScreen* prof =
            dynamic_cast<sf2::app::EquipmentScreen*>(app.screens().top());
        fire("Moves", "", "");
        const int prof_moves = prof != nullptr ? prof->tab() : -1;
        fire("Perks", "PERK_DOUBLE_SWEEP", "");
        const int prof_perks = prof != nullptr ? prof->tab() : -1;
        std::fprintf(stdout, "[changetab] profile tab Moves=%d Perks=%d\n",
                     prof_moves, prof_perks);
        check(prof_moves == 1 && prof_perks == 0, "Profile: Moves->1, Perks->0");
        // 4b. Slot 4 (BattlePass, vj 15 -> `To.hOa(15)` 4): `vb.hla`'s guard is
        //     `a != 5` (L1127569), so slot 4 is ACCEPTED and stored in `vV`
        //     even though the `cs.Tw` strip renders only 4 buttons.
        fire("BattlePass", "", "");
        const int prof_bp = prof != nullptr ? prof->tab() : -1;
        std::fprintf(stdout, "[changetab] profile tab BattlePass=%d\n", prof_bp);
        check(prof_bp == 4, "Profile: BattlePass->4 (hla a!=5)");
        // 5. Map `Tab="StoryMapStage"`: the JS `Ya.rF` is an EMPTY stub
        //    (L1096890), so the action executes but changes no map state.
        app.screens().pop();
        app.screens().push(sf2::app::make_screen(app.screens(), sf2::app::kScreenMap));
        app.run_one_frame();
        const std::size_t ta1 = app.quest_engine().tab_actions();
        fire("StoryMapStage", "", "");
        check(app.quest_engine().tab_actions() == ta1 + 1,
              "Map: StoryMapStage executes (Ya.rF empty stub)");
        const bool all = checks == passed;
        std::fprintf(stdout, "[changetab] RESULT %d/%d -> %s\n", passed, checks,
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
    } else if (quest_action_probe) {
        // --- `--quest-action-probe`: the shipped `ToggleItems`/`Discount` ----
        // Neither action fires in ANY gate (their quests need a purchase/
        // session condition), so this drives the SHIPPED actions directly
        // through the engine's own parse path (`run_action_probe`) and logs
        // the save/price before/after. Hidden window + RULE 0 watchdog
        // (driver_mode). NO OS input, no visible window.
        //
        // `ToggleItems Label="ZONE_2" Toggle="on"` is the shipped
        // quests.xml L3019 action; `ZONE_2` owns the shipped
        // `WEAPON_CRESCENT_KNIVES` (list.xml PackLabel="ZONE_2"), so both
        // halves of `p.iMa` (L112419: `p.o.vq`/`tnb` L267 + `Jrb`/`hnb`
        // L167) are observable. `Discount Item=... Percent=25 Toggle=1` is
        // the dynamic_discounts.xml L319 shape.
        glfwHideWindow(app.renderer().window());
        int checks = 0, passed = 0;
        const auto check = [&](bool ok, const char* what) {
            ++checks;
            if (ok) ++passed;
            std::fprintf(stdout, "[qa] %-58s %s\n", what, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        };
        const char* const kItem = "WEAPON_CRESCENT_KNIVES";  // PackLabel ZONE_2
        const char* const kLabel = "ZONE_2";                 // quests.xml L3019
        const auto fire_action =
            [&](const char* tag,
                const std::vector<std::pair<const char*, const char*>>& attrs) {
                sf2::app::QuestAction act;
                act.tag = tag;
                for (const auto& kv : attrs) act.attrs[kv.first] = kv.second;
                sf2::app::QuestJournal j;
                app.quest_engine().run_action_probe(app, {act}, j);
            };
        const auto lock_present = [&](const char* name) -> int {
            try {
                return app.save().load().shop_lock_contains(name) ? 1 : 0;
            } catch (const std::exception&) {
                return -1;
            }
        };
        const auto equipped_of = [&](const char* name) -> int {
            try {
                for (const auto& oi : app.save().load().items) {
                    if (oi.name == name) return oi.equipped ? 1 : 0;
                }
            } catch (const std::exception&) {
            }
            return -1;  // not owned
        };
        const auto weapon_slot = [&]() -> std::string {
            try {
                return app.save().load().weapon;
            } catch (const std::exception&) {
                return std::string();
            }
        };
        const auto base_price = [&](const char* name) -> int {
            for (const sf2::app::CatalogItem& ci : sf2::app::load_full_catalog(app)) {
                if (ci.name == name) return ci.price;
            }
            return 0;
        };
        const auto shown_price = [&](const char* name) -> int {
            return app.quest_engine().offer_price(name, base_price(name));
        };
        // Seed a COPY: own the ZONE_2 weapon so the equip half is observable.
        sf2::app::WarriorSave original;
        bool have_original = false;
        try {
            original = app.save().load();
            have_original = true;
            sf2::app::WarriorSave seeded = original;
            bool owned = false;
            for (const auto& oi : seeded.items) {
                if (oi.name == kItem) owned = true;
            }
            if (!owned) {
                sf2::app::WarriorSave::OwnedItem oi;
                oi.name = kItem;
                oi.count = 1;
                oi.equipped = false;
                seeded.items.push_back(oi);
            }
            app.save().save(seeded);
        } catch (const std::exception& e) {
            std::fprintf(stdout, "[qa] seed failed: %s\n", e.what());
        }
        const int base = base_price(kItem);
        // --- BEFORE -------------------------------------------------------
        const int lock_before = lock_present(kLabel);
        const int eq_before = equipped_of(kItem);
        const std::string slot_before = weapon_slot();
        const int price_before = shown_price(kItem);
        std::fprintf(stdout,
                     "[qa] BEFORE: lock(%s)=%d equipped(%s)=%d weapon=%s "
                     "price(base %d)=%d\n",
                     kLabel, lock_before, kItem, eq_before, slot_before.c_str(),
                     base, price_before);
        std::fflush(stdout);
        // --- ToggleItems on (the lock-grant half of `iMa`) ----------------
        fire_action("ToggleItems", {{"Label", kLabel}, {"Toggle", "on"}});
        const int lock_on = lock_present(kLabel);
        const int eq_on = equipped_of(kItem);
        const std::string slot_on = weapon_slot();
        std::fprintf(stdout,
                     "[qa] AFTER  ToggleItems %s=on : lock=%d equipped=%d "
                     "weapon=%s\n",
                     kLabel, lock_on, eq_on, slot_on.c_str());
        std::fflush(stdout);
        check(lock_before == 0 && lock_on == 1,
              "ToggleItems on: <Shop><Lock Name> granted + persisted");
        check(eq_before == 0 && eq_on == 1,
              "ToggleItems on: pack item equipped (Jrb)");
        check(slot_on == kItem, "ToggleItems on: weapon slot written");
        // --- ToggleItems on AGAIN (already locked): `vq` false -> no Jrb ---
        {
            sf2::app::WarriorSave w = app.save().load();
            for (auto& oi : w.items) {
                if (oi.name == kItem) oi.equipped = false;
            }
            w.weapon = "Fists";
            app.save().save(w);
        }
        fire_action("ToggleItems", {{"Label", kLabel}, {"Toggle", "on"}});
        const int lock_reon = lock_present(kLabel);
        const int eq_reon = equipped_of(kItem);
        std::fprintf(stdout,
                     "[qa] AFTER  ToggleItems %s=on (again) : lock=%d equipped=%d\n",
                     kLabel, lock_reon, eq_reon);
        std::fflush(stdout);
        check(lock_reon == 1 && eq_reon == 0,
              "ToggleItems on (already locked): vq false -> no re-equip");
        // --- ToggleItems off (the unlock half: `tnb` + `hnb`) -------------
        fire_action("ToggleItems", {{"Label", kLabel}, {"Toggle", "off"}});
        const int lock_off = lock_present(kLabel);
        const int eq_off = equipped_of(kItem);
        std::fprintf(stdout,
                     "[qa] AFTER  ToggleItems %s=off : lock=%d equipped=%d\n",
                     kLabel, lock_off, eq_off);
        std::fflush(stdout);
        check(lock_off == 0, "ToggleItems off: <Shop><Lock Name> removed (tnb)");
        check(eq_off == 0, "ToggleItems off: pack item unequipped (hnb)");
        // --- Discount on/off (the offer price shown/charged) --------------
        fire_action("Discount",
                    {{"Item", kItem}, {"Percent", "25"}, {"Toggle", "1"}});
        const int price_on = shown_price(kItem);
        std::fprintf(stdout,
                     "[qa] AFTER  Discount %s Percent=25 Toggle=1 : "
                     "shown/charged=%d (base %d)\n",
                     kItem, price_on, base);
        std::fflush(stdout);
        check(base > 0 && price_before == base && price_on == (base * 75) / 100,
              "Discount on: shop price shown/charged = base*0.75");
        fire_action("Discount", {{"Item", kItem}, {"Toggle", "0"}});
        const int price_off = shown_price(kItem);
        std::fprintf(stdout,
                     "[qa] AFTER  Discount %s Toggle=0 : shown/charged=%d\n",
                     kItem, price_off);
        std::fflush(stdout);
        check(price_off == base, "Discount off: offer cleared -> base price");
        // --- GiveItem (`Yn.S` 554285 -> `Pa.W$a` L631756) -------------------
        const auto owned_count = [&](const char* name) -> int {
            try {
                for (const auto& oi : app.save().load().items) {
                    if (oi.name == name) return oi.count;
                }
            } catch (const std::exception&) {
            }
            return 0;
        };
        const auto owned_upgrade = [&](const char* name) -> int {
            try {
                for (const auto& oi : app.save().load().items) {
                    if (oi.name == name) return oi.upgrade_level;
                }
            } catch (const std::exception&) {
            }
            return 0;
        };
        fire_action("GiveItem", {{"Name", "ARMOR_CEREMONIAL"}});
        check(owned_count("ARMOR_CEREMONIAL") == 1,
              "GiveItem ARMOR_CEREMONIAL -> granted to inventory");
        fire_action("GiveItem", {{"Name", "HELM_CEREMONIAL|330"}});
        check(owned_count("HELM_CEREMONIAL") == 1 &&
                  owned_upgrade("HELM_CEREMONIAL") == 330,
              "GiveItem HELM_CEREMONIAL|330 -> count 1, upgrade 330");
        // The SHIPPED form: `GiveItem Name="?Concat[ITEM|,?Sum[?Multi[100,
        // ?Player[].Level],230]]"`. Before the `QNa` port this Name was UNKNOWN
        // (`GiveItem (Name unresolved)`) and granted nothing; the `|`-right
        // value becomes the upgrade level (cf. HELM_CEREMONIAL|330 above).
        {
            const char* const kConcatName =
                "?Concat[ARMOR_CEREMONIAL|,?Sum[?Multi[100,?Player[].Level],230]]";
            sf2::app::QuestJournal cj;
            const std::string csum =
                app.quest_engine().resolve_for_test(
                    app, "?Sum[?Multi[100,?Player[].Level],230]", cj);
            fire_action("GiveItem", {{"Name", kConcatName}});
            std::fprintf(stdout,
                         "[qa] GIVEITEM CONCAT: resolved_sum=%s upgrade=%d "
                         "count=%d\n",
                         csum.c_str(), owned_upgrade("ARMOR_CEREMONIAL"),
                         owned_count("ARMOR_CEREMONIAL"));
            check(owned_count("ARMOR_CEREMONIAL") >= 1 &&
                      std::to_string(owned_upgrade("ARMOR_CEREMONIAL")) == csum,
                  "GiveItem Name=\"?Concat[...]\" -> resolved + granted");
        }
        // `FightEnd` (`Tn.S`): recorded as a fight-scene request, not UNKNOWN.
        const std::size_t fe_before = app.quest_engine().fight_end_actions();
        fire_action("FightEnd", {});
        check(app.quest_engine().fight_end_actions() == fe_before + 1,
              "FightEnd action -> recorded fight-scene request");
        // --- GiveCurrency/TakeCurrency (`Xn` g="1F1" / `rg` g="20C") ---------
        // The two shipped currency actions: `Xn.S` grants (`Fr`/`vl`/`TH`),
        // `rg.S` gates on `p.o.Xfa` then `J0a` deducts. Observables: the save's
        // `Money`/`Bonus`/`<Currencies>` (persisted by `apply_effects`).
        {
            const auto money_now = [&]() -> int {
                try {
                    return app.save().load().money;
                } catch (const std::exception&) {
                    return -1;
                }
            };
            const auto bonus_now = [&]() -> int {
                try {
                    return app.save().load().bonus;
                } catch (const std::exception&) {
                    return -1;
                }
            };
            const auto ruby_now = [&]() -> int {
                try {
                    const sf2::app::WarriorSave w = app.save().load();
                    const auto it = w.currencies.find("Ruby");
                    return it != w.currencies.end() ? it->second : 0;
                } catch (const std::exception&) {
                    return -1;
                }
            };
            const int m0 = money_now(), b0 = bonus_now(), r0 = ruby_now();
            fire_action("GiveCurrency", {{"Type", "Gold"}, {"Value", "500"}});
            fire_action("GiveCurrency", {{"Type", "Bonus"}, {"Value", "7"}});
            fire_action("GiveCurrency", {{"Type", "Ruby"}, {"Value", "3"}});
            const int m1 = money_now(), b1 = bonus_now(), r1 = ruby_now();
            std::fprintf(stdout,
                         "[qa] CURRENCY give: Money %d->%d Bonus %d->%d Ruby %d->%d\n",
                         m0, m1, b0, b1, r0, r1);
            std::fflush(stdout);
            check(m1 == m0 + 500 && b1 == b0 + 7 && r1 == r0 + 3,
                  "GiveCurrency Gold/Bonus/Ruby -> money/bonus/currencies");
            // `rg` unaffordable: `Xfa` false -> `<Error>`, `J0a` not called.
            fire_action("TakeCurrency",
                        {{"Type", "Ruby"}, {"Name", "Ruby"}, {"Value", "9999"}});
            const int m2 = money_now(), b2 = bonus_now(), r2 = ruby_now();
            check(m2 == m1 && b2 == b1 && r2 == r1,
                  "TakeCurrency unaffordable -> no write (Xfa/Error branch)");
            // `rg` affordable: `J0a` deducts exactly (`Gold` -> `Fr(Tb-c)`).
            fire_action("TakeCurrency",
                        {{"Type", "Gold"}, {"Name", "Gold"}, {"Value", "200"}});
            const int m3 = money_now(), b3 = bonus_now(), r3 = ruby_now();
            std::fprintf(stdout, "[qa] CURRENCY take: Money %d->%d\n", m2, m3);
            std::fflush(stdout);
            check(m3 == m2 - 200 && b3 == b2 && r3 == r2,
                  "TakeCurrency Gold 200 -> money -200 (J0a)");
        }
        // --- UnlockCharacter (`Ko` g="210"): JS no-op -----------------------
        // `Ko.S` is `super.S(a); this.sa()` — nothing; `parse` discards `Name`.
        {
            const sf2::app::WarriorSave wb = [&] {
                try {
                    return app.save().load();
                } catch (const std::exception&) {
                    return sf2::app::WarriorSave{};
                }
            }();
            fire_action("UnlockCharacter", {{"Name", "AnyCharacter"}});
            const sf2::app::WarriorSave wa = [&] {
                try {
                    return app.save().load();
                } catch (const std::exception&) {
                    return sf2::app::WarriorSave{};
                }
            }();
            check(wb.money == wa.money && wb.bonus == wa.bonus &&
                      wa.perks.size() == wb.perks.size() &&
                      wa.currencies.size() == wb.currencies.size(),
                  "UnlockCharacter -> no-op (Ko.S empty; name ignored)");
        }
        // SetDataVersion (`po` L1039 -> `Oqb` L181): writes the ROOT
        // `<Versions><DataVersion Value>`. `Full` empty -> `u7a` =
        // Production.Major.Minor.DataVersion.
        {
            const std::string dv_before = app.save().data_version();
            fire_action("SetDataVersion", {{"Production", "1"},
                                           {"Major", "0"},
                                           {"Minor", "42"},
                                           {"DataVersion", "0"}});
            const std::string dv_after = app.save().data_version();
            check(dv_after == "1.0.42.0",
                  "SetDataVersion -> root <Versions><DataVersion Value=1.0.42.0>");
            std::fprintf(stdout, "[qa]   DataVersion %s -> %s\n",
                         dv_before.c_str(), dv_after.c_str());
        }
        // GivePerk `ApplyTo="Player"` (`$n` -> `jXa` -> `C1a` L555926):
        // `<Perk Name Level UpgradeLevel>` rows are granted when the name is in
        // the catalog (`d8a`) via `p.o.co.K1a` (port `WarriorSave::learn_perk`).
        if (app.has_fight_assets() && !app.fight_assets().perk_catalog.empty()) {
            const std::string perk_name =
                app.fight_assets().perk_catalog.begin()->first;
            const std::size_t perks_before = app.save().load().perks.size();
            sf2::app::QuestAction gp;
            gp.tag = "GivePerk";
            gp.attrs["ApplyTo"] = "Player";
            sf2::app::QuestAction pk;
            pk.tag = "Perk";
            pk.attrs["Name"] = perk_name;
            pk.attrs["Level"] = "1";
            pk.attrs["UpgradeLevel"] = "0";
            gp.children.push_back(pk);
            sf2::app::QuestJournal pj;
            app.quest_engine().run_action_probe(app, {gp}, pj);
            bool granted = false;
            for (const auto& ps : app.save().load().perks) {
                if (ps.name == perk_name) granted = true;
            }
            check(granted && app.save().load().perks.size() >= perks_before,
                  "GivePerk ApplyTo=Player -> <Perk Name Level UpgradeLevel> granted");
        } else {
            std::fprintf(stdout,
                         "[qa] GivePerk Player probe skipped (no perk catalog)\n");
        }
        // --- `He.jkb` L1056-1057 row-button: the `DeliveryDelay` bug ---------
        // Before the fix a `DeliveryDelay` row was not a row at all, so its
        // nested `<GiveItem>` NEVER ran (the shipped quests.xml L1395 shape).
        // Prove the child now runs through the row-button dispatch
        // (`He.dhb` L1061 `a<this.eOa`, first row id 5).
        {
            const int before_cnt = owned_count("Energy_Refill");
            sf2::app::QuestAction dlg;
            dlg.tag = "Dialog";
            dlg.attrs["Type"] = "Multiline";
            dlg.attrs["Title"] = "_Title_Assistant";
            sf2::app::QuestAction row;
            row.tag = "DeliveryDelay";
            row.attrs["Item"] = "Energy_Refill";
            row.attrs["Text"] = "dlgInstantBuyMessage1";
            row.attrs["ButtonText"] = "dlgStoryBtnMore";
            sf2::app::QuestAction give;
            give.tag = "GiveItem";
            give.attrs["Name"] = "Energy_Refill";
            give.attrs["PutOn"] = "1";
            give.attrs["Quantity"] = "0";
            row.children.push_back(give);
            dlg.children.push_back(row);
            sf2::app::QuestJournal dj;
            app.quest_engine().run_action_probe(app, {dlg}, dj);
            const bool queued = app.quest_engine().has_dialog();
            const int mid_cnt = owned_count("Energy_Refill");
            // REAL click: hit-test the row's own box and inject the tap
            // through the app's own pointer path (`inject_click` ->
            // `poll_input` -> the screen's `quest_modal_consume` ->
            // `He.dhb` L1061 row id). No OS input, hidden window (RULE 0).
            double row_cx = 0.0, row_cy = 0.0;
            const std::vector<sf2::app::QuestDialogRowButton> rb =
                sf2::app::quest_dialog_row_buttons(app, app.quest_engine().dialog());
            if (!rb.empty()) {
                row_cx = rb[0].x + rb[0].w * 0.5;
                row_cy = rb[0].y + rb[0].h * 0.5;
            }
            app.inject_click(row_cx, row_cy);
            for (int f = 0; f < 4 && app.quest_engine().has_dialog(); ++f) {
                app.run_one_frame();
            }
            const int after_cnt = owned_count("Energy_Refill");
            std::fprintf(stdout,
                         "[qa] DELIVERYDELAY row: queued=%d owned %d->%d "
                         "(after row click %.1f,%.1f -> id5) ->%d\n",
                         queued ? 1 : 0, before_cnt, mid_cnt, row_cx, row_cy, after_cnt);
            std::fflush(stdout);
            check(queued && mid_cnt == before_cnt && after_cnt > before_cnt,
                  "DeliveryDelay nested <GiveItem> runs (real row click id 5)");
        }
        // --- `sh` `BuyItem` (`EBuyItem` g="1D4" L526589) Ruby path ----------
        // `Energy_Refill` (list.xml L2318, BonusPrice=5); the shipped form is
        // quests.xml L1762 `<BuyItem Name="Energy_Refill" Currency="Ruby"/>`
        // inside the Right plate. `YDa` L107236 case 2 reads `p.o.fd` (the
        // port's `bonus`), so seed that balance.
        {
            sf2::app::WarriorSave w = app.save().load();
            w.bonus += 100;  // seed the Ruby (bonus) balance
            app.save().save(w);
            const int bonus_before = app.save().load().bonus;
            const int cnt_before = owned_count("Energy_Refill");
            const std::size_t buys_before = app.quest_engine().purchase_actions();
            sf2::app::QuestAction dlg;
            dlg.tag = "Dialog";
            dlg.attrs["Type"] = "Multiline";
            dlg.attrs["Title"] = "_Title_Assistant";
            sf2::app::QuestAction line;
            line.tag = "Line";
            line.attrs["Text"] = "dlgBuyEnergyMessage1";
            dlg.children.push_back(line);
            sf2::app::QuestAction btn;
            btn.tag = "Button";
            btn.attrs["Type"] = "Right";
            btn.attrs["Color"] = "Green";
            btn.attrs["Text"] = "dlgStoryBtnBuyEnergyForGems";
            sf2::app::QuestAction buy;
            buy.tag = "BuyItem";
            buy.attrs["Name"] = "Energy_Refill";
            buy.attrs["Currency"] = "Ruby";
            btn.children.push_back(buy);
            dlg.children.push_back(btn);
            sf2::app::QuestJournal bj;
            app.quest_engine().run_action_probe(app, {dlg}, bj);
            app.quest_engine().press_dialog(app, 1);  // the Right plate
            const int bonus_after = app.save().load().bonus;
            const int cnt_after = owned_count("Energy_Refill");
            const std::size_t buys_after = app.quest_engine().purchase_actions();
            const int price =
                app.quest_engine().bonus_price(app, "Energy_Refill");
            std::fprintf(stdout,
                         "[qa] BUYITEM Ruby: bonus %d->%d (price=%d) owned %d->%d "
                         "purchase %zu->%zu\n",
                         bonus_before, bonus_after, price, cnt_before, cnt_after,
                         buys_before, buys_after);
            std::fflush(stdout);
            check(price > 0 && bonus_after == bonus_before - price &&
                      cnt_after > cnt_before && buys_after == buys_before + 1,
                  "BuyItem Ruby (Energy_Refill) -> deduct bonus + grant + Purchase");
        }
        // Restore the profile exactly as found.
        if (have_original) {
            try {
                app.save().save(original);
            } catch (const std::exception&) {
            }
        }
        const bool all = checks == passed;
        std::fprintf(stdout, "[qa] RESULT %d/%d -> %s\n", passed, checks,
                     all ? "PASS" : "FAIL");
        std::fflush(stdout);
        app.shutdown();
        return all ? 0 : 1;
    } else if (flow_verify) {
        // ---- flow-verify: the three repaired flow bugs, asserted headlessly --
        // Every click is an `App::inject_click` tap (the JS `ma.Bd` primitive);
        // the window is hidden and RULE 0's watchdog is armed (driver_mode).
        glfwHideWindow(app.renderer().window());
        app.set_headless_frames(1);
        int checks = 0;
        int passed = 0;
        const auto check = [&](bool ok, const char* what) {
            ++checks;
            if (ok) ++passed;
            std::fprintf(stdout, "[flowverify] %-52s %s\n", what, ok ? "PASS" : "FAIL");
            std::fflush(stdout);
        };
        const auto tick = [&](int n) {
            for (int i = 0; i < n; ++i) {
                glfwPollEvents();
                app.run_one_frame();
            }
        };
        tick(600);  // settle the boot (Preloader -> Loader -> Dojo)
        // (1) BOSS LADDER: the win must walk `_$Fight` |1 (SHIN) -> |2 (BRICK).
        check(map_fight_index(app, "BOSS_LYNX", 3) == 0,
              "BOSS_LYNX fresh -> _$Fight |1 (SHIN)");
        try {
            sf2::app::WarriorSave w = app.save().load();
            bool found = false;
            for (auto& f : w.fights) {
                if (f.name == "BOSS_LYNX") {
                    ++f.wins;
                    found = true;
                }
            }
            if (!found) w.fights.push_back({"BOSS_LYNX", 1});
            w.battle_unlock("ZONE_2", "BOSS_HERMIT");  // a 2nd dot for (3)
            app.save().save(w);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[flowverify] seed failed: %s\n", e.what());
        }
        check(map_fight_index(app, "BOSS_LYNX", 3) == 1,
              "1 win -> _$Fight |2 (BRICK intro)");
        // (2) MENU: the collapsed `za` header must expand on the MAP (it did
        // not outside the Dojo: the `force_collapsed` early-return).
        app.screens().push(make_screen(app.screens(), kScreenMap));
        tick(10);
        check(!za_nav_expanded(kScreenMap), "map `za` starts collapsed (fresh)");
        app.inject_click(120.0, 90.0);  // the header (`dojo_menu_open` beat)
        // The `gk` open run is `expand(.3)` (L2000) = 18 fixed steps and
        // `gk.aa` (L1998) swallows input while `PF`, so wait the run out.
        tick(28);
        check(za_nav_expanded(kScreenMap), "map header tap EXPANDS the menu column");
        app.inject_click(120.0, 90.0);
        tick(28);
        check(!za_nav_expanded(kScreenMap), "map header tap collapses it again");
        // (3) ZONE DOTS: a tap on the strip must switch the shown zone.
        const int before = map_zone_selected(app);
        int target = -1;
        float dot_x = 0.0f;
        float dot_y = 0.0f;
        for (std::size_t zi = 0; zi < 32 && target < 0; ++zi) {
            float cx = 0.0f;
            float cy = 0.0f;
            if (!map_zone_dot_center(app, zi, cx, cy)) continue;
            if (static_cast<int>(zi) == before) continue;
            target = static_cast<int>(zi);
            dot_x = cx;
            dot_y = cy;
        }
        check(before >= 0, "map is live and a zone is selected");
        check(target >= 0, "a SECOND zone dot renders (ZONE_2 unlocked)");
        if (target >= 0) {
            std::fprintf(stdout, "[flowverify] zone dot %d at (%.0f, %.0f) -> click\n",
                         target, dot_x, dot_y);
            std::fflush(stdout);
            app.inject_click(dot_x, dot_y);
            tick(8);
            check(map_zone_selected(app) == target, "zone dot tap SWITCHES the map zone");
        }
        // ===== NEW (this task): Settings / Pause / shop-confirm gates ========
        // All through the internal injection path (`App::inject_click`; the JS
        // `ma.Bd` tap). No OS input. The window stays hidden + watchdog-armed.
        sf2::audio::AudioEngine& au = sf2::audio::AudioEngine::instance();
        // (4) SETTINGS `un` dialog bus rows (JS L1916-1931): `un.rHa` case 1
        // calls `lb.WT`/`lb.VT` (L1276 `ta.WT(a);p.TJ.save()`), so each row
        // drives ITS OWN bus AND persists it. The dialogue is the shared
        // overlay (`open_settings_dialog`); it is consumed by the ACTIVE
        // screen's input gate (`quest_modal_consume` -> `settings_dialog_consume`).
        {
            open_settings_dialog(app);
            tick(14);  // `settings_dialog_consume` press debounce (`age_ > 10`)
            float cx = 0.0f, cy = 0.0f, w = 0.0f, h = 0.0f;
            const bool have = settings_bus_row_center(true, cx, cy, w, h);
            const bool m0 = au.music_muted();
            const bool s0 = au.sfx_muted();
            app.inject_click(cx, cy);  // Music row
            tick(10);
            check(have && au.music_muted() != m0 && au.sfx_muted() == s0,
                  "settings Music row -> MUSIC bus only");
            const bool m1 = au.music_muted();
            const bool s1 = au.sfx_muted();
            settings_bus_row_center(false, cx, cy, w, h);
            app.inject_click(cx, cy);  // Sound row
            tick(10);
            check(au.sfx_muted() != s1 && au.music_muted() == m1,
                  "settings Sound row -> SFX bus only");
            sf2::app::WarriorSave pw = app.save().load();
            check(pw.music_muted == au.music_muted() && pw.sound_muted == au.sfx_muted(),
                  "settings bus mutes persist to the save <Sounds>");
            const bool keep_m = pw.music_muted, keep_s = pw.sound_muted;
            au.set_music_muted(!keep_m);  // poison the live bus
            au.set_sfx_muted(!keep_s);
            open_settings_dialog(app);    // re-reads + re-applies the save
            check(au.music_muted() == keep_m && au.sfx_muted() == keep_s,
                  "bus mutes RESTORED from the save");
            close_settings_dialog();
            tick(4);
        }
        // (5) PAUSE `Dr` dialog rows (JS L2066-2067): the HUD pause icon
        // (screens.cpp `kPauseIx/Iy` = 640,117 68px) opens it; the Music row
        // (x=561.25) toggles the MUSIC bus, the Sound row (x=718.75) the SFX
        // bus (row y=396, tile 135). Each must move ITS OWN bus only, and (JS
        // `lb.WT`/`lb.VT` L1276 `p.TJ.save()`) persist the change.
        {
            // Deterministic start: the Settings block above left both buses
            // muted AND persisted. Clear the live buses AND the save's mute
            // fields so each pause row's change is observable and its
            // persistence is unambiguous (a stale `true` would mask a missing
            // `lb.WT`/`lb.VT` save).
            {
                sf2::app::WarriorSave rw = app.save().load();
                rw.music_muted = false;
                rw.sound_muted = false;
                app.save().save(rw);
            }
            au.set_music_muted(false);
            au.set_sfx_muted(false);
            sf2::app::PendingBattle& pb = app.pending_battle();
            pb.battle_name = "Training";
            pb.zone.clear();
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            app.screens().push(make_screen(app.screens(), kScreenFight));
            tick(4);
            sf2::app::FightScreen* fs = nullptr;
            for (int i = 0; i < 900 && fs == nullptr; ++i) {
                tick(1);
                sf2::app::Screen* t = app.screens().top();
                if (t != nullptr && t->id() == kScreenFight) {
                    sf2::app::FightScreen* c = static_cast<sf2::app::FightScreen*>(t);
                    if (!c->round_wait()) fs = c;  // a live round (icon hittable)
                }
            }
            check(fs != nullptr, "fight reaches a live round (pause icon live)");
            if (fs != nullptr) {
                app.inject_click(640.0, 117.0);  // HUD pause icon (`Jn`)
                tick(8);
                check(fs->pause_dialog_open(), "pause HUD icon opens the `Dr` dialog");
                const bool m0 = au.music_muted();
                const bool s0 = au.sfx_muted();
                app.inject_click(561.25, 396.0);  // Music row (`tp`)
                tick(8);
                check(au.music_muted() != m0 && au.sfx_muted() == s0,
                      "pause Music row -> MUSIC bus only");
                app.inject_click(718.75, 396.0);  // Sound row (`Sla`)
                tick(8);
                check(au.sfx_muted() != s0 && au.music_muted() != m0,
                      "pause Sound row -> SFX bus only");
                sf2::app::WarriorSave fw = app.save().load();
                check(fw.music_muted == au.music_muted() &&
                          fw.sound_muted == au.sfx_muted(),
                      "pause bus mutes persist to the save");
            }
            app.screens().pop();
            tick(4);
        }
        // (6) SHOP `M8` price plate -> `Ne.ZYa` L2251 / `Pa.iwa` L1228: the
        // UNOWNED press runs the confirm/buy flow. `Pa.iwa` L1228 picks the
        // branch `a.Ec>0 ? Pa.y2a -> rb.QS() (snd_upgrade) : Pa.gI -> rb.U3()
        // (snd_buy)`. The immediate `Ec==0` branch is the one the shipped
        // catalog can reach; assert it fires `snd_buy` (not `snd_upgrade`)
        // and grants+equips the item.
        {
            sf2::app::WarriorSave sw = app.save().load();  // fund the buy
            if (sw.money < 5000) sw.money = 5000;
            app.save().save(sw);
            shop_open_at(app, "Weapon", "WEAPON_KNIVES");  // unowned baseline
            tick(20);
            const auto buy0 = au.played("snd_buy");
            const auto up0 = au.played("snd_upgrade");
            app.inject_click(934.6, 460.1);  // the `M8` plate centre (1280x720)
            tick(14);
            bool owned = false;
            try {
                const sf2::app::WarriorSave nw = app.save().load();
                for (const auto& oi : nw.items) {
                    if (oi.name == "WEAPON_KNIVES") owned = true;
                }
            } catch (const std::exception&) {
            }
            check(au.played("snd_buy") == buy0 + 1 && au.played("snd_upgrade") == up0 &&
                      owned,
                  "shop price plate (Ec==0) fires snd_buy + buys/equips");
            if (catalog_max_delivery_sec(app) <= 0) {
                std::fprintf(stdout,
                             "[flowverify] shop Ec>0 (snd_upgrade) branch NOT DRIVEN: "
                             "no shipped catalog row carries DeliveryTime>0 (list.xml "
                             "uses DeliveryDescription); hook: a catalog item with "
                             "delivery_sec>0 at the price plate\n");
                std::fflush(stdout);
            }
            app.screens().pop();
            tick(4);
        }
        std::fprintf(stdout, "[flowverify] RESULT %d/%d\n", passed, checks);
        std::fflush(stdout);
        app.shutdown();
        return passed == checks ? 0 : 1;
    } else if (za_nav_verify) {
        // ---- M4: the `za` nav open/close proof on EVERY main screen --------
        // JS `ma.D1` (L1832) builds a fresh `za` per screen and the `gk` ctor
        // `collapse(0)` (L1998) starts it COLLAPSED; the header tap toggles it
        // (`Bgb` L2000) under the 0.3 s `PF` input lock (`aa` L1998). Per
        // screen: after the MOUNT -> collapsed, after the OPEN tap -> expanded,
        // after the CLOSE tap -> collapsed.
        glfwHideWindow(app.renderer().window());
        app.set_headless_frames(1);
        struct ZaScreen {
            sf2::app::ScreenId id;
            const char* name;
        };
        const ZaScreen za_screens[5] = {
            {sf2::app::kScreenDojo, "Dojo"},
            {sf2::app::kScreenMap, "Map"},
            {sf2::app::kScreenShop, "Shop"},
            {sf2::app::kScreenProfile, "Profile"},
            {sf2::app::kScreenSettings, "Settings"},
        };
        int za_checks = 0;
        int za_passed = 0;
        const auto za_tick = [&](int n) {
            for (int i = 0; i < n; ++i) {
                glfwPollEvents();
                app.run_one_frame();
            }
        };
        za_tick(600);  // Preloader -> Loader -> Dojo
        for (const ZaScreen& zs : za_screens) {
            if (app.screens().current_id() != static_cast<int>(zs.id)) {
                app.screens().push(make_screen(app.screens(), zs.id));  // mount
            }
            za_tick(20);
            const bool after_mount = za_nav_expanded(zs.id);
            app.inject_click(120.0, 90.0);  // the collapsed `gk` header
            za_tick(4);                     // still INSIDE the expand(.3) run
            app.inject_click(120.0, 90.0);  // a close DURING the run
            za_tick(1);
            const bool swallow_open = za_nav_expanded(zs.id);  // lock held
            za_tick(27);                    // the run ends, the lock clears
            const bool after_open = za_nav_expanded(zs.id);
            app.inject_click(120.0, 90.0);
            za_tick(28);                    // the collapse(.3) run + lock clear
            const bool after_close = za_nav_expanded(zs.id);
            const bool ok = !after_mount && swallow_open && after_open && !after_close;
            std::fprintf(stdout,
                         "[zanav] %-8s mount=%s open=%s lock_swallow=%s close=%s -> %s\n",
                         zs.name, after_mount ? "OPEN" : "shut",
                         after_open ? "OPEN" : "shut",
                         swallow_open ? "OPEN(kept)" : "closed(LEAK)",
                         after_close ? "OPEN" : "shut", ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            ++za_checks;
            if (ok) ++za_passed;
        }
        std::fprintf(stdout, "[zanav] RESULT %d/%d\n", za_passed, za_checks);
        std::fflush(stdout);
        app.shutdown();
        return za_passed == za_checks ? 0 : 1;

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
            pb.owned = loadout_owned(loadout);
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

        // The probe expectations are authored per loadout (the decision record
        // carries the candidate set, so it differs with the equipped weapon):
        // the default Fists tape uses `kVerifyProbes`; `--loadout Knives`
        // runs the same tape against the knives move set.
        const VerifyProbe* active_probes = kVerifyProbes;
        int active_probe_count = kVerifyProbeCount;
        if (loadout == "Knives") {
            active_probes = kVerifyProbesKnives;
            active_probe_count = kVerifyProbesKnivesCount;
        }
        (void)active_probes;
        (void)active_probe_count;

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
                for (int p = 0; p < active_probe_count; ++p) {
                    if (active_probes[p].frame == fight_frames) pending = &active_probes[p];
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
                fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
                // [ROUND-plate lead-in] Index the tape/probes on the FIGHT's
                // own phase-local frame (`FightController::frame_` resets to 0
                // at phase 1, JS `FNa` L409; the ROUND-plate lead-in is
                // excluded from the counter). The tape frames below are
                // authored phase-local, so the wall-clock count from the
                // screen push would land every edge `lead-in` frames early and
                // the 2key double-tap windows would miss.
                fight_frames = fs != nullptr ? fs->fight_frame() : fight_frames + 1;
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
                         active_probe_count - probe_failures, active_probe_count);
            std::fflush(stdout);
        }
        return probe_failures == 0 ? 0 : 1;
    } else if (verify_place) {
        // ------------------------------------------------------------------
        // `--verify-place <me_x> <enemy_x>`: the proof hooks for the two
        // 900490d6 fixes + the item-1 restart gate.
        //   * MIRROR (`vm.he` L749): with the enemy on the player's LEFT
        //     (`Ae.Wl = sign(enemy_x-me_x) < 0`) the parsed requirement is
        //     direction-reversed (`zd.Fha` L688, 2<->8 / 3<->7 / 4<->6), so the
        //     `Forward` key (control 3) must resolve a `Back`-requiring move
        //     instead of `StepForward`. The unmirrored formula picks
        //     `StepForward`.
        //   * THROW (`CurrentInterval Player="Enemy"`): a held Back + Punch
        //     tap in range must resolve `ThrowSuplex` / `ThrowThroughTheBack`
        //     through the fixed `ctx.intervals_enemy` fill.
        //   * ITEM 1 (interval restart gate): a re-press inside the running
        //     move's `SelfUninterrupt` window must NOT restart it.
        // ------------------------------------------------------------------
        {
            PendingBattle& pb = app.pending_battle();
            // The throw probe needs an ANIMATED AI opponent: the Training
            // punchbag is `NotAnimation="1"` (stages.xml L12) and never plays a
            // move, so its interval set is empty and the `Throw` template's
            // `<CurrentInterval Player="Enemy" Name="Throwable"/>` gate can
            // never pass. `Duel` (ZONE_1) is the shipped animated duel.
            pb.battle_name = fight_battle.empty() ? std::string("Duel") : fight_battle;
            pb.zone = fight_zone.empty() ? std::string("ZONE_1") : fight_zone;
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned = loadout_owned(loadout.empty() ? std::string("Fists") : loadout);
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        app.set_headless_frames(1);
        auto* fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
        if (fs == nullptr) {
            std::fprintf(stderr, "[place] no fight screen\n");
            return 1;
        }
        int guard = 0;
        while (guard < 6000 && fs->fight_frame() < 140) {
            glfwPollEvents();
            app.run_one_frame();
            ++guard;
        }
        // Park the fighters and let the update rebuild the context geometry.
        fs->reset_player_move();
        fs->place_fighters(place_me_x, place_enemy_x);
        for (int i = 0; i < 3; ++i) app.run_one_frame();
        const float me_actual = fs->player_world_x();
        const float en_actual = fs->enemy_world_x();
        const float wl = en_actual - me_actual;
        // ---- probe 1: mirrored directional resolution --------------------
        // With the enemy LEFT (`Wl = sign(enemy-me) < 0`) `vm.he` L749 takes
        // the `TDa` branch: the parsed requirement is reversed (`zd.Fha` L688:
        // 2<->8 / 3<->7 / 4<->6). So the FORWARD key (control 3) resolves a
        // `Back`-requiring move (`StepBack`), while the FORWARD MOVE
        // (`StepForward`) is now reached by the BACK key (control 7) and
        // travels TOWARD the enemy. The unmirrored formula would pick
        // `StepForward` for the Forward key.
        const std::string mir_mv0 = fs->player_current_move();
        const float mir_x0 = fs->player_world_x();
        fs->on_key(68, true);   // D -> Forward (control 3)
        fs->on_key(68, false);
        for (int i = 0; i < 6; ++i) app.run_one_frame();
        const std::string mir_fwd = fs->player_current_move();
        const float mir_x1 = fs->player_world_x();
        const bool mir_ok = (wl < 0.0f) ? (mir_fwd != "StepForward" && !mir_fwd.empty())
                                        : (mir_fwd == "StepForward");
        std::fprintf(stdout,
                     "[place] MIRROR placed(%.0f,%.0f) actual(%.0f,%.0f) Wl=%+.0f "
                     "Forward-key(D): '%s'->'%s' x %.0f->%.0f dx=%+.0f "
                     "(unmirrored formula would pick StepForward) %s\n",
                     place_me_x, place_enemy_x, me_actual, en_actual,
                     wl < 0.0f ? -1.0f : 1.0f, mir_mv0.c_str(), mir_fwd.c_str(),
                     mir_x0, mir_x1, mir_x1 - mir_x0, mir_ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        // The forward MOVE via the mirrored key: with `Wl<0` the `Back` key
        // (control 7) maps to the `Forward` requirement, so `StepForward` must
        // resolve and the fighter must travel TOWARD the enemy (dx < 0 here).
        for (int i = 0; i < 90; ++i) app.run_one_frame();
        fs->reset_player_move();
        fs->place_fighters(place_me_x, place_enemy_x);
        for (int i = 0; i < 2; ++i) app.run_one_frame();
        const float fwd_x0 = fs->player_world_x();
        fs->inject_game_key(7, true);   // Back key (mirrored -> Forward move)
        fs->inject_game_key(7, false);
        for (int i = 0; i < 6; ++i) app.run_one_frame();
        const std::string fwd_mv = fs->player_current_move();
        const float fwd_x1 = fs->player_world_x();
        const bool fwd_ok = (fwd_mv == "StepForward") && (fwd_x1 < fwd_x0);
        std::fprintf(stdout,
                     "[place] MIRROR forward-move via Back key(7): '%s' x %.0f->%.0f dx=%+.0f "
                     "toward-enemy=%s %s\n",
                     fwd_mv.c_str(), fwd_x0, fwd_x1, fwd_x1 - fwd_x0,
                     fwd_x1 < fwd_x0 ? "yes" : "no", fwd_ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        // Settle to the idle before the next probe.
        for (int i = 0; i < 90; ++i) app.run_one_frame();
        // ---- probe 2: throw gate (retry while the enemy is Throwable) ----
        // The throw keys are `Punch Tap + Back Hold` (moves.xml ThrowSuplex
        // L1714552). The requirement is MIRRORED when `Wl<0` (`vm.he` L749:
        // `Back`(7) -> `Forward`(3)), so the physical hold key that satisfies
        // `Back Hold` flips with the facing. Try both hold keys x both facings;
        // the first combination that resolves a `Throw*` proves the
        // `<CurrentInterval Player="Enemy" Name="Throwable"/>` gate (the
        // `Cond::player` / `ctx.intervals_enemy` fix).
        const char* hold_label[2] = {"Back(7)", "Forward(3)"};
        const int hold_key[2] = {7, 3};
        std::string thr_mv0, thr_mv1, thr_dec, thr_last, thr_label;
        bool thr_ok = false;
        // The throw's `Distance Max="100"` gate measures the fighters' ROOT x,
        // which the clip anchor re-pins every frame — a fixed teleport does not
        // stick (the player snaps back to its stance anchor). Park the ENEMY 60
        // units from the player's ACTUAL settled x (<= 100 with slack) instead.
        for (int er = 0; er < 2 && !thr_ok; ++er) {
            for (int h = 0; h < 2 && !thr_ok; ++h) {
                for (int attempt = 0; attempt < 40 && !thr_ok; ++attempt) {
                    fs->inject_game_key(hold_key[h], false);
                    fs->reset_player_move();
                    for (int i = 0; i < 4; ++i) app.run_one_frame();
                    const float px = fs->player_world_x();
                    const float ex = er ? px + 60.0f : px - 60.0f;
                    fs->place_fighters(px, ex);
                    app.run_one_frame();
                    thr_mv0 = fs->player_current_move();
                    fs->inject_game_key(hold_key[h], true);
                    app.run_one_frame();
                    app.run_one_frame();
                    // Re-park right before the tap: the AI opponent drifts, and
                    // the throw `Distance Max="100"` gate is measured at the tap.
                    const float px2 = fs->player_world_x();
                    fs->place_fighters(px2, er ? px2 + 60.0f : px2 - 60.0f);
                    fs->inject_game_key(9, true);   // Punch Tap (control 9)
                    fs->inject_game_key(9, false);
                    app.run_one_frame();
                    thr_mv1 = fs->player_current_move();
                    thr_dec = fs->player_decision();
                    thr_last = fs->player_last_decision();
                    fs->inject_game_key(hold_key[h], false);
                    thr_ok = (thr_mv1 == "ThrowSuplex" ||
                              thr_mv1 == "ThrowThroughTheBack");
                    if (thr_ok) {
                        thr_label = std::string(hold_label[h]) +
                                    (er ? " enemyRIGHT(Wl>0)" : " enemyLEFT(Wl<0)");
                        // ---- probe 2b: the THROW VICTIM -----------------
                        // The thrower fires `<PlayAnimation Player="Enemy"
                        // Animation="…V"/>` (moves.xml:42439/42632/42835); the
                        // victim's `…V` move opens with
                        //   <Align Axis="X|Z"><Pivot Object="Animation"/>
                        //   <Position Player="Enemy" Object="Animation"/></Align>
                        // so (JS `Te.Gub` L557-559) `d = 0`, `e = c.Fk` where
                        // `c = BBa(b4=Enemy)` (L563, `Te.cQ` = the THROWER) and
                        // `this.Fk = e - d`; `Gla(Fk.x, eja, Fk.z)` (L559) then
                        // shifts the victim's clip ONTO the thrower. Log both
                        // fighters' x + facing per victim frame: the victim must
                        // NOT be pulled back to its own origin and must NOT be
                        // turned away from the thrower.
                        std::fprintf(stdout,
                                     "[victim] t=%d BEFORE thrower='%s' x=%.1f f=%+.0f "
                                     "| victim='%s' x=%.1f f=%+.0f\n",
                                     fs->fight_frame(), thr_mv1.c_str(),
                                     fs->player_world_x(), fs->player_facing(),
                                     fs->enemy_current_move().c_str(),
                                     fs->enemy_world_x(), fs->enemy_facing());
                        for (int vf = 0; vf < 48; ++vf) {
                            app.run_one_frame();
                            std::fprintf(stdout,
                                         "[victim] t=%d thrower='%s' x=%.1f f=%+.0f "
                                         "| victim='%s' x=%.1f f=%+.0f\n",
                                         fs->fight_frame(),
                                         fs->player_current_move().c_str(),
                                         fs->player_world_x(), fs->player_facing(),
                                         fs->enemy_current_move().c_str(),
                                         fs->enemy_world_x(), fs->enemy_facing());
                        }
                        std::fflush(stdout);
                    } else {
                        for (int i = 0; i < 20; ++i) app.run_one_frame();
                    }
                }
            }
        }
        const float thr_gap = 60.0f;  // the enemy is parked 60 from the player root
        std::fprintf(stdout,
                     "[place] THROW BackHold+PunchTap gap=%.0f: '%s'->'%s' hold=%s %s\n"
                     "[place]   last=%s\n[place]   decision: %s\n",
                     thr_gap, thr_mv0.c_str(), thr_mv1.c_str(),
                     thr_label.empty() ? "<none-worked>" : thr_label.c_str(),
                     thr_ok ? "PASS" : "FAIL", thr_last.c_str(), thr_dec.c_str());
        std::fflush(stdout);
        // ---- probe 3: the interval restart gate (item 1) -----------------
        // The `Step` template's `<CurrentAnimation Name="Step"/>` guard is the
        // restart blocker: with `anims_me` carrying the move's transitive
        // template chain, a StepForward re-press inside `SelfUninterrupt[0,13]`
        // must NOT restart. Park the enemy on the RIGHT of the player's ACTUAL
        // settled x so the unmirrored FORWARD key resolves `StepForward`.
        for (int i = 0; i < 60; ++i) app.run_one_frame();
        fs->reset_player_move();
        for (int i = 0; i < 4; ++i) app.run_one_frame();
        {
            const float px = fs->player_world_x();
            fs->place_fighters(px, px + 120.0f);
        }
        for (int i = 0; i < 2; ++i) app.run_one_frame();
        const int rs_start = fs->player_moves_started();
        fs->inject_game_key(3, true);   // Forward Tap -> StepForward
        fs->inject_game_key(3, false);
        for (int i = 0; i < 3; ++i) app.run_one_frame();
        const std::string rs_mv = fs->player_current_move();
        const int rs_after_first = fs->player_moves_started();
        fs->inject_game_key(3, true);   // re-press inside SelfUninterrupt [0,13]
        fs->inject_game_key(3, false);
        for (int i = 0; i < 2; ++i) app.run_one_frame();
        const int rs_after_second = fs->player_moves_started();
        const std::string rs_dec = fs->player_decision();
        const bool rs_blocked = (rs_after_second == rs_after_first);
        std::fprintf(stdout,
                     "[place] INTERVAL restart: first='%s' started %d->%d, re-press in "
                     "SelfUninterrupt[0,13] -> started=%d %s\n"
                     "[place]   reject decision: %s\n",
                     rs_mv.c_str(), rs_start, rs_after_first, rs_after_second,
                     rs_blocked ? "PASS (no restart)" : "FAIL (restarted)",
                     rs_dec.c_str());
        std::fflush(stdout);
        // ---- probe 4: HighPunch (control — its chain has no `Step`) --------
        // HighPunch's chain (`1key|Central|Unarmed|Punch`) carries no `Step`
        // tag, so the item-1 fix adds nothing to it. Its re-press is governed
        // by the inherited `Controlled` template's
        // `<CurrentInterval Name="Uninterrupt" Not="1"/>` (HighPunch owns
        // `<Interval Name="Uninterrupt" End="9"/>`), which is JS-exact and
        // UNCHANGED by the item-1 guard — reported for contrast (not gated).
        for (int i = 0; i < 60; ++i) app.run_one_frame();
        fs->reset_player_move();
        for (int i = 0; i < 4; ++i) app.run_one_frame();
        {
            const float px = fs->player_world_x();
            fs->place_fighters(px, px + 150.0f);
        }
        for (int i = 0; i < 2; ++i) app.run_one_frame();
        const int hp_start = fs->player_moves_started();
        fs->inject_game_key(9, true);   // Punch Tap -> HighPunch
        fs->inject_game_key(9, false);
        for (int i = 0; i < 3; ++i) app.run_one_frame();
        const std::string hp_mv = fs->player_current_move();
        const int hp_after_first = fs->player_moves_started();
        // Re-press AFTER HighPunch's `Uninterrupt` window (`<Interval
        // Name="Uninterrupt" End="9"/>`; the inherited `Controlled` template
        // gates on `<CurrentInterval Name="Uninterrupt" Not="1"/>`, so an
        // in-window re-press is blocked for BOTH fighters' moves — JS-exact).
        // The item-1 fix is specific to the `Step`/`DoubleStep` chain, so
        // HighPunch's re-press outside that window must still restart.
        for (int i = 0; i < 10; ++i) app.run_one_frame();
        const std::string hp_pre = fs->player_current_move();
        const int hp_pre_frame = fs->player_move_frame();
        fs->inject_game_key(9, true);   // re-press: no `Step` guard on this chain
        fs->inject_game_key(9, false);
        for (int i = 0; i < 2; ++i) app.run_one_frame();
        const std::string hp_post = fs->player_current_move();
        const int hp_after_second = fs->player_moves_started();
        const bool hp_restarted = (hp_after_second > hp_after_first);
        std::fprintf(stdout,
                     "[place] HIGHPUNCH restart: first='%s' started %d->%d, re-press "
                     "-> started=%d %s\n[place]   pre='%s'@%d post='%s' F=%d decision: %s\n",
                     hp_mv.c_str(), hp_start, hp_after_first, hp_after_second,
                     hp_restarted ? "PASS (restarted)" : "FAIL (blocked)",
                     hp_pre.c_str(), hp_pre_frame, hp_post.c_str(), fs->fight_frame(),
                     fs->player_decision().c_str());
        std::fflush(stdout);
        app.shutdown();
        return (mir_ok && fwd_ok && thr_ok && rs_blocked) ? 0 : 1;
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
            pb.owned = loadout_owned(loadout);
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
        // (live-path parity, item 3) The REAL running game polls GLFW in
        // `App::poll_input` (app.cpp:807 `kFightKeys`, L814-827) and routes
        // every edge to `screens_->top()->on_key`; `App::inject_key`
        // (app.cpp:1169) calls the SAME `screens_->top()->on_key`, and the
        // tape calls `fs->on_key` where `fs == screens_->top()`. Byte-identical
        // consumer, so no routing divergence. Remaining live-only differences
        // are structural: (a) `poll_input` returns early while an injected
        // click is pending (app.cpp:763-775) — a one-frame delay, never a
        // dropped key; (b) `glfwGetKey` is level-based, so a press+release
        // inside one frame is invisible; (c) a RELEASE routes to the CURRENT
        // top screen (L824), not the screen that saw the press. Prove the
        // D + punch x2 pair through that same consumer:
        app.inject_key(68, true);
        const int live_d = fs->last_input_key_type();
        app.inject_key(68, false);
        app.inject_key(75, true);
        const int live_k = fs->last_input_key_type();
        app.inject_key(75, false);
        std::fprintf(stdout,
                     "[tape] live-path parity (App::inject_key -> screens_->top()->on_key): "
                     "D->key_type=%d (direct=3) punch(K)->key_type=%d (direct=9) %s\n",
                     live_d, live_k, (live_d == 3 && live_k == 9) ? "PASS" : "FAIL");
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

        // The JS DIAGONAL rule (`Za.bbb` keyboard setup, `gu.De` L232345): the
        // movement key-PAIRS are registered BEFORE the cardinals and `gu.Oba`
        // fires only the FIRST satisfied binding (the `this.TD` latch), so
        // W+D must emit up_forward(2) and W+A up_back(8) - never up(1)/
        // forward(3). Prove the exact control each key combination emits.
        struct ComboProbe { int a; int b; int expect; const char* name; };
        static const ComboProbe kCombos[] = {
            {87, 68, 2, "W+D"}, {87, 65, 8, "W+A"},
            {83, 68, 4, "S+D"}, {83, 65, 6, "S+A"},
            {87, 0, 1, "W"},    {68, 0, 3, "D"},
            {83, 0, 5, "S"},    {65, 0, 7, "A"},
        };
        int combo_ok = 0;
        const int combo_n = static_cast<int>(sizeof(kCombos) / sizeof(kCombos[0]));
        std::fprintf(stdout, "[tape] JS diagonal combos (Za.bbb):\n");
        for (const ComboProbe& cp : kCombos) {
            fs->on_key(cp.a, true);
            if (cp.b != 0) fs->on_key(cp.b, true);
            const int got = fs->last_input_key_type();
            if (cp.b != 0) fs->on_key(cp.b, false);
            fs->on_key(cp.a, false);
            if (got == cp.expect) ++combo_ok;
            std::fprintf(stdout, "[tape]   %-4s -> control %d (expect %d) %s\n",
                         cp.name, got, cp.expect, got == cp.expect ? "PASS" : "FAIL");
        }
        std::fprintf(stdout, "[tape] diagonal combos %d/%d\n", combo_ok, combo_n);
        std::fflush(stdout);

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
        // (a0) the KEYBOARD on the hub. A physical key must reach the hub's
        //      own controller through the SAME `player_input` path the drawn
        //      pad uses (JS `Za.bbb` keyboard -> `Za.hS` -> `ca.N0a`). Before
        //      the fix the base `Screen::on_key` swallowed every key here, so
        //      only the pad moved the character.
        if (ds != nullptr) {
            for (int i = 0; i < 6; ++i) app.run_one_frame();  // settle to idle
            const float kx0 = ds->dojo_player_x();
            const std::string km0 = ds->dojo_player_move();
            ds->on_key(68, true);  // D -> forward (control 3)
            const int kKeyCtl = ds->dojo_last_key_type();
            for (int i = 0; i < 8; ++i) app.run_one_frame();
            const float kx1 = ds->dojo_player_x();
            const std::string km1 = ds->dojo_player_move();
            ds->on_key(68, false);
            std::fprintf(stdout,
                         "[tape] dojo KEY forward (D, control %d): move '%s'->'%s' "
                         "x %.2f->%.2f dx=%.2f %s\n",
                         kKeyCtl, km0.c_str(), km1.c_str(), kx0, kx1,
                         kx1 - kx0, (kx1 != kx0 || km1 != km0) ? "PASS" : "FAIL");
            // The JS diagonal pair: W then D must emit up(1) then up_forward(2).
            ds->on_key(87, true);  // W -> up (control 1)
            const int d1 = ds->dojo_last_key_type();
            ds->on_key(68, true);  // + D -> up_forward (control 2)
            const int d2 = ds->dojo_last_key_type();
            ds->on_key(68, false);
            ds->on_key(87, false);
            std::fprintf(stdout,
                         "[tape] dojo KEY diagonal W(->%d)+D(->%d) expect 1->2 %s\n",
                         d1, d2, (d1 == 1 && d2 == 2) ? "PASS" : "FAIL");
            // Clear the running move before the pad probes: a punch delivered
            // inside a move's `Uninterrupt` window cannot start (JS `vm.he`
            // L749 gates every attack on `CurrentInterval Uninterrupt Not=1`).
            // Waiting for the stance idle is exactly how the fight tape spaces
            // its keys. (Before the 900490d6 gate fix `ctx.intervals` was never
            // filled, so the guard was silently OFF and the punch started for
            // the WRONG reason.)
            for (int i = 0; i < 600; ++i) {
                const std::string m = ds->dojo_player_move();
                if (m.find("Idle") != std::string::npos ||
                    m.find("Stance") != std::string::npos) {
                    break;
                }
                app.run_one_frame();
            }
            std::fprintf(stdout,
                         "[tape] dojo settle: move='%s' frame=%d (idle reached)\n",
                         ds->dojo_player_move().c_str(), ds->dojo_player_move_frame());
            std::fflush(stdout);
        }
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
        // The `gk` open run (`expand(.3)` L2000) swallows input while `PF`
        // (L1998), so wait the run out before the MAP row tap.
        for (int i = 0; i < 40; ++i) app.run_one_frame();
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
        // kScreenFight. The player's move list is built from the EQUIPPED
        // Weapon slot (JS `ra.Hza` L684-685 + `Fd` L808): with no
        // `--loadout` the boot resolves the save's equipped slots
        // (`owned_items`), so a save with WEAPON_KNIVES equipped gets the
        // knives moves; `--loadout Fists` pins the shipped Fists default.
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
            pb.owned = loadout_owned(loadout);
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
    } else if (round_log) {
        // [probe] Round-transition frame log (M2 verification). Boot the
        // direct fight, run with NO input until the first round transition
        // hides the 3-D view (`FightController::apply_round_result` ->
        // `set_scene_visible(false)`), then print per frame: the
        // `scene_visible()` flag, both fighters' world x and the camera
        // centre x. `enter_start_stance` repositions the fighters (teleport)
        // and then re-shows the scene in the SAME function, so the x jump
        // must only ever land on the first visible frame — never on a drawn
        // old->new step. No capture, no OS input.
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name = fight_battle.empty() ? std::string("Duel") : fight_battle;
            pb.zone = fight_zone.empty() ? std::string("ZONE_1") : fight_zone;
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned = loadout_owned(loadout.empty() ? std::string("Fists") : loadout);
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        app.set_headless_frames(1);  // uncapped deterministic stepping
        auto* fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
        std::fprintf(stdout, "[roundlog] boot battle=%s zone=%s\n",
                     fight_battle.empty() ? "Duel" : fight_battle.c_str(),
                     fight_zone.empty() ? "ZONE_1" : fight_zone.c_str());
        std::fflush(stdout);
        int guard = 0;
        bool fight_seen = false;
        int ff = 0;
        bool found = false;
        int hidden_at = -1;
        std::string pre[4];  // ring of the last <=4 visible frames
        int pre_n = 0;
        while (guard < 20000 && fs != nullptr) {
            glfwPollEvents();
            app.run_one_frame();
            ++guard;
            if (app.screens().current_id() != kScreenFight) {
                std::fprintf(stdout, "[roundlog] fight screen left at guard %d\n", guard);
                std::fflush(stdout);
                break;
            }
            if (!fight_seen) {
                fight_seen = true;
                ff = 0;
                continue;
            }
            ++ff;
            const bool vis = fs->scene_visible();
            char line[192];
            std::snprintf(line, sizeof(line),
                          "[roundlog] F%d vis=%d px=%.1f ex=%.1f cx=%.1f rw=%d",
                          ff, vis ? 1 : 0,
                          static_cast<double>(fs->player_world_x()),
                          static_cast<double>(fs->enemy_world_x()),
                          static_cast<double>(fs->camera_center_x()),
                          fs->round_wait() ? 1 : 0);
            if (!found) {
                if (vis) {
                    pre[pre_n & 3] = line;
                    ++pre_n;
                } else {
                    found = true;
                    hidden_at = ff;
                    const int start = pre_n > 4 ? pre_n - 4 : 0;
                    for (int i = start; i < pre_n; ++i) {
                        std::fprintf(stdout, "%s\n", pre[i & 3].c_str());
                    }
                    std::fprintf(stdout, "%s\n", line);
                    std::fflush(stdout);
                }
            } else {
                std::fprintf(stdout, "%s\n", line);
                std::fflush(stdout);
                if (vis) {
                    std::fprintf(stdout,
                                 "[roundlog] transition: hidden F%d..F%d (%d frames), "
                                 "scene re-shown at F%d\n",
                                 hidden_at, ff - 1, ff - hidden_at, ff);
                    std::fflush(stdout);
                    break;
                }
            }
        }
        if (!found) {
            std::fprintf(stdout,
                         "[roundlog] no round transition within %d frames "
                         "(last fight frame %d)\n",
                         guard, ff);
            std::fflush(stdout);
        }
        app.shutdown();
        return 0;
    } else if (d3_probe) {
        // [probe] `--d3-probe`: build a headless FightController with a Fans
        // loadout, force the named move (default FansSuperSlash) on the
        // player, step to the target frame, then print the attacker's part
        // set the OLD way (yD(4) only) vs the NEW way (the xqb union over
        // every active type-4 interval) and the hit_test result. No OS input,
        // no window (driver_mode), watchdog armed.
        if (!app.has_fight_assets()) {
            std::fprintf(stderr, "[d3probe] fight assets not loaded\n");
            app.shutdown();
            return 1;
        }
        sf2::app::FightAssets& fa = app.fight_assets();
        sf2::scene::BattleParams battle;
        battle.name = "Training";
        battle.location = "dojo";
        battle.rounds = 2;
        battle.round_time = 99;
        battle.max_hp = 100;
        battle.player_spawn_x = 690.0f;
        battle.player_spawn_y = -93.0f;
        battle.enemy_spawn_x = 973.0f;
        battle.enemy_spawn_y = -110.0f;
        const std::vector<sf2::scene::OwnedItem> owned =
            loadout_owned(loadout.empty() ? std::string("Fans") : loadout);
        // The player's OWN model must carry the Fans weapon capsules (the
        // `FansSuperSlash` AttackingParts resolve against them). Build it from
        // the skeleton + the Fans weapon part (JS `xc.cM`).
        sf2::scene::Model player_model_storage;
        const sf2::scene::Model* player_model = nullptr;
        {
            std::vector<sf2::scene::Model> parts;
            parts.push_back(fa.skeleton);
            const sf2::scene::Model* wp = fa.load_part("mdl_weapon_val17_fans");
            if (wp != nullptr) parts.push_back(*wp);
            if (!fa.body.bones.empty()) parts.push_back(fa.body);
            if (!fa.head.bones.empty()) parts.push_back(fa.head);
            player_model_storage = sf2::scene::build_fighter_model(parts);
            if (!player_model_storage.bones.empty()) {
                player_model = &player_model_storage;
            }
        }
        sf2::scene::FightController ctl;
        std::mt19937 rng(0x5F2);
        auto roll01 = [&rng]() {
            return static_cast<float>(rng()) / static_cast<float>(rng.max());
        };
        const sf2::scene::TacticDef* tactic = nullptr;
        const auto tit = fa.tactic_defs.find("Standard");
        if (tit != fa.tactic_defs.end()) tactic = &tit->second;
        ctl.init_locks(battle, fa.merged, fa.moves, fa.clips, fa.tactics_sets,
                       tactic, "Player", "Enemy", battle.player_spawn_x,
                       battle.player_spawn_y, battle.enemy_spawn_x,
                       battle.enemy_spawn_y, battle.max_hp, battle.max_hp,
                       roll01, owned, sf2::scene::PerkSetup(), nullptr,
                       player_model);
        std::fprintf(stdout, "[d3probe] forced move=%s\n", d3_probe_move.c_str());
        std::fflush(stdout);
        // The move's tactics gate the Distance (FansSuperSlash: 300..800), so
        // park the fighters inside that band before forcing.
        ctl.debug_place_fighters(690.0f, 1190.0f);
        // Advance the phase machine to the live FIGHT phase (2) first: a move
        // forced during the StartStance intro is reset by the phase switch.
        // `release_intro` ends the held ROUND-plate lead-in (the VS overlay
        // gate) so the phase machine can run.
        ctl.release_intro();
        {
            int pguard = 0;
            while (pguard < 20000 && ctl.phase() != 2) {
                ctl.update(1.0f / 60.0f);
                ++pguard;
            }
            std::fprintf(stdout, "[d3probe] phase=%d (guard %d)\n", ctl.phase(),
                         pguard);
            std::fflush(stdout);
        }
        ctl.debug_place_fighters(690.0f, 1190.0f);
        if (!ctl.debug_force_player_move(d3_probe_move)) {
            std::fprintf(stderr, "[d3probe] move '%s' did not start\n",
                         d3_probe_move.c_str());
            app.shutdown();
            return 1;
        }
        // Step the sim to the target move frame (the probe frame is a MOVE
        // frame, not a fight frame). The enemy is parked at the probe distance
        // each step so the geometry test sees the requested gap.
        int guard = 0;
        while (guard < 20000 &&
               ctl.player().fighter.move_frame() < d3_probe_frame) {
            ctl.debug_place_fighters(690.0f, 690.0f + d3_probe_dist);
            ctl.update(1.0f / 60.0f);
            ++guard;
        }
        ctl.debug_place_fighters(690.0f, 690.0f + d3_probe_dist);
        std::fprintf(stdout, "[d3probe] reached move frame %d (guard %d)\n",
                     ctl.player().fighter.move_frame(), guard);
        std::fflush(stdout);
        const bool hit = ctl.debug_d3_probe(d3_probe_frame);
        app.shutdown();
        return hit ? 0 : 1;
    } else if (boss_hit_probe) {
        // [probe] `--boss-hit-probe`: boot a BOSS fight (default
        // BOSS_LYNX/ZONE_1), drive the player into range through the internal
        // `player_input` path (`inject_game_key` — NO OS input), land an attack
        // on the boss, then log ~150 fight frames of the boss's hit reaction:
        // its `current_move()` name, `ragdoll_active()` (JS `Al.nk`), its
        // `world_x`, its facing and the started-move counter (an AI replacement
        // bumps it). This is the end-to-end proof that the 22cbe41f reaction
        // gate (`de.hcb` L598 + `wd.Qnb` L507) survives a scripted player hit:
        // `wd.Qnb` starts the reaction and leaves `Te.Pe=false`, `de.hcb` then
        // returns false so `de.ia` issues no decision and `Al.nk` keeps driving
        // the pose instead of the AI replacing it the next frame.
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name =
                fight_battle.empty() ? std::string("BOSS_LYNX") : fight_battle;
            pb.zone = fight_zone.empty() ? std::string("ZONE_1") : fight_zone;
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned = loadout_owned(loadout.empty() ? std::string("Fists") : loadout);
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        app.set_headless_frames(1);  // uncapped deterministic stepping
        auto* fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
        std::fprintf(stdout, "[bossprobe] boot battle=%s zone=%s\n",
                     fight_battle.empty() ? "BOSS_LYNX" : fight_battle.c_str(),
                     fight_zone.empty() ? "ZONE_1" : fight_zone.c_str());
        std::fflush(stdout);
        if (fs == nullptr) {
            std::fprintf(stderr, "[bossprobe] no fight screen\n");
            app.shutdown();
            return 1;
        }
        // --- 1. wait for the live fight (past the 133-frame StartStance) ----
        //    The sim runs under the VS overlay; gate on `fight_frame`.
        int guard = 0;
        while (guard < 20000 && app.screens().current_id() == kScreenFight &&
               fs->fight_frame() < 140) {
            glfwPollEvents();
            app.run_one_frame();
            ++guard;
        }
        std::fprintf(stdout,
                     "[bossprobe] live at fight frame %d (guard %d) px=%.1f "
                     "ex=%.1f gap=%.1f\n",
                     fs->fight_frame(), guard,
                     static_cast<double>(fs->player_world_x()),
                     static_cast<double>(fs->enemy_world_x()),
                     static_cast<double>(fs->enemy_world_x() - fs->player_world_x()));
        std::fflush(stdout);
        // --- 2. approach: tap the toward-key until within punch range -------
        //    With the enemy on the RIGHT the Forward key (3) walks toward it;
        //    on the LEFT the Back key (7) does (the `vm.he` mirror; see the
        //    `--verify-place` mirror probe). Taps only — no OS input.
        for (int f = 0; f < 420; ++f) {
            glfwPollEvents();
            const float px = fs->player_world_x();
            const float ex = fs->enemy_world_x();
            if (std::fabs(ex - px) <= 70.0f) break;
            if (f % 8 == 0) {
                const int toward = (ex >= px) ? 3 : 7;
                fs->inject_game_key(toward, true);
                fs->inject_game_key(toward, false);
            }
            app.run_one_frame();
        }
        std::fprintf(stdout, "[bossprobe] after approach px=%.1f ex=%.1f gap=%.1f\n",
                     static_cast<double>(fs->player_world_x()),
                     static_cast<double>(fs->enemy_world_x()),
                     static_cast<double>(fs->enemy_world_x() - fs->player_world_x()));
        std::fflush(stdout);
        // --- 3. attack until a hit lands; then log ~150 frames --------------
        //    Re-park the boss at punch range right before each tap (the
        //    `--verify-place` throw technique: the AI drifts, and the move's
        //    Distance gate + the capsule overlap are measured at the tap).
        //    A parked boss stays in its `*StartStanceIdle`, which inherits
        //    the `<Stance>` template's Block interval (moves.xml L75-82), so
        //    a tap then is always ABSORBED (`[hit] ... BLOCK`). The boss
        //    only leaves the idle when its AI reaches the JS `Pqb` L606
        //    `else` path — i.e. while the PLAYER is not inside its own
        //    Uninterrupt window (`de.Ycb(b)` false, L604). So the probe taps
        //    ONLY while the boss is outside its stance idle (a real window);
        //    between windows it leaves the player idle, which is what lets
        //    the boss's `$E` QuickAttack slots fire at all.
        const int kLogFrames = 150;
        int react_at = -1;
        int react_started = 0;
        std::string react_move;
        for (int f = 0; f < 1200; ++f) {
            glfwPollEvents();
            if (react_at < 0) {
                const float px = fs->player_world_x();
                const float ex = fs->enemy_world_x();
                // Keep the pair at punch range (the AI drifts).
                if (std::fabs(ex - px) > 70.0f) {
                    const float side = (ex >= px) ? 1.0f : -1.0f;
                    fs->place_fighters(px, px + side * 55.0f);
                }
                // [FIX probe gate] The boss's LIVE idle is the
                // `*StartStanceIdle` family (`KnivesStartStanceIdle`) — the
                // exact same class the player's live idle belongs to. It is
                // NOT a transient "parked" state to exclude: excluding it
                // left `boss_hittable` permanently false (the boss never
                // leaves the idle on its own), so no tap was ever injected
                // -> NO-HIT. Taps landed while the boss guards are absorbed
                // (`[hit] ... BLOCK`), but they provoke the boss's AI, which
                // then leaves the idle and opens the real reaction window the
                // probe waits for. The player may only START a move while it
                // is itself idle, otherwise the tap restarts its move and the
                // pair stays permanently committed — which would stop the
                // boss's AI ever reaching the `Pqb` L606 `else` path again.
                const std::string boss_move = fs->enemy_current_move();
                const bool boss_hittable = !boss_move.empty();
                const std::string my_move = fs->player_current_move();
                // [FIX probe gate] The player's LIVE idle after the intro is
                // the `IdleStance` family — `StanceIdle` (moves.xml L1056,
                // Priority 0) — once the one-shot `StartIdleStance`
                // (`FistsStartStanceIdle-*`) opening frame is over. BOTH names
                // carry the `StanceIdle` substring, so match that; the old
                // `StartStanceIdle`-only test missed the plain `StanceIdle`
                // the player actually sits in, so `player_idle` was never true
                // and no attack was ever injected -> NO-HIT.
                const bool player_idle =
                    my_move.empty() ||
                    my_move.find("StanceIdle") != std::string::npos;
                if (boss_hittable && player_idle) {
                    const int atk = ((f / 8) % 2 == 0) ? 9 : 10;  // Punch / Kick
                    fs->inject_game_key(atk, true);
                    fs->inject_game_key(atk, false);
                }
            }
            app.run_one_frame();
            if (react_at < 0 && fs->enemy_ragdoll_active()) {
                react_at = f;
                react_started = fs->enemy_moves_started();
                react_move = fs->enemy_ragdoll_name();
                std::fprintf(stdout,
                             "[bossprobe] HIT at f=%d reaction='%s' nk=1 "
                             "started=%d\n",
                             f, react_move.c_str(), react_started);
                std::fflush(stdout);
            }
            if (react_at >= 0) {
                const int t = f - react_at;
                std::fprintf(stdout,
                             "[bossreact] t=%d move=%s nk=%d rframe=%d "
                             "started=%d px=%.1f ex=%.1f efac=%+.0f\n",
                             t, fs->enemy_current_move().c_str(),
                             fs->enemy_ragdoll_active() ? 1 : 0,
                             fs->enemy_ragdoll_frame(), fs->enemy_moves_started(),
                             static_cast<double>(fs->player_world_x()),
                             static_cast<double>(fs->enemy_world_x()),
                             static_cast<double>(fs->enemy_facing()));
                std::fflush(stdout);
                if (t + 1 >= kLogFrames) break;
            }
        }
        std::fprintf(stdout,
                     "[bossprobe] done: react_at=%d reaction='%s' "
                     "started_before=%d started_after=%d -> %s\n",
                     react_at, react_move.c_str(), react_started,
                     fs->enemy_moves_started(), react_at >= 0 ? "HIT" : "NO-HIT");
        std::fflush(stdout);
        app.shutdown();
        return react_at >= 0 ? 0 : 1;
    } else if (boss_loss_probe) {
        // [probe] Drive a COMPETENT scripted player through a full BOSS fight
        // via the internal `inject_game_key` path (NO OS input): approach the
        // boss, then punch/kick whenever the player is idle and in range,
        // through every round. The per-second `[fight]` HP log, the `[hit]`
        // damage lines and the `[fight] summary` carry the evidence.
        {
            PendingBattle& pb = app.pending_battle();
            pb.battle_name =
                fight_battle.empty() ? std::string("BOSS_LYNX") : fight_battle;
            pb.zone = fight_zone.empty() ? std::string("ZONE_1") : fight_zone;
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned =
                loadout_owned(loadout.empty() ? std::string("Fists") : loadout);
        }
        app.screens().push(make_screen(app.screens(), kScreenFight));
        app.set_headless_frames(1);
        auto* fs = static_cast<sf2::app::FightScreen*>(app.screens().top());
        std::fprintf(stdout, "[bossloss] boot battle=%s zone=%s loadout=%s\n",
                     fight_battle.empty() ? "BOSS_LYNX" : fight_battle.c_str(),
                     fight_zone.empty() ? "ZONE_1" : fight_zone.c_str(),
                     loadout.empty() ? "Fists" : loadout.c_str());
        std::fflush(stdout);
        if (fs == nullptr) {
            std::fprintf(stderr, "[bossloss] no fight screen\n");
            app.shutdown();
            return 1;
        }
        int guard = 0;
        while (guard < 20000 && app.screens().current_id() == kScreenFight &&
               fs->fight_frame() < 140) {
            glfwPollEvents();
            app.run_one_frame();
            ++guard;
        }
        int drive_frames = 0;
        for (int f = 0; f < 40000; ++f) {
            glfwPollEvents();
            if (app.screens().current_id() != kScreenFight) {
                drive_frames = f;
                break;
            }
            const float px = fs->player_world_x();
            const float ex = fs->enemy_world_x();
            if (std::fabs(ex - px) > 80.0f) {
                if (f % 8 == 0) {
                    const int toward = (ex >= px) ? 3 : 7;
                    fs->inject_game_key(toward, true);
                    fs->inject_game_key(toward, false);
                }
            } else {
                const std::string my_move = fs->player_current_move();
                const bool player_idle =
                    my_move.empty() ||
                    my_move.find("StanceIdle") != std::string::npos;
                if (player_idle) {
                    const int atk = ((f / 8) % 2 == 0) ? 9 : 10;  // Punch / Kick
                    fs->inject_game_key(atk, true);
                    fs->inject_game_key(atk, false);
                }
            }
            app.run_one_frame();
            drive_frames = f + 1;
        }
        std::fprintf(stdout, "[bossloss] end frames=%d screen=%d\n",
                     drive_frames, static_cast<int>(app.screens().current_id()));
        std::fflush(stdout);
        app.shutdown();
        return 0;
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
