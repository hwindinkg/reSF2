// === STAGED — awaits integration ===
// Address: 0x1000cc00
// Ghidra: FUN_1000cc00 -> nameRangeEqualsStr
// Verdict: VERIFIED GREEN (round 1)
//
// Compares NameRange [begin, end) with null-terminated C-string.
// Returns low byte = 1 for exact match, 0 for mismatch.
// Parameter order: (NameRange, C-string) — REVERSED from stringEqualWithRange.
//
// Called from ConditionInterval::virtual_8 (0x10086b90) and ~100 other
// callers across the binary.
//
// For ACCEPTED needs:
//   1. Define NameRange struct in engine types
//   2. Wire a caller (e.g. ConditionInterval)
//   3. Ground-truth test
// ============================================================

#include <cstdint>

struct NameRange {
    const char* begin;
    const char* end;
};

uint32_t __cdecl nameRangeEqualsStr(const NameRange& range, const char* str)
{
    // Inline strlen
    const char* p = str;
    while (*p != '\0')
        ++p;
    uint32_t len = static_cast<uint32_t>(p - str);

    const char* rp = range.begin;
    uint32_t rlen = static_cast<uint32_t>(range.end - range.begin);

    // Length mismatch -> return with low byte = 0
    if (rlen != len)
        return len & 0xFFFFFF00u;

    // 4-byte word loop (unaligned reads — valid on x86)
    int32_t rem = static_cast<int32_t>(len - 4);
    while (rem >= 0) {
        if (*reinterpret_cast<const uint32_t*>(rp) !=
            *reinterpret_cast<const uint32_t*>(str)) {
            goto tail;  // rp/str NOT advanced, rem NOT decremented
        }
        rp += 4;
        str += 4;
        rem -= 4;
    }

tail:
    // rem: -4 = 0 bytes left, -3 = 1 byte, -2 = 2 bytes, -1 = 3 bytes
    if (rem != -4) {
        if (rp[0] != str[0] ||
            (rem != -3 && (rp[1] != str[1] ||
            (rem != -2 && (rp[2] != str[2] ||
            (rem != -1 && rp[3] != str[3])))))) {
            goto mismatch;
        }
    }

    // match
    return (static_cast<uint32_t>(rem) & 0xFFFFFF00u) | 1u;

mismatch:
    return static_cast<uint32_t>(rem) & 0xFFFFFF00u;
}
