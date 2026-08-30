#pragma once

// BMFont .fnt parser.
//
// The game's .fnt files (e.g. res/fight/digits.c9e1eb7a.fnt,
// res/ui/font-en.7043b83b.fnt) are the **binary** BMFont format, version 3
// (magic "BMF" + version byte 3), NOT the text format. Verified from the
// actual file headers.
//
// The game's reader (`tq.ek` in sf2.502f0946.js) ignores block types and
// walks the file as fixed sequential blocks. Layout as read by the game:
//
//   "BMF" + version(3)                    3 + 1 bytes
//   u32 blockSize                         4 bytes
//   info block (blockSize bytes):
//     u16 fontSize, u8, u8, u16, u8       (8 bytes)
//     u8 padding[4], u8 spacing[2], u8    (7 bytes)
//     u8 fontName[NUL]                    (blockSize-15 bytes; skipped)
//   u32 blockSize                         4 bytes
//   common block (blockSize bytes):
//     u16 lineHeight, u16 base, u16 scaleW, u16 scaleH, u16, u8 x5
//     (lineHeight is used as `max(fontSize, u16)`; base/scale used directly)
//   u32 blockSize                         4 bytes
//   pages block: NUL-terminated strings (first is the atlas texture name)
//   u32 blockSize                         4 bytes
//   chars block: charCount = blockSize/20, then per char (20 bytes):
//     u32 id, u16 x, u16 y, u16 width, u16 height,
//     i16 xoffset, i16 yoffset, i16 xadvance, u8 page, u8 chnl
//   optional kerning block: u32 blockSize, then triples
//     (u32 first, u32 second, i16 amount) until EOF.

#include <cstdint>
#include <string>
#include <vector>

namespace sf2::data {

struct font_char {
    std::uint32_t id = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int xoffset = 0;
    int yoffset = 0;
    int xadvance = 0;
};

struct font {
    int line_height = 0;
    int base = 0;
    int scale_w = 0;
    int scale_h = 0;
    std::string page;  // texture file name (first page; the game uses 1 page)
    std::vector<font_char> chars;
};

// Parses a BMFont binary v3 .fnt (UTF-8/ASCII bytes). Throws std::runtime_error
// on malformed/truncated input.
font font_parse(const std::uint8_t* data, std::size_t size);

} // namespace sf2::data
