// engine/reverse/dz_decoder.cpp
//
// DZ (derbh) decompressor — clean-room reimplementation.
//
// This is a clean-room reimplementation based on algorithm analysis of
// libs3e_android.so (Marmalade SDK). The algorithm is an LZMA-variant
// range coder with:
//   - 32-bit range coder (range + code registers)
//   - Probability model with bit-tree decoding
//   - LZ77-style match references
//   - 5-byte context window for probability table selection
//
// The streaming nature (overlapping file offsets in the archive) means
// the entire archive's data section is one continuous compressed stream.

#include "dz_decoder.hpp"
#include <cstdio>
#include <algorithm>

namespace resf2::dz {

// CRC32 table (polynomial 0x04C11DB7, big-endian)
const uint32_t DzDecompressor::CRC_TABLE[256] = {
    0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B,
    0x1A864DB2, 0x1E475005, 0x2608ED8B, 0x22C9F00C, 0x2F8AD6D5, 0x2B4BCB62,
    0x350C9B66, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD, 0x4C11DB70, 0x48D0C94C,
    0x4593D401, 0x4152A95E, 0x5F15ADAC, 0x5BD4B0BB, 0x569796C2, 0x52738775,
    0x6A51BCEA, 0x6E90950D, 0x6360D6D4, 0x67A16363, 0x79864ED6, 0x7D5B9761,
    0x720C7E98, 0x7635CF2F, 0x84237281, 0x80E20D36, 0x8DA546EB, 0x89643C5C,
    0x9434D73E, 0x907F1F89, 0x9D561F50, 0x9926BFE7, 0xAF581CF8, 0xAB39104F,
    0xA6503D96, 0xA231C221, 0xB4543E94, 0xB0359323, 0xBD701FFA, 0xB9119E4D,
    0xC8273E78, 0xCC042D6F, 0xD1095C36, 0xD5648D81, 0xE056DB58, 0xE4371FEF,
    0xEF3636C6, 0xEB5736E5, 0xFB929498, 0xFF33532F, 0xE80C6C3E, 0xEC6D3D6F,
    // ... (truncated — full table generated at runtime if needed)
};

// Actually, let's generate the CRC table at runtime for correctness.
static uint32_t crc_table[256];
static bool crc_table_initialized = false;

static void init_crc_table() {
    if (crc_table_initialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i << 24;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80000000) {
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF;
            } else {
                crc = (crc << 1) & 0xFFFFFFFF;
            }
        }
        crc_table[i] = crc;
    }
    crc_table_initialized = true;
}

static uint32_t crc32_hash(const uint8_t window[5]) {
    init_crc_table();
    uint32_t crc = 0;
    for (int i = 1; i < 5; ++i) {
        crc = ((crc << 8) ^ crc_table[(crc >> 24) ^ window[i]]) & 0xFFFFFFFF;
    }
    return crc;
}

// Range coder state
struct RangeCoder {
    uint32_t range;
    uint32_t code;
    const uint8_t* data;
    size_t size;
    size_t pos;

    void init(const uint8_t* d, size_t s, size_t offset) {
        data = d;
        size = s;
        pos = offset;
        range = 0xFFFFFFFF;
        code = 0;
        for (int i = 0; i < 4 && pos < size; ++i) {
            code = (code << 8) | data[pos++];
        }
    }

    void normalize() {
        if (range < 0x1000000) {
            range <<= 8;
            if (pos < size) {
                code = (code << 8) | data[pos++];
            } else {
                code <<= 8;
            }
        }
    }

    // Decode a bit with probability *prob (in [1, 0x7FF])
    int decode_bit(uint16_t& prob) {
        normalize();
        uint32_t bound = (range >> 11) * prob;
        if (code < bound) {
            range = bound;
            prob += (0x800 - prob) >> 5;
            return 0;
        } else {
            code -= bound;
            range -= bound;
            prob -= prob >> 5;
            return 1;
        }
    }

    // Decode a bit-tree of num_bits levels
    uint32_t decode_bit_tree(uint16_t* probs, int num_bits) {
        uint32_t m = 1;
        for (int i = 0; i < num_bits; ++i) {
            m = (m << 1) + decode_bit(probs[m]);
        }
        return m - (1u << num_bits);
    }

    // Decode a direct bits value (num_bits bits, no probability model)
    uint32_t decode_direct_bits(int num_bits) {
        uint32_t result = 0;
        for (int i = 0; i < num_bits; ++i) {
            normalize();
            range >>= 1;
            if (code >= range) {
                code -= range;
                result = (result << 1) | 1;
            } else {
                result = result << 1;
            }
        }
        return result;
    }
};

// Probability tables — LZMA-style layout
// From ARM decompilation (0x389f8, 0x3751c):
//   state[8..0x11] = 9 tables of 64 × uint16_t (0x80 bytes each)
//   state[0x10] = base + 0x400 (is-match table)
//   state[0x11] = base + 0x480 (is-rep table)
//   r4 + 0x640 + 4 = offset probs (64 entries)
//   r4 + 0xE60 + 0xC = literal probs (256×8 = 2048 entries)
//   r4 + 0x660 + 4 = match length probs (64 entries)
struct DzProbTables {
    static constexpr uint16_t INIT_PROB = 0x400;  // 1024

    // Main tables (9 × 64 entries)
    uint16_t tables[9][64];

    // Literal context: 256 symbols × 8 bits = 2048 entries
    uint16_t literal_probs[256][8];

    // Match length: 64 entries (bit-tree of 4 levels)
    uint16_t match_len_probs[64];

    // Offset: 64 entries (bit-tree of 6 levels)
    uint16_t offset_probs[64];

    void init() {
        for (int t = 0; t < 9; ++t)
            for (int i = 0; i < 64; ++i)
                tables[t][i] = INIT_PROB;
        for (int i = 0; i < 256; ++i)
            for (int j = 0; j < 8; ++j)
                literal_probs[i][j] = INIT_PROB;
        for (int i = 0; i < 64; ++i) {
            match_len_probs[i] = INIT_PROB;
            offset_probs[i] = INIT_PROB;
        }
    }
};

std::vector<uint8_t> DzDecompressor::decompress(const uint8_t* compressed, size_t comp_size,
                                                  size_t uncomp_size) {
    if (comp_size < 4 || uncomp_size == 0) {
        std::fprintf(stderr, "[DZ] decompress: invalid input (comp=%zu, uncomp=%zu)\n",
                     comp_size, uncomp_size);
        return {};
    }

    std::fprintf(stderr, "[DZ] decompress: comp=%zu, uncomp=%zu\n", comp_size, uncomp_size);

    std::vector<uint8_t> output;
    output.reserve(uncomp_size);

    RangeCoder rc;
    rc.init(compressed, comp_size, 0);

    DzProbTables probs;
    probs.init();

    // 5-byte context window (LZMA-style)
    uint8_t window[5] = {0, 0, 0, 0, 0};
    int window_pos = 0;

    int literal_count = 0, match_count = 0, iterations = 0;
    int max_iter = (int)uncomp_size * 10;

    while (output.size() < uncomp_size && rc.pos < comp_size && iterations < max_iter) {
        iterations++;

        // Update window with last output byte
        if (!output.empty()) {
            window[window_pos] = output.back();
            window_pos = (window_pos + 1) % 5;
        }

        // Compute context from window using CRC32 hash
        // From decompilation: context = (window[1]<<24)|(window[2]<<16)|(window[3]<<8)|window[4]
        uint32_t ctx = crc32_hash(window);
        uint32_t table_idx = ctx % 64;

        // Decode is-match flag (table 0, index table_idx)
        int is_match = rc.decode_bit(probs.tables[0][table_idx]);

        if (!is_match) {
            // Literal byte — decode 8 bits using bit-tree
            literal_count++;
            uint8_t prev_byte = output.empty() ? 0 : output.back();
            uint8_t symbol = 0;
            // Use literal_probs[prev_byte] as the bit-tree
            uint32_t m = 1;
            for (int bit = 0; bit < 8; ++bit) {
                int b = rc.decode_bit(probs.literal_probs[prev_byte][m]);
                m = (m << 1) + b;
            }
            symbol = (uint8_t)(m - 256);
            output.push_back(symbol);
        } else {
            // Match — decode length and offset
            match_count++;

            // Decode match length: bit-tree of 4 levels (values 0..15)
            uint32_t len_code = rc.decode_bit_tree(probs.match_len_probs, 4);
            uint32_t match_len = len_code + 2;  // 2..17

            // Decode offset: bit-tree of 6 levels (values 0..63)
            uint32_t offset_code = rc.decode_bit_tree(probs.offset_probs, 6);
            uint32_t match_offset = offset_code + 1;  // 1..64

            if (match_offset > output.size()) {
                std::fprintf(stderr, "[DZ] Invalid match: off=%u, out=%zu (lit=%d,mat=%d,iter=%d)\n",
                             match_offset, output.size(), literal_count, match_count, iterations);
                return {};
            }

            // Copy match
            size_t src_pos = output.size() - match_offset;
            for (uint32_t i = 0; i < match_len && output.size() < uncomp_size; ++i) {
                output.push_back(output[src_pos++]);
            }
        }
    }

    std::fprintf(stderr, "[DZ] result: %zu/%zu (lit=%d,mat=%d,iter=%d,in=%zu/%zu)\n",
                 output.size(), uncomp_size, literal_count, match_count, iterations, rc.pos, comp_size);

    if (!output.empty()) {
        std::fprintf(stderr, "[DZ] output first 32: ");
        for (size_t i = 0; i < 32 && i < output.size(); ++i)
            std::fprintf(stderr, "%02x ", output[i]);
        std::fprintf(stderr, "\n[DZ] as text: %.60s\n", output.data());
    }
    if (output.size() != uncomp_size) {
        std::fprintf(stderr, "[DZ] Size mismatch: exp=%zu got=%zu\n", uncomp_size, output.size());
    }
    return output;
}

std::vector<uint8_t> DzDecompressor::decompress_streaming(
    const uint8_t* compressed, size_t comp_size,
    size_t offset, size_t uncomp_size) {
    // For streaming, we need to maintain state across calls.
    // For now, use the simple decompress with offset adjustment.
    if (offset >= comp_size) return {};

    // Note: Proper streaming requires maintaining range coder state.
    // This is a simplified version that decompresses from the given offset.
    // It may not work for archives where files share a continuous stream.
    std::vector<uint8_t> output;
    output.reserve(uncomp_size);

    RangeCoder rc;
    rc.init(compressed, comp_size, offset);

    DzProbTables probs;
    probs.init();

    uint8_t window[5] = {0, 0, 0, 0, 0};
    int window_pos = 0;

    while (output.size() < uncomp_size && rc.pos < comp_size) {
        if (!output.empty()) {
            window[window_pos] = output.back();
            window_pos = (window_pos + 1) % 5;
        }

        uint32_t ctx = crc32_hash(window);
        uint32_t table_idx = ctx % 64;

        int is_match = rc.decode_bit(probs.tables[0][table_idx]);

        if (!is_match) {
            uint8_t prev_byte = output.empty() ? 0 : output.back();
            uint8_t symbol = 0;
            for (int bit = 0; bit < 8; ++bit) {
                int b = rc.decode_bit(probs.literal_probs[prev_byte][bit]);
                symbol = (uint8_t)((symbol << 1) | b);
            }
            output.push_back(symbol);
        } else {
            uint32_t len_code = rc.decode_bit_tree(probs.match_len_probs, 4);
            uint32_t match_len = len_code + 2;
            uint32_t offset_code = rc.decode_bit_tree(probs.offset_probs, 6);
            uint32_t match_offset = offset_code + 1;

            if (match_offset > output.size()) {
                std::fprintf(stderr, "[DZ] Invalid match in streaming mode\n");
                return {};
            }

            size_t src_pos = output.size() - match_offset;
            for (uint32_t i = 0; i < match_len && output.size() < uncomp_size; ++i) {
                output.push_back(output[src_pos++]);
            }
        }
    }

    return output;
}

}  // namespace resf2::dz
