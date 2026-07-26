#pragma once
#include <cstdint>
#include <cstring>

// Animation slot — 12 bytes each in the original binary
// Layout: [0] name_begin (char*), [4] name_end (char*), [8] field_8 (int)
struct AnimSlot {
    const char* name_begin;
    const char* name_end;
    int32_t field_8;
};

// Range of slots: [0] ptr to AnimSlot array, [4] one-past-end ptr
struct AnimSlotRange {
    AnimSlot* begin;
    AnimSlot* end;
};

/// Searches for a slot whose name matches slotStart within the given range.
/// [ORIGINAL] FUN_10083ca0 — findMatchingSlotInList — VERIFIED GREEN.
inline uint32_t findMatchingSlotInList(const AnimSlot* slotStart, const AnimSlotRange& listRange) noexcept
{
    int count = (int)((const char*)listRange.end - (const char*)listRange.begin) / (int)sizeof(AnimSlot);
    uint32_t result = 0;

    if (count != 0) {
        AnimSlot* pCur = listRange.begin;
        AnimSlot* pEnd = listRange.end;
        if (pCur < pEnd) {
            int nameLen = (int)(slotStart->name_end - slotStart->name_begin);
            do {
                int slotNameLen = (int)(pCur->name_end - pCur->name_begin);
                if (nameLen == slotNameLen) {
                    const char* pA = pCur->name_begin;
                    const char* pB = slotStart->name_begin;
                    int rem = nameLen;
                    rem -= 4;
                    if (rem >= 0) {
                        do {
                            if (*(const int32_t*)pA != *(const int32_t*)pB) goto tail;
                            pA += 4; pB += 4;
                            rem -= 4;
                        } while (rem >= 0);
                    }
                    if (rem == -4) return (result & 0xFFFFFF00) | 1;
tail:
                    if (pA[0] == pB[0] &&
                        (rem == -3 ||
                        (pA[1] == pB[1] &&
                        (rem == -2 ||
                        (pA[2] == pB[2] &&
                        (rem == -1 ||
                        pA[3] == pB[3]))))))
                        return (result & 0xFFFFFF00) | 1;
                }
                pCur += 1;
            } while (pCur < pEnd);
        }
    }
    return result & 0xFFFFFF00;
}
