#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../core/math.hpp"

namespace resf2::core { struct Vec2; }

namespace resf2::fight {
using core::Vec2;
using core::Rect;

// A single keyframe from a .bin animation file
struct Keyframe {
    float time = 0;       // normalized time (0..1)
    Vec2 position;        // node position
    float rotation = 0;   // node rotation (degrees)
    Vec2 scale{1, 1};     // node scale
};

// A node in the animation skeleton
struct AnimNode {
    std::string name;
    int parent_index = -1;
    std::vector<Keyframe> keyframes;
};

// Animation clip (.bin format)
struct AnimationClip {
    std::string name;
    float duration = 1.0f; // in seconds
    std::vector<AnimNode> nodes;

    // Bounding box for the whole animation
    Vec2 bound_min, bound_max;

    // Root motion (NPivot)
    struct RootMotion {
        Vec2 pivot;        // base pivot position
        Vec2 displacement; // total root displacement over the animation
    };
    RootMotion root_motion;

    // Load from .bin data
    bool load_from_bin(const uint8_t* data, size_t size, const std::string& anim_name);
};

// Animation player - interpolates keyframes and produces current pose
class AnimationPlayer {
public:
    void play(const AnimationClip* clip, bool loop = false);
    void stop();
    void update(float dt);

    // Get current pose
    struct Pose {
        std::vector<Vec2> positions;
        std::vector<float> rotations;
        std::vector<Vec2> scales;
    };
    const Pose& current_pose() const { return pose_; }

    // Root motion displacement this frame
    Vec2 root_motion_delta() const { return rm_delta_; }

    bool is_playing() const { return clip_ != nullptr; }
    bool is_finished() const { return finished_; }
    float time() const { return time_; }
    float duration() const { return clip_ ? clip_->duration : 0; }
    float progress() const { return clip_ && clip_->duration > 0 ? time_ / clip_->duration : 0; }
    const AnimationClip* clip() const { return clip_; }

    void set_speed(float s) { speed_ = s; }

private:
    const AnimationClip* clip_ = nullptr;
    float time_ = 0;
    float speed_ = 1.0f;
    bool looping_ = false;
    bool finished_ = true;
    Pose pose_;
    Vec2 rm_delta_;
    Vec2 last_pivot_;

    void interpolate_pose();
    Vec2 lerp_keyframes(const std::vector<Keyframe>& kfs, float t) const;
    float lerp_keyframes_rotation(const std::vector<Keyframe>& kfs, float t) const;
    Vec2 lerp_keyframes_scale(const std::vector<Keyframe>& kfs, float t) const;
};

} // namespace resf2::fight
