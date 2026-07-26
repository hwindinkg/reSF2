#define _CRT_SECURE_NO_WARNINGS
#include <cstdint>
#include <cstdio>
#include <vector>

uint32_t normalize(uint32_t& range, uint32_t& code, const uint8_t*& pos, const uint8_t* end) {
    while (range < 0x1000000) {
        range <<= 8;
        code = (code << 8) | (pos < end ? *pos++ : 0);
    }
    return range;
}

int bit(uint32_t& range, uint32_t& code, uint16_t& prob, const uint8_t*& pos, const uint8_t* end) {
    normalize(range, code, pos, end);
    uint32_t bound = (range >> 11) * prob;
    if (code < bound) {
        range = bound;
        prob += static_cast<uint16_t>((0x800 - prob) >> 5);
        return 0;
    }
    code -= bound;
    range -= bound;
    prob -= static_cast<uint16_t>(prob >> 5);
    return 1;
}

int decode_first(const uint8_t* stream, size_t sz, uint16_t ip, int tree_off) {
    std::vector<uint16_t> probs(2048, ip);
    uint32_t range = 0xFFFFFFFF;
    uint32_t code = (uint32_t(stream[0]) << 24) | (uint32_t(stream[1]) << 16) | (uint32_t(stream[2]) << 8) | uint32_t(stream[3]);
    const uint8_t* pos = stream + 4;
    const uint8_t* end = stream + sz;
    
    int is_match = bit(range, code, probs[0], pos, end);
    if (is_match != 0) return -1;
    
    uint32_t sym = 1;
    for (int i = 0; i < 8; i++)
        sym = (sym << 1) | bit(range, code, probs[tree_off + sym], pos, end);
    return int(sym - 256);
}

int main() {
    FILE* f = fopen("assets/files.dz", "rb");
    if (!f) { printf("FAIL open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(sz);
    fread(dz.data(), 1, sz, f); fclose(f);
    const uint8_t* s = dz.data() + 0x1937;
    size_t cs = 883;
    
    printf("=== Sweep: init=1..2048, offset=0..128 ===\n");
    int hits = 0;
    for (int ip = 1; ip <= 2048; ip += 8) {
        for (int off = 0; off <= 128; off++) {
            int byte = decode_first(s, cs, uint16_t(ip), off);
            if (byte == 0x3C) { printf("HIT: init=%d(0x%04X) off=%d\n", ip, ip, off); hits++; }
        }
    }
    printf("Total hits: %d\n", hits);
    
    printf("\n=== Sweep wider: init=1..4096, off=0..32 ===\n");
    for (int off = 0; off <= 32; off++) {
        int byte = decode_first(s, cs, 0x400, off);
        printf("  offset=%d: byte=0x%02X\n", off, byte);
    }
    return 0;
}
