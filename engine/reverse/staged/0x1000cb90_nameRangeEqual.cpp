// === STAGED — awaits integration ===
// Address: 0x1000cb90
// Ghidra: FUN_1000cb90 -> nameRangeEqual
// Verdict: VERIFIED GREEN (round 1)
//
// Compares two NameRange [begin, end) for byte-exact equality.
// Returns low byte = 1 for exact match, 0 for mismatch.
//
// Called from ConditionInterval::virtual_8 (0x10086b90) and ~60 other
// callers across the binary.
//
// For ACCEPTED needs:
//   1. Define NameRange struct in engine types (if not already)
//   2. Wire a caller (e.g. ConditionInterval::virtual_8 when integrated)
//   3. Ground-truth test with known-answer vectors
// ============================================================

#include <cstdint>

struct NameRange {
    const char* begin;
    const char* end;
};

uint32_t __cdecl nameRangeEqual(const NameRange& lhs, const NameRange& rhs)
{
    uint32_t len1 = static_cast<uint32_t>(lhs.end - lhs.begin);
    uint32_t len2 = static_cast<uint32_t>(rhs.end - rhs.begin);
    uint32_t result = len2;

    // Length mismatch -> return with low byte = 0
    if (len1 != len2)
        return result & 0xFFFFFF00u;

    const char* p1 = lhs.begin;
    const char* p2 = rhs.begin;
    int32_t remaining = static_cast<int32_t>(len1);

    // 4-byte word loop (unaligned reads — valid on x86)
    while (remaining > 3) {
        result = *reinterpret_cast<const uint32_t*>(p1);
        if (result != *reinterpret_cast<const uint32_t*>(p2))
            goto byte_compare;          // p1/p2 NOT advanced on mismatch
        p1 += 4;
        p2 += 4;
        remaining -= 4;
    }

    // All bytes matched in word loop
    if (remaining == 0)
        goto match;

byte_compare:
    // Byte 0
    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(*p1));
    if (*p1 != *p2)
        goto mismatch;
    if (remaining == 1)
        goto match;

    // Byte 1
    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[1]));
    if (p1[1] != p2[1])
        goto mismatch;
    if (remaining == 2)
        goto match;

    // Byte 2
    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[2]));
    if (p1[2] != p2[2])
        goto mismatch;
    if (remaining == 3)
        goto match;

    // Byte 3
    result = (result & 0xFFFFFF00u) | static_cast<uint32_t>(static_cast<unsigned char>(p1[3]));
    if (p1[3] != p2[3])
        goto mismatch;

match:
    return (result & 0xFFFFFF00u) | 1u;

mismatch:
    return result & 0xFFFFFF00u;
}
