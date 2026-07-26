// === STAGED — awaits integration ===
// Address: 0x1002bb10
// Ghidra: FUN_1002bb10 -> stringEqualWithRange
// Verdict: VERIFIED GREEN (round 1)
//
// Compares a null-terminated C-string with a NameRange [begin, end).
// Returns low byte = 1 for exact match, 0 for mismatch.
// Used by CCA::isEqual (0x10083bb0) for "$Move" comparison
// and ~40 other callers across the binary.
//
// For ACCEPTED needs:
//   1. Define NameRange struct in engine types
//   2. Wire a caller (e.g. CCA::isEqual when integrated)
//   3. Ground-truth test with known-answer vectors
// ============================================================

#include <cstdint>

struct NameRange {
    const char* begin;
    const char* end;
};

uint32_t __cdecl stringEqualWithRange(const char* str, const NameRange& range)
{
    // Inline strlen
    const char* s = str;
    while (*s != '\0')
        ++s;
    uint32_t len = static_cast<uint32_t>(s - str);

    // Length mismatch -> return with low byte = 0
    uint32_t rangeLen = static_cast<uint32_t>(range.end - range.begin);
    if (len != rangeLen)
        return len & 0xFFFFFF00u;

    // 4-byte word loop (unaligned reads — valid on x86)
    const char* p1 = str;
    const char* p2 = range.begin;
    int32_t remaining = static_cast<int32_t>(len) - 4;

    if (remaining >= 0) {
        do {
            if (*reinterpret_cast<const uint32_t*>(p1) !=
                *reinterpret_cast<const uint32_t*>(p2)) {
                // Mismatch: p1/p2 NOT advanced, remaining NOT decremented
                break;
            }
            p1 += 4;
            p2 += 4;
            remaining -= 4;
        } while (remaining >= 0);
    }

    // Byte-by-byte remainder (matches ASM control flow exactly)
    if (remaining == -4)        // All bytes matched in word loop
        goto match;
    if (*p1 != *p2)             // Byte 0 mismatch
        goto mismatch;
    if (remaining == -3)        // Only 1 byte remaining
        goto match;
    if (p1[1] != p2[1])         // Byte 1 mismatch
        goto mismatch;
    if (remaining == -2)        // Only 2 bytes remaining
        goto match;
    if (p1[2] != p2[2])         // Byte 2 mismatch
        goto mismatch;
    if (remaining == -1)        // Only 3 bytes remaining
        goto match;
    if (p1[3] != p2[3])         // Byte 3 mismatch
        goto mismatch;

match:
    return (static_cast<uint32_t>(remaining) & 0xFFFFFF00u) | 1u;

mismatch:
    return static_cast<uint32_t>(remaining) & 0xFFFFFF00u;
}
