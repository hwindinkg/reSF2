// TexturePacker atlas JSON parser — nlohmann/json.

#include "atlas.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace sf2::data {

atlas atlas_parse(const std::uint8_t* data, std::size_t size) {
    nlohmann::json doc = nlohmann::json::parse(data, data + size);
    atlas result;

    const auto& meta = doc.value("meta", nlohmann::json::object());
    const auto& size_obj = meta.value("size", nlohmann::json::object());
    result.w = size_obj.value("w", 0);
    result.h = size_obj.value("h", 0);

    const auto& frames = doc.value("frames", nlohmann::json::array());
    result.frames.reserve(frames.size());
    for (const auto& f : frames) {
        atlas_frame frame;
        frame.name = f.value("filename", "");
        frame.rotated = f.value("rotated", false);
        frame.trimmed = f.value("trimmed", false);
        const auto& fr = f.value("frame", nlohmann::json::object());
        frame.x = fr.value("x", 0);
        frame.y = fr.value("y", 0);
        frame.w = fr.value("w", 0);
        frame.h = fr.value("h", 0);
        const auto& ss = f.value("sourceSize", nlohmann::json::object());
        frame.source_w = ss.value("w", 0);
        frame.source_h = ss.value("h", 0);
        const auto& sss = f.value("spriteSourceSize", nlohmann::json::object());
        frame.offset_x = sss.value("x", 0);
        frame.offset_y = sss.value("y", 0);
        result.frames.push_back(std::move(frame));
    }

    return result;
}

} // namespace sf2::data
