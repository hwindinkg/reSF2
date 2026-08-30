// DDS decoder — BC1 (DXT1), BC2 (DXT3), BC3 (DXT5).
//
// Layout (verified against the 922 real .dds files in res/items/, all BC3):
//   DDS_PIXELFORMAT at offset 72 (32 bytes): dwSize=32, dwFlags=0x4
//     (DDPF_FOURCC), dwFourCC = "DXT5" at offset 84.
//   Height at offset 12, Width at offset 16 (u32 LE).
//   After the 128-byte header: compressed blocks, 4x4 pixels each.
//
// Block formats (Microsoft DDS):
//   BC1/DXT1: 8 bytes per block.  u16 color0, u16 color1 (RGB565) + 4x4 2-bit
//     indices. color0 > color1 => 4-color mode (bit 0x4 expansion); otherwise
//     icolor0 = 2*(c0)+1 over 3 => 3-color mode (index 3 = transparent).
//   BC2/DXT3: 16 bytes per block. First 8 bytes = 4x4 4-bit alpha, then the
//     same RGB part as BC1 with ALWAYS 4-color mode.
//   BC3/DXT5: 16 bytes per block. First 8 bytes = 4x4 3-bit alpha with two
//     8-bit alpha endpoints (a0, a1); indices select one of 8 interpolated
//     values (6-step interpolation when a0 > a1, 4-step + 0/255 when a0 <= a1).
//     Then the BC1 RGB part (4-color mode).
//
// Color endpoint expansion (RGB565 -> RGB888): shift + bit replication.

#include "texture.hpp"

#include <cstring>

namespace sf2::data {
namespace {

constexpr std::uint32_t kMagicDds = 0x20534444u;  // "DDS "

constexpr std::uint32_t kDdpfFourCC = 0x00000004u;
constexpr std::uint32_t kDdpfRgb = 0x00000040u;

// Reads a little-endian u32 from an arbitrary offset (no alignment issues).
std::uint32_t rd32(const std::uint8_t* p, std::size_t off) {
    return static_cast<std::uint32_t>(p[off]) |
           (static_cast<std::uint32_t>(p[off + 1]) << 8) |
           (static_cast<std::uint32_t>(p[off + 2]) << 16) |
           (static_cast<std::uint32_t>(p[off + 3]) << 24);
}

std::uint16_t rd16(const std::uint8_t* p, std::size_t off) {
    return static_cast<std::uint16_t>(p[off]) |
           (static_cast<std::uint16_t>(p[off + 1]) << 8);
}

// RGB565 -> RGB888 with bit replication (5->8, 6->8).
void expand565(std::uint16_t c, std::uint8_t out[3]) {
    const int r5 = (c >> 11) & 0x1F;
    const int g6 = (c >> 5) & 0x3F;
    const int b5 = c & 0x1F;
    out[0] = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
    out[1] = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
    out[2] = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
}

// Decodes the BC1 8-byte RGB block: writes 4x4 RGB(A) into dst (4 bytes/px).
// `is_bc1` selects 3-color (transparent index 3) vs 4-color mode.
void decode_bc1_block(const std::uint8_t* src, std::uint8_t* dst, bool is_bc1) {
    const std::uint16_t c0 = rd16(src, 0);
    const std::uint16_t c1 = rd16(src, 2);
    std::uint8_t colors[4][3];
    expand565(c0, colors[0]);
    expand565(c1, colors[1]);
    if (c0 > c1) {
        // 4-color mode: 2/3 c0 + 1/3 c1 and 1/3 c0 + 2/3 c1.
        for (int i = 0; i < 3; ++i) {
            colors[2][i] = static_cast<std::uint8_t>((2 * colors[0][i] + colors[1][i]) / 3);
            colors[3][i] = static_cast<std::uint8_t>((colors[0][i] + 2 * colors[1][i]) / 3);
        }
    } else {
        // 3-color mode: color2 = average, color3 = transparent (black).
        for (int i = 0; i < 3; ++i) {
            colors[2][i] = static_cast<std::uint8_t>((colors[0][i] + colors[1][i]) / 2);
            colors[3][i] = 0;
        }
    }
    const std::uint32_t indices = rd32(src, 4);
    for (int px = 0; px < 16; ++px) {
        const int idx = (indices >> (2 * px)) & 3;
        if (is_bc1 && idx == 3 && c0 <= c1) {
            dst[4 * px + 0] = 0;
            dst[4 * px + 1] = 0;
            dst[4 * px + 2] = 0;
            dst[4 * px + 3] = 0;
        } else {
            dst[4 * px + 0] = colors[idx][0];
            dst[4 * px + 1] = colors[idx][1];
            dst[4 * px + 2] = colors[idx][2];
            dst[4 * px + 3] = 255;
        }
    }
}

// Decodes the BC2 (DXT3) 16-byte block: explicit 4-bit alpha + 4-color BC1 RGB.
void decode_bc2_block(const std::uint8_t* src, std::uint8_t* dst) {
    for (int px = 0; px < 16; ++px) {
        const int shift = 4 * px;
        const std::uint8_t a = static_cast<std::uint8_t>((src[shift >> 3] >> (shift & 7)) & 0xF);
        dst[4 * px + 3] = static_cast<std::uint8_t>(a * 17);
    }
    // BC1 RGB part: 4-color mode is forced (color0 > color1 not required).
    decode_bc1_block(src + 8, dst, /*is_bc1=*/false);
}

// Decodes the BC3 (DXT5) 16-byte block: interpolated 3-bit alpha + BC1 RGB.
void decode_bc3_block(const std::uint8_t* src, std::uint8_t* dst) {
    const std::uint8_t a0 = src[0];
    const std::uint8_t a1 = src[1];
    std::uint8_t alpha[8];
    alpha[0] = a0;
    alpha[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            alpha[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            alpha[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        }
        alpha[6] = 0;
        alpha[7] = 255;
    }
    // 4x4 3-bit alpha indices, packed LSB-first over 6 bytes.
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(src[2 + i]) << (8 * i);
    }
    for (int px = 0; px < 16; ++px) {
        dst[4 * px + 3] = alpha[(bits >> (3 * px)) & 7];
    }
    decode_bc1_block(src + 8, dst, /*is_bc1=*/false);
}

} // namespace

bool decode_dds(const std::uint8_t* data, std::size_t size, Texture& out) {
    if (data == nullptr || size < 128 || rd32(data, 0) != kMagicDds) {
        return false;
    }
    const std::uint32_t header_size = rd32(data, 4);
    if (header_size != 124) {
        return false;
    }
    const int width = static_cast<int>(rd32(data, 16));
    const int height = static_cast<int>(rd32(data, 12));
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return false;
    }

    // DDS_PIXELFORMAT (offset 72): dwSize=32, dwFlags, dwFourCC at 84.
    const std::uint32_t pf_flags = rd32(data, 80);
    const std::uint32_t fourcc = rd32(data, 84);
    const std::uint32_t rgb_bit_count = rd32(data, 88);

    enum class kind { none, bc1, bc2, bc3 };
    kind fmt = kind::none;
    if ((pf_flags & kDdpfFourCC) != 0) {
        switch (fourcc) {
            case 0x31545844u: fmt = kind::bc1; break;  // "DXT1"
            case 0x33545844u: fmt = kind::bc2; break;  // "DXT3"
            case 0x35545844u: fmt = kind::bc3; break;  // "DXT5"
            default: return false;
        }
    } else if ((pf_flags & kDdpfRgb) != 0 && rgb_bit_count == 32) {
        fmt = kind::none;  // uncompressed BGRA — not needed by the game's assets
    }
    if (fmt == kind::none) {
        return false;
    }

    const int bw = (width + 3) / 4;
    const int bh = (height + 3) / 4;
    const int block_size = (fmt == kind::bc1) ? 8 : 16;
    const std::size_t blocks_needed =
        static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * block_size;
    // Only the first mip is decoded; a multi-mip file carries more data.
    if (128 + blocks_needed > size) {
        return false;
    }

    Texture tex;
    tex.w = width;
    tex.h = height;
    tex.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);

    std::size_t src_off = 128;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            std::uint8_t block_rgba[64];  // 16 px * 4
            switch (fmt) {
                case kind::bc1:
                    decode_bc1_block(data + src_off, block_rgba, /*is_bc1=*/true);
                    break;
                case kind::bc2:
                    decode_bc2_block(data + src_off, block_rgba);
                    break;
                case kind::bc3:
                    decode_bc3_block(data + src_off, block_rgba);
                    break;
                case kind::none:
                    return false;
            }
            src_off += static_cast<std::size_t>(block_size);

            // Copy the block into the image, clipping at the right/bottom edge.
            const int px_x = bx * 4;
            const int px_y = by * 4;
            for (int row = 0; row < 4; ++row) {
                const int y = px_y + row;
                if (y >= height) {
                    break;
                }
                for (int col = 0; col < 4; ++col) {
                    const int x = px_x + col;
                    if (x >= width) {
                        break;
                    }
                    const std::size_t dst_off =
                        static_cast<std::size_t>(y) * width * 4 + static_cast<std::size_t>(x) * 4;
                    const std::size_t blk_off =
                        static_cast<std::size_t>(row) * 16 + static_cast<std::size_t>(col) * 4;
                    std::memcpy(tex.rgba.data() + dst_off, block_rgba + blk_off, 4);
                }
            }
        }
    }

    out = std::move(tex);
    return true;
}

} // namespace sf2::data
