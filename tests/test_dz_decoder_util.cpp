#include <cstdio>
#include <cstring>
#include <cstdint>
#include "../engine/reverse/dz_decoder.hpp"

int main() {
    std::printf("Test DZ decoder utilities\n");
    int failures = 0;

    // --- dz_decode_bit_tree tests ---
    std::printf("\n--- dz_decode_bit_tree ---\n");
    {
        // Synthetic probability array: all 0x400 (50%), depth=3 (8 symbols)
        // With all probs at 0x400, the tree should decode codewords based
        // on the bit stream. We can't test specific values without controlling
        // the bit stream, but we can verify the function exists and returns
        // values in range.
        uint16_t probs[15]; // enough for depth 3 (2^(3+1) - 1 = 15)
        for (int i = 0; i < 15; i++) probs[i] = 0x400;

        // Initialize a minimal DzContext with known code/range
        resf2::dz::DzContext ctx{};
        ctx.range = 0xFFFFFFFF;
        // code = 0x7FFFFFFF -> first bit decode: bound = (0xFFFFFFFF>>11)*0x400 = (0x1FFFFF)*0x400
        // With code=0x7FFFFFFF and bound=(range>>11)*prob, code<bound -> bit=0
        // We use a deterministic setup to test the tree structure
        ctx.code = 0x7FFFFFFF;

        // Try decoding with depth=3 - should return 0..7
        // Since we can't easily control the bit output, just verify it runs
        // and returns something in range
        uint32_t sym = resf2::dz::dz_decode_bit_tree(ctx, probs, 3);
        std::printf("  sym=%u (expected 0..7)\n", sym);
        if (sym > 7) {
            std::fprintf(stderr, "  FAIL: sym out of range\n");
            ++failures;
        }
    }

    // --- crc32_window tests ---
    std::printf("\n--- crc32_window ---\n");
    {
        // Known CRC32 values (IEEE poly, big-endian)
        // CRC32("") = 0
        uint32_t crc_empty = resf2::dz::crc32_window(nullptr, 0);
        std::printf("  CRC32(empty) = 0x%08X (expected 0)\n", crc_empty);
        if (crc_empty != 0) {
            std::fprintf(stderr, "  FAIL: CRC32(empty) expected 0, got 0x%08X\n", crc_empty);
            ++failures;
        }

        // CRC32("ABCDE") - standard test vector
        const uint8_t test5[] = { 'A', 'B', 'C', 'D', 'E' };
        uint32_t crc_abcde = resf2::dz::crc32_window(test5, 5);
        std::printf("  CRC32(ABCDE) = 0x%08X\n", crc_abcde);

        // CRC32("123456789") - standard test vector = 0xCBF43926
        const uint8_t test9[] = "123456789";
        uint32_t crc_123 = resf2::dz::crc32_window(test9, 9);
        std::printf("  CRC32(123456789) = 0x%08X (expected 0xCBF43926)\n", crc_123);
        if (crc_123 != 0xCBF43926) {
            std::fprintf(stderr, "  FAIL: CRC32(123456789) expected 0xCBF43926, got 0x%08X\n", crc_123);
            ++failures;
        }
    }

    std::printf("\nResults: %d failures\n", failures);
    return failures;
}
