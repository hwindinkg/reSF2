// tests/tool_visual_audit.cpp
//
// Visual audit tool: renders the dojo headlessly and dumps every geometric
// quantity the framing depends on, so the remaining visual divergences can be
// measured instead of eyeballed.
//
// Motivation: the engine's screenshots show the location background loading but
// the composition wrong — no visible floor strip, the fighter mis-scaled, the
// punching bag drawn as a plain rectangle. game_clean.hpp carries
// [HEURISTIC-TODO] markers admitting the camera framing and the fighter's
// model->world mapping are unverified. This prints the actual numbers.
//
// Reference: the original at 1440x720 puts the floor line at ~87.5% of the
// frame height and the two dojo fighters at ~36% / ~64% of the width.
//
// Usage:
//   tool_visual_audit [asset_root] [out_dir]

#include "headless_test_runner.hpp"

// The implementation lives in resf2_renderer (stb_image_impl.cpp); we only
// need the declaration here.
#include "engine/renderer/stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace resf2;

namespace {

struct Row {
    std::string label;
    std::string value;
};

void print_section(const char* title) {
    std::printf("\n=== %s ===\n", title);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string asset_root = argc > 1 ? argv[1] : "assets";
    const std::string out_dir = argc > 2 ? argv[2] : "screenshots_audit";

    test::HeadlessTestConfig cfg;
    cfg.asset_root = asset_root;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.fixed_dt_ms = 16;
    // Open the dojo directly. On a fresh save the tutorial pushes a Dialogue
    // scene over it, and Dialogue does not call host_update_gameplay, so the
    // fighter would never animate and the audit would measure a frozen pose.
    cfg.start_scene = "dojo";

    test::HeadlessTestRunner runner(cfg);
    if (!runner.init()) {
        std::fprintf(stderr, "[audit] init failed\n");
        return 1;
    }

    // Walk Boot -> Loading -> MainMenu. The tutorial pushes a Dialogue scene
    // over the dojo on a fresh save, and Dialogue does NOT call
    // host_update_gameplay, so the fighter never animates while it is up. That
    // must be dismissed or the audit measures a frozen rest pose.
    // Dismiss the tutorial dialog if it still appears, then let the dojo run
    // long enough for the stance animation to advance past its first frame.
    runner.run_frames(30);
    for (int attempt = 0; attempt < 12; ++attempt) {
        runner.tap_key(platform::Key::Escape);
        runner.run_frames(6);
    }
    runner.run_frames(180);

    auto& g = runner.game();

    print_section("location");
    g.audit_dump_location();

    print_section("camera / framing");
    g.audit_dump_camera();

    print_section("fighter transform");
    g.audit_dump_fighter();

    print_section("punching bag");
    g.audit_dump_bag();

    // Capture a frame for visual inspection.
    if (const auto* adapter = runner.renderer()) {
        const auto& soft = adapter->soft_renderer();
        const auto pixels = soft.framebuffer();
        const int w = soft.width();
        const int h = soft.height();
        if (!pixels.empty() && w > 0 && h > 0) {
            std::error_code ec;
            std::filesystem::create_directories(out_dir, ec);
            const std::string path = out_dir + "/audit_dojo.png";
            if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4)) {
                std::printf("\nwrote %s (%dx%d)\n", path.c_str(), w, h);
            } else {
                std::fprintf(stderr, "\n[audit] png write failed: %s\n", path.c_str());
            }
        } else {
            std::fprintf(stderr, "\n[audit] empty framebuffer\n");
        }
    }

    return 0;
}
