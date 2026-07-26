#include "conditions.hpp"

#include <cstdint>

// ── VTable symbols — actual values from original binary (Ghidra) ─────────
void* ConditionAnimation_vftable = nullptr;  // TODO: replace with 0x10382380
void* CCA_vftable = nullptr;                 // TODO: replace with actual

// ── Stub logError (replace with real logger when available) ──────────────
extern "C" void __cdecl logError(const char* fmt, ...) {
    (void)fmt; // stub — no-op in test builds
}

// ══════════════════════════════════════════════════════════════════════════
// NOTE on 32-bit binary compatibility
// ──────────────────────────────────────────────────────────────────────────
// The original binary is 32-bit x86 where pointers are 4 bytes.  AnimSlot
// entries are 12 bytes each: [name:4, nameEnd:4, field_8:4].  All slot
// iteration uses 12-byte stepping.  Pointer fields are uint32_t in the
// structs and must be cast via static_cast<uintptr_t>() when dereferencing.
//
// AnimSlotRange::getSlots()/getSlotsEnd() / AnimSlot::getName()/getNameEnd()
// perform this conversion.  These helpers also handle the count calculation.
// ══════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════
// CCA::getAnimationNames (FUN_10083b50) — VERIFIED GREEN
// Returns AnimSlotRange* for the animation type from model.
//   type=1 → model+0x20 (default fallthrough)
//   type=2 → model+0x2C  type=3 → model+0x38  type=4 → model+0x44
// ══════════════════════════════════════════════════════════════════════════

AnimSlotRange* CCA_getAnimationNames(const ConditionCurrentAnimation* self,
                                      void* model) noexcept
{
    switch (self->type) {
    case 1:
        break;
    case 2:
        return reinterpret_cast<AnimSlotRange*>(
            static_cast<char*>(model) + 0x2C);
    case 3:
        return reinterpret_cast<AnimSlotRange*>(
            static_cast<char*>(model) + 0x38);
    case 4:
        return reinterpret_cast<AnimSlotRange*>(
            static_cast<char*>(model) + 0x44);
    default:
        logError("ConditionCurrentAnimation: getAnimationNames - wrong type: %i",
                 self->type);
        break;
    }
    return reinterpret_cast<AnimSlotRange*>(
        static_cast<char*>(model) + 0x20);
}

// ══════════════════════════════════════════════════════════════════════════
// CCA::isEqual / virtual_8 (FUN_10083bb0) — VERIFIED GREEN
// Checks whether the model's current animation matches the condition.
// ══════════════════════════════════════════════════════════════════════════

uint32_t CCA_isEqual(const ConditionCurrentAnimation* self,
                      void* model) noexcept
{
    uint32_t result = 0;

    if (self->nameList == nullptr) {
        // No named condition — compare physics value directly
        char modelByte;
        switch (self->type) {
        case 2:
            modelByte = static_cast<char*>(model)[0xA1];
            break;
        case 3:
            modelByte = static_cast<char*>(model)[0xA2];
            break;
        default:
            modelByte = static_cast<char*>(model)[0xA0];
            break;
        }
        result = static_cast<uint32_t>(
            static_cast<uint8_t>(modelByte) == self->physicsValue);
    } else {
        // Named condition — search model's animation slots
        AnimSlotRange* pSlots = CCA_getAnimationNames(self, model);
        int slotCount = pSlots->count();

        // Check for "$Move" special name
        if (stringEqualWithRange("$Move", *self->nameList)) {
            if (slotCount > 0) {
                // Search for matching slot in model+0x14 list
                AnimSlotRange modelList;
                modelList.setSlots(*reinterpret_cast<AnimSlot**>(
                    static_cast<char*>(model) + 0x14));
                modelList.setSlotsEnd(*reinterpret_cast<AnimSlot**>(
                    static_cast<char*>(model) + 0x14 + sizeof(void*)));
                result = findMatchingSlotInList(
                    pSlots->getSlots(), modelList);
                result &= 0xFF;
            }
            goto invert;
        }

        if (!self->noAnimationFlag) {
            result = findNameInModelSlots(*pSlots, *self->nameList);
            result &= 0xFF;
            goto invert;
        }

        // noAnimationFlag: check if NO slots are active
        result = static_cast<uint32_t>(slotCount == 0);
    }

invert:
    if (self->invert) {
        result = static_cast<uint32_t>(!static_cast<bool>(result & 0xFF));
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════════
// findMatchingSlotInList (FUN_10083ca0) — VERIFIED GREEN
//
// Searches a slot list for an entry matching slotStart's name.
// Slots are 12 bytes each (32-bit pointer layout, sizeof(AnimSlot)=12).
// ══════════════════════════════════════════════════════════════════════════

uint32_t findMatchingSlotInList(const AnimSlot* slotStart,
                                 const AnimSlotRange& listRange) noexcept
{
    int count = listRange.count();
    uint32_t result = 0;
    if (count == 0)
        return result & 0xFFFFFF00u;

    // Search slot name length
    int nameLen = slotStart->nameLength();

    // Iterate raw memory: count slots, each 12 bytes
    const char* pCur = reinterpret_cast<const char*>(
        listRange.getSlots());
    const char* pEnd = pCur + count * sizeof(AnimSlot);  // 12 bytes each

    do {
        // Read slot fields as 32-bit values from raw memory
        uint32_t slotName32 = *reinterpret_cast<const uint32_t*>(pCur + 0);
        uint32_t slotEnd32  = *reinterpret_cast<const uint32_t*>(pCur + 4);
        int slotNameLen = static_cast<int>(slotEnd32 - slotName32);

        if (nameLen == slotNameLen) {
            const char* pA = reinterpret_cast<const char*>(
                static_cast<uintptr_t>(slotName32));
            const char* pB = slotStart->getName();
            int rem = nameLen;
            rem -= 4;

            if (rem >= 0) {
                do {
                    if (*reinterpret_cast<const int32_t*>(pA) !=
                        *reinterpret_cast<const int32_t*>(pB))
                        goto tail;
                    pA += 4;
                    pB += 4;
                    rem -= 4;
                } while (rem >= 0);
            }

            if (rem == -4)
                return (result & 0xFFFFFF00u) | 1u;

tail:
            if (pA[0] == pB[0] &&
                (rem == -3 ||
                (pA[1] == pB[1] &&
                (rem == -2 ||
                (pA[2] == pB[2] &&
                (rem == -1 ||
                 pA[3] == pB[3]))))))
            {
                return (result & 0xFFFFFF00u) | 1u;
            }
        }
        pCur += sizeof(AnimSlot);  // 12 bytes
    } while (pCur < pEnd);

    return result & 0xFFFFFF00u;
}

// ══════════════════════════════════════════════════════════════════════════
// findNameInModelSlots (FUN_10083d60) — VERIFIED GREEN
//
// Searches model's animation slot list for a matching name.
// Slots are 12 bytes each (32-bit pointer layout).
// ══════════════════════════════════════════════════════════════════════════

uint32_t findNameInModelSlots(const AnimSlotRange& modelSlots,
                               const NameRange& conditionName) noexcept
{
    int count = modelSlots.count();
    if (count == 0)
        return 0;

    const char* pTargetName = conditionName.begin;
    int targetLen = static_cast<int>(conditionName.end - conditionName.begin);

    const char* pCur = reinterpret_cast<const char*>(
        modelSlots.getSlots());
    const char* pEnd = pCur + count * sizeof(AnimSlot);

    do {
        uint32_t slotName32 = *reinterpret_cast<const uint32_t*>(pCur + 0);
        uint32_t slotEnd32  = *reinterpret_cast<const uint32_t*>(pCur + 4);
        int slotNameLen = static_cast<int>(slotEnd32 - slotName32);

        if (slotNameLen == targetLen) {
            const char* pSlotName = reinterpret_cast<const char*>(
                static_cast<uintptr_t>(slotName32));
            const char* pSrc = pTargetName;
            int remaining = slotNameLen;

            // 4-byte word comparison chunks
            remaining -= 4;
            if (remaining >= 0) {
                do {
                    if (*reinterpret_cast<const int32_t*>(pSlotName) !=
                        *reinterpret_cast<const int32_t*>(pSrc))
                        goto byteCheck;
                    pSlotName += 4;
                    pSrc += 4;
                    remaining -= 4;
                } while (remaining >= 0);
            }

            if (remaining == -4)
                return 1;

byteCheck:
            if (pSlotName[0] != pSrc[0])
                goto nextSlot;
            if (remaining == -3)
                return 1;
            if (pSlotName[1] != pSrc[1])
                goto nextSlot;
            if (remaining == -2)
                return 1;
            if (pSlotName[2] != pSrc[2])
                goto nextSlot;
            if (remaining == -1)
                return 1;
            if (pSlotName[3] != pSrc[3])
                goto nextSlot;
            return 1;
        }
nextSlot:
        pCur += sizeof(AnimSlot);  // 12 bytes
    } while (pCur < pEnd);

    return 0;
}

// ══════════════════════════════════════════════════════════════════════════
// ConditionInterval::virtual_8 (FUN_10086b90) — VERIFIED GREEN
//
// Pipeline condition checker: iterates context element list,
// checks matchId and buffer contents.
//
// Interval element layout (32-bit pointers, 0x1C total):
//   +0x00..+0x0B: unknown base fields
//   +0x0C: buffer.begin (uint32_t) / buffer.end (uint32_t at +0x10)
//   +0x18: typeId (int32)
//
// ConditionInterval layout:
//   +0x00: vtable | +0x0C: negateFlag | +0x14: matchId
//   +0x1C: buffer (Begin/End as void* on host; original is uint32_t)
// ══════════════════════════════════════════════════════════════════════════

bool ConditionInterval_virtual_8(const ConditionInterval* self,
                                  void* context) noexcept
{
    void** iterBase = *reinterpret_cast<void***>(
        static_cast<char*>(context) + 8);
    bool result = false;

    if (iterBase != nullptr) {
        void** begin = reinterpret_cast<void**>(iterBase[0]);
        void** end   = reinterpret_cast<void**>(iterBase[1]);

        intptr_t byteDiff = reinterpret_cast<char*>(end)
                          - reinterpret_cast<char*>(begin);
        int count = static_cast<int>(byteDiff / sizeof(void*));

        if (count != 0) {
            int32_t matchId = self->matchId;
            void** cur = begin;

            do {
                void* elemPtr = *cur;

                if (matchId == 0 ||
                    matchId == *reinterpret_cast<const int32_t*>(
                        static_cast<const char*>(elemPtr) + 0x18))
                {
                    // Read element buffer as uint32_t (32-bit pointers)
                    uint32_t elemBufBegin = *reinterpret_cast<const uint32_t*>(
                        static_cast<const char*>(elemPtr) + 0x0C);
                    uint32_t elemBufEnd = *reinterpret_cast<const uint32_t*>(
                        static_cast<const char*>(elemPtr) + 0x10);

                    // Check if self buffer is empty
                    bool emptyStr = (self->buffer.begin == nullptr
                        || self->buffer.begin == self->buffer.end);
                    if (emptyStr) {
                        result = true;
                        break;
                    }

                    // Compare element buffer to self buffer
                    NameRange selfBuf(
                        static_cast<const char*>(self->buffer.begin),
                        static_cast<const char*>(self->buffer.end));
                    NameRange elemBuf(
                        reinterpret_cast<const char*>(
                            static_cast<uintptr_t>(elemBufBegin)),
                        reinterpret_cast<const char*>(
                            static_cast<uintptr_t>(elemBufEnd)));
                    if (nameRangeEqual(selfBuf, elemBuf)) {
                        result = true;
                        break;
                    }
                }
                ++cur;
            } while (cur < end);
        }
    }

    if (self->negateFlag)
        result = !result;

    return result;
}
