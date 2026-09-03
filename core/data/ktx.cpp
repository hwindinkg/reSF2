// KTX (KTX1) decoder — ETC1 + ETC2 RGB/RGBA + ASTC.
// Container (verified against real .ktx files in res/):
//   u8  identifier[12] = {0xAB, 'K','T','X',' ','1','1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A}
//   u32 endianness       (0x04030201 LE)   offset 12
//   u32 glType           (0 for compressed) offset 16
//   u32 glTypeSize                            offset 20
//   u32 glFormat         (0 for compressed) offset 24
//   u32 glInternalFormat                      offset 28   <-- compression format
//   u32 glBaseInternalFormat                  offset 32
//   u32 pixelWidth                            offset 36
//   u32 pixelHeight                           offset 40
//   u32 pixelDepth (1)                        offset 44
//   u32 numberOfArrayElements (0)             offset 48
//   u32 numberOfFaces (1)                     offset 52
//   u32 numberOfMipmapLevels (1)              offset 56
//   u32 bytesOfKeyValueData                   offset 60
//   key/value data (bytesOfKeyValueData)
//   for each mip: u32 imageSize, then pixel data (padded to 4 bytes)
//
// The game's .ktx files are ASTC (glInternalFormat 0x93B0..0x93BD).
// This file implements ETC1/ETC2 and ASTC (via astc_dec) CPU decode.

#include "texture.hpp"

#include <cstdint>
#include <cstring>

#include "third_party/astc/astc_decomp.h"

namespace sf2::data {
namespace {

constexpr std::uint32_t kInternalEtc1 = 0x8D64u;        // GL_ETC1_RGB8_OES
constexpr std::uint32_t kInternalEtc2Rgb = 0x9274u;     // GL_COMPRESSED_RGB8_ETC2
constexpr std::uint32_t kInternalEtc2Rgba = 0x9278u;    // GL_COMPRESSED_RGBA8_ETC2_EAC
constexpr std::uint32_t kInternalEtc2Rgba1 = 0x9272u;   // GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2

// ASTC KHR enums (0x93B0 = 4x4 ... 0x93BC = 12x12, plus vendor 0x93BD = 12x12 alias).
constexpr std::uint32_t kAstc4x4 = 0x93B0u;
constexpr std::uint32_t kAstc5x4 = 0x93B1u;
constexpr std::uint32_t kAstc5x5 = 0x93B2u;
constexpr std::uint32_t kAstc6x5 = 0x93B3u;
constexpr std::uint32_t kAstc6x6 = 0x93B4u;
constexpr std::uint32_t kAstc8x5 = 0x93B5u;
constexpr std::uint32_t kAstc8x6 = 0x93B6u;
constexpr std::uint32_t kAstc8x8 = 0x93B7u;
constexpr std::uint32_t kAstc10x5 = 0x93B8u;
constexpr std::uint32_t kAstc10x6 = 0x93B9u;
constexpr std::uint32_t kAstc10x10 = 0x93BAu;
constexpr std::uint32_t kAstc12x10 = 0x93BBu;
constexpr std::uint32_t kAstc12x12 = 0x93BCu;
constexpr std::uint32_t kAstc12x12Alt = 0x93BDu; // game uses 0x93BD for 12x12

bool astc_block_dims(std::uint32_t internal, int* bw, int* bh) {
    switch (internal) {
        case kAstc4x4: *bw = 4; *bh = 4; return true;
        case kAstc5x4: *bw = 5; *bh = 4; return true;
        case kAstc5x5: *bw = 5; *bh = 5; return true;
        case kAstc6x5: *bw = 6; *bh = 5; return true;
        case kAstc6x6: *bw = 6; *bh = 6; return true;
        case kAstc8x5: *bw = 8; *bh = 5; return true;
        case kAstc8x6: *bw = 8; *bh = 6; return true;
        case kAstc8x8: *bw = 8; *bh = 8; return true;
        case kAstc10x5: *bw = 10; *bh = 5; return true;
        case kAstc10x6: *bw = 10; *bh = 6; return true;
        case kAstc10x10: *bw = 10; *bh = 10; return true;
        case kAstc12x10: *bw = 12; *bh = 10; return true;
        case kAstc12x12: case kAstc12x12Alt: *bw = 12; *bh = 12; return true;
        default: return false;
    }
}

// Modifier tables for individual/differential modes (Mesa etc1_modifier_tables).
constexpr int kModifierTables[8][4] = {
    {-8, -2, 2, 8},     {-17, -5, 5, 17},   {-29, -9, 9, 29},
    {-42, -13, 13, 42}, {-60, -18, 18, 60}, {-80, -24, 24, 80},
    {-106, -33, 33, 106}, {-183, -47, 47, 183},
};

// Punch-through transparent-block modifier tables (Mesa etc2_modifier_tables_non_opaque).
constexpr int kModifierTablesNonOpaque[8][4] = {
    {0, 8, 0, -8},       {0, 17, 0, -17},    {0, 29, 0, -29},
    {0, 42, 0, -42},     {0, 60, 0, -60},    {0, 80, 0, -80},
    {0, 106, 0, -106},   {0, 183, 0, -183},
};

// Distance table for T/H modes (Mesa etc2_distance_table).
constexpr int kDistanceTable[8] = {3, 6, 11, 16, 23, 32, 41, 64};

// EAC alpha modifier tables (Mesa etc2_modifier_tables).
constexpr int kEacModifierTables[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},   {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},   {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},   {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},   {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},    {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},    {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},    {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},     {-3, -5, -7, -9, 2, 4, 6, 8},
};

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Pixel index for texel (x, y) in a 4x4 block (Mesa etc2_rgb8_fetch_texel).
// `word` = (src[4] << 24) | (src[5] << 16) | (src[6] << 8) | src[7].
inline int pixel_index(std::uint32_t word, int x, int y) {
    const int bit = y + x * 4;
    return ((word >> (15 + bit)) & 0x2) | ((word >> bit) & 0x1);
}

// Pixel index for EAC alpha (Mesa etc2_get_pixel_index): 3 bits, MSB-first,
// bit = ((3-y) + (3-x)*4) * 3 within the 48-bit index field.
inline int eac_pixel_index(std::uint64_t idx48, int x, int y) {
    const int bit = ((3 - y) + (3 - x) * 4) * 3;
    return static_cast<int>((idx48 >> bit) & 0x7);
}

void write_px(std::uint8_t* dst, int stride, int x, int y, int r, int g, int b, int a) {
    std::uint8_t* p = dst + (static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)) * 4;
    p[0] = static_cast<std::uint8_t>(clamp255(r));
    p[1] = static_cast<std::uint8_t>(clamp255(g));
    p[2] = static_cast<std::uint8_t>(clamp255(b));
    p[3] = static_cast<std::uint8_t>(a);
}

enum class etc_kind { etc1, etc2_rgb, etc2_rgba, etc2_rgba1 };

struct etc_block_state {
    bool ind_mode, diff_mode, t_mode, h_mode, planar_mode;
    bool opaque;  // punch-through
    int distance;
    int modifier_tables[2][4];
    int base_colors[3][3];   // [subblock][rgb]
    int paint_colors[4][3];  // [paint][rgb]
};

// Extends 4-bit to 8-bit by replicating the high nibble (Mesa extend4to8bits).
inline int ext4(int v) { return (v << 4) | v; }
// Extends 5-bit to 8-bit by replicating the top 3 bits.
inline int ext5(int v) { return (v << 3) | (v >> 2); }
// Sign-extends 3-bit two's-complement (-4..3).
inline int sgn3(int v) { return v >= 4 ? v - 8 : v; }

void parse_etc_rgb_block(const std::uint8_t* src, bool punchthrough, etc_block_state& s) {
    s = etc_block_state{};
    bool diffbit = false;
    static const int lookup[8] = {0, 1, 2, 3, -4, -3, -2, -1};
    const int R_plus_dR = (src[0] >> 3) + lookup[src[0] & 7];
    const int G_plus_dG = (src[1] >> 3) + lookup[src[1] & 7];
    const int B_plus_dB = (src[2] >> 3) + lookup[src[2] & 7];

    if (punchthrough) {
        s.opaque = src[3] & 0x2;
    } else {
        diffbit = src[3] & 0x2;
    }

    if (!diffbit && !punchthrough) {
        s.ind_mode = true;
        for (int i = 0; i < 3; ++i) {
            s.base_colors[0][i] = ext4(src[i] >> 4);
            s.base_colors[1][i] = ext4(src[i] & 15);
        }
    } else if (R_plus_dR < 0 || R_plus_dR > 31) {
        s.t_mode = true;
        s.base_colors[0][0] = ext4((((src[0] >> 3) & 3) << 2) | (src[0] & 3));
        s.base_colors[0][1] = ext4(src[1] >> 4);
        s.base_colors[0][2] = ext4(src[1] & 15);
        s.base_colors[1][0] = ext4(src[2] >> 4);
        s.base_colors[1][1] = ext4(src[2] & 15);
        s.base_colors[1][2] = ext4(src[3] >> 4);
        s.distance = kDistanceTable[(((src[3] >> 2) & 3) << 1) | (src[3] & 1)];
        for (int i = 0; i < 3; ++i) {
            s.paint_colors[0][i] = clamp255(s.base_colors[0][i]);
            s.paint_colors[1][i] = clamp255(s.base_colors[1][i] + s.distance);
            s.paint_colors[2][i] = clamp255(s.base_colors[1][i]);
            s.paint_colors[3][i] = clamp255(s.base_colors[1][i] - s.distance);
        }
    } else if (G_plus_dG < 0 || G_plus_dG > 31) {
        s.h_mode = true;
        s.base_colors[0][0] = ext4((src[0] >> 3) & 15);
        s.base_colors[0][1] = ext4(((src[0] & 7) << 1) | ((src[1] >> 4) & 1));
        s.base_colors[0][2] = ext4((src[1] & 8) | (((src[1] & 3) << 1) | ((src[2] >> 7) & 1)));
        s.base_colors[1][0] = ext4((src[2] >> 3) & 15);
        s.base_colors[1][1] = ext4(((src[2] & 7) << 1) | ((src[3] >> 7) & 1));
        s.base_colors[1][2] = ext4((src[3] >> 3) & 15);
        const int v1 = (s.base_colors[0][0] << 16) | (s.base_colors[0][1] << 8) | s.base_colors[0][2];
        const int v2 = (s.base_colors[1][0] << 16) | (s.base_colors[1][1] << 8) | s.base_colors[1][2];
        s.distance = kDistanceTable[(src[3] & 4) | ((src[3] & 1) << 1) | (v1 >= v2 ? 1 : 0)];
        for (int i = 0; i < 3; ++i) {
            s.paint_colors[0][i] = clamp255(s.base_colors[0][i] + s.distance);
            s.paint_colors[1][i] = clamp255(s.base_colors[0][i] - s.distance);
            s.paint_colors[2][i] = clamp255(s.base_colors[1][i] + s.distance);
            s.paint_colors[3][i] = clamp255(s.base_colors[1][i] - s.distance);
        }
    } else if (B_plus_dB < 0 || B_plus_dB > 31) {
        s.planar_mode = true;
        s.opaque = true;
    } else if (diffbit || punchthrough) {
        s.diff_mode = true;
        s.base_colors[0][0] = ext5(src[0] >> 3);
        s.base_colors[0][1] = ext5(src[1] >> 3);
        s.base_colors[0][2] = ext5(src[2] >> 3);
        s.base_colors[1][0] = ext5(R_plus_dR);
        s.base_colors[1][1] = ext5(G_plus_dG);
        s.base_colors[1][2] = ext5(B_plus_dB);
    }

    if (s.ind_mode || s.diff_mode) {
        const int t1 = (src[3] >> 5) & 7;
        const int t2 = (src[3] >> 2) & 7;
        const int (*tbl)[4] = (!punchthrough || s.opaque) ? kModifierTables : kModifierTablesNonOpaque;
        for (int i = 0; i < 4; ++i) {
            s.modifier_tables[0][i] = tbl[t1][i];
            s.modifier_tables[1][i] = tbl[t2][i];
        }
    }
}

void fetch_etc_rgb_texel(const etc_block_state& s, const std::uint8_t* src, bool punchthrough,
                         std::uint8_t* dst, int stride, int bx, int by,
                         int x, int y) {
    const std::uint32_t word = (std::uint32_t(src[4]) << 24) | (std::uint32_t(src[5]) << 16) |
                               (std::uint32_t(src[6]) << 8) | std::uint32_t(src[7]);
    const int idx = pixel_index(word, x, y);
    int r, g, b;

    if (s.ind_mode || s.diff_mode) {
        if (punchthrough) {
            if (!s.opaque && idx == 2) {
                write_px(dst, stride, bx + x, by + y, 0, 0, 0, 0);
                return;
            }
        }
        const bool blk = (src[3] & 1) ? (y >= 2) : (x >= 2);
        const int* base = s.base_colors[blk];
        const int mod = s.modifier_tables[blk][idx];
        r = base[0] + mod;
        g = base[1] + mod;
        b = base[2] + mod;
        write_px(dst, stride, bx + x, by + y, r, g, b, 255);
    } else if (s.t_mode || s.h_mode) {
        if (punchthrough) {
            if (!s.opaque && idx == 2) {
                write_px(dst, stride, bx + x, by + y, 0, 0, 0, 0);
                return;
            }
        }
        write_px(dst, stride, bx + x, by + y, s.paint_colors[idx][0], s.paint_colors[idx][1],
                 s.paint_colors[idx][2], 255);
    } else if (s.planar_mode) {
        auto ext6 = [](int v) { return (v << 2) | (v >> 4); };
        auto ext7 = [](int v) { return (v << 1) | (v >> 6); };
        const int RO = (src[0] >> 1) & 0x3f;
        const int GO = ((src[0] & 1) << 6) | ((src[1] >> 1) & 0x3f);
        const int BO = ((src[1] & 1) << 5) | (src[2] & 0x18) | (((src[2] & 3) << 1) | ((src[3] >> 7) & 1));
        const int RH = ((src[3] & 0x7c) >> 1) | (src[3] & 1);
        const int GH = (src[4] >> 1) & 0x7f;
        const int BH = ((src[4] & 1) << 5) | ((src[5] >> 3) & 0x1f);
        const int RV = ((src[5] & 7) << 3) | ((src[6] >> 5) & 7);
        const int GV = ((src[6] & 0x1f) << 2) | ((src[7] >> 6) & 3);
        const int BV = src[7] & 0x3f;
        const int R8 = ext6(RO), G8 = ext7(GO), B8 = ext6(BO);
        const int Rh8 = ext6(RH), Gh8 = ext7(GH), Bh8 = ext6(BH);
        const int Rv8 = ext6(RV), Gv8 = ext7(GV), Bv8 = ext6(BV);
        const int red = (x * (Rh8 - R8) + y * (Rv8 - R8) + 4 * R8 + 2) >> 2;
        const int green = (x * (Gh8 - G8) + y * (Gv8 - G8) + 4 * G8 + 2) >> 2;
        const int blue = (x * (Bh8 - B8) + y * (Bv8 - B8) + 4 * B8 + 2) >> 2;
        write_px(dst, stride, bx + x, by + y, red, green, blue, 255);
    }
}

// ---- Whole-texture loops ------------------------------------------------------

void decode_etc_rgb_texture(const std::uint8_t* data, std::size_t data_size,
                            int width, int height, bool punchthrough, Texture& tex) {
    const int bw = (width + 3) / 4;
    const int bh = (height + 3) / 4;
    const std::size_t blocks = static_cast<std::size_t>(bw) * bh;
    if (blocks * 8 > data_size) {
        return;
    }
    std::size_t off = 0;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* src = data + off;
            etc_block_state s;
            parse_etc_rgb_block(src, punchthrough, s);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if (bx * 4 + x < width && by * 4 + y < height) {
                        fetch_etc_rgb_texel(s, src, punchthrough, tex.rgba.data(), width,
                                            bx, by, x, y);
                    }
                }
            }
            off += 8;
        }
    }
}

void decode_etc2_rgba_texture(const std::uint8_t* data, std::size_t data_size,
                              int width, int height, Texture& tex) {
    const int bw = (width + 3) / 4;
    const int bh = (height + 3) / 4;
    const std::size_t blocks = static_cast<std::size_t>(bw) * bh;
    if (blocks * 16 > data_size) {
        return;
    }
    std::size_t off = 0;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* src = data + off;
            const int base = src[0];
            const int mult = (src[1] >> 4) & 15;
            const int table = src[1] & 15;
            std::uint64_t idx48 = 0;
            for (int i = 0; i < 6; ++i) {
                idx48 = (idx48 << 8) | src[2 + i];
            }
            const int* mods = kEacModifierTables[table];
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if (bx * 4 + x >= width || by * 4 + y >= height) {
                        continue;
                    }
                    const int idx = eac_pixel_index(idx48, x, y);
                    const int a = base + mult * mods[idx];
                    std::uint8_t* p = tex.rgba.data() +
                                      (static_cast<std::size_t>(by * 4 + y) * width + bx * 4 + x) * 4;
                    p[3] = static_cast<std::uint8_t>(clamp255(a));
                }
            }
            etc_block_state s;
            parse_etc_rgb_block(src + 8, false, s);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if (bx * 4 + x < width && by * 4 + y < height) {
                        fetch_etc_rgb_texel(s, src + 8, false, tex.rgba.data(), width,
                                            bx, by, x, y);
                    }
                }
            }
            off += 16;
        }
    }
}

void decode_astc_texture(const std::uint8_t* data, std::size_t data_size,
                         int width, int height, int block_w, int block_h, Texture& tex) {
    const int bw = (width + block_w - 1) / block_w;
    const int bh = (height + block_h - 1) / block_h;
    const std::size_t need = static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 16;
    if (need > data_size) {
        return;
    }
    std::size_t off = 0;
    std::vector<std::uint8_t> block_out(static_cast<std::size_t>(block_w * block_h * 4));
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* src = data + off;
            // Decompress one ASTC block to RGBA8.
            const bool ok = basisu::astc::decompress(block_out.data(), src, false, block_w, block_h);
            if (!ok) {
                // On error fill with magenta (ASTC error color is already magenta, but ensure visibility).
                for (int i = 0; i < block_w * block_h; ++i) {
                    block_out[static_cast<std::size_t>(i) * 4 + 0] = 0xFF;
                    block_out[static_cast<std::size_t>(i) * 4 + 1] = 0x00;
                    block_out[static_cast<std::size_t>(i) * 4 + 2] = 0xFF;
                    block_out[static_cast<std::size_t>(i) * 4 + 3] = 0xFF;
                }
            }
            for (int y = 0; y < block_h; ++y) {
                for (int x = 0; x < block_w; ++x) {
                    const int px = bx * block_w + x;
                    const int py = by * block_h + y;
                    if (px >= width || py >= height) continue;
                    const std::uint8_t* s = block_out.data() + static_cast<std::size_t>(y * block_w + x) * 4;
                    std::uint8_t* d = tex.rgba.data() + (static_cast<std::size_t>(py) * width + px) * 4;
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = s[3];
                }
            }
            off += 16;
        }
    }
}

} // namespace

bool decode_ktx(const std::uint8_t* data, std::size_t size, Texture& out) {
    static const std::uint8_t kIdentifier[12] = {
        0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (data == nullptr || size < 64) {
        return false;
    }
    if (std::memcmp(data, kIdentifier, 12) != 0) {
        return false;
    }
    auto rd32 = [&](std::size_t off) {
        return static_cast<std::uint32_t>(data[off]) |
               (static_cast<std::uint32_t>(data[off + 1]) << 8) |
               (static_cast<std::uint32_t>(data[off + 2]) << 16) |
               (static_cast<std::uint32_t>(data[off + 3]) << 24);
    };
    const std::uint32_t endian = rd32(12);
    if (endian != 0x04030201u) {
        return false;
    }
    const std::uint32_t gl_internal = rd32(28);
    const int width = static_cast<int>(rd32(36));
    const int height = static_cast<int>(rd32(40));
    const std::uint32_t depth = rd32(44);
    const std::uint32_t array_elements = rd32(48);
    const std::uint32_t faces = rd32(52);
    const std::uint32_t mips = rd32(56);
    const std::uint32_t kv_size = rd32(60);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return false;
    }
    if (depth != 1 && depth != 0) {
        // Some files report depth 0 (means 2D); treat as 1.
    }
    if (array_elements != 0 || faces != 1 || mips < 1) {
        return false;
    }

    bool has_alpha = false;
    bool punchthrough = false;
    int astc_bw = 0;
    int astc_bh = 0;
    bool is_astc = false;
    switch (gl_internal) {
        case kInternalEtc1: break;
        case kInternalEtc2Rgb: break;
        case kInternalEtc2Rgba: has_alpha = true; break;
        case kInternalEtc2Rgba1: punchthrough = true; break;
        default:
            if (astc_block_dims(gl_internal, &astc_bw, &astc_bh)) {
                is_astc = true;
            } else {
                return false;
            }
            break;
    }

    // Header is 64 bytes, kv data follows. Image data starts after kv + padding.
    std::size_t off = 64 + static_cast<std::size_t>(kv_size);
    // kv data is padded to 4 bytes; round up.
    // Actually kv_size already includes padding to 4, but ensure.
    if (off > size) {
        return false;
    }
    if (off + 4 > size) {
        return false;
    }
    const std::uint32_t image_size = rd32(off);
    off += 4;
    if (off + image_size > size) {
        return false;
    }

    Texture tex;
    tex.w = width;
    tex.h = height;
    tex.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);

    if (is_astc) {
        decode_astc_texture(data + off, image_size, width, height, astc_bw, astc_bh, tex);
    } else if (has_alpha) {
        decode_etc2_rgba_texture(data + off, image_size, width, height, tex);
    } else {
        decode_etc_rgb_texture(data + off, image_size, width, height, punchthrough, tex);
    }

    out = std::move(tex);
    return true;
}

} // namespace sf2::data
