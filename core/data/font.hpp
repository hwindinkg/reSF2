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
//
// `id` is a **Unicode codepoint**, not a raw byte. Proof from the shipped
// assets + JS: `tq.ek` reads it as `a.ti()` (u32); `uq.Qq` builds every glyph
// as `new Pj(id, String.fromCodePoint(id), ...)`, and `uq.vAa` stores it into
// an array indexed by `id` (`c[e.id] = e`). The text node then looks a glyph
// up directly by code (`charset.Uy[65]` = 'A', `charset.Uy[U+041D..]` = the
// Cyrillic fallback caps). Confirmed on disk: `font-en.7043b83b.fnt` carries
// ids 0xD7/0xE9/0x2013… and `font-ru.32eaddc0.fnt` carries ids 0x410.. (U+0410
// CYRILLIC CAPITAL LETTER A). Source strings here are UTF-8, so decode
// codepoints before the lookup — see `utf8_next`/`find_glyph` below.

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

// --- UTF-8 text helpers --------------------------------------------------
// The .fnt `id` field is a Unicode codepoint (see the header note), so the
// draw/measure path must decode the UTF-8 source string into codepoints and
// look each one up by `id`. Iterating raw bytes only accidentally matches ASCII
// (single-byte codepoints); Cyrillic (U+0410..) is multi-byte UTF-8 and would
// otherwise be skipped.

// Returned by `utf8_next` for malformed/truncated input.
inline constexpr std::uint32_t kUtf8ReplacementChar = 0xFFFD;  // U+FFFD

// Decodes the UTF-8 codepoint at `text[i]`, advancing `i` past it. Invalid or
// truncated sequences consume exactly one byte and return
// `kUtf8ReplacementChar`, so malformed input can never loop or read out of
// bounds. Returns `kUtf8ReplacementChar` (no advance) when `i >= text.size()`.
std::uint32_t utf8_next(const std::string& text, std::size_t& i) noexcept;

// Linear glyph lookup by codepoint. Returns nullptr when absent.
const font_char* find_glyph(const font& f, std::uint32_t codepoint) noexcept;

// UTF-8-aware advance width. Unknown codepoints contribute nothing (the
// previous byte-skip behavior), so ASCII output is unchanged.
float measure_text_utf8(const font& f, const std::string& text, float scale) noexcept;

} // namespace sf2::data
