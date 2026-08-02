// Probe: reproduce the soak D4 scenario — map FIGHT click -> Dialogue (2 lines)
// -> Space advance. Drive the REAL MapScene::on_update click path.
#include "headless_test_runner.hpp"
#include <cstdio>

namespace plat = resf2::platform;
namespace scn = resf2::scene;

int main() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = "map";
    resf2::test::HeadlessTestRunner runner(config);
    if (!runner.init()) { std::fprintf(stderr, "init failed\n"); return 1; }

    // Let the map build nodes (rebuild happens on render).
    runner.run_frames(10);

    // Click the FIGHT button. MapLayout at 320x180:
    //   panel_h = top_panel_h(180); scroll on the right; fight button inside.
    // Print the layout numbers by re-deriving them (from scenes.cpp map_layout).
    const float h = 180.0f;
    const float w = 320.0f;
    const float panel_h = resf2::ui::top_panel_h(h);
    const float scroll_w = w * 0.235f;
    const float scroll_x = w - scroll_w - w * 0.012f;
    const float scroll_y = panel_h + h * 0.055f;
    const float scroll_h = h - scroll_y - h * 0.105f;
    const float fight_w = scroll_w * 0.86f;
    const float fight_h = scroll_h * 0.13f;
    const float fight_x = scroll_x + (scroll_w - fight_w) * 0.5f;
    const float fight_y = scroll_y + scroll_h - fight_h - scroll_h * 0.11f;
    std::fprintf(stderr, "[probe] fight rect x=%.1f y=%.1f w=%.1f h=%.1f\n",
                 fight_x, fight_y, fight_w, fight_h);

    runner.platform().poll_events();
    runner.inject_pointer_down(fight_x + fight_w / 2, fight_y + fight_h / 2, 0);
    runner.game().on_update(runner.platform(), 16);
    runner.game().on_render(runner.platform());
    runner.platform().advance_time_ms(16);
    runner.inject_pointer_up(0);
    runner.game().on_update(runner.platform(), 16);
    runner.game().on_render(runner.platform());
    runner.platform().advance_time_ms(16);

    std::fprintf(stderr, "[probe] after FIGHT click: scene=%d dialogue_lines=%zu\n",
                 (int)runner.game().host_get_current_scene(),
                 runner.game().host_get_dialogue().size());

    for (int i = 0; i < 30; ++i) {
        runner.run_frames(1);
        if (runner.game().host_get_current_scene() == scn::SceneId::Dialogue) break;
    }
    std::fprintf(stderr, "[probe] scene after settle=%d lines=%zu\n",
                 (int)runner.game().host_get_current_scene(),
                 runner.game().host_get_dialogue().size());

    // Now press Space: does line advance? (watch the scene and the log)
    runner.tap_key(plat::Key::Space, 1);
    runner.run_frames(5);
    std::fprintf(stderr, "[probe] after Space #1: scene=%d\n",
                 (int)runner.game().host_get_current_scene());
    runner.tap_key(plat::Key::Space, 1);
    runner.run_frames(5);
    std::fprintf(stderr, "[probe] after Space #2: scene=%d\n",
                 (int)runner.game().host_get_current_scene());
    return 0;
}
