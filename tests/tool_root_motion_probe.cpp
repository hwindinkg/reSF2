// tests/tool_root_motion_probe.cpp
//
// Diagnostic (not a test): dumps the authored NPivot / Align-anchor X
// trajectories of the movement animations from the real .bin data, plus the
// net displacement the ENGINE applies for each (raw NPivot deltas when no
// align pinning is active, anchor-relative deltas when it is).
//
//   cmake --build build --config Release --target tool_root_motion_probe
//   build/bin/Release/tool_root_motion_probe assets

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../engine/game/asset_manager.hpp"

namespace fs = std::filesystem;

static int npivot_idx(const resf2::game::AssetManager& assets) {
    const auto& order = assets.ordered_node_names();
    for (int i = 0; i < (int)order.size(); ++i)
        if (order[i] == "NPivot") return i;
    return -1;
}

static int node_idx(const resf2::game::AssetManager& assets, const std::string& name) {
    const auto& order = assets.ordered_node_names();
    for (int i = 0; i < (int)order.size(); ++i)
        if (order[i] == name) return i;
    return -1;
}

int main(int argc, char** argv) {
    const fs::path root = (argc > 1) ? fs::path(argv[1]) : fs::path(".");
    resf2::game::AssetManager assets;
    assets.load_skeleton(root.string(), "dojo");
    assets.load_moves(root.string());
    assets.load_animations(root.string());

    const int npi = npivot_idx(assets);
    if (npi < 0) { std::fprintf(stderr, "no NPivot in skeleton\n"); return 1; }

    const char* names[] = {
        "step_forward", "step_back", "stance_idle", "forward_roll", "back_roll",
        "back_handflip", "double_step_forward", "jump", "jump_away",
        "front_flip", "back_flip",
    };
    for (const char* name : names) {
        auto it = assets.animations().find(name);
        if (it == assets.animations().end()) {
            std::printf("%-22s  NOT LOADED\n", name);
            continue;
        }
        const auto& anim = it->second;
        std::string anchor;  // Align anchor from the move that names this anim
        int anchor_i = -1;
        for (const auto& [mname, m] : assets.moves()) {
            std::string fn = m.filename;
            if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".bin")
                fn = fn.substr(0, fn.size() - 4);
            if (fn == name && m.has_align && !m.moveinside_pivot_node.empty()) {
                anchor = m.moveinside_pivot_node;
                break;
            }
        }
        if (!anchor.empty()) anchor_i = node_idx(assets, anchor);

        std::vector<float> npiv_x, anch_x;
        float np0 = 0, np1 = 0;
        for (int f = 0; f < anim.frame_count; ++f) {
            float x, y, z;
            if (anim.get_node_pos(f, npi, x, y, z)) {
                npiv_x.push_back(x);
                if (f == 0) np0 = x;
                np1 = x;
            }
            if (anchor_i >= 0) {
                float ax, ay, az;
                if (anim.get_node_pos(f, anchor_i, ax, ay, az)) anch_x.push_back(ax);
            }
        }
        std::printf("%-22s frames=%3d  NPivot X: %8.2f .. %8.2f  net=%+7.2f",
                    name, anim.frame_count, np0, np1, np1 - np0);
        if (anchor_i >= 0 && anch_x.size() == npiv_x.size() && !npiv_x.empty()) {
            // anchor-relative net: -(rel_end - rel_start) is what aligned
            // pinning applies; raw NPivot net is what non-pinned plays apply.
            float rel0 = anch_x[0] - npiv_x[0];
            float rel1 = anch_x[anch_x.size() - 1] - npiv_x[npiv_x.size() - 1];
            std::printf("  align['%s'] rel %+7.2f -> %+7.2f  net=%+7.2f",
                        anchor.c_str(), rel0, rel1, rel0 - rel1);
        }
        std::printf("\n");
        // Sparse frame dump (every frame if <= 24, else every 4th + last)
        if ((int)npiv_x.size() <= 24) {
            for (int f = 0; f < (int)npiv_x.size(); ++f) {
                std::printf("    f%02d  npivot=%+8.2f%s\n", f, npiv_x[f],
                            anchor_i >= 0 ? "" : "");
            }
        } else {
            for (int f = 0; f < (int)npiv_x.size(); f += 4)
                std::printf("    f%02d  npivot=%+8.2f\n", f, npiv_x[f]);
            std::printf("    f%02d  npivot=%+8.2f\n",
                        (int)npiv_x.size() - 1, npiv_x[npiv_x.size() - 1]);
        }
    }
    return 0;
}
