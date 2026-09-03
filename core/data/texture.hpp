#pragma once

// Texture decoding for the SF2 web-game assets.
//
// Decodes PNG/JPEG (stb_image), WebP (libwebp), DDS (BC1/BC2/BC3, CPU) and
// KTX (ETC1/ETC2 RGB/RGBA + ASTC 4x4..12x12, CPU) into 8-bit RGBA.
// DDS CRN (Hx) is not CPU-decoded — the KTX ASTC sibling is used instead.
// AVIF is not decoded (the game ships .webp where needed).
//
// All code in this module is portable C++17.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf2::data {

// Decoded 8-bit RGBA image. `rgba` has w*h*4 bytes, row-major, no padding.
struct Texture {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
};

// Detects the container by magic bytes and decodes the whole texture. `ext`
// is the lower-case extension INCLUDING the dot (".png", ".webp", ...); it is
// only used as a fallback when magic-byte sniffing is ambiguous. Returns false
// (and leaves `out` untouched) when the format is not supported or the data is
// malformed. Never throws.
bool decode_texture_bytes(const std::uint8_t* data, std::size_t size,
                          const std::string& ext, Texture& out);

// Reads `path` and decodes it. Convenience wrapper over decode_texture_bytes.
bool decode_texture(const std::string& path, Texture& out);

// --- individual decoders (also usable directly) -----------------------------

// PNG/JPEG via stb_image. Supports 1/2/3/4-channel sources; always yields RGBA.
bool decode_png_or_jpeg(const std::uint8_t* data, std::size_t size, Texture& out);

// WebP via libwebp (WebPDecodeRGBA).
bool decode_webp(const std::uint8_t* data, std::size_t size, Texture& out);

// DDS: BC1 (DXT1), BC2 (DXT3), BC3 (DXT5). The game's real .dds files are
// all BC3 (922 of them, under res/items/); BC1/BC2 are included for
// completeness. See core/data/dds.cpp for the block format.
bool decode_dds(const std::uint8_t* data, std::size_t size, Texture& out);

// KTX (KTX1 container): ETC1 + ETC2 RGB/RGBA + ASTC (4x4..12x12, via astc_dec).
// The game's .ktx files are ASTC (glInternalFormat 0x93B0..0x93BD).
bool decode_ktx(const std::uint8_t* data, std::size_t size, Texture& out);

} // namespace sf2::data
