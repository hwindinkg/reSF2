#include "animation.hpp"
#include <cstring>
#include <algorithm>

namespace resf2::fight {

static float read_f32(const uint8_t*& p) {
    float v;
    memcpy(&v, p, 4); p += 4;
    return v;
}

static int32_t read_i32(const uint8_t*& p) {
    int32_t v;
    memcpy(&v, p, 4); p += 4;
    return v;
}

// .bin format (VERIFIED from Gymnast-Tool-Suite Blender plugin):
//   u32  frame_count              (little-endian)
//   frame × {
//     u8   skip_byte              (1=keyframe, 5=interframe)
//     u32  node_count             (little-endian)
//     node_count × 3 f32          (X, Y, -Z) each little-endian
//   }
//
// Coordinate mapping: bin stores (game.X, game.Y, -game.Z)
// Node order: skeleton.xml XML order (67 nodes: 54 Node + 1 COM + 12 MacroNode)
// Positions are ABSOLUTE (world space).
bool AnimationClip::load_from_bin(const uint8_t* data, size_t size, const std::string& anim_name) {
    name = anim_name;
    const uint8_t* p = data;
    if (size < 4) return false;

    int32_t frame_count = read_i32(p);
    if (frame_count <= 0 || frame_count > 10000) return false;

    // First pass: determine total unique nodes across all frames
    const uint8_t* scan = p;
    int max_nodes = 0;
    for (int fi = 0; fi < frame_count; ++fi) {
        if (scan + 5 > data + size) break;
        uint8_t skip_byte = scan[0];
        (void)skip_byte;
        int32_t nc;
        memcpy(&nc, scan + 1, 4);
        scan += 5; // skip byte + node_count
        scan += nc * 12; // nc * 3 floats * 4 bytes
        if (nc > max_nodes) max_nodes = nc;
    }
    if (max_nodes == 0) return false;

    // Create nodes: one per skeleton slot, store as "node{index}"
    nodes.resize(max_nodes);
    for (int i = 0; i < max_nodes; i++) {
        nodes[i].name = "node" + std::to_string(i);
        nodes[i].parent_index = -1;
        nodes[i].keyframes.resize(frame_count);
    }

    // Second pass: read frame data
    p = data + 4;
    for (int fi = 0; fi < frame_count; ++fi) {
        float t = (float)fi / (float)(frame_count - 1);

        if (p + 5 > data + size) break;
        uint8_t skip_byte = p[0];
        (void)skip_byte;
        int32_t nc;
        memcpy(&nc, p + 1, 4);
        p += 5;

        for (int i = 0; i < nc && i < max_nodes; ++i) {
            if (p + 12 > data + size) break;
            auto& kf = nodes[i].keyframes[fi];
            kf.time = t;
            kf.position.x = read_f32(p);
            kf.position.y = read_f32(p);
            float neg_z = read_f32(p);
            // p points past Z

            // Track bounding box (only for nodes that exist this frame)
            if (fi == 0 && i == 0) {
                bound_min = bound_max = kf.position;
            } else {
                bound_min.x = std::min(bound_min.x, kf.position.x);
                bound_min.y = std::min(bound_min.y, kf.position.y);
                bound_max.x = std::max(bound_max.x, kf.position.x);
                bound_max.y = std::max(bound_max.y, kf.position.y);
            }
        }
    }

    // Duration: 20 FPS game clock
    duration = frame_count / 20.0f;
    return true;
}

void AnimationPlayer::play(const AnimationClip* clip, bool loop) {
    clip_ = clip;
    time_ = 0;
    speed_ = 1.0f;
    looping_ = loop;
    finished_ = false;
    rm_delta_ = {};
    last_pivot_ = {};
    if (clip && !clip->nodes.empty()) {
        pose_.positions.resize(clip->nodes.size());
        pose_.rotations.resize(clip->nodes.size());
        pose_.scales.resize(clip->nodes.size());
        interpolate_pose();
        // Store initial pivot (node 18 = NPivot in skeleton.xml order)
        if (clip->nodes.size() > 18)
            last_pivot_ = pose_.positions[18];
    }
}

void AnimationPlayer::stop() {
    clip_ = nullptr;
    finished_ = true;
}

void AnimationPlayer::update(float dt) {
    if (!clip_ || finished_) return;

    time_ += dt * speed_;

    if (time_ >= clip_->duration) {
        if (looping_) {
            time_ = fmodf(time_, clip_->duration);
        } else {
            time_ = clip_->duration;
            finished_ = true;
        }
    }

    // Store previous pivot (node 18)
    if (clip_->nodes.size() > 18 && pose_.positions.size() > 18)
        last_pivot_ = pose_.positions[18];

    interpolate_pose();

    // Calculate root motion delta
    rm_delta_ = {};
    if (clip_->nodes.size() > 18 && pose_.positions.size() > 18)
        rm_delta_ = pose_.positions[18] - last_pivot_;
}

void AnimationPlayer::interpolate_pose() {
    if (!clip_ || clip_->nodes.empty()) return;

    float t = clip_->duration > 0 ? time_ / clip_->duration : 0;

    for (size_t i = 0; i < clip_->nodes.size(); i++) {
        const auto& node = clip_->nodes[i];
        pose_.positions[i] = lerp_keyframes(node.keyframes, t);
        pose_.rotations[i] = 0; // no rotation in .bin
        pose_.scales[i] = {1, 1}; // no scale in .bin
    }
}

Vec2 AnimationPlayer::lerp_keyframes(const std::vector<Keyframe>& kfs, float t) const {
    if (kfs.empty()) return {};
    if (kfs.size() == 1 || t <= kfs[0].time) return kfs[0].position;
    if (t >= kfs.back().time) return kfs.back().position;

    for (size_t i = 0; i < kfs.size() - 1; i++) {
        if (t >= kfs[i].time && t < kfs[i + 1].time) {
            float local_t = (t - kfs[i].time) / (kfs[i + 1].time - kfs[i].time);
            return kfs[i].position + (kfs[i + 1].position - kfs[i].position) * local_t;
        }
    }
    return kfs.back().position;
}

float AnimationPlayer::lerp_keyframes_rotation(const std::vector<Keyframe>& kfs, float t) const {
    (void)kfs; (void)t;
    return 0;
}

Vec2 AnimationPlayer::lerp_keyframes_scale(const std::vector<Keyframe>& kfs, float t) const {
    (void)kfs; (void)t;
    return {1, 1};
}

} // namespace resf2::fight
