#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct DzContext {
    uint16_t* prob;
    uint8_t*  in_pos;
    uint8_t*  in_end;
    uint32_t  range;
    uint32_t  code;
};

void dz_normalize(DzContext& ctx) {
    while (ctx.range < 0x1000000) {
        ctx.range <<= 8;
        if (ctx.in_pos < ctx.in_end)
            ctx.code = (ctx.code << 8) | *ctx.in_pos++;
        else
            ctx.code <<= 8;
    }
}

int dz_bit_lzma(DzContext& ctx, uint16_t& prob) {
    dz_normalize(ctx);
    uint32_t bound = (ctx.range >> 11) * static_cast<uint32_t>(prob);
    if (ctx.code < bound) {
        ctx.range = bound;
        prob += static_cast<uint16_t>((0x800 - prob) >> 5);
        return 0;
    }
    ctx.code -= bound;
    ctx.range -= bound;
    prob -= static_cast<uint16_t>(prob >> 5);
    return 1;
}

int dz_bit_dz(DzContext& ctx, uint16_t& prob) {
    dz_normalize(ctx);
    uint32_t bound = (static_cast<uint64_t>(ctx.range) * prob) >> 16;
    if (ctx.code < bound) {
        ctx.range = bound;
        prob += static_cast<uint16_t>((0x10000 - prob) >> 5);
        return 0;
    }
    ctx.code -= bound;
    ctx.range -= bound;
    prob -= static_cast<uint16_t>(prob >> 5);
    return 1;
}

int decode_first_byte_lzma(const uint8_t* stream, size_t comp_size, uint16_t init_prob, int prob_offset) {
    std::vector<uint16_t> prob_table(2048, init_prob);
    DzContext ctx{};
    ctx.range = 0xFFFFFFFF;
    ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
    ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
    ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;
    ctx.prob = prob_table.data();

    int is_match = dz_bit_lzma(ctx, prob_table[0]);
    if (is_match != 0) return -1;

    uint32_t sym = 1;
    for (int i = 0; i < 8; i++) {
        int b = dz_bit_lzma(ctx, prob_table[prob_offset + sym]);
        sym = (sym << 1) | b;
    }
    return static_cast<int>(sym - 256);
}

int decode_first_byte_dz(const uint8_t* stream, size_t comp_size, uint16_t init_prob, int prob_offset) {
    std::vector<uint16_t> prob_table(2048, init_prob);
    DzContext ctx{};
    ctx.range = 0xFFFFFFFF;
    ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
    ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
    ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;
    ctx.prob = prob_table.data();

    int is_match = dz_bit_dz(ctx, prob_table[0]);
    if (is_match != 0) return -2;

    uint32_t sym = 1;
    for (int i = 0; i < 8; i++) {
        int b = dz_bit_dz(ctx, prob_table[prob_offset + sym]);
        sym = (sym << 1) | b;
    }
    return static_cast<int>(sym - 256);
}

int main() {
    FILE* f = nullptr;
    fopen_s(&f, "assets/files.dz", "rb");
    if (!f) { printf("Can't open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(sz);
    fread(dz.data(), 1, sz, f);
    fclose(f);

    const uint8_t* stream = dz.data() + 0x1937;
    size_t comp_size = 883;

    printf("=== Brute-force sweep: find initial prob that gives 0x3C ===\n");

    printf("\n--- LZMA formula (range>>11)*prob, offset 0x736 ---\n");
    for (int init = 1; init <= 4096; init++) {
        int byte = decode_first_byte_lzma(stream, comp_size, static_cast<uint16_t>(init), 0x736);
        if (byte == 0x3C) {
            printf("  HIT: init_prob=%d (0x%04X) -> byte=0x3C\n", init, init);
        }
    }

    printf("\n--- DZ formula (range*prob)>>16, offset 0x736 ---\n");
    for (int init = 1; init <= 65536; init++) {
        if (init % 256 == 0) {
            uint16_t p = static_cast<uint16_t>(init & 0xFFFF);
            int byte = decode_first_byte_dz(stream, comp_size, p, 0x736);
            if (byte == 0x3C) {
                printf("  HIT: init_prob=%d (0x%04X) -> byte=0x3C\n", p, p);
            }
        }
    }

    printf("\n--- DZ formula, offset 0 ---\n");
    for (int init = 0x7000; init <= 0x9000; init += 8) {
        uint16_t p = static_cast<uint16_t>(init & 0xFFFF);
        int byte = decode_first_byte_dz(stream, comp_size, p, 0);
        if (byte == 0x3C) {
            printf("  HIT: init_prob=%d (0x%04X) offset=0 -> byte=0x3C\n", p, p);
        }
    }

    printf("\n--- LZMA formula, sweeping offset ---\n");
    for (int off = 0; off <= 4096; off += 2) {
        int byte = decode_first_byte_lzma(stream, comp_size, 0x400, off);
        if (byte == 0x3C) {
            printf("  HIT: init_prob=0x400 offset=%d (0x%04X) -> byte=0x3C\n", off, off);
        }
    }

    printf("\n--- DZ formula, sweeping offset, prob=0x8000 ---\n");
    for (int off = 0; off <= 4096; off += 2) {
        int byte = decode_first_byte_dz(stream, comp_size, 0x8000, off);
        if (byte == 0x3C) {
            printf("  HIT: init_prob=0x8000 offset=%d (0x%04X) -> byte=0x3C\n", off, off);
        }
    }

    printf("\nDone.\n");
    return 0;
}
