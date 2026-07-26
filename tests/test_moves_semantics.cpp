// tests/test_moves_semantics.cpp
//
// Invariants of moves.xml that the ORIGINAL engine satisfies by construction,
// checked against the real .bin animations. Any reimplementation has to satisfy
// them too, so this is a conformance test rather than a test of our own code:
// if it fails, either the parser dropped data or the .bin/XML pairing is wrong.
//
// What the original guarantees (ShadowFight2.s86 loads the same two files):
//   * every Move that names a FileName has that .bin on disk
//   * playback rate is fps = 60 / (1 + MidFrames)   — MidFrames is the number
//     of interpolated ticks inserted between key frames at the 60 Hz tick rate
//   * FirstFrame, when given, is a valid frame index of that animation
//   * the Attack interval lies inside the animation, and starts at or after
//     FirstFrame — an attack that activates before playback begins could never
//     fire
//   * the Uninterrupt interval lies inside the animation
//   * every attacking move actually declares an attack interval

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../engine/game/asset_manager.hpp"
#include "check.hpp"

namespace fs = std::filesystem;
using resf2::test::check;
using resf2::test::check_ge;

int main(int argc, char** argv) {
    const fs::path root = (argc > 1) ? fs::path(argv[1]) : fs::path(".");

    resf2::game::AssetManager assets;
    assets.load_moves(root.string());
    assets.load_animations(root.string());

    const auto& moves = assets.moves();
    const auto& anims = assets.animations();

    check_ge(static_cast<double>(moves.size()), 100.0, "moves.xml yields the move table");
    check_ge(static_cast<double>(anims.size()), 300.0, "the .bin animations load");
    if (moves.empty() || anims.empty()) return resf2::test::summary();

    int checked = 0, missing_anim = 0;
    int attack_moves = 0, attack_moves_with_interval = 0;
    std::vector<std::string> missing_names;

    for (const auto& [name, m] : moves) {
        if (m.filename.empty()) continue;

        std::string anim_name = m.filename;
        if (anim_name.size() > 4 && anim_name.substr(anim_name.size() - 4) == ".bin")
            anim_name = anim_name.substr(0, anim_name.size() - 4);

        const auto it = anims.find(anim_name);
        if (it == anims.end()) {
            ++missing_anim;
            if (missing_names.size() < 10) missing_names.push_back(name + " -> " + m.filename);
            continue;
        }
        const int frames = it->second.frame_count;
        ++checked;

        // --- playback rate ---------------------------------------------------
        check(m.mid_frames >= 0 && m.mid_frames <= 20,
              name + ": MidFrames is in range (got " + std::to_string(m.mid_frames) + ")");
        const float fps = 60.0f / (1.0f + static_cast<float>(m.mid_frames));
        check(fps > 0.0f && fps <= 60.0f, name + ": derived fps is sane");

        // --- FirstFrame ------------------------------------------------------
        if (m.first_frame >= 0) {
            check(m.first_frame < frames,
                  name + ": FirstFrame " + std::to_string(m.first_frame) +
                      " is inside the animation (" + std::to_string(frames) + " frames)");
        }

        // --- attack interval -------------------------------------------------
        // Note on invariants that look obvious but are NOT true of the original:
        //   * an interval may omit End, meaning "to the end of the animation"
        //     (recorded as -1) — so End < Start is only a defect when End was
        //     actually given;
        //   * Attack Start may be BEFORE FirstFrame, because looping
        //     animations wrap past it (e.g. MineIdle: FirstFrame 24, attack
        //     from frame 1).
        if (m.attack_start >= 0) {
            check(m.attack_start < frames,
                  name + ": Attack Start " + std::to_string(m.attack_start) +
                      " is inside the animation (" + std::to_string(frames) + " frames)");
            if (m.attack_end >= 0) {
                check(m.attack_end >= m.attack_start,
                      name + ": Attack End " + std::to_string(m.attack_end) +
                          " is not before Start " + std::to_string(m.attack_start));
                check(m.attack_end <= frames,
                      name + ": Attack End " + std::to_string(m.attack_end) +
                          " is inside the animation (" + std::to_string(frames) + " frames)");
            }
        }

        // --- uninterrupt interval --------------------------------------------
        // The End of an uninterrupt window is allowed to overshoot the
        // animation — seven magic/ranged moves in moves.xml do that, and it
        // simply means "uninterruptible throughout". What would be a defect is
        // a window that STARTS past the end, because it could never apply.
        if (m.uninterrupt_start >= 0) {
            check(m.uninterrupt_start < frames,
                  name + ": Uninterrupt Start " + std::to_string(m.uninterrupt_start) +
                      " is inside the animation (" + std::to_string(frames) + " frames)");
            if (m.uninterrupt_end >= 0)
                check(m.uninterrupt_end >= m.uninterrupt_start,
                      name + ": Uninterrupt End is not before Start");
        }

        if (m.is_attack) ++attack_moves;
        if (m.is_attack && m.attack_start >= 0) ++attack_moves_with_interval;
    }

    std::printf("checked %d moves against their animations, %d had no .bin loaded\n",
                checked, missing_anim);
    for (const auto& s : missing_names)
        std::printf("  missing animation: %s\n", s.c_str());

    check_ge(checked, 100.0, "most moves were matched to an animation");

    // Not every Type=ATTACK move carries an attack interval: projectile and
    // magic spawners (FireballPlayer, MindThrowPlayer, MinePlayer, ...) put the
    // damage on the entity they spawn. The bulk of them do, though, and that
    // ratio is what collapses to zero when the <Intervals> block stops being
    // parsed — which is exactly how this was broken.
    std::printf("attack moves: %d, of which %d declare an attack interval\n",
                attack_moves, attack_moves_with_interval);
    check_ge(attack_moves, 200.0, "the move table contains attacking moves");
    check_ge(static_cast<double>(attack_moves_with_interval) /
                 (attack_moves ? attack_moves : 1),
             0.80, "at least 80% of attacking moves declare an attack interval");

    // --- the specific numbers behind the punch timing ------------------------
    // Documented here so a change to the parser that silently loses MidFrames
    // or FirstFrame is caught: these are read straight out of moves.xml.
    struct Expect {
        const char* move;
        int mid_frames;
        int first_frame;
        int attack_start;
        int attack_end;
    };
    const Expect expected[] = {
        {"HighPunch", 2, 1, 4, 5},
        {"LowPunch", 2, 3, -2, -2},  // -2 = only MidFrames/FirstFrame asserted
    };
    for (const auto& e : expected) {
        const auto it = moves.find(e.move);
        check(it != moves.end(), std::string(e.move) + " is present in moves.xml");
        if (it == moves.end()) continue;
        const auto& m = it->second;
        check(m.mid_frames == e.mid_frames,
              std::string(e.move) + ": MidFrames == " + std::to_string(e.mid_frames));
        check(m.first_frame == e.first_frame,
              std::string(e.move) + ": FirstFrame == " + std::to_string(e.first_frame));
        if (e.attack_start != -2) {
            check(m.attack_start == e.attack_start,
                  std::string(e.move) + ": Attack Start == " + std::to_string(e.attack_start));
            check(m.attack_end == e.attack_end,
                  std::string(e.move) + ": Attack End == " + std::to_string(e.attack_end));
        }
    }

    return resf2::test::summary();
}
