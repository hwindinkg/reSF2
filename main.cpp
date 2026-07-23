#include "engine/game/game.hpp"

int main(int argc, char* argv[]) {
    std::string asset_root;
    std::string input_script_path;
    std::string start_location = "dojo";
    int max_frames = -1;  // -1 = unlimited
    bool replay_mode = false;  // skip menus, go directly to Battle
    bool dump_state = false;   // --dump-state: print structured state every frame
    bool list_locations = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--assets" && i + 1 < argc) asset_root = argv[++i];
        else if (arg == "--input-script" && i + 1 < argc) input_script_path = argv[++i];
        else if (arg == "--max-frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (arg == "--replay") replay_mode = true;
        else if (arg == "--dump-state") dump_state = true;
        else if (arg == "--no-log") g_debug_log_enabled = false;
        else if (arg == "--list-locations") list_locations = true;
        else if ((arg == "--location" || arg == "-l") && i + 1 < argc) start_location = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: resf2_app [--assets <path>]\n"
                        "                 [--input-script <path>] [--max-frames N]\n"
                        "                 [--replay] [--dump-state] [--no-log]\n"
                        "                 [--list-locations] [--location <name>]\n");
            return 0;
        }
    }
    // If --list-locations is specified, create a minimal Game to discover locations
    // and print them, then exit.
    if (list_locations) {
        resf2::game::Game temp_game(asset_root.empty() ? "." : asset_root);
        auto names = temp_game.location_names();
        std::printf("\n=== Discovered %zu locations ===\n\n", names.size());
        for (const auto& name : names) {
            std::printf("  %s\n", name.c_str());
        }
        std::printf("\nUse: resf2_app [--assets <path>] --location <name>\n");
        return 0;
    }
    // [ORIGINAL] Initialize debug log file in the asset root.
    if (g_debug_log_enabled) {
        std::string log_path = asset_root.empty() ? "resf2_debug.log" :
            (std::filesystem::path(asset_root) / "resf2_debug.log").string();
        debug_log_init(log_path);
        std::printf("[LOG] Debug log: %s\n", log_path.c_str());
    }
    auto platform = std::make_unique<plat::GlfwPlatform>();
    plat::WindowConfig cfg;
    cfg.title = "reSF2 - Shadow Fight 2";
    cfg.width = 1280; cfg.height = 720; cfg.vsync = true;
    if (!platform->init(cfg)) {
        std::fprintf(stderr, "Platform init failed.\n"); return 1;
    }
    // [DIAGNOSTIC] Load deterministic input script if provided.
    if (!input_script_path.empty()) {
        (void)platform->load_input_script(input_script_path);
    }
    resf2::game::Game game(asset_root, replay_mode, dump_state);
    if (!start_location.empty() && start_location != "dojo") {
        game.set_start_location(start_location);
    }
    if (!platform->make_gl_current()) {
        std::fprintf(stderr, "Failed to make GL context current.\n"); return 1;
    }
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
        else if (!is_paused && was_paused) {
            game.on_resume(*platform); was_paused = false; last_ms = platform->now_ms();
        }
        if (is_paused) { platform->sleep_ms(100); continue; }
        auto now = platform->now_ms();
        auto dt = (std::min)(now > last_ms ? (uint32_t)(now - last_ms) : 0u, 200u);
        last_ms = now;
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
