// === STAGED — ждёт интеграции ===
// Адрес: 0x10083bb0
// Ghidra: FUN_10083bb0 → ConditionCurrentAnimation_isEqual
// Вердикт: VERIFIED GREEN (calls 5:11, branches 10:9)
//
// ConditionCurrentAnimation::isEqual
// Проверяет, совпадает ли текущая анимация модели с условием.
//
// Логика:
//   Если nameList == NULL:
//     switch (type) { 1→model+0xA0, 2→model+0xA1, 3→model+0xA2 }
//     результат = (modelByte == physicsValue)
//   Если nameList != NULL:
//     getAnimationNames → AnimSlotRange
//     Если "$Move": проверка наличия первого слота в model+0x14
//     Если !noAnimationFlag: поиск имени условия в слотах модели
//     Если noAnimationFlag: true если нет активных слотов
//   Финально: invert (this+0x0C) инвертирует результат
//
// Layout ConditionCurrentAnimation:
//   +0x00: vtable
//   +0x04: baseField
//   +0x08: baseParam (16)
//   +0x0C: invert (bool)
//   +0x10: type (1/2/3/4)
//   +0x14: nameList (NameRange* or NULL)
//   +0x18: noAnimationFlag (bool)
//   +0x19: physicsValue (uint8)
//
// Layout Model (param_1):
//   +0x14: slotList (AnimSlotRange)
//   +0x20: slotsType1, +0x2C: slotsType2, +0x38: slotsType3, +0x44: slotsType4
//   +0xA0: physics1, +0xA1: physics2, +0xA2: physics3
//
// Callee dependencies (in batch):
//   getAnimationNames (0x10083b50) — [target 2/5]
//   stringEqualWithRange (0x1002bb10) — compare C-string with NameRange
//   findMatchingSlotInList (0x10083ca0) — поиск в model+0x14
//   findNameInModelSlots (0x10083d60) — поиск имени в слотах
//   logError (0x101471b0) — вывод ошибки
//
// Для ACCEPTED нужно:
//   1. Реверснуть все callee
//   2. Определить ConditionCurrentAnimation в types.hpp
//   3. Вшить вызыватель в condition pipeline
//   4. Ground-truth тест с эталоном из оригинала
// ============================================================

#pragma once
#include <cstdint>

struct CCA_NameRange { char* data; char* dataEnd; };
struct CCA_AnimSlot { char* name; char* nameEnd; int field_8; };
struct CCA_AnimSlotRange { void* slots; void* slotsEnd; };

struct ConditionCurrentAnimation {
    void* vtable;           // +0x00
    int   baseField;        // +0x04
    int   baseParam;        // +0x08
    bool  invert;           // +0x0C
    int   type;             // +0x10
    CCA_NameRange* nameList; // +0x14
    bool  noAnimationFlag;  // +0x18
    uint8_t physicsValue;   // +0x19
};

extern "C" {
    CCA_AnimSlotRange* __thiscall getAnimationNames(ConditionCurrentAnimation* self, void* model);
    uint32_t __cdecl stringEqualWithRange(const char* str, const CCA_NameRange& range);
    uint32_t __cdecl findMatchingSlotInList(const CCA_AnimSlot* slotStart, const CCA_AnimSlotRange& listRange);
    uint32_t __cdecl findNameInModelSlots(const CCA_AnimSlotRange& modelSlots, const CCA_NameRange& conditionName);
    void __cdecl logError(const char* format, ...);
}

static inline uint32_t CCA_isEqual(ConditionCurrentAnimation* self, void* model)
{
    uint32_t result = 0;
    if (self->nameList == nullptr) {
        char modelByte;
        switch (self->type) {
            case 1: goto readA0;
            case 2: modelByte = static_cast<char*>(model)[0xA1]; break;
            case 3: modelByte = static_cast<char*>(model)[0xA2]; break;
            default:
                logError("ConditionCurrentAnimation: getAnimationNames - wrong type: %i");
            readA0:
                modelByte = static_cast<char*>(model)[0xA0]; break;
        }
        result = static_cast<uint32_t>(modelByte == self->physicsValue);
    } else {
        auto* pSlots = getAnimationNames(self, model);
        if (stringEqualWithRange("$Move", *self->nameList)) {
            int slotRange = reinterpret_cast<char*>(pSlots->slotsEnd) - reinterpret_cast<char*>(pSlots->slots);
            int signBit = slotRange >> 31;
            if (slotRange / 12 + signBit != signBit) {
                result = findMatchingSlotInList(static_cast<CCA_AnimSlot*>(pSlots->slots),
                    *static_cast<CCA_AnimSlotRange*>(static_cast<char*>(model) + 0x14));
                result &= 0xFF;
            }
            goto invert;
        }
        if (!self->noAnimationFlag) {
            result = findNameInModelSlots(*pSlots, *self->nameList);
            result &= 0xFF;
            goto invert;
        }
        int slotRange = reinterpret_cast<char*>(pSlots->slotsEnd) - reinterpret_cast<char*>(pSlots->slots);
        int signBit = slotRange >> 31;
        result = static_cast<uint32_t>(slotRange / 12 + signBit == signBit);
    }
invert:
    return self->invert ? static_cast<uint32_t>(!static_cast<bool>(result & 0xFF)) : result;
}