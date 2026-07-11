// engine/reverse/bitmap_font.hpp
//
// Read-only parser for AngelCode BMFont text format (.fnt).
//
// All 16 .fnt files in the Shadow Fight 2 APK use this format.
// Each .fnt is paired with a same-name .png containing the glyph atlas.
//
// Format spec: https://www.angelcode.com/products/bmfont/doc/file_format.html
//
// Stage 5 task S5.3.

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resf2::reverse::font {

// Info line fields.
struct Info {
    std::string face;             // font face name
    std::int32_t size = 0;        // font size in pixels
    bool bold = false;
    bool italic = false;
    std::string charset;          // empty = Unicode
    bool unicode = false;
    std::int32_t stretch_h = 100; // horizontal stretch %
    bool smooth = false;
    std::int32_t aa = 0;          // anti-alias level
    std::int32_t padding[4] = {}; // left, top, right, bottom
    std::int32_t spacing[2] = {}; // horizontal, vertical
};

// Common line fields.
struct Common {
    std::int32_t line_height = 0;
    std::int32_t base = 0;
    std::int32_t scale_w = 0;
    std::int32_t scale_h = 0;
    std::int32_t pages = 0;
    bool packed = false;
};

// One page (texture atlas).
struct Page {
    std::int32_t id = 0;
    std::string  file;
};

// One glyph definition.
struct Char {
    std::int32_t id = 0;          // Unicode code point
    std::int32_t x = 0;           // position in atlas
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t xoffset = 0;
    std::int32_t yoffset = 0;
    std::int32_t xadvance = 0;
    std::int32_t page = 0;
    std::int32_t chnl = 0;        // channel (15 = all)
};

// One kerning pair.
struct Kerning {
    std::int32_t first = 0;
    std::int32_t second = 0;
    std::int32_t amount = 0;
};

// Parsed .fnt file.
struct ParsedFont {
    Info info;
    Common common;
    std::vector<Page> pages;
    std::vector<Char> chars;
    std::vector<Kerning> kernings;
    std::unordered_map<std::int32_t, std::size_t> char_index;  // codepoint -> chars[] index
    // Kerning lookup: (first << 32) | second -> amount
    std::unordered_map<std::uint64_t, std::int32_t> kerning_index;
};

enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kMalformedLine,
    kMissingInfoLine,
    kMissingCommonLine,
};

[[nodiscard]] const char* to_string(ParseError e) noexcept;

[[nodiscard]] auto parse(std::string_view text) -> std::expected<ParsedFont, ParseError>;

[[nodiscard]] auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::string>, ParsedFont>, ParseError>;

// Get the kerning adjustment for a (first, second) pair. Returns 0 if none.
[[nodiscard]] std::int32_t kerning_amount(const ParsedFont& font,
                                          std::int32_t first,
                                          std::int32_t second) noexcept;

}  // namespace resf2::reverse::font
