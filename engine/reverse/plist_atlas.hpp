// engine/reverse/plist_atlas.hpp
//
// Read-only parser for Cocos2d-x TexturePacker v2 plist atlas format.
//
// All 148 .plist files in the Shadow Fight 2 APK use TexturePacker
// format v2 (confirmed by `<integer>2</integer>` in the metadata block).
//
// This parser uses pugixml (vendored) for XML parsing and a small
// hand-written parser for the `{x,y}` / `{{x,y},{w,h}}` geometry strings.
//
// Stage 5 task S5.1.

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resf2::reverse::plist {

// A single sprite frame inside the atlas.
struct Frame {
    std::string name;           // sprite name (e.g. "background_1.png")
    std::int32_t atlas_x = 0;   // x position in atlas texture
    std::int32_t atlas_y = 0;   // y position in atlas texture
    std::int32_t atlas_w = 0;   // width in atlas (post-rotation if rotated)
    std::int32_t atlas_h = 0;   // height in atlas (post-rotation if rotated)
    std::int32_t offset_x = 0;  // center offset x
    std::int32_t offset_y = 0;  // center offset y
    bool         rotated = false; // true if sprite is stored rotated 90° CW
    std::int32_t source_x = 0;  // source color rect x
    std::int32_t source_y = 0;  // source color rect y
    std::int32_t source_w = 0;  // source color rect width
    std::int32_t source_h = 0;  // source color rect height
    std::int32_t source_size_w = 0;  // original (untrimmed) width
    std::int32_t source_size_h = 0;  // original (untrimmed) height
};

// Atlas metadata (the `metadata` dict in the plist).
struct Metadata {
    std::int32_t format = 2;            // always 2 in this game
    std::string  real_texture_filename; // e.g. "bg.png"
    std::int32_t texture_w = 0;         // atlas texture width
    std::int32_t texture_h = 0;         // atlas texture height
    std::string  smart_update;          // TexturePacker hash
    std::string  texture_filename;      // e.g. "bg.png"
};

// A parsed atlas file.
struct ParsedAtlas {
    std::vector<Frame>            frames;
    Metadata                      metadata;
    std::unordered_map<std::string, std::size_t> name_index; // name -> frames[] index
};

// Error codes.
enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kXmlParseFailed,
    kNotPlistFormat,
    kUnsupportedFormat,    // format != 2
    kMalformedGeometry,    // bad {x,y} or {{x,y},{w,h}} string
    kMissingMetadata,
};

[[nodiscard]] const char* to_string(ParseError e) noexcept;

// Parse a plist atlas from a UTF-8 string view.
// Returns ParsedAtlas on success, ParseError on failure.
[[nodiscard]] auto parse(std::string_view xml) -> std::expected<ParsedAtlas, ParseError>;

// Convenience: parse from a file on disk.
[[nodiscard]] auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::string>, ParsedAtlas>, ParseError>;

}  // namespace resf2::reverse::plist
