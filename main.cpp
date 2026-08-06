#include "engine/game/game.hpp"

int main(int argc, char* argv[]) {
    std::string asset_root;
    std::string input_script_path;
    std::string start_location = "dojo";
    int max_frames = -1;
    bool replay_mode = false;
    bool dump_state = false;
    bool list_locations = false;
    bool debug_world = false;
    // [Wave 10A defect 3] E2E hooks: --round-time <s> forces the battle
    // round clock (stages.xml RoundTime is 99 s everywhere); --tutorial-start
    // starts the Sensei tutorial flow in a scripted run.
    int round_time_override_s = 0;
    bool tutorial_start = false;
    // Window size is a command-line option so the resolution-dependent layout
    // rules can actually be exercised. Everything in the HUD is scaled from the
    // viewport height (render_hud, menu_roll_rect), and a rule that is only ever
    // run at one size is a rule nobody has checked.
    int win_w = 1280, win_h = 720;
    std::string start_scene;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--assets" && i + 1 < argc) asset_root = argv[++i];
        else if (arg == "--input-script" && i + 1 < argc) input_script_path = argv[++i];
        else if (arg == "--max-frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (arg == "--replay") replay_mode = true;
        else if (arg == "--dump-state") dump_state = true;
        else if (arg == "--no-log") g_debug_log_enabled = false;
        else if (arg == "--list-locations") list_locations = true;
        else if (arg == "--debug-world") debug_world = true;
        else if (arg == "--scene" && i + 1 < argc) start_scene = argv[++i];
        else if (arg == "--round-time" && i + 1 < argc) round_time_override_s = std::atoi(argv[++i]);
        else if (arg == "--tutorial-start") tutorial_start = true;
        else if (arg == "--window" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const size_t x = spec.find_first_of("xX");
            if (x != std::string::npos) {
                const int w = std::atoi(spec.substr(0, x).c_str());
                const int h = std::atoi(spec.substr(x + 1).c_str());
                if (w > 0 && h > 0) { win_w = w; win_h = h; }
                else std::fprintf(stderr, "--window: bad size '%s', keeping %dx%d\n",
                                  spec.c_str(), win_w, win_h);
            } else {
                std::fprintf(stderr, "--window: expected WxH, got '%s'\n", spec.c_str());
            }
        }
        else if ((arg == "--location" || arg == "-l") && i + 1 < argc) start_location = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: resf2_app [--assets <path>]\n"
                        "                 [--input-script <path>] [--max-frames N]\n"
                        "                 [--replay] [--dump-state] [--no-log]\n"
                        "                 [--list-locations] [--location <name>]\n"
                        "                 [--debug-world]   world-geometry overlay (F1 toggles)\n"
                        "                 [--window WxH]    viewport size (default 1280x720)\n"
                        "                 [--scene <name>]  open a screen directly:\n"
                        "                   dojo map shop settings dialogue battle results profile\n");
            return 0;
        }
    }
    if (list_locations) {
        resf2::game::Game temp_game(asset_root.empty() ? "." : asset_root);
        auto names = temp_game.location_names();
        std::printf("\n=== Discovered %zu locations ===\n\n", names.size());
        for (const auto& name : names) std::printf("  %s\n", name.c_str());
        std::printf("\nUse: resf2_app [--assets <path>] --location <name>\n");
        return 0;
    }
    if (g_debug_log_enabled) {
        std::string log_path = asset_root.empty() ? "resf2_debug.log" :
            (std::filesystem::path(asset_root) / "resf2_debug.log").string();
        debug_log_init(log_path);
        std::printf("[LOG] Debug log: %s\n", log_path.c_str());
    }
    auto platform = std::make_unique<plat::GlfwPlatform>();
    plat::WindowConfig cfg;
    cfg.title = "reSF2 - Shadow Fight 2";
    cfg.width = win_w; cfg.height = win_h; cfg.vsync = true;
    if (!platform->init(cfg)) { std::fprintf(stderr, "Platform init failed.\n"); return 1; }
    if (!input_script_path.empty()) (void)platform->load_input_script(input_script_path);
    resf2::game::Game game(asset_root, replay_mode, dump_state);
    if (!start_location.empty() && start_location != "dojo") game.set_start_location(start_location);
    game.set_debug_world(debug_world);
    // A scripted run must not inherit the machine's saved profile — see the
    // comment at the host_load_progress() call site. Same reasoning as the
    // fixed timestep below.
    game.set_hermetic_run(!input_script_path.empty());
    game.set_start_scene(start_scene);
    game.set_round_time_override(round_time_override_s);
    game.set_tutorial_start(tutorial_start);
    if (!platform->make_gl_current()) { std::fprintf(stderr, "Failed to make GL context current.\n"); return 1; }
    game.on_init(*platform);
    auto last_ms = platform->now_ms();
    bool was_paused = false;
    int frame_count = 0;
    while (true) {
        if (!platform->poll_events()) break;
        if (platform->should_quit()) break;
        if (game.quit_requested()) break;
        bool is_paused = platform->is_paused();
        if (is_paused && !was_paused) { game.on_pause(*platform); was_paused = true; }
        else if (!is_paused && was_paused) { game.on_resume(*platform); was_paused = false; last_ms = platform->now_ms(); }
        if (is_paused) { platform->sleep_ms(100); continue; }
        auto now = platform->now_ms();
        auto dt = (std::min)(now > last_ms ? (uint32_t)(now - last_ms) : 0u, 200u);
        last_ms = now;
        // A scripted run is a measurement, so it must not depend on how fast
        // this machine happens to render. With the wall clock the physics
        // stepped differently every time and a marginal collision landed in
        // some runs and not others — which is how "the bag never reacts" was
        // measured from a run where the punch simply missed.
        if (!input_script_path.empty()) dt = 16;
        game.on_update(*platform, dt);
        game.on_render(*platform);
        platform->swap_buffers();
        ++frame_count;
        if (max_frames > 0 && frame_count >= max_frames) break;
    }
    game.on_shutdown(*platform);
    platform->shutdown();
    debug_log_close();
    return 0;
}
