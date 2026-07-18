#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resf2::format {

// A single sprite frame inside a TexturePacker JSON atlas (Hash format).
struct JsonAtlasFrame {
    std::string filename;
    bool rotated = false;
    int x = 0, y = 0;      // frame position in atlas
    int w = 0, h = 0;      // frame size in atlas
    int ox = 0, oy = 0;    // offset (spriteSourceSize)
    int sw = 0, sh = 0;    // sourceSize
    int sw0 = 0, sh0 = 0;  // original source size (sourceSize)
};

struct JsonAtlasMeta {
    std::string image;
    int w = 0, h = 0;
    float scale = 1.0f;
};

struct JsonAtlas {
    std::vector<JsonAtlasFrame> frames;
    JsonAtlasMeta meta;
    std::unordered_map<std::string, std::size_t> name_index;
};

enum class JsonAtlasError {
    Ok = 0,
    EmptyInput,
    JsonParseFailed,
    MissingFrames,
    MissingMeta,
    InvalidFrameData,
};

[[nodiscard]] const char* to_string(JsonAtlasError e) noexcept;

[[nodiscard]] auto parse_json_atlas(std::string_view json)
    -> std::expected<JsonAtlas, JsonAtlasError>;

[[nodiscard]] auto parse_json_atlas_file(const std::string& path)
    -> std::expected<JsonAtlas, JsonAtlasError>;

} // namespace resf2::format