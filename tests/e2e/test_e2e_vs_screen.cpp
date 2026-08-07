// tests/e2e/test_e2e_vs_screen.cpp
//
// Wave 11C P1 — VS SCREEN + PORTRAITS: "нет показ аватарок соперника и
// игрока перед битвой, в самой битве" — entering a battle must show the
// PreFight (VS) screen and the battle HUD must show both fighters' avatar
// portraits.
//
// Verified (VERIFY_W11.md Q1 GREEN; SPEC_PRESENTATION.md Q1):
//   - PreFight ctor 0x8F416444 loads textures/fullscreen/VS_Fon.xml (two
//     halves VS_Fon_left/right.jpg), Stripe_left/right.png, and
//     textures/misc/VS.png as the "VS" label; the PreFight scene runs before
//     ScreenFight (RTTI "8PreFight" / "11ScreenFight").
//   - Portraits are avatar PNGs image/users/image/<key>.png built by
//     FUN_8F411EDC (avatar sprite builder, x=-110, z=3): player key
//     avatar_hero (profile Avatar default), enemy key derived from the
//     enemy (e.g. character_disciple for the dojo disciple; _small variant
//     for the tutorial HUD), fallback "UnkownEnemyAvatar".
//
// E2E on the REAL binary: boot --scene battle and observe the battle
// opening through the app's own diagnostics:
//   (a) a PreFight phase renders at battle entry — [VS] probe rows from the
//       first frames, before any combat animation plays, carrying the VS
//       label frame, both VS_Fon halves and both avatar keys;
//   (b) the fight HUD draws the two avatar portraits — [HUD-AVATAR] probe
//       rows with the player and enemy keys;
//   (c) avatar files resolve from the asset tree (avatar_hero + the enemy's
//       character key; found=1 in the probes).
// RED on HEAD: no [VS] and no [HUD-AVATAR] rows at all (PreFight screen and
// HUD portraits not ported).

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

bool has_needle(const std::string& line, const char* needle) {
    return line.find(needle) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_vs_screen <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ---------------------------------------------------------------- script
    // A single punch at frame 400 gives the run a combat animation after the
    // battle opening; everything before it is intro stance + the PreFight
    // phase. 700 frames is plenty.
    std::vector<e2e::InputEvent> events;
    events.push_back({400, true, "O"});
    events.push_back({402, false, "O"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_vs_screen_input.txt";
    spec.out_name = "e2e_vs_screen";
    spec.max_frames = 700;
    spec.no_log = true;       // stdout [VS]/[HUD-AVATAR]/[STATE] probes
    spec.extra_args = {"--scene", "battle", "--round-time", "99"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    // ------------------------------------------------ (a) PreFight phase
    const auto vs = e2e::filter_lines(run.stdout_lines, "[VS]");
    std::printf("vs-screen: %zu [VS] row(s)\n", vs.size());
    check(!vs.empty(),
          "the battle opening rendered the PreFight (VS) screen ([VS] probe)");

    // The VS screen must appear at battle entry — before any combat action
    // (first [STATE] row whose anim is not the intro/idle stance).
    long long first_combat = -1;
    for (const auto& fr : frames) {
        if (fr.anim != "stance_2" && fr.anim != "stance_idle" &&
            fr.anim != "fists_idle" && !fr.anim.empty()) {
            first_combat = fr.frame;
            break;
        }
    }
    std::printf("vs-screen: first combat [STATE] at frame %lld\n", first_combat);
    check(first_combat > 0, "the battle produced a combat animation");
    bool vs_before_fight = false;
    for (const auto& l : vs) {
        const auto a = l.find(" f=");
        if (a != std::string::npos) {
            const long long f = std::stoll(l.c_str() + a + 3);
            if (f < first_combat) vs_before_fight = true;
        }
    }
    check(vs_before_fight,
          "the PreFight screen rendered before the first combat animation");

    // The VS screen must carry the full verified composition: the VS label
    // frame, both VS_Fon halves, and both avatar keys.
    bool vs_label = false, vs_fon = false;
    for (const auto& l : vs) {
        if (has_needle(l, "vs_label=1")) vs_label = true;
        if (has_needle(l, "fon=2")) vs_fon = true;
    }
    check(vs_label,
          "the VS label frame (textures/misc/VS.png) rendered on the "
          "PreFight screen");
    check(vs_fon,
          "both VS_Fon halves (VS_Fon_left/right.jpg) rendered on the "
          "PreFight screen");

    // ------------------------------------------------ (c) avatar files
    // resolve from the asset tree
    bool player_avatar_ok = false, enemy_avatar_ok = false;
    bool player_key_ok = false, enemy_key_ok = false;
    for (const auto& l : vs) {
        const auto p = l.find("player='");
        if (p != std::string::npos) {
            const auto q = l.find('\'', p + 8);
            if (q != std::string::npos) {
                const std::string key = l.substr(p + 8, q - p - 8);
                if (key == "avatar_hero") player_key_ok = true;
            }
        }
        const auto e = l.find("enemy='");
        if (e != std::string::npos) {
            const auto q = l.find('\'', e + 7);
            if (q != std::string::npos) {
                const std::string key = l.substr(e + 7, q - e - 7);
                if (key == "character_disciple") enemy_key_ok = true;
            }
        }
        if (has_needle(l, "player_found=1")) player_avatar_ok = true;
        if (has_needle(l, "enemy_found=1")) enemy_avatar_ok = true;
    }
    check(player_key_ok,
          "the player avatar resolves to the profile default key "
          "(image/users/image/avatar_hero.png)");
    check(enemy_key_ok,
          "the enemy avatar resolves to the enemy's character key "
          "(image/users/image/character_disciple.png for the dojo "
          "disciple)");
    check(player_avatar_ok && enemy_avatar_ok,
          "both avatar files resolve from the asset tree "
          "(player_found=1 and enemy_found=1 in the probe)");

    // ------------------------------------------------ (b) HUD portraits
    const auto hud = e2e::filter_lines(run.stdout_lines, "[HUD-AVATAR]");
    std::printf("vs-screen: %zu [HUD-AVATAR] row(s)\n", hud.size());
    check(!hud.empty(),
          "the battle HUD rendered the avatar portraits ([HUD-AVATAR] probe)");
    bool hud_player = false, hud_enemy = false;
    for (const auto& l : hud) {
        if (has_needle(l, "player='avatar_hero'")) hud_player = true;
        if (has_needle(l, "enemy='character_disciple'")) hud_enemy = true;
    }
    check(hud_player && hud_enemy,
          "the fight HUD shows both portraits (player avatar_hero + enemy "
          "character_disciple)");

    return resf2::test::summary();
}
