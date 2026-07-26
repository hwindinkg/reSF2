// === STAGED — ждёт интеграции ===
// Адрес: 0x10083ac0
// Ghidra: FUN_10083ac0 → ConditionCurrentAnimation конструктор
// Вердикт: YELLOW→GREEN (round 2 — убран спуриозный this->nameList = nullptr)
//   call_count: PASS, control_flow: PASS (SEH-шум), fp_consistency: PASS,
//   stub_detect: PASS, body_proportion: PASS, side_effects: PASS
//
// ConditionCurrentAnimation::ConditionCurrentAnimation(uint8_t allocFlag)
// Конструктор CCA: устанавливает vtable, вызывает Condition::Condition,
// обрабатывает флаг аллокации.
//
// Условная очистка nameList: если nameList != NULL, освобождает
// содержимое через FUN_102f7780 и thunk_FUN_102f9190.
//
// ВАЖНО: ASM НЕ пишет null в this+0x14 — инициализация поля может
// происходить через calloc-обнуление памяти ДО вызова конструктора.
//
// Для ACCEPTED нужно:
//   1. Определить класс ConditionCurrentAnimation с наследованием от Condition
//   2. Определить Condition::Condition
//   3. Вшить в condition pipeline (фабрику)
//   4. Ground-truth тест
// ============================================================

#pragma once
#include <cstdint>

// Внешние объявления
extern void Condition_Condition(void* self);
extern void thunk_FUN_102f9190(void* ptr);
extern void FUN_102f7780(void* ptr);
extern const void* ConditionCurrentAnimation_vftable;

struct CCA_NameRange_ctor { char** data; char** dataEnd; };

static inline void ConditionCurrentAnimation_constructor(void* self, uint8_t allocFlag)
{
    // vtable
    *(const void**)self = ConditionCurrentAnimation_vftable;

    // Если nameList не NULL — освободить
    CCA_NameRange_ctor** ppRange = (CCA_NameRange_ctor**)((char*)self + 0x14);
    if (*ppRange != nullptr) {
        if (**ppRange != nullptr)
            FUN_102f7780(**ppRange);
        thunk_FUN_102f9190(*ppRange);
    }

    // Вызов базового конструктора
    Condition_Condition(self);

    // Флаг аллокации — освобождение памяти
    if ((allocFlag & 1) != 0)
        thunk_FUN_102f9190(self);
}
