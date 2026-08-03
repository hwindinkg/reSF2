// tests/integration/test_hardcode_fidelity.cpp
//
// HARDCODE-FIDELITY WAVE (HARDCODE_AUDIT.md, commit 2cf966e): the engine
// must READ ASSET FILES, not invent substitutes that didn't exist in the
// original. Each HIGH item ships as a RED-first probe: the assertion pins
// the real asset value, so a regression back to the invented constant
// fails the build.
//
// Data provenance (all from reverse/data, the device dump):
//   list.xml           item Model attr (= file base name), MagicDamage
//   stages.xml         warrior <Template> <Items> = the enemy loadout
//   moves.xml          real animation names / FileName attrs
//   users.xml          the save: Level / Money
//   models/*.xml       magic projectile models (magic_fireball.xml etc.)
//
// Runs the headless harness (real Game), so the probes hit the engine's
// actual load paths.

#include "../headless_test_runner.hpp"
#include "../check.hpp"

#include <cstdio>
#include <string>

namespace {

// H07: the invented "fists_idle" alias (HARDCODE_AUDIT I03/H07,
// asset_manager.cpp:845) is gone from the catalog, and the enemy idle
// resolves the REAL catalog stance idle (fists1_stance_idle.bin is real;
// "fists_idle" is not a moves.xml name). RED on HEAD: the alias existed,
// so fists_idle WAS in the catalog after load.
static void test_h07_idle_alias(const resf2::test::HeadlessTestRunner& runner) {
    const resf2::game::Game& g = runner.game();
    const bool has_fists_idle = g.host_has_animation("fists_idle");
    const bool has_real_idle = g.host_has_animation("fists1_stance_idle");
    const std::string idle = g.host_get_enemy_idle_anim();
    std::fprintf(stderr, "  [H07] fists_idle=%d fists1_stance_idle=%d enemy_idle='%s'\n",
                 (int)has_fists_idle, (int)has_real_idle, idle.c_str());
    resf2::test::check(!has_fists_idle,
                       "H07: no invented 'fists_idle' animation in the catalog");
    resf2::test::check(has_real_idle,
                       "H07: the real 'fists1_stance_idle' animation is in the catalog");
    resf2::test::check_eq(idle, std::string("fists1_stance_idle"),
                          "H07: the enemy idle resolves the real catalog stance idle (fists)");
}

}  // namespace

int main() {
    std::printf("=== Hardcode-Fidelity Wave (HARDCODE_AUDIT HIGH items) ===\n");
    std::fflush(stdout);

    // ---- H07 ----
    {
        resf2::test::HeadlessTestConfig config;
        config.asset_root = "assets";
        config.width = 1280;
        config.height = 720;
        config.fixed_dt_ms = 16;
        config.start_scene = "dojo";
        config.hermetic = true;

        resf2::test::HeadlessTestRunner runner(config);
        if (!runner.init()) {
            std::fprintf(stderr, "FAIL: H07 init() returned false\n");
            return resf2::test::summary() ? 1 : 1;
        }
        runner.run_frames(40);  // Loading scene triggers load_animations()
        test_h07_idle_alias(runner);
    }

    return resf2::test::summary();
}
