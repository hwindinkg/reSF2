#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct DzContext {
    uint16_t* prob;
    uint8_t*  out_pos;
    uint32_t  range;
    uint32_t  code;
    uint8_t*  in_pos;
    uint8_t*  in_end;
    uint32_t  window[5];
    uint32_t  window_filled;
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

int dz_bit_v1(DzContext& ctx, uint16_t& prob) {
    // LZMA-style: bound = (range >> 11) * prob
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

int dz_bit_v2(DzContext& ctx, uint16_t& prob) {
    // DZ-hypothesis: bound = (range * prob) >> 16
    dz_normalize(ctx);
    uint32_t bound = (static_cast<uint64_t>(ctx.range) * prob) >> 16;
    int bit;
    if (ctx.code < bound) {
        ctx.range = bound;
        prob += static_cast<uint16_t>((0x10000 - prob) >> 6);
        bit = 0;
    } else {
        ctx.code -= bound;
        ctx.range -= bound;
        prob -= static_cast<uint16_t>(prob >> 6);
        bit = 1;
    }
    return bit;
}

int dz_bit_v3(DzContext& ctx, uint16_t& prob) {
    // LZMA-style but with 64-bit range
    uint64_t r64 = ctx.range;
    uint32_t bound = static_cast<uint32_t>((r64 >> 11) * prob);
    if (ctx.code < bound) {
        ctx.range = bound;
        prob += static_cast<uint16_t>((0x800 - prob) >> 5);
        return 0;
    } else {
        ctx.code -= bound;
        ctx.range = static_cast<uint32_t>(r64 - static_cast<uint64_t>(bound));
        prob -= static_cast<uint16_t>(prob >> 5);
        return 1;
    }
}

int decode_literal_v2(DzContext& ctx, uint16_t* base) {
    uint32_t sym = 1;
    for (int i = 0; i < 8; i++) {
        int b = dz_bit_v2(ctx, base[sym]);
        sym = (sym << 1) | b;
    }
    return static_cast<int>(sym - 256);
}

int main() {
    // Read the DZ file
    FILE* f = nullptr;
    fopen_s(&f, "assets/files.dz", "rb");
    if (!f) { printf("Can't open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(sz);
    fread(dz.data(), 1, sz, f);
    fclose(f);

    const uint8_t* stream = dz.data() + 0x1937;  // First file
    size_t comp_size = 883;

    // Global RangeSettings header at 0x192D
    printf("Global header at 0x192D:");
    for (int i = 0; i < 10; i++) printf(" %02X", dz[0x192D + i]);
    printf("\n");

    constexpr int PROB_SIZE = 8192;

    // Test 1: v1 (original) 
    {
        std::vector<uint16_t> prob_table(PROB_SIZE, 0x400);
        DzContext ctx{};
        ctx.prob = prob_table.data();
        ctx.range = 0xFFFFFFFF;
        ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
        ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
        ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;

        int is_match = dz_bit_v1(ctx, prob_table[0]);  
        printf("v1(range>>11)*prob, init=0x400: is_match=%d, range=0x%08X, code=0x%08X\n", is_match, ctx.range, ctx.code);

        // Decode 8 bits for literal
        uint32_t sym = 1;
        for (int i = 0; i < 8; i++) {
            int b = dz_bit_v1(ctx, prob_table[0x736 + sym]);
            sym = (sym << 1) | b;
        }
        uint8_t byte = static_cast<uint8_t>(sym - 256);
        printf("  literal byte: 0x%02X ('%c')\n", byte, byte >= 32 && byte < 127 ? byte : '.');
    }

    // Test 2: v2 (range*prob)>>16, init=0x8000
    {
        std::vector<uint16_t> prob_table(PROB_SIZE, 0x8000);
        DzContext ctx{};
        ctx.prob = prob_table.data();
        ctx.range = 0xFFFFFFFF;
        ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
        ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
        ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;

        int is_match = dz_bit_v2(ctx, prob_table[0]);
        printf("\nv2(range*prob)>>16, init=0x8000, rate=6: is_match=%d, range=0x%08X, code=0x%08X\n", is_match, ctx.range, ctx.code);

        uint32_t sym = 1;
        for (int i = 0; i < 8; i++) {
            int b = dz_bit_v2(ctx, prob_table[0x736 + sym]);
            sym = (sym << 1) | b;
        }
        uint8_t byte = static_cast<uint8_t>(sym - 256);
        printf("  literal byte: 0x%02X ('%c')\n", byte, byte >= 32 && byte < 127 ? byte : '.');
    }

    // Test 3: v2 with prob=0x400, rate=5
    {
        std::vector<uint16_t> prob_table(PROB_SIZE, 0x400);
        DzContext ctx{};
        ctx.prob = prob_table.data();
        ctx.range = 0xFFFFFFFF;
        ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
        ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
        ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;

        int is_match = dz_bit_v2(ctx, prob_table[0]);
        printf("\nv2(range*prob)>>16, init=0x400, rate=6: is_match=%d, range=0x%08X, code=0x%08X\n", is_match, ctx.range, ctx.code);

        uint32_t sym = 1;
        for (int i = 0; i < 8; i++) {
            int b = dz_bit_v2(ctx, prob_table[0x736 + sym]);
            sym = (sym << 1) | b;
        }
        uint8_t byte = static_cast<uint8_t>(sym - 256);
        printf("  literal byte: 0x%02X ('%c')\n", byte, byte >= 32 && byte < 127 ? byte : '.');
    }

    // Test 4: v3 (64-bit range, LZMA formula), init=0x400
    {
        std::vector<uint16_t> prob_table(PROB_SIZE, 0x400);
        DzContext ctx{};
        ctx.range = 0xFFFFFFFF;
        ctx.code = (stream[0] << 24) | (stream[1] << 16) | (stream[2] << 8) | stream[3];
        ctx.in_pos = const_cast<uint8_t*>(stream) + 4;
        ctx.in_end = const_cast<uint8_t*>(stream) + comp_size;

        int is_match = dz_bit_v3(ctx, prob_table[0]);
        printf("\nv3(64bit range, (r>>11)*prob), init=0x400: is_match=%d, range=0x%08X, code=0x%08X\n", is_match, ctx.range, ctx.code);

        uint32_t sym = 1;
        for (int i = 0; i < 8; i++) {
            int b = dz_bit_v3(ctx, prob_table[0x736 + sym]);
            sym = (sym << 1) | b;
        }
        uint8_t byte = static_cast<uint8_t>(sym - 256);
        printf("  literal byte: 0x%02X ('%c')\n", byte, byte >= 32 && byte < 127 ? byte : '.');
    }

    return 0;
}
