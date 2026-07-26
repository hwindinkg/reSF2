// === STAGED — ждёт интеграции ===
// Адрес: 0x10083b50
// Ghidra: FUN_10083b50 → ConditionCurrentAnimation::getAnimationNames
// Вердикт: VERIFIED GREEN (calls 1:1, branches 2:1)
//
// Возвращает AnimSlotRange* для данного type из модели:
//   type=1 → model+0x20 (default fallthrough)
//   type=2 → model+0x2C
//   type=3 → model+0x38
//   type=4 → model+0x44
// ============================================================

#pragma once
#include <cstdint>

struct CCA_AnimSlotRange { void* slots; void* slotsEnd; };

static inline CCA_AnimSlotRange* CCA_getAnimationNames(void* self, void* model) {
    switch (*reinterpret_cast<int*>(static_cast<char*>(self) + 0x10)) {
    case 1: break;
    case 2: return reinterpret_cast<CCA_AnimSlotRange*>(static_cast<char*>(model) + 0x2C);
    case 3: return reinterpret_cast<CCA_AnimSlotRange*>(static_cast<char*>(model) + 0x38);
    case 4: return reinterpret_cast<CCA_AnimSlotRange*>(static_cast<char*>(model) + 0x44);
    default: extern void __cdecl logError(const char*, ...);
             logError("ConditionCurrentAnimation: getAnimationNames - wrong type: %i");
    }
    return reinterpret_cast<CCA_AnimSlotRange*>(static_cast<char*>(model) + 0x20);
}