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

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "app/app.hpp"
#include "app/save_system.hpp"

namespace {

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [res_root] [save_path] [--headless N] [--autoclick]\n"
                 "  res_root  default reference/www/res\n"
                 "  save_path default reference/saves/save.xml\n",
                 argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string res_root = "reference/www/res";
    std::string save_path = "reference/saves/save.xml";
    int headless = 0;
    bool auto_click = false;
    std::string capture_dir;  // when set, capture screens to this dir

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless" && i + 1 < argc) {
            headless = std::atoi(argv[++i]);
        } else if (arg == "--autoclick") {
            auto_click = true;
        } else if (arg == "--capture" && i + 1 < argc) {
            capture_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (res_root == "reference/www/res" && save_path == "reference/saves/save.xml") {
            // First positional = res_root, second = save_path.
            res_root = arg;
        } else if (save_path == "reference/saves/save.xml") {
            save_path = arg;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    // Verify the save round-trip before opening the window (the same
    // SaveSystem the shell uses): load users_default -> bump money ->
    // save -> reload -> money persists. Uses a throwaway path so the real
    // save file starts from the template.
    {
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

    if (!capture_dir.empty()) {
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
