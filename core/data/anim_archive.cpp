#include "anim_archive.hpp"

#include <cstring>
#include <stdexcept>

namespace sf2::data {
namespace {

// Bounds-checked little-endian reader over a byte buffer (same style as the
// reader in xml_archive.cpp).
class reader {
public:
    reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8() {
        require(1);
        return data_[pos_++];
    }

    std::uint16_t u16le() {
        require(2);
        const std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) |
                                static_cast<std::uint16_t>(data_[pos_ + 1]) << 8;
        pos_ += 2;
        return v;
    }

    std::uint32_t u32le() {
        require(4);
        const std::uint32_t v = static_cast<std::uint32_t>(data_[pos_]) |
                                static_cast<std::uint32_t>(data_[pos_ + 1]) << 8 |
                                static_cast<std::uint32_t>(data_[pos_ + 2]) << 16 |
                                static_cast<std::uint32_t>(data_[pos_ + 3]) << 24;
        pos_ += 4;
        return v;
    }

    // Signed 16-bit little-endian (the game's `Zd()`).
    std::int16_t i16le() {
        require(2);
        const std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) |
                                static_cast<std::uint16_t>(data_[pos_ + 1]) << 8;
        pos_ += 2;
        return static_cast<std::int16_t>(v);
    }

    // IEEE-754 float32 little-endian (the game's `RK()` = jf.xab(ti())).
    float f32le() {
        require(4);
        std::uint32_t bits = static_cast<std::uint32_t>(data_[pos_]) |
                             static_cast<std::uint32_t>(data_[pos_ + 1]) << 8 |
                             static_cast<std::uint32_t>(data_[pos_ + 2]) << 16 |
                             static_cast<std::uint32_t>(data_[pos_ + 3]) << 24;
        pos_ += 4;
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    void skip(std::size_t n) { require(n); pos_ += n; }

private:
    void require(std::size_t n) {
        if (n > size_ - pos_) {
            throw std::runtime_error("anim_archive: truncated input");
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

} // namespace

anim_clip anim_clip_parse(const std::string& name, const std::uint8_t* data,
                          std::size_t size) {
    reader r(data, size);
    anim_clip clip;
    clip.name = name;
    clip.version = r.u8();

    if (clip.version == 1) {
        // u8 frame count; per frame: u16 bone count + 3 x i16 LE (x, y, z),
        // position = (x/16, -y/16, z/16).
        const std::uint8_t frame_count = r.u8();
        clip.frames.reserve(frame_count);
        for (std::uint8_t f = 0; f < frame_count; ++f) {
            anim_frame frame;
            const std::uint16_t bone_count = r.u16le();
            frame.bones.reserve(bone_count);
            for (std::uint16_t b = 0; b < bone_count; ++b) {
                anim_keyframe kf;
                kf.x = r.i16le() / 16.0f;
                kf.y = -(r.i16le() / 16.0f);
                kf.z = r.i16le() / 16.0f;
                frame.bones.push_back(kf);
            }
            clip.frames.push_back(std::move(frame));
        }
    } else if (clip.version == 0) {
        // u32 frame count; per frame: 1 skip byte + u32 bone count +
        // 3 x float32 LE (x, y, z), position = (x, -y, z).
        const std::uint32_t frame_count = r.u32le();
        clip.frames.reserve(frame_count);
        for (std::uint32_t f = 0; f < frame_count; ++f) {
            r.skip(1);
            anim_frame frame;
            const std::uint32_t bone_count = r.u32le();
            frame.bones.reserve(bone_count);
            for (std::uint32_t b = 0; b < bone_count; ++b) {
                anim_keyframe kf;
                kf.x = r.f32le();
                kf.y = -r.f32le();
                kf.z = r.f32le();
                frame.bones.push_back(kf);
            }
            clip.frames.push_back(std::move(frame));
        }
    } else {
        throw std::runtime_error("anim_archive: unknown animation version " +
                                 std::to_string(clip.version) + " in '" + name + "'");
    }
    return clip;
}

} // namespace sf2::data