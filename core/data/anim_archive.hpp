#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf2::data {

// One bone position at one frame, in world units. `y` is already negated to
// match the game's coordinate convention (the JS builds H(x/16, -(y/16), z/16)).
struct anim_keyframe {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// All bone positions at one frame. `frames[f].bones[b]` is the position of
// bone `b` at frame `f` — the same layout the game uses (`Te.Kk[frame][bone]`).
struct anim_frame {
    std::vector<anim_keyframe> bones;
};

// One named animation clip — one entry of animations.*.dat.
struct anim_clip {
    std::string name;
    int version = 0;  // 0 = float32 keyframes, 1 = u16/16 keyframes
    std::vector<anim_frame> frames;

    // Bone count of the first frame (all frames share one in practice).
    std::size_t bone_count() const {
        return frames.empty() ? 0 : frames.front().bones.size();
    }
};

// Parses one animation entry (the game's `Vlb` format, see core/data/README.md).
// Throws std::runtime_error on malformed/truncated input.
anim_clip anim_clip_parse(const std::string& name, const std::uint8_t* data,
                          std::size_t size);

} // namespace sf2::data