// BMFont binary v3 .fnt parser.
//
// Mirrors the game's parser `tq.ek` in reference/www/sf2.502f0946.js
// (byte-for-byte read order — see font.hpp for the layout).

#include "font.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sf2::data {
namespace {

// LE u16/u32 reads from a byte buffer.
std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::int16_t rdi16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(rd16(p));
}

// Reads a NUL-terminated string starting at `data + off`, returning the
// string and the offset just past the NUL. Throws on truncation.
std::pair<std::string, std::size_t> read_cstr(const std::uint8_t* data, std::size_t size,
                                              std::size_t off) {
    const std::size_t begin = off;
    while (off < size && data[off] != 0) {
        ++off;
    }
    if (off >= size) {
        throw std::runtime_error("font: unterminated string");
    }
    return {std::string(reinterpret_cast<const char*>(data + begin), off - begin), off + 1};
}

// Block header read, mirroring the game's `a.ea()` (skip the block-type byte)
// then `a.ti()` (u32 LE block size). Returns the offset just past the header.
std::size_t read_block_header(const std::uint8_t* data, std::size_t size, std::size_t off,
                              std::uint32_t* block_size) {
    if (off + 5 > size) {
        throw std::runtime_error("font: truncated block header");
    }
    *block_size = rd32(data + off + 1);
    if (static_cast<std::size_t>(*block_size) > size - (off + 5)) {
        throw std::runtime_error("font: truncated block");
    }
    return off + 5;
}

} // namespace

font font_parse(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 6 || std::memcmp(data, "BMF", 3) != 0) {
        throw std::runtime_error("font: not a BMFont file");
    }
    if (data[3] != 3) {
        throw std::runtime_error("font: unsupported BMFont version " +
                                 std::to_string(data[3]) + " (expected 3)");
    }

    font result;
    std::size_t off = 4;  // "BMF" + version
    int line_height_candidate = 0;  // fontSize from info block; the game does
                                    // lineHeight = max(common.lineHeight, fontSize)

    // --- info block ---
    // The game reads: u32 blockSize, i16 fontSize, u8, u8, u16, u8,
    // u8 padding[4], u8 spacing[2], u8, then skips (blockSize-14) bytes
    // (the NUL-terminated font name). We only need fontSize (for lineHeight).
    {
        std::uint32_t block_size = 0;
        off = read_block_header(data, size, off, &block_size);
        if (block_size < 14) {
            throw std::runtime_error("font: short info block");
        }
        const std::uint16_t font_size = rd16(data + off);  // i16 fontSize (always positive here)
        line_height_candidate = static_cast<int>(font_size);
        result.size = static_cast<int>(font_size);  // JS `charset.eF` (L1672)
        off += block_size;  // skip the whole payload
    }

    // --- common block ---
    // The game reads: u16 lineHeight, u16 base, u16 scaleW, u16 scaleH,
    // u16, u8 x5, and stores lineHeight = max(lineHeight, fontSize).
    {
        std::uint32_t block_size = 0;
        off = read_block_header(data, size, off, &block_size);
        if (block_size < 15) {
            throw std::runtime_error("font: short common block");
        }
        const std::uint16_t line = rd16(data + off);  // u16 lineHeight
        result.line_height = std::max(static_cast<int>(line), line_height_candidate);
        result.base = rd16(data + off + 2);   // u16 base
        result.scale_w = rd16(data + off + 4);   // u16 scaleW
        result.scale_h = rd16(data + off + 6);   // u16 scaleH
        // u16, u8 x5 (skipped: pages, bitField, alpha/red/green/blue chnl)
        off += block_size;
    }

    // --- pages block ---
    // NUL-terminated strings; the first is the atlas texture file name
    // (the game reads it via `WJa`, then skips the remaining strings).
    {
        std::uint32_t block_size = 0;
        off = read_block_header(data, size, off, &block_size);
        const std::size_t block_end = off + block_size;
        if (block_end > size) {
            throw std::runtime_error("font: truncated pages block");
        }
        std::size_t pos = off;
        auto [first, next] = read_cstr(data, block_end, pos);
        result.page = first;
        pos = next;
        while (pos < block_end) {
            std::string tmp;
            std::tie(tmp, pos) = read_cstr(data, block_end, pos);
        }
        off = block_end;
    }

    // --- chars block ---
    // charCount = blockSize / 20 (the game's `a.ti()/20|0`). Per char
    // (20 bytes): u32 id, u16 x, u16 y, u16 width, u16 height,
    // i16 xoffset, i16 yoffset, i16 xadvance, u8 page, u8 chnl.
    {
        std::uint32_t block_size = 0;
        off = read_block_header(data, size, off, &block_size);
        if (block_size < 20) {
            throw std::runtime_error("font: short chars block");
        }
        const std::uint32_t count = block_size / 20;
        result.chars.reserve(count);
        const std::uint8_t* p = data + off;
        for (std::uint32_t i = 0; i < count; ++i) {
            font_char fc;
            fc.id = rd32(p);            // u32 id
            fc.x = rd16(p + 4);         // u16 x
            fc.y = rd16(p + 6);         // u16 y
            fc.w = rd16(p + 8);         // u16 width
            fc.h = rd16(p + 10);        // u16 height
            fc.xoffset = rdi16(p + 12); // i16 xoffset
            fc.yoffset = rdi16(p + 14); // i16 yoffset
            fc.xadvance = rdi16(p + 16); // i16 xadvance
            // u8 page, u8 chnl (skipped)
            result.chars.push_back(fc);
            p += 20;
        }
        off += block_size;
    }

    // --- optional kerning block (skipped) ---
    // The game reads it only when bytes remain: skip the block-type byte,
    // read u32 blockSize, then triples (u32 first, u32 second, i16 amount)
    // until EOF.
    if (off < size) {
        std::uint32_t ksize = 0;
        off = read_block_header(data, size, off, &ksize);
        off += ksize;
        if (off > size) {
            throw std::runtime_error("font: truncated kerning block");
        }
    }

    return result;
}

// --- UTF-8 text helpers ---------------------------------------------------

std::uint32_t utf8_next(const std::string& text, std::size_t& i) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t n = text.size();
    if (i >= n) {
        return kUtf8ReplacementChar;
    }

    const std::uint32_t lead = p[i++];
    if (lead < 0x80) {
        return lead;  // ASCII: byte == codepoint (EN path unchanged)
    }

    std::size_t extra = 0;
    std::uint32_t cp = 0;
    std::uint32_t min_cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        cp = lead & 0x1F;
        min_cp = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        cp = lead & 0x0F;
        min_cp = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        cp = lead & 0x07;
        min_cp = 0x10000;
    } else {
        return kUtf8ReplacementChar;  // invalid lead byte (consume 1)
    }

    if (extra > n - i) {
        return kUtf8ReplacementChar;  // truncated sequence (consume 1)
    }
    for (std::size_t k = 0; k < extra; ++k) {
        const std::uint32_t b = p[i + k];
        if ((b & 0xC0) != 0x80) {
            return kUtf8ReplacementChar;  // bad continuation (consume 1)
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    // Reject overlong encodings, UTF-16 surrogates, and out-of-range values.
    if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return kUtf8ReplacementChar;
    }
    i += extra;
    return cp;
}

const font_char* find_glyph(const font& f, std::uint32_t codepoint) noexcept {
    for (const auto& c : f.chars) {
        if (c.id == codepoint) {
            return &c;
        }
    }
    return nullptr;
}

float measure_text_utf8(const font& f, const std::string& text, float scale) noexcept {
    float width = 0.0f;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = utf8_next(text, i);
        if (const font_char* g = find_glyph(f, cp)) {
            width += static_cast<float>(g->xadvance) * scale;
        }
        // Unknown codepoint: contributes nothing (previous skip behavior).
    }
    return width;
}

float measure_text_height_utf8(const font& f, const std::string& text, float scale) noexcept {
    float height = 0.0f;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = utf8_next(text, i);
        if (const font_char* g = find_glyph(f, cp)) {
            const float h = static_cast<float>(g->h) * scale;
            if (h > height) height = h;
        }
    }
    return height;
}

} // namespace sf2::data
