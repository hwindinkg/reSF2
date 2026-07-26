// === STAGED — ждёт интеграции ===
// Адрес: 0x10086b90 (0x10086b90–0x10086c3f)
// Ghidra: ConditionInterval_virtual_8
// Вердикт: VERIFIED GREEN (completeness 63%, eff. 84.7%)
// 
// ConditionInterval::virtual_8 (vtable slot 8)
// Pipeline condition checker: итерирует список элементов контекста,
// проверяет совпадение по typeId и содержимому буфера.
//
// Логика:
//   1. Читает iterPair из context+8 → {begin, end} указателей
//   2. Для каждого элемента:
//      a. Если matchId==0 ИЛИ matchId==element->typeId (offset +0x18):
//         - Если self->buffer пуст ИЛИ element->buffer == self->buffer:
//           → result=true, break
//   3. Если negate флаг (self+0x0C) установлен, инвертировать результат
//
// Field layout (ConditionInterval):
//   +0x00: vtable ptr
//   +0x0C: negateFlag (char/bool)
//   +0x14: matchId (int32, 0=match all)
//   +0x1C: buffer (BufferDescriptor: {void* begin, void* end})
//
// Element layout:
//   +0x0C: buffer (BufferDescriptor)
//   +0x18: typeId (int32)
//
// Context layout:
//   +0x08 → iterPair → {void**, void**} → array of element pointers
//
// Callee dependencies (generic utility — не ConditionInterval-specific):
//   FUN_1000cc00 (0x1000cc00): uint32_t __cdecl strcmp_buf(buf_desc, null_term_str)
//   FUN_1000cb90 (0x1000cb90): uint32_t __cdecl memcmp_buf(buf_desc_a, buf_desc_b)
//   g_szEmptyString (0x10374b40): const char[] = {0,0,0,0,0,0,0,0} (пустая строка)
//
// Для ACCEPTED нужно:
//   1. Вшить вызыватель — в реконструированном исходнике его ещё нет
//   2. Ground-truth тест с эталоном из оригинала
//   3. Определить ConditionInterval class в types.hpp
// ============================================================

#pragma once

#include <cstdint>

struct ConditionInterval_BufferDesc {
    void* begin;
    void* end;
};

extern "C" uint32_t __cdecl strcmp_buf(void* bufDesc, const char* str);
extern "C" uint32_t __cdecl memcmp_buf(void* a, void* b);
extern const char g_emptyString[];

// ConditionInterval::virtual_8 — проверка условия на списке элементов
static inline bool ConditionInterval_virtual_8(void* self, void* context)
{
    uint32_t** iterPair = *static_cast<uint32_t***>(
        static_cast<char*>(context) + 8);
    bool result = false;

    if (iterPair) {
        int** begin = reinterpret_cast<int**>(iterPair[0]);
        int** end   = reinterpret_cast<int**>(iterPair[1]);

        int count = static_cast<int>(
            (reinterpret_cast<char*>(end) - reinterpret_cast<char*>(begin)) >> 2);
        if (count != 0) {
            int matchId = *reinterpret_cast<int*>(
                static_cast<char*>(self) + 0x14);
            int** cur = begin;
            int* elem = *cur;

            do {
                if (matchId == 0 || matchId == *reinterpret_cast<int*>(
                        reinterpret_cast<char*>(elem) + 0x18))
                {
                    uint32_t cr;
                    cr = strcmp_buf(static_cast<char*>(self) + 0x1C, g_emptyString);
                    if (static_cast<char>(cr) != '\0' ||
                        (cr = memcmp_buf(
                            reinterpret_cast<char*>(elem) + 0x0C,
                            static_cast<char*>(self) + 0x1C),
                         static_cast<char>(cr) != '\0'))
                    {
                        result = true;
                        break;
                    }
                }
                ++cur;
                if (cur < end) elem = reinterpret_cast<int*>(*cur);
            } while (cur < end);
        }
    }
    char neg = *reinterpret_cast<char*>(static_cast<char*>(self) + 0x0C);
    return neg ? !result : result;
}