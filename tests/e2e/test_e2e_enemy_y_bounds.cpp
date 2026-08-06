// tests/e2e/test_e2e_enemy_y_bounds.cpp
//
// Wave 11B W2 - ENEMY Y + BOUNDS: "противник ниже чем нужно на локации...
// противник всё равно может выйти за локацию, либо барьеры дальше чем
// нужно". VERIFY_W11 3 / SPEC_WORLD_FEEL 3 (adjudicated): the enemy world
// Y comes from <ModelsViewer EnemyPositionY> in the Type=2 layer of
// locations/<loc>/params.xml (dojo: -105) -> Location+0x54 -> fighter;
// the arena bounds are the WALL/floor physics collisions - the wall
// objects from the <Image ClassName="left"/"right"> anchors (dojo:
// X=+-680, the wall sprites; the engine's old clamp(x, +-width/2) =
// +-980 has no counterpart in the binary and must be replaced).
//
// E2E on the REAL binary:
//  (a) Run 1 (dojo): the enemy world Y equals the params.xml
//      EnemyPositionY (-105) and the player's equals PlayerPositionY
//      (-93) at every gameplay frame.
//  (b) Run 1: holding A then D walks the player INTO both walls - the
//      player stops AT the wall (px ~ +-680, the params.xml left/right
//      wall anchors), NOT at +-width/2 (+-980). RED on HEAD (walks to
//      +-980); GREEN after the clamp source change.
//  (c) Run 2 (dojo sparring, B toggled): the herd recipe knocks the
//      enemy right; the enemy is stopped by the SAME wall (ex_max ~
//      +680, never past it). RED on HEAD (enemy clamped at +980).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;
using resf2::test::check_ge;
using resf2::test::check_near;

namespace {

// dojo params.xml: <Root Width="1960" Wall="305">; ModelsViewer
// PlayerPositionY="-93" EnemyPositionY="-105"; the wall sprites
// <Image ClassName="left|right"> stand at X=+-680 (world coords) - the
// wall boundary the fighters must stop at.
constexpr float kWallX = 680.0f;
constexpr float kSlack = 10.0f;       // float/walk noise allowance
constexpr float kPlayerSpawnY = -93.0f;
constexpr float kEnemySpawnY = -105.0f;

}  // namespace

// Run 1: player walks into both walls; Y spawns checked.
static int run_walk(const std::string& app, const std::string& root) {
    // Intro stance ~160 frames. A held 200..600 walks LEFT from spawn
    // (-290) past the -680 wall (~500 frames at ~1.3 u/frame), then D held
    // 700..2000 walks RIGHT past the +680 wall. Both walls are hit with
    // overshoot, so an unclamped (or +-980-clamped) walk is caught.
    std::vector<e2e::InputEvent> events;
    events.push_back({200, true, "A"});
    events.push_back({600, false, "A"});
    events.push_back({700, true, "D"});
    events.push_back({2000, false, "D"});

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_enemy_y_walk_input.txt";
    spec.out_name = "e2e_enemy_y_walk";
    spec.max_frames = 2100;
    spec.no_log = true;
    spec.extra_args = {"--scene", "dojo"};
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly (walk run)");
    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the walk run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    float px_min = 1e9f, px_max = -1e9f;
    float py_min = 1e9f, py_max = -1e9f;
    float ey_min = 1e9f, ey_max = -1e9f;
    long long f_min = 0, f_max = 0;
    for (const auto& fr : frames) {
        if (fr.px == 0.0f) continue;  // dialogue/blank rows
        if (fr.px < px_min) { px_min = fr.px; f_min = fr.frame; }
        if (fr.px > px_max) { px_max = fr.px; f_max = fr.frame; }
        py_min = std::min(py_min, fr.py);
        py_max = std::max(py_max, fr.py);
        ey_min = std::min(ey_min, fr.ey);
        ey_max = std::max(ey_max, fr.ey);
    }
    std::printf("enemy-y-bounds: %zu frames px[%.1f @f%lld .. %.1f @f%lld] "
                "py[%.1f..%.1f] ey[%.1f..%.1f] (walls +-%.0f)\n",
                frames.size(), px_min, f_min, px_max, f_max,
                py_min, py_max, ey_min, ey_max, kWallX);

    // (a) Y spawns from params.xml ModelsViewer, both fighters.
    check_near(static_cast<double>(py_min), kPlayerSpawnY, 1.0,
               "the player's world Y = PlayerPositionY (-93)");
    check_near(static_cast<double>(py_max), kPlayerSpawnY, 1.0,
               "the player Y never drifts from the spawn Y");
    check_near(static_cast<double>(ey_min), kEnemySpawnY, 1.0,
               "the enemy's world Y = EnemyPositionY (-105)");
    check_near(static_cast<double>(ey_max), kEnemySpawnY, 1.0,
               "the enemy Y never drifts from the spawn Y");

    // (b) Both walls were actually reached, and the player stopped AT
    // them (the left/right wall anchors +-680), not at +-width/2.
    check(px_min <= -(kWallX - kSlack),
          "the back walk reached the LEFT wall (px <= -670)");
    check(px_max >= (kWallX - kSlack),
          "the forward walk reached the RIGHT wall (px >= +670)");
    check(px_min >= -(kWallX + kSlack) && px_max <= (kWallX + kSlack),
          "the player stops AT the wall (+-680), not at +-width/2 (+-980)");

    return resf2::test::summary();
}

// Run 2: the sparring enemy is knocked into the SAME wall and stops there.
static int run_knockback(const std::string& app, const std::string& root) {
    // The knockback herd recipe (test_e2e_knockback_bounds): B at 190
    // toggles the sparring partner; walk right, punch bursts, then
    // walk+punch cycles ratchet the enemy right into the wall.
    std::vector<e2e::InputEvent> events;
    events.push_back({190, true, "B"});
    events.push_back({191, false, "B"});
    events.push_back({200, true, "D"});
    events.push_back({356, false, "D"});
    for (int f : {360, 384, 408}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }
    for (int cyc = 0; cyc < 40; ++cyc) {
        const int w0 = 430 + cyc * 80;
        events.push_back({w0, true, "D"});
        events.push_back({w0 + 64, false, "D"});
        const int p0 = w0 + 68;
        events.push_back({p0, true, "O"});
        events.push_back({p0 + 2, false, "O"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_enemy_y_knock_input.txt";
    spec.out_name = "e2e_enemy_y_knock";
    spec.max_frames = 3800;
    spec.no_log = true;
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly (knockback run)");
    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the knockback run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    const auto hits = e2e::filter_lines(run.stdout_lines, "[HIT-FEEDBACK]");
    int unblocked = 0;
    for (const auto& l : hits)
        if (l.find("blocked=0") != std::string::npos) ++unblocked;

    float ex_min = 1e9f, ex_max = -1e9f;
    long long f_max = 0;
    for (const auto& fr : frames) {
        if (fr.px == 0.0f) continue;
        if (fr.ex < ex_min) ex_min = fr.ex;
        if (fr.ex > ex_max) { ex_max = fr.ex; f_max = fr.frame; }
    }
    std::printf("enemy-y-bounds: knockback %d unblocked hit(s) of %zu, "
                "enemy x range [%.1f .. %.1f @f%lld] (walls +-%.0f)\n",
                unblocked, hits.size(), ex_min, ex_max, f_max, kWallX);

    check_ge(static_cast<double>(unblocked), 4,
             "at least 4 unblocked hits landed (the enemy was driven)");
    check(ex_max >= (kWallX - 2.0f * kSlack),
          "the herd carried the enemy INTO the right wall (ex >= +660)");
    check(ex_max <= (kWallX + kSlack),
          "the enemy is stopped by the SAME wall (+-680), never past it");
    return resf2::test::summary();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_enemy_y_bounds <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    int rc = run_walk(app, root);
    if (rc != 0) return rc;
    return run_knockback(app, root);
}
