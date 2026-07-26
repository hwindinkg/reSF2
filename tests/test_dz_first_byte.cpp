#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../engine/reverse/dz_decoder.hpp"

// Reproduce the first byte decode with full tracing
int main() {
    // Read the .dz file
    FILE* f = nullptr;
    if (fopen_s(&f, "assets/files.dz", "rb") || !f) {
        std::fprintf(stderr, "Cannot open assets/files.dz\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dz(fsize);
    fread(dz.data(), 1, fsize, f);
    fclose(f);

    // File 0 parameters (from analysis):
    // - Data section header at 0x192D
    // - Compressed data at 0x1937, 883 bytes
    // - Expected uncompressed: 5955
    const uint8_t* comp = dz.data() + 0x1937;
    size_t comp_size = 883;
    size_t uncomp_size = 5955;

    std::vector<uint16_t> prob(8192, 0x400);

    struct DzState {
        uint16_t* prob;
        uint8_t* in_pos;
        uint8_t* in_end;
        uint8_t* out_pos;
        uint32_t range;
        uint32_t code;
        uint32_t out_count;
    };

    DzState st{};
    st.prob = prob.data();
    st.range = 0xFFFFFFFF;
    st.code = (static_cast<uint32_t>(comp[0]) << 24) |
              (static_cast<uint32_t>(comp[1]) << 16) |
              (static_cast<uint32_t>(comp[2]) << 8)  |
              (static_cast<uint32_t>(comp[3]));
    st.in_pos = const_cast<uint8_t*>(comp) + 4;
    st.in_end = const_cast<uint8_t*>(comp) + comp_size;
    st.out_count = 0;

    // Theory: DZ uses LZMA-style init (discard first byte of code)
    {
        auto try_init = [&](int skip_bytes, int ninit_bytes, const char* label) {
            std::vector<uint16_t> prob_try(8192, 0x400);
            DzState st{};
            st.range = 0xFFFFFFFF;
            st.code = 0;
            st.in_pos = const_cast<uint8_t*>(comp) + skip_bytes;
            st.in_end = const_cast<uint8_t*>(comp) + comp_size;
            int avail = static_cast<int>(st.in_end - st.in_pos);
            for (int i = 0; i < ninit_bytes && i < avail; i++)
                st.code = (st.code << 8) | *st.in_pos++;
            if (ninit_bytes > 4) st.code >>= 8;  // LZMA: discard first byte
            auto norm = [&]() {
                while (st.range < 0x1000000 && st.in_pos < st.in_end + 10) {
                    st.range <<= 8;
                    if (st.in_pos < st.in_end)
                        st.code = (st.code << 8) | *st.in_pos++;
                    else st.code <<= 8;
                }
            };
            auto bit = [&](uint16_t& p) -> int {
                norm();
                uint32_t b = (st.range >> 11) * p;
                if (st.code < b) { st.range = b; p += (0x800 - p) >> 5; return 0; }
                else { st.code -= b; st.range -= b; p -= p >> 5; return 1; }
            };
            (void)bit(prob_try[0]);
            uint32_t s = 1;
            for (int i = 0; i < 8; i++)
                s = (s << 1) | static_cast<uint32_t>(bit(prob_try[0x736 + s]));
            uint8_t byte = static_cast<uint8_t>(s - 256);
            std::printf("  %s: byte=0x%02X%s\n", label, byte, byte == 0x3C ? " MATCH!" : "");
        };
        try_init(0, 4, "code=bytes[0..3] (current)");
        try_init(0, 5, "LZMA: code=bytes[0..4], discard byte[0]");
        try_init(1, 4, "skip=1: code=bytes[1..4]");
    }

    // Try different literal tree configurations
    {
        struct LitCfg { int offset; int depth; const char* desc; };
        LitCfg cfgs[] = {
            {0xE6C/2, 8, "orig 8-bit tree at byte 0xE6C"},
            {0xE6C/2, 7, "orig offset, 7-bit tree"},
            {0xE6C/2, 9, "orig offset, 9-bit tree"},
            {252, 8, "right after core (252), 8-bit"},
            {0, 8, "at index 0, 8-bit"},
            {96+12+12+12+12+36+36, 8, "after all core tables"},
            {0x100, 8, "at byte offset 0x100 (header end)"},
            {108, 8, "at is_rep0 region"},
            {144, 8, "at len_choice region"},
            {180, 8, "at rep_len region"},
            {216, 8, "at dist_slot region"},
        };
        for (auto& cfg : cfgs) {
            std::vector<uint16_t> prob_try(8192, 0x400);
            DzState st{};
            st.prob = prob_try.data();
            st.range = 0xFFFFFFFF;
            st.code = (static_cast<uint32_t>(comp[0]) << 24) |
                      (static_cast<uint32_t>(comp[1]) << 16) |
                      (static_cast<uint32_t>(comp[2]) << 8)  |
                      (static_cast<uint32_t>(comp[3]));
            st.in_pos = const_cast<uint8_t*>(comp) + 4;
            st.in_end = const_cast<uint8_t*>(comp) + comp_size;

            auto norm = [&]() {
                while (st.range < 0x1000000 && st.in_pos < st.in_end + 10) {
                    st.range <<= 8;
                    if (st.in_pos < st.in_end)
                        st.code = (st.code << 8) | *st.in_pos++;
                    else st.code <<= 8;
                }
            };
            auto bit = [&](uint16_t& p) -> int {
                norm();
                uint32_t b = (st.range >> 11) * p;
                if (st.code < b) { st.range = b; p += (0x800 - p) >> 5; return 0; }
                else { st.code -= b; st.range -= b; p -= p >> 5; return 1; }
            };

            (void)bit(prob_try[0]);
            uint32_t s = 1;
            int depth = cfg.depth;
            for (int i = 0; i < depth; i++)
                s = (s << 1) | static_cast<uint32_t>(bit(prob_try[cfg.offset + s]));
            uint8_t byte = static_cast<uint8_t>(s - (1u << depth));
            std::printf("  %s: byte=0x%02X%s\n", cfg.desc, byte, byte == 0x3C ? " MATCH!" : "");
        }
    }

    // Try different initial code positions
    for (int skip = 0; skip < 6; skip++) {
        // Reset state
        std::vector<uint16_t> prob_try(8192, 0x400);
        DzState st{};
        st.prob = prob_try.data();
        st.range = 0xFFFFFFFF;

        if (comp_size < size_t(skip + 5)) continue;
        st.code = (static_cast<uint32_t>(comp[skip]) << 24) |
                  (static_cast<uint32_t>(comp[skip+1]) << 16) |
                  (static_cast<uint32_t>(comp[skip+2]) << 8)  |
                  (static_cast<uint32_t>(comp[skip+3]));
        st.in_pos = const_cast<uint8_t*>(comp) + skip + 4;
        st.in_end = const_cast<uint8_t*>(comp) + comp_size;
        st.out_count = 0;

        auto normalize2 = [&]() {
            int guard = 0;
            while (st.range < 0x1000000 && guard < 10) {
                guard++;
                st.range <<= 8;
                if (st.in_pos < st.in_end)
                    st.code = (st.code << 8) | *st.in_pos++;
                else
                    st.code <<= 8;
            }
        };

        uint32_t init_code = st.code;
        auto dz_bit2 = [&](uint16_t& p) -> int {
            normalize2();
            uint32_t b = (st.range >> 11) * static_cast<uint32_t>(p);
            int bit;
            if (st.code < b) {
                st.range = b;
                p += static_cast<uint16_t>((0x800 - p) >> 5);
                bit = 0;
            } else {
                st.code -= b;
                st.range -= b;
                p -= static_cast<uint16_t>(p >> 5);
                bit = 1;
            }
            return bit;
        };

        // is_match
        (void)dz_bit2(prob_try[0]);

        // Literal
        uint32_t s = 1;
        for (int i = 0; i < 8; i++) {
            s = (s << 1) | static_cast<uint32_t>(dz_bit2(prob_try[0x736 + s]));
        }
        uint8_t byte = static_cast<uint8_t>(s - 256);
        std::printf("skip=%d init_code=0x%08X -> first_byte=0x%02X (%s)\n",
               skip, init_code, byte, byte == 0x3C ? "MATCH" : "");
    }

    // Now trace the actual decoder in detail
    // First decision: is_match
    auto normalize = [&]() {
        int guard = 0;
        while (st.range < 0x1000000 && guard < 10) {
            guard++;
            st.range <<= 8;
            if (st.in_pos < st.in_end)
                st.code = (st.code << 8) | *st.in_pos++;
            else
                st.code <<= 8;
        }
    };

    auto dz_bit = [&](uint16_t& prob) -> int {
        normalize();
        uint32_t bound = (st.range >> 11) * static_cast<uint32_t>(prob);
        int bit;
        if (st.code < bound) {
            st.range = bound;
            prob += static_cast<uint16_t>((0x800 - prob) >> 5);
            bit = 0;
        } else {
            st.code -= bound;
            st.range -= bound;
            prob -= static_cast<uint16_t>(prob >> 5);
            bit = 1;
        }
        return bit;
    };

    std::printf("Initial: range=0x%08X code=0x%08X in_pos_offset=%zu\n",
           st.range, st.code, st.in_pos - comp);

    // is_match
    normalize();
    uint32_t bound = (st.range >> 11) * static_cast<uint32_t>(prob[0]);
    std::printf("is_match: prob=0x%04X bound=0x%08X code=0x%08X bit=%d\n",
           prob[0], bound, st.code, st.code < bound ? 0 : 1);
    int is_match = dz_bit(prob[0]);
    std::printf("  -> is_match=%d range=0x%08X code=0x%08X prob=0x%04X\n",
           is_match, st.range, st.code, prob[0]);

    // Literal bit-tree at prob_table[0xE6C/2 + sym] = prob_table[0x736 + sym]
    std::printf("\nDecoding literal byte (expected 0x3C):\n");
    uint32_t sym = 1;
    for (int i = 0; i < 8; i++) {
        normalize();
        uint16_t& p = prob[0x736 + sym];
        bound = (st.range >> 11) * p;
        int bit = st.code < bound ? 0 : 1;
        std::printf("  bit %d: prob=0x%04X bound=0x%08X code=0x%08X bit=%d sym=%u->%u\n",
               i, p, bound, st.code, bit, sym, (sym << 1) | bit);
        sym = (sym << 1) | dz_bit(p);
    }
    uint8_t byte_val = static_cast<uint8_t>(sym - 256);
    std::printf("  -> byte = 0x%02X ('%c')\n", byte_val, byte_val >= 32 ? byte_val : '.');

    // NOTE: This is a reverse-engineering research test.
    // The decoder currently produces 0x%02X instead of the expected 0x3C ('<').
    // This test always passes — it exists to print diagnostic trace output
    // during RE work, not as a regression assertion.
    // When the decoder is fixed to produce 0x3C, update the expected value here.
    std::printf("\n  VERDICT: decoder first_byte=0x%02X, expected=0x3C — %s\n",
           byte_val, byte_val == 0x3C ? "MATCH" : "MISMATCH (known WIP)");
    return 0;  // Informational test — always pass
}
