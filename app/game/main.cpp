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

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <GLFW/glfw3.h>

#include "app/app.hpp"
#include "app/save_system.hpp"
#include "app/screens.hpp"

namespace {

using namespace sf2::app;  // kScreen* ids + the App/SaveSystem types

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [res_root] [save_path] [--headless N] [--autoclick] [--headless-loop]\n"
                  "                  [--fight] [--dump-pose N] [--dump-clip <name>]\n"
                  "                  [--ui-tour]\n"
                 "  res_root  default reference/www/res\n"
                 "  save_path default reference/saves/save.xml\n"
                 "  --headless-loop  run the scripted playable loop, then exit\n"
                 "                   (menu -> map -> Bosses fight -> results -> shop buy\n"
                 "                    WEAPON_KNIVES -> equip -> map -> Training fight -> results)\n"
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
    // Zone-tab pre-click (the map's zone strips; 0/0 = none). The map opens
    // on the save's zone (ZONE_1); fights living in another zone (Punchbag)
    // need their tab selected first (JS `Ya.HXa` L2123 strips).
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
//   dojo -> Map -> Bosses fight -> Results -> Map -> BACK to dojo
//   dojo -> Shop -> buy knives -> BACK to dojo
//   dojo -> Equipment -> equip knives -> BACK to dojo
//   dojo -> Map -> Training fight -> Results -> Map
//
// Layout math (matches the screen implementations in core/app/screens.cpp):
//   - dojo/menu buttons: y = 0.72*720 = 518, x = 0.28/0.46/0.64/0.82*1280
//     (FIGHT, MAP, SHOP, PROFILE).
//   - map nodes: x = X + 640, y = 360 - Y (stages.xml <Zone> coords):
//       Training (X=158,Y=145) -> (798, 215)
//       Bosses    (X=-100,Y=-40) -> (540, 400)
//     (the first zone = the tutorial zone the map shows).
//   - shop card grid: first card center (0.25*1280+150, 200+75).
//   - equipment owned-item grid: first card (0.55*1280+110, 220+40).
//   - BACK buttons: top-left (64, 40) on Map/Shop/Equipment (pops back to
//     the Dojo hub).
static const LoopStep kLoopSteps[] = {
    // 0: Dojo -> Map (the MAP button). Capture loop_map.png on arrival.
    {1280 * 0.46f, 720 * 0.72f, "dojo->map (MAP)", kScreenDojo, 0, kScreenMap, 0,
     "loop_map.png"},
    // 1: Map -> Bosses fight (540, 400) - the first money-bearing fight
    //    (Reward Money=70 Exp=10). The fight runs to KO (auto-attack) and
    //    pushes Results. Capture the fists fight (before-equip evidence).
    //    Punchbag tab (200, 38) first: the map opens on the save's ZONE_1.
    {540.0f, 400.0f, "map->Bosses fight", kScreenMap, 0, kScreenFight, 0,
     "loop_fight_fists.png", 200.0f, 38.0f},
    // 2: Results -> Map (click anywhere pops; the results->map flow pops
    //    the dead Fight screen too). Capture loop_results.png on arrival.
    {1280 * 0.5f, 360.0f, "results->map", kScreenResults, 0, kScreenMap, 0, "loop_results.png"},
    // 3: Map -> BACK to the Dojo hub (top-left).
    {64.0f, 40.0f, "map->dojo (BACK)", kScreenMap, 0, kScreenDojo, 0, nullptr},
    // 4: Dojo -> Shop (the SHOP button). Capture loop_shop.png on arrival.
    {1280 * 0.64f, 720 * 0.72f, "dojo->shop", kScreenDojo, 0, kScreenShop, 0,
     "loop_shop.png"},
    // 5: Shop -> buy WEAPON_KNIVES (first card, price 50).
    {1280 * 0.25f + 150.0f, 200.0f + 75.0f, "shop->buy WEAPON_KNIVES", kScreenShop, 0, -1, 12,
     nullptr},
    // 6: Shop -> BACK to the Dojo hub.
    {64.0f, 40.0f, "shop->dojo (BACK)", kScreenShop, 0, kScreenDojo, 0, nullptr},
    // 7: Dojo -> Equipment (the PROFILE button). Capture loop_equip.png on
    //    arrival.
    {1280 * 0.82f, 720 * 0.72f, "dojo->equipment (PROFILE)", kScreenDojo, 0,
     kScreenProfile, 0, "loop_equip.png"},
    // 8: Equipment -> equip WEAPON_KNIVES. With the 5 base items
    //    (Body/Head/Fists/NoRanged/NoMagic) + the bought knives, the grid
    //    (only Weapon/Armor/Helm cards count) is: Body(0) (704,220),
    //    Head(1) (944,220), Fists(2) (704,330), WEAPON_KNIVES(3) (944,330)
    //    - grid_x = 0.55*1280 = 704, card 3 = row 1 col 1.
    {1280 * 0.55f + 240.0f, 220.0f + 110.0f, "equip WEAPON_KNIVES", kScreenProfile, 0, -1, 12,
     nullptr},
    // 9: Equipment -> BACK to the Dojo hub.
    {64.0f, 40.0f, "equipment->dojo (BACK)", kScreenProfile, 0, kScreenDojo, 0, nullptr},
    // 10: Dojo -> Map again (MAP).
    {1280 * 0.46f, 720 * 0.72f, "dojo->map (MAP)", kScreenDojo, 0, kScreenMap, 0,
     nullptr},
    // 11: Map -> Training fight (798, 215) with the knives equipped.
    //     Capture loop_fight.png on arrival (the after-equip fight).
    //     Punchbag tab first (same zone reason as step 1).
    {798.0f, 215.0f, "map->Training fight (knives)", kScreenMap, 0, kScreenFight, 0,
     "loop_fight.png", 200.0f, 38.0f},
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
    int before_equip_moves = 0;
    int after_equip_moves = 0;
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
                    before_equip_moves = size;
                    logged_before = true;
                    std::fprintf(stdout, "[loop] move list before equip: %d moves (Fists)\n",
                                 size);
                } else if (step == 11) {
                    after_equip_moves = size;
                    logged_after = true;
                    std::fprintf(stdout, "[loop] move list after equip: %d moves (Knives)\n",
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
                             "[loop] move-list diff: %d moves (Fists) -> %d moves (Knives equipped)\n",
                             before_equip_moves, after_equip_moves);
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
};

static const UiTourStep kUiTourSteps[] = {
    // 0: Dojo hub at boot (fresh save) - settle then capture.
    {0.0f, 0.0f, "dojo hub", 3, 150, -1, 60, "port_dojo.png", 0, true},
    // 1: Dojo -> Map.
    {589.0f, 518.0f, "dojo->map", 3, 10, 5, 60, "port_map.png"},
    // 2: Map -> Dojo (BACK).
    {64.0f, 40.0f, "map->dojo", 5, 10, 3, 0, nullptr},
    // 3: Dojo -> Shop.
    {819.0f, 518.0f, "dojo->shop", 3, 10, 4, 60, "port_shop.png"},
    // 4: Shop tab 2 (same screen).
    {640.0f, 100.0f, "shop tab 2", 4, 10, -1, 40, "port_shop_tab2.png"},
    // 5: Shop -> Dojo (BACK).
    {64.0f, 40.0f, "shop->dojo", 4, 10, 3, 0, nullptr},
    // 6: Dojo -> Equipment (PROFILE).
    {1050.0f, 518.0f, "dojo->profile", 3, 10, 7, 60, "port_profile.png"},
    // 7: Equipment -> Dojo (BACK).
    {64.0f, 40.0f, "profile->dojo", 7, 10, 3, 0, nullptr},
    // 8: Dojo -> FIGHT (Training). Settle deep into phase 2 for the HUD.
    {358.0f, 518.0f, "dojo->fight", 3, 10, 6, 250, "port_fight.png"},
    // 9: Pause via P, capture the pause menu.
    {0.0f, 0.0f, "pause", 6, 10, -1, 40, "port_pause.png", 80},
    // 10: Resume via P, run to KO -> Results captures on arrival.
    {0.0f, 0.0f, "resume->results", 6, 10, 10, 0, "port_results.png", 80},
    // 11: Results -> Dojo (the tour went Dojo->Fight directly, so the
    // results flow pops back to the hub, unlike the loop's map route).
    {640.0f, 360.0f, "results->dojo", 10, 10, 3, 0, nullptr},
    // 12: Dojo -> Settings (SETUP).
    {85.0f, 34.0f, "dojo->settings", 3, 10, 11, 0, "port_settings.png"},
};
constexpr int kUiTourStepCount = static_cast<int>(sizeof(kUiTourSteps) / sizeof(kUiTourSteps[0]));

struct UiTourDriver {
    int step = 0;
    int step_frame = 0;
    int last_seen = -1;
    bool acted = false;
    bool key_up_done = false;
    bool next_clicked = false;
    int guard = 0;
    bool finished = false;

    void frame_tick(sf2::app::App& app) {
        const UiTourStep& s = kUiTourSteps[step];
        const int cur = app.screens().current_id();
        if (cur != last_seen) {
            last_seen = cur;
            std::fprintf(stdout, "[tour] screen %d (step %d/%d %s)\n", cur, step + 1,
                         kUiTourStepCount, s.label);
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
            std::fprintf(stdout, "[tour] round_wait -> NEXT click (%.0f, %.0f)\n", cx, cy);
            std::fflush(stdout);
        } else if (!fight_waiting) {
            next_clicked = false;
        }

        if (!acted) {
            if (cur == s.wait_screen && step_frame >= s.min_delay) {
                if (s.key != 0) {
                    std::fprintf(stdout, "[tour] step %d/%d %s -> key %d\n", step + 1,
                                 kUiTourStepCount, s.label, s.key);
                    std::fflush(stdout);
                    app.inject_key(s.key, true);
                } else if (!s.no_click) {
                    std::fprintf(stdout, "[tour] step %d/%d %s -> click (%.0f, %.0f)\n",
                                 step + 1, kUiTourStepCount, s.label, s.x, s.y);
                    std::fflush(stdout);
                    app.inject_click(s.x, s.y);
                } else {
                    std::fprintf(stdout, "[tour] step %d/%d %s -> settle\n", step + 1,
                                 kUiTourStepCount, s.label);
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
            const std::string path = std::string("reference/traces/ui/") + s.capture;
            app.capture_png(path);
            std::fprintf(stdout, "[tour] capture %s\n", s.capture);
            std::fflush(stdout);
        }
        std::fprintf(stdout, "[tour] step done (%s)\n", s.label);
        std::fflush(stdout);
    }

    void advance() {
        ++step;
        step_frame = 0;
        acted = false;
        key_up_done = false;
        if (step >= kUiTourStepCount) {
            finished = true;
            std::fprintf(stdout, "[tour] ALL %d STEPS DONE\n", kUiTourStepCount);
            std::fflush(stdout);
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
    bool capture_fight = false;
    bool capture_idle_fight = false;  // --capture-idle-fight-at N: boot direct + no input, capture at fight frame N
    bool auto_attack = false;
    bool fight_mode = false;  // --fight: boot DIRECTLY into the dojo fight
    int capture_fight_frame = 300;  // fight frames after the Fight screen appears
    std::string capture_dir;  // when set, capture screens to this dir
    std::string dump_clip;    // --dump-clip <name>: dump one anim clip as JSON, exit
    int dump_pose_frames = 0;  // --dump-pose N: dump the first N fight frames (0 = off)

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
        UiTourDriver driver;
        app.set_auto_attack(false);
        app.set_headless_frames(1);
        std::filesystem::create_directories("reference/traces/ui");
        while (!driver.finished && driver.guard < 60000) {
            glfwPollEvents();
            driver.frame_tick(app);
            app.run_one_frame();
            ++driver.guard;
        }
        if (!driver.finished) {
            std::fprintf(stderr, "[tour] did not finish after %d frames (step %d)\n",
                         driver.guard, driver.step);
            return 1;
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
            pb.battle_name = "Training";
            pb.location = "dojo";
            pb.has_result = false;
            pb.reward_money = 0;
            pb.reward_exp = 0;
            pb.owned.clear();
            std::fprintf(stdout, "[fight] direct boot: battle=%s location=%s owned=%zu\n",
                         pb.battle_name.c_str(), pb.location.c_str(), pb.owned.size());
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
