// tests/integration/test_soak_dialogue_defects.cpp
//
// Soak-fix Wave 4 (reverse/analysis/SOAK_TRIAGE.md §4 Dialogue): behavioral
// tests for D1-D6, written from the player's perspective — each asserts what
// the player SEES, not what the pipeline traces.
//
//   D1: the dialogue panel must be CENTERED and must NOT darken the whole
//       background. The soak showed a bottom-anchored box over a full-screen
//       dim.
//   D2: after the dialogue ends, the game must return to the scene it came
//       from WITHOUT re-loading the location. The soak showed the dojo fully
//       reloading after the dialogue.
//   D3: the full line must be visible IMMEDIATELY (no letter-by-letter
//       typewriter reveal). The soak showed text appearing character by
//       character.
//   D4 (blocker): each confirm input advances to the NEXT line and the last
//       line completes the dialogue. The soak showed the dialogue stuck on
//       line 1/2 forever ("typewriter=100% line=1/2" repeating).
//   D5: in an English session the dialogue text must resolve from eng.xml
//       (Latin), never from the Russian file, and the HUD font must be the
//       English font (eng/sakkal.fnt), not the Russian one.
//   D6: the dialogue scroll's bottom texture must not be stretched beyond
//       the panel (the "подложка" the soak showed stretched too far).
//
// All probes FAIL on the current engine (RED evidence committed with this
// file). Fixes are implemented test-first: no fix before these RED tests.

#include "../headless_test_runner.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Suppress noisy stdout from the game's internal logging so the test
// doesn't time out from I/O overhead. FAIL diagnostics go to stderr.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;
namespace scn = resf2::scene;

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

// Count occurrences of `needle` in a stdout capture file, then delete it.
static int count_in_capture(const char* path, const char* needle) {
    int n = 0;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
            if (line.find(needle) != std::string::npos) ++n;
    }
    std::remove(path);
    return n;
}

// Find the first "HUD font loaded:" line in a stdout capture file, then
// delete the file. The font is loaded once during location init, so the
// capture must cover the init frames.
static std::string font_line_in_capture(const char* path) {
    std::string found;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("HUD font loaded:") != std::string::npos) {
                found = line;
                break;
            }
        }
    }
    std::remove(path);
    return found;
}

int main() {
    std::printf("=== Soak Dialogue Defects Test (D1-D6) ===\n");
    std::fflush(stdout);
    suppress_stdout();

    // ---- D4 (blocker): confirm advances line 1 -> line 2 -> complete ----
    // Drive the Dialogue scene with 2 injected lines, the same shape the map
    // FIGHT button produces ({"Sly", battle}, {"Narrator", location}). Each
    // Space press must advance one line; the second press must end the scene.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D4 init() returned false\n"); return 1; }
        // Reach the dojo (MainMenu) first so the dialogue has a host scene.
        runner.run_frames(200);

        runner.game().host_set_battle_location("dojo");  // like the map FIGHT flow
        runner.game().host_set_dialogue({{"Sly", "BOSS_LYNX"},
                                         {"Narrator", "Location: dojo"}});
        runner.game().request_scene_transition(scn::SceneId::Dialogue);
        runner.run_frames(2);  // apply the transition, enter the scene

        CHECK(runner.game().host_get_current_scene() == scn::SceneId::Dialogue,
              "D4: the dialogue scene is active with 2 lines");
        CHECK(runner.game().host_get_dialogue().size() == 2,
              "D4: the dialogue carries 2 lines");

        // First confirm: line 1 -> line 2 (still inside the dialogue).
        // [D4] The soak player was pressing their COMBAT keys (P punch /
        // O kick — the only keys in the whole session log) and the dialogue
        // never advanced: the scene only listened for Space/Enter/click.
        // Each input must advance the dialogue, like the original's
        // tap-anywhere. Drive the advance with P (punch), the key the
        // soak player actually used.
        const char* cap = "test_soak_dialogue_d4_a.tmp";
        std::freopen(cap, "w", stdout);
        runner.tap_key(plat::Key::P, 1);
        // Wait for the line-2 reveal to complete so the renderer reports it.
        runner.run_frames(200);
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif
        const int line2_logs = count_in_capture(cap, "typewriter=100% line=2/2");
        std::fprintf(stderr, "  [D4] after 1st P: line=2/2 logged %d time(s)\n",
                     line2_logs);
        CHECK(runner.game().host_get_current_scene() == scn::SceneId::Dialogue,
              "D4: one Space advances 1 -> 2 and stays in the dialogue");
        CHECK(line2_logs >= 1,
              "D4: the renderer reports the SECOND line after one confirm");

        // Second confirm: last line completes -> exit to Battle (dojo queued).
        runner.tap_key(plat::Key::P, 1);
        bool exited = false;
        for (int i = 0; i < 30; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_current_scene() != scn::SceneId::Dialogue) {
                exited = true;
                break;
            }
        }
        std::fprintf(stderr, "  [D4] after 2nd Space: scene=%d exited=%d\n",
                     (int)runner.game().host_get_current_scene(), (int)exited);
        CHECK(exited, "D4: the last line completes the dialogue (scene exits)");
    }

    // ---- D3: the full line is visible immediately ----
    // The typewriter reveal (30 ms/char) shows partial text for a long line.
    // A 60-char line must be fully revealed on the very first render frame.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D3 init() returned false\n"); return 1; }
        runner.run_frames(200);
        const std::string long_line(80, 'x');  // 80 chars: 2.4 s of reveal at 30 ms/char
        runner.game().host_set_dialogue({{"Sensei", long_line}});
        runner.game().request_scene_transition(scn::SceneId::Dialogue);
        runner.run_frames(1);  // enter the scene

        const char* cap = "test_soak_dialogue_d3.tmp";
        std::freopen(cap, "w", stdout);
        runner.run_frames(3);  // first frames of the dialogue
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif
        const int full_logs = count_in_capture(cap, "typewriter=100% line=1/1");
        std::fprintf(stderr, "  [D3] full-line reveal logged %d time(s) in 3 frames\n",
                     full_logs);
        CHECK(full_logs >= 1,
              "D3: the full 80-char line is revealed on the first frames "
              "(no letter-by-letter typewriter)");
    }

    // ---- D1: panel centered, no full-screen dim ----
    // The dialogue render must NOT paint a full-screen black overlay, and the
    // parchment panel must sit in the vertical middle of the screen.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D1 init() returned false\n"); return 1; }
        // Settle in the dojo (MainMenu), then open the dialogue. The Dialogue
        // scene does NOT re-render the location — the framebuffer shows the
        // dojo's clear color (#281408 = 40,20,8) wherever the panel isn't.
        // The full-screen dim {0,0,0,70} scales it to ~0.73x = (29,14,6);
        // asserting the undimmed value detects the dim exactly.
        runner.run_frames(200);

        auto* sw = runner.renderer();
        CHECK(sw != nullptr, "D1: software renderer available");
        if (sw) {
            const auto& fb = sw->soft_renderer().framebuffer();
            const int w = sw->soft_renderer().width();
            const int h = sw->soft_renderer().height();
            CHECK((int)fb.size() >= w * h * 4, "D1: framebuffer is populated");

            auto px = [&](int x, int y) {
                size_t i = ((size_t)y * w + x) * 4;
                return std::make_tuple(fb[i], fb[i+1], fb[i+2]);
            };

            runner.game().host_set_dialogue({{"Sensei", "A short line."}});
            runner.game().request_scene_transition(scn::SceneId::Dialogue);
            runner.run_frames(2);

            // Corner DURING the dialogue: undimmed dojo clear color is
            // (40,20,8); the dim darkens it to (29,14,6) (r < 32).
            auto [cr, cg, cb] = px(8, 8);
            const bool dimmed = cr < 32;
            std::fprintf(stderr,
                         "  [D1] corner during dialogue=(%d,%d,%d) dimmed=%d\n",
                         cr, cg, cb, (int)dimmed);
            CHECK(!dimmed,
                  "D1: no full-screen dim darkens the background corner");

            // Parchment panel: scan the frame for the parchment fill
            // {226,205,163}. Compute the vertical centre of the parchment
            // band and assert it is near the screen middle, not the bottom.
            int top = -1, bottom = -1;
            for (int y = 0; y < h; ++y) {
                for (int x = w / 4; x < 3 * w / 4; ++x) {
                    auto [r, g, b] = px(x, y);
                    if (r > 200 && g > 180 && b > 130) {
                        if (top < 0) top = y;
                        bottom = y;
                    }
                }
            }
            if (top >= 0) {
                const int mid = (top + bottom) / 2;
                const int screen_mid = h / 2;
                std::fprintf(stderr,
                             "  [D1] parchment band y=[%d..%d] mid=%d screen_mid=%d\n",
                             top, bottom, mid, screen_mid);
                CHECK(mid > (int)(h * 0.35f) && mid < (int)(h * 0.65f),
                      "D1: the parchment panel is vertically centred");
            } else {
                CHECK(false, "D1: parchment panel pixels found in the frame");
            }
        }
    }

    // ---- D2: dialogue exit returns without reloading the location ----
    // The dojo must NOT be re-loaded when the dialogue ends. A reload shows
    // up as a second location load log line; the location load counter must
    // not increase across the dialogue.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D2 init() returned false\n"); return 1; }
        // Load the dojo (MainMenu enter does it) and settle.
        runner.run_frames(250);
        CHECK(runner.game().host_location_loaded(),
              "D2: the dojo location is loaded before the dialogue");

        const char* cap = "test_soak_dialogue_d2.tmp";
        std::freopen(cap, "w", stdout);
        // Enter the dialogue (no battle behind it: the tutorial flow).
        runner.game().host_set_dialogue({{"Sensei", "line one"}, {"Sensei", "line two"}});
        runner.game().request_scene_transition(scn::SceneId::Dialogue);
        runner.run_frames(2);
        runner.tap_key(plat::Key::P, 1);
        runner.run_frames(2);
        runner.tap_key(plat::Key::P, 1);
        for (int i = 0; i < 60; ++i) {
            runner.run_frames(1);
            if (runner.game().host_get_current_scene() != scn::SceneId::Dialogue) break;
        }
        runner.run_frames(30);  // let any reload happen
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif
        // A dojo reload re-runs init_location(), which re-prints the
        // "Localization 'eng': N strings" line. Zero new prints across the
        // dialogue = the location was NOT re-loaded.
        const int location_loads = count_in_capture(cap, "Localization 'eng':");
        std::fprintf(stderr, "  [D2] localization (re)loads during dialogue+exit: %d\n",
                     location_loads);
        CHECK(location_loads == 0,
              "D2: no location reload during/after the dialogue");
    }

    // ---- D5: English session resolves English strings + English font ----
    // userSettings.xml selects eng.xml; the dialogue must resolve Latin text,
    // and the HUD font must be the eng/sakkal.fnt — not the Russian font.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D5 init() returned false\n"); return 1; }

        // Capture stdout from the very first frame: the HUD font is loaded
        // once during location init, so its log line only lands in the
        // capture if the capture covers the init frames.
        const char* cap = "test_soak_dialogue_d5.tmp";
        std::freopen(cap, "w", stdout);
        runner.run_frames(250);  // location init + settle in the dojo
#ifdef _WIN32
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif

        // The tutorial dialogue lines are resolved through the localization
        // map. In an English session they must be Latin text.
        const std::string line1 = runner.game().host_localized("tutorial_begin_1");
        std::fprintf(stderr, "  [D5] tutorial_begin_1 = '%s'\n", line1.c_str());
        bool latin = !line1.empty();
        for (unsigned char c : line1) {
            if (c >= 0x80) { latin = false; break; }  // any non-ASCII byte -> not Latin
        }
        CHECK(latin, "D5: tutorial_begin_1 resolves to Latin (eng.xml) text");

        // The HUD font for an English session must be the English font:
        // the init log line "HUD font loaded: ...eng\sakkal.fnt ..." names
        // the file that was actually parsed.
        const std::string font_line = font_line_in_capture(cap);
        std::fprintf(stderr, "  [D5] font log: %s\n", font_line.c_str());
        CHECK(font_line.find("eng") != std::string::npos,
              "D5: the HUD font log names the eng/ font for an English session");
        CHECK(font_line.find("rus") == std::string::npos,
              "D5: the HUD font is not the Russian font");
    }

    // ---- D6: the scroll's bottom texture is not stretched beyond the panel ----
    // The dialogue parchment is ~0.20*H tall; the bottom roll bar must stay
    // within the panel, not extend past it. We assert the panel's drawn
    // height (parchment band) matches the intended box height within
    // tolerance instead of being stretched far beyond content.
    {
        resf2::test::HeadlessTestRunner runner = make_dojo_runner();
        if (!runner.init()) { std::fprintf(stderr, "FAIL: D6 init() returned false\n"); return 1; }
        runner.run_frames(200);
        runner.game().host_set_dialogue({{"Sensei", "A short line."}});
        runner.game().request_scene_transition(scn::SceneId::Dialogue);
        runner.run_frames(2);

        auto* sw = runner.renderer();
        if (sw) {
            const auto& fb = sw->soft_renderer().framebuffer();
            const int w = sw->soft_renderer().width();
            const int h = sw->soft_renderer().height();
            auto px = [&](int x, int y) {
                size_t i = ((size_t)y * w + x) * 4;
                return std::make_tuple(fb[i], fb[i+1], fb[i+2]);
            };
            // The parchment fill {226,205,163} is the panel's identity: it is
            // the only flat paint with that color on screen. A loose warm
            // filter (r>120&&g>100&&b>60) ALSO matches the dojo's wood tones,
            // and since Wave 10A D4 the location renders BEHIND the dialogue
            // (the intended "no full-screen dim" behavior), the old probe
            // measured the whole background as "panel" (drawn 150 px vs the
            // 36 px box). Measure the fill band at the CENTER column (inside
            // the panel, clear of the avatar and the side edges) and assert
            // it sits where the box does: the band must reach the box's
            // bottom edge (a sheet drawn/stretched past it fails) and start
            // at least halfway up (a panel dropped to the bottom fails).
            auto is_fill = [&](int x, int y) {
                auto [r, g, b] = px(x, y);
                return std::abs(r - 226) <= 35 && std::abs(g - 205) <= 35 &&
                       std::abs(b - 163) <= 35;
            };
            const int cx = w / 2;
            int top = -1, bottom = -1;
            for (int y = 0; y < h; ++y) {
                if (!is_fill(cx, y)) continue;
                if (top < 0) top = y;
                bottom = y;
            }
            const int box_top = (int)((h - h * 0.20f) * 0.5f);
            const int box_bottom = box_top + (int)(h * 0.20f);
            const int slack = (int)(h * 0.05f);
            std::fprintf(stderr,
                         "  [D6] parchment band=[%d..%d] (box %d..%d, H=%d)\n",
                         top, bottom, box_top, box_bottom, h);
            if (top >= 0) {
                const int band_h = bottom - top + 1;
                CHECK(band_h >= (int)(h * 0.10f),
                      "D6: the parchment band is drawn (>= 0.10H of fill)");
                CHECK(top >= box_top - slack,
                      "D6: the panel starts at the box's top edge (not dropped "
                      "to the bottom)");
                CHECK(bottom <= box_bottom + slack,
                      "D6: the panel's drawn height stays near the intended "
                      "box height (bottom texture not stretched)");
            } else {
                CHECK(false, "D6: dialogue panel pixels found in the frame");
            }
        } else {
            CHECK(false, "D6: software renderer available");
        }
    }

    // ---- Final verdict ----
    if (tests_failed > 0) {
        std::fprintf(stderr, "\n=== SOAK DIALOGUE DEFECTS TEST FAILED (%d failures) ===\n",
                     tests_failed);
        return 1;
    }
    std::fprintf(stderr, "\n=== SOAK DIALOGUE DEFECTS TEST PASSED ===\n");
    return 0;
}
