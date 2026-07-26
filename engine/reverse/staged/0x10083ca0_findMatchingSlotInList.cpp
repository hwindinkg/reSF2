// === STAGED — ждёт интеграции ===
// Адрес: 0x10083ca0
// Ghidra: FUN_10083ca0 → findMatchingSlotInList
// Вердикт: VERIFIED GREEN (calls 0:0, branches 15:12)
//
// uint __cdecl findMatchingSlotInList(AnimSlot* slotStart, AnimSlotRange& listRange)
// Ищет слот с совпадающим именем в списке слотов.
// Каждый слот 12 байт: [char* name, char* nameEnd, int field_8]
// Сравнение: сначала int[4], потом byte[1..3] tails.
// Возвращает: (result & 0xFFFFFF00) | 1 при совпадении, (result & 0xFFFFFF00) при несовпадении
// (= фактически 1 или 0 через маску 0xFF)
//
// Вызывается из: CCA::isEqual (0x10083bb0) — поиск "$Move" слота в model+0x14
//
// Для ACCEPTED нужно:
//   1. Определить структуры AnimSlot/AnimSlotRange в types.hpp
//   2. Вшить вызыватель в CCA::isEqual
//   3. Ground-truth тест с эталоном из оригинала
// ============================================================

#pragma once
#include <cstdint>

// uint __cdecl
static inline uint32_t findMatchingSlotInList(void* slotStart, void* listRange)
{
    void* pSlots = *(void**)listRange;
    int count = (*(int*)((char*)listRange + 4) - (int)pSlots) / 12;
    uint32_t result = 0;

    if (count != 0) {
        char* pCur = (char*)pSlots;
        char* pEnd = pCur + count * 12;
        if (pCur < pEnd) {
            int nameLen = *(int*)((char*)slotStart + 4) - *(int*)slotStart;
            do {
                int slotNameLen = *(int*)(pCur + 4) - *(int*)pCur;
                if (nameLen == slotNameLen) {
                    char* pA = *(char**)pCur;
                    char* pB = *(char**)slotStart;
                    int rem = nameLen;
                    rem -= 4;
                    if (rem >= 0) {
                        do {
                            if (*(int*)pA != *(int*)pB) goto tail;
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
                pCur += 12;
            } while (pCur < pEnd);
        }
    }
    return result & 0xFFFFFF00;
}
