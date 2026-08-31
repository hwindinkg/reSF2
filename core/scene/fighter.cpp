// Fighter: clip sampling (Te.eda, MODEL_FORMAT §2.2) + macro-node
// computation (Fl.seb, L797) + 2D triangle vertex build (dv.ia, L840).

#include "scene/fighter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "anim_archive.hpp"

namespace sf2::scene {

void Fighter::set_model(const Model& model) {
    model_ = model;
    pos_.assign(model_.bones.size() * 2, 0.0f);
}

void Fighter::sample(const sf2::data::anim_clip& clip, int frame, float x,
                     float y, int facing) {
    if (clip.frames.empty()) {
        return;
    }
    frame = std::max(0, std::min(frame, static_cast<int>(clip.frames.size()) - 1));
    const std::size_t n = model_.bones.size();
    if (n == 0) {
        return;
    }
    const auto& bones = model_.bones;
    const auto& frame_bones = clip.frames[static_cast<std::size_t>(frame)].bones;
    const std::size_t nclip = std::min(frame_bones.size(), n);

    // 1. Per-bone positions: clip absolute world positions for bones 0..nclip-1,
    //    bind position for the rest (the game's eda leaves those at their
    //    current — here initial bind — position).
    std::vector<float> px(n), py(n), pz(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (i < nclip) {
            px[i] = frame_bones[i].x;
            py[i] = frame_bones[i].y;
            pz[i] = frame_bones[i].z;
        } else {
            px[i] = bones[i].x;
            py[i] = bones[i].y;
            pz[i] = bones[i].z;
        }
    }

    // 2. MacroNodes not driven by the clip (index >= clip bone count) are the
    //    weighted average of their child bones' current positions (Fl.seb).
    //    Clip-driven macro nodes were explicitly placed (Ega) and skipped.
    //    Children may be skeleton bones or other macros; recursion is bounded
    //    by a per-call visited set (defensive against malformed cycles).
    std::vector<std::uint8_t> visiting(n, 0);
    std::function<void(std::size_t)> compute_macro = [&](std::size_t idx) {
        if (visiting[idx]) {
            return;  // cycle guard
        }
        visiting[idx] = 1;
        const auto& name = bones[idx].name;
        const auto it = model_.macro_children.find(name);
        if (it != model_.macro_children.end()) {
            const MacroChildren& mc = it->second;
            float ax = 0.0f, ay = 0.0f, az = 0.0f;
            for (std::size_t c = 0; c < mc.child_names.size(); ++c) {
                const int ci = model_.bone_by_name(mc.child_names[c]);
                if (ci < 0) {
                    continue;
                }
                const std::size_t u = static_cast<std::size_t>(ci);
                if (u >= n) {
                    continue;
                }
                if (bones[u].is_macro && u >= nclip) {
                    compute_macro(u);
                }
                const float w = c < mc.weights.size() ? mc.weights[c] : 0.0f;
                ax += px[u] * w;
                ay += py[u] * w;
                az += pz[u] * w;
            }
            px[idx] = ax;
            py[idx] = ay;
            pz[idx] = az;
        }
        visiting[idx] = 0;
    };
    for (std::size_t i = nclip; i < n; ++i) {
        if (bones[i].is_macro) {
            compute_macro(i);
        }
    }

    // 3. World placement: the COM bone anchors the fighter at (x, y)
    //    (wd.oL offsets all bones relative to the COM). Facing mirrors X.
    int com = model_.bone_by_name("COM");
    if (com < 0) {
        com = 0;
    }
    const std::size_t com_u = static_cast<std::size_t>(com);
    const float dx = x - px[com_u];
    const float dy = y - py[com_u];
    const float f = facing < 0 ? -1.0f : 1.0f;
    for (std::size_t i = 0; i < n; ++i) {
        pos_[i * 2] = (px[i] + dx) * f;
        pos_[i * 2 + 1] = py[i] + dy;
    }
}

std::size_t Fighter::build_vertices(std::vector<float>& out) const {
    out.clear();
    out.reserve(model_.resolved_tris.size() * 6);
    for (const TriResolved& tri : model_.resolved_tris) {
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2 + 1]);
    }
    return out.size() / 2;
}

void Fighter::triangle_bbox(float& min_x, float& min_y, float& max_x,
                            float& max_y) const {
    min_x = min_y = max_x = max_y = 0.0f;
    bool first = true;
    for (const TriResolved& tri : model_.resolved_tris) {
        const int idx[3] = {tri.i1, tri.i2, tri.i3};
        for (int k = 0; k < 3; ++k) {
            const float vx = pos_[static_cast<std::size_t>(idx[k]) * 2];
            const float vy = pos_[static_cast<std::size_t>(idx[k]) * 2 + 1];
            if (first) {
                min_x = max_x = vx;
                min_y = max_y = vy;
                first = false;
            } else {
                min_x = std::min(min_x, vx);
                max_x = std::max(max_x, vx);
                min_y = std::min(min_y, vy);
                max_y = std::max(max_y, vy);
            }
        }
    }
}

} // namespace sf2::scene
