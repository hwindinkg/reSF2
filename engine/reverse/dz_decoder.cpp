#include "dz_decoder.hpp"
#include <cstring>
#include <cassert>
#include <cstdio>

namespace resf2::dz {

// Probability table layout:
// The DZ format organizes probabilities into a flat uint16 array.
// Core tables (state-based decisions) occupy the first ~252 entries:
//   [0..95]:     is_match[state][pos_state]      (12x8 = 96)
//   [96..107]:   is_rep[state]                   (12)
//   [108..119]:  is_rep0[state]                  (12)
//   [120..131]:  is_rep1[state]                  (12)
//   [132..143]:  is_rep2[state]                  (12)
//   [144..179]:  match_len[pos_state][bit]       (12x3 = 36)
//   [180..215]:  rep_match_len[pos_state][bit]   (12x3 = 36)
//   [216..251]:  dist_slot                       (36)
//
// Context tables follow at higher indices, sized according to
// RangeSettings (offset_contexts, offset_tables, ref tables).
//
// For the initial implementation, we use a large buffer (PROB_SIZE=8192)
// and byte-offset-based indexing from the initial ARM analysis.
// These offsets will be refactored once the decode loop is validated.

// CRC32 lookup table (poly 0xEDB88320, reflected IEEE 802.3)
static uint32_t g_crc32_table[256];
static bool g_crc32_table_init = false;

static void crc32_init_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        g_crc32_table[i] = crc;
    }
    g_crc32_table_init = true;
}

uint32_t crc32_window(const uint8_t* window, int len) {
    if (!window || len <= 0) return 0;
    if (!g_crc32_table_init) crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++)
        crc = g_crc32_table[(crc ^ window[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t dz_decode_bit_tree(DzContext& ctx, uint16_t* tree, uint32_t depth) {
    uint32_t sym = 1;
    for (uint32_t i = 0; i < depth; i++)
        sym = (sym << 1) | static_cast<uint32_t>(dz_bit(ctx, tree[sym]));
    return sym - (1u << depth);
}

bool parse_range_settings(const uint8_t* header, RangeSettings& rs) {
    if (!header) return false;
    rs.win_size = header[0];
    rs.flags = header[1];
    rs.offset_table_size = header[2];
    rs.offset_tables = header[3];
    rs.offset_contexts = header[4];
    rs.ref_length_table_size = header[5];
    rs.ref_length_tables = header[6];
    rs.ref_offset_table_size = header[7];
    rs.ref_offset_tables = header[8];
    rs.big_min_match = header[9];
    return true;
}

void dz_normalize(DzContext& ctx) {
    int guard = 0;
    while (ctx.range < 0x1000000 && guard < 10) {
        guard++;
        ctx.range <<= 8;
        if (ctx.in_pos < ctx.in_end)
            ctx.code = (ctx.code << 8) | *ctx.in_pos++;
        else
            ctx.code <<= 8;
    }
}

int dz_bit(DzContext& ctx, uint16_t& prob) {
    dz_normalize(ctx);
    uint32_t bound = (ctx.range >> 11) * static_cast<uint32_t>(prob);
    int bit;
    if (ctx.code < bound) {
        ctx.range = bound;
        prob += static_cast<uint16_t>((0x800 - prob) >> 5);
        bit = 0;
    } else {
        ctx.code -= bound;
        ctx.range -= bound;
        prob -= static_cast<uint16_t>(prob >> 5);
        bit = 1;
    }
    return bit;
}

static uint32_t decode_len(DzContext& ctx, uint16_t* probs, uint32_t pos_state) {
    int b0 = dz_bit(ctx, probs[pos_state * 3]);
    if (b0 == 0) return 2;
    int b1 = dz_bit(ctx, probs[pos_state * 3 + 1]);
    if (b1 == 0) return 3;
    int b2 = dz_bit(ctx, probs[pos_state * 3 + 2]);
    if (b2 == 0) return 4;
    uint32_t len = 5;
    for (int i = 0; i < 3; i++) {
        int bn = dz_bit(ctx, probs[pos_state * 3 + 3 + i]);
        if (bn == 0) return len;
        len++;
    }
    return len;
}

static uint32_t decode_dist(DzContext& ctx, uint16_t* probs) {
    uint32_t slot = 1;
    for (int i = 0; i < 6; i++) {
        int bn = dz_bit(ctx, probs[0x40 / 2 + slot]);
        slot = (slot << 1) | bn;
    }
    slot -= 64;

    if (slot < 4) return slot + 1;

    uint32_t extra = (slot >> 1) - 1;
    uint32_t base = (2 + (slot & 1)) << extra;
    for (uint32_t i = 0; i < extra; i++) {
        dz_normalize(ctx);
        ctx.range >>= 1;
        uint32_t bit = 0;
        if (ctx.code >= ctx.range) {
            ctx.code -= ctx.range;
            bit = 1;
        }
        base |= bit << i;
    }
    return base;
}

std::vector<uint8_t> dz_decompress(const uint8_t* compressed, size_t comp_size, size_t uncomp_size) {
    if (comp_size < 8 || uncomp_size < 1) {
        printf("[DZ] comp_size=%zu uncomp_size=%zu -- too small\n", comp_size, uncomp_size);
        return {};
    }

    const uint8_t* stream = compressed;
    size_t stream_size = comp_size;

    std::vector<uint8_t> out(uncomp_size, 0);

    constexpr int PROB_SIZE = 8192;
    std::vector<uint16_t> prob_table(PROB_SIZE, 0x400);

    auto p = [&](size_t byte_off) -> uint16_t& {
        return prob_table[byte_off / 2];
    };

    DzContext ctx{};
    ctx.prob = prob_table.data();
    ctx.range = 0xFFFFFFFF;

    if (stream_size < 5) return {};
    ctx.code = (static_cast<uint32_t>(stream[0]) << 24) |
               (static_cast<uint32_t>(stream[1]) << 16) |
               (static_cast<uint32_t>(stream[2]) << 8)  |
               (static_cast<uint32_t>(stream[3]));

    ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
    ctx.in_end = const_cast<uint8_t*>(stream) + stream_size;
    ctx.out_pos = out.data();
    ctx.out_count = 0;
    ctx.out_limit = static_cast<uint32_t>(uncomp_size);
    ctx.state = 0;
    ctx.rep0 = ctx.rep1 = ctx.rep2 = ctx.rep3 = 0;
    ctx.pos_state_mask = 7;
    ctx.total_out = 0;
    ctx.input_consumed = 0;
    memset(ctx.window, 0, sizeof(ctx.window));
    ctx.window_filled = 0;

    uint32_t out_count = 0;
    uint32_t max_iter = 200000;
    uint32_t iter = 0;

    while (out_count < uncomp_size && iter < max_iter) {
        iter++;
        uint32_t pos_state = out_count & ctx.pos_state_mask;
        uint32_t sv = ctx.state;

        int is_match = dz_bit(ctx, prob_table[sv * 16 + pos_state]);  // DZ uses *16 not *8

        if (is_match == 0) {
            // LITERAL: 8-bit bit-tree at byte offset 0xE6C
            uint32_t sym = 1;
            for (int i = 0; i < 8; i++) {
                int b = dz_bit(ctx, prob_table[0xE6C / 2 + sym]);
                sym = (sym << 1) | b;
            }
            uint8_t byte_val = static_cast<uint8_t>(sym - 256);
            if (out_count < uncomp_size) {
                ctx.out_pos[out_count++] = byte_val;
            }

            if (sv < 4) ctx.state = 0;
            else if (sv < 10) ctx.state = sv - 3;
            else ctx.state = sv - 6;

        } else {
            // Match
            int is_long = dz_bit(ctx, p(0x180 + sv * 2));  // DZ: is_rep at byte offset 0x180

            uint16_t* len_probs;
            uint32_t match_len;
            uint32_t match_dist;

            if (is_long == 0) {
                len_probs = &prob_table[0x664 / 2];
                match_len = decode_len(ctx, len_probs, pos_state);
                match_dist = decode_dist(ctx, len_probs);
                ctx.state = 7;
            } else {
                len_probs = &prob_table[0xA68 / 2];
                match_len = decode_len(ctx, len_probs, pos_state);
                match_dist = decode_dist(ctx, len_probs);
                ctx.state = 10;
            }

            if (match_dist == 0) match_dist = 1;
            if (match_dist > out_count) match_dist = out_count ? out_count : 1;
            if (match_dist == 0) match_dist = 1;

            uint8_t* src = ctx.out_pos + (out_count - match_dist);
            for (uint32_t i = 0; i < match_len && out_count < uncomp_size; i++) {
                uint8_t b = *src++;
                ctx.out_pos[out_count++] = b;
            }
        }
    }

    ctx.total_out = static_cast<uint32_t>(out_count);
    ctx.input_consumed = static_cast<uint32_t>(ctx.in_pos - stream);

    if (out_count > 0) {
        out.resize(out_count);
        printf("[DZ] Decoded %u bytes (limit=%zu, iter=%u)\n", out_count, uncomp_size, iter);
        printf("[DZ] First 16 bytes:");
        uint32_t n = out_count < 16 ? out_count : 16;
        for (uint32_t i = 0; i < n; i++)
            printf(" %02x", out[i]);
        printf("\n");
        return out;
    }

    printf("[DZ] FAILED: out_count=%u iter=%u\n", out_count, iter);
    return {};
}

}  // namespace resf2::dz
