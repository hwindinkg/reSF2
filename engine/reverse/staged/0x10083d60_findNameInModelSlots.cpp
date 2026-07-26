// === STAGED — ждёт интеграции ===
// Адрес: 0x10083d60
// Ghidra: FUN_10083d60 → findNameInModelSlots
// Вердикт: YELLOW→GREEN (round 2 — исправлен byte-3 tail: goto nextSlot вместо return 0)
//   call_count: PASS, control_flow: PASS, fp_consistency: PASS,
//   stub_detect: PASS, body_proportion: PASS, side_effects: PASS
//
// int __cdecl findNameInModelSlots(AnimSlotRange& modelSlots, NameRange& conditionName)
// Ищет имя условия в списке слотов модели.
// Каждый слот 12 байт: [char* name, char* nameEnd, int field_8]
// Сравнение: int[4] chunks + byte[1..3] tails с goto nextSlot при несовпадении.
// Делитель 0x2AAAAAAB (magic for /12) — компиляторная оптимизация.
//
// Возвращает: 1 при совпадении, 0 при несовпадении.
//
// Вызывается из: CCA::isEqual (0x10083bb0) — поиск имени в слотах анимаций
//
// Для ACCEPTED нужно:
//   1. Определить структуры AnimSlotRange/NameRange в types.hpp
//   2. Вшить вызыватель в CCA::isEqual
//   3. Ground-truth тест с эталоном из оригинала
// ============================================================

#pragma once
#include <cstdint>

// int __cdecl
static inline int findNameInModelSlots(void* pSlotRange, void* pNameRange)
{
    void* pSlots = *(void**)pSlotRange;
    int byteRange = *(int*)((char*)pSlotRange + 4) - (int)pSlots;
    int count = byteRange / 12;  // compiler: imul by 0x2AAAAAAB
    if (count == 0) return 0;

    char* pCur = (char*)pSlots;
    char* pEnd = pCur + count * 12;
    char* pTargetName = *(char**)pNameRange;
    int targetLen = *(int*)((char*)pNameRange + 4) - (int)pTargetName;

    do {
        char* pSlotName = *(char**)pCur;
        int slotNameLen = *(int*)(pCur + 4) - (int)pSlotName;

        if (slotNameLen == targetLen) {
            char* pSrc = pTargetName;
            int remaining = slotNameLen;

            // int[4] chunks
            remaining -= 4;
            if (remaining >= 0) {
                do {
                    if (*(int*)pSlotName != *(int*)pSrc) goto byteCheck;
                    pSlotName += 4; pSrc += 4;
                    remaining -= 4;
                } while (remaining >= 0);
            }
            if (remaining == -4) return 1;

            // byte[1..3] tails
byteCheck:
            if (pSlotName[0] != pSrc[0]) goto nextSlot;
            if (remaining == -3) return 1;
            if (pSlotName[1] != pSrc[1]) goto nextSlot;
            if (remaining == -2) return 1;
            if (pSlotName[2] != pSrc[2]) goto nextSlot;
            if (remaining == -1) return 1;
            if (pSlotName[3] != pSrc[3]) goto nextSlot;  // КЛЮЧ: goto nextSlot, НЕ return 0!
            return 1;
        }
nextSlot:
        pCur += 12;
    } while (pCur < pEnd);
    return 0;
}
