#pragma once
#include <cstdint>
#include "name_utils.hpp"  // for NameRange

// ██ AnimSlot / AnimSlotRange ████████████████████████████████████████████
// [ORIGINAL] Used by CCA::isEqual (0x10083bb0) and helpers
//
// IMPORTANT: The original binary is 32-bit x86 where pointers are 4 bytes.
// To maintain binary-accurate struct layout we use uint32_t for pointer
// fields.  These hold the original 32-bit pointer value.  Use helpers
// slotPtr() / AnimSlot::get/setName() to convert to real pointers.
//
// Each slot is 12 bytes: [name:4, nameEnd:4, field_8:4]

#pragma pack(push, 1)
struct AnimSlot {
    uint32_t name;       // +0x00: 32-bit pointer to name string
    uint32_t nameEnd;    // +0x04: 32-bit pointer to name end
    int32_t field_8;     // +0x08

    // Convert 32-bit pointer to real pointer
    const char* getName() const noexcept {
        return reinterpret_cast<const char*>(
            static_cast<uintptr_t>(name));
    }
    const char* getNameEnd() const noexcept {
        return reinterpret_cast<const char*>(
            static_cast<uintptr_t>(nameEnd));
    }
    void setName(const char* p) noexcept {
        name = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(p));
    }
    void setNameEnd(const char* p) noexcept {
        nameEnd = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(p));
    }
    int nameLength() const noexcept {
        return static_cast<int>(nameEnd - name);
    }
};

struct AnimSlotRange {
    uint32_t slots;      // +0x00: 32-bit pointer to first AnimSlot
    uint32_t slotsEnd;   // +0x04: 32-bit pointer past last AnimSlot

    AnimSlot* getSlots() const noexcept {
        return reinterpret_cast<AnimSlot*>(
            static_cast<uintptr_t>(slots));
    }
    AnimSlot* getSlotsEnd() const noexcept {
        return reinterpret_cast<AnimSlot*>(
            static_cast<uintptr_t>(slotsEnd));
    }
    void setSlots(const AnimSlot* p) noexcept {
        slots = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(p));
    }
    void setSlotsEnd(const AnimSlot* p) noexcept {
        slotsEnd = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(p));
    }
    int count() const noexcept {
        return static_cast<int>(
            (slotsEnd - slots) / sizeof(AnimSlot));
    }
};
#pragma pack(pop)

static_assert(sizeof(AnimSlot) == 12,
    "AnimSlot must be exactly 12 bytes (32-bit pointer layout)");
static_assert(sizeof(AnimSlotRange) == 8,
    "AnimSlotRange must be exactly 8 bytes (32-bit pointer layout)");

// ██ ConditionAnimation — base class █████████████████████████████████████
// [ORIGINAL] FUN_10083190 — VERIFIED GREEN
// vtable at 0x10382380 (28 bytes, 7 pointers)

struct ConditionAnimation {
    void* vtable;  // +0x00
};

// ██ ConditionCurrentAnimation — extends ConditionAnimation █████████████
// [ORIGINAL] Multiple FUNs — VERIFIED GREEN
// Layout (from Ghidra analysis 0x10083bb0, 0x10083b50, 0x10083ac0):
//   +0x00: vtable (inherited)
//   +0x04: baseField (int32_t)
//   +0x08: baseParam (int32_t, always 16)
//   +0x0C: invert (bool)
//   +0x10: type (int32_t, 1-4)
//   +0x14: nameList (NameRange*, nullable)
//   +0x18: noAnimationFlag (bool)
//   +0x19: physicsValue (uint8_t)

struct ConditionCurrentAnimation {
    void* vtable;            // +0x00
    int32_t baseField;       // +0x04
    int32_t baseParam;       // +0x08 (always 0x10 = 16)
    bool invert;             // +0x0C
    int32_t type;            // +0x10 (1/2/3/4)
    NameRange* nameList;     // +0x14 (nullptr if no named condition)
    bool noAnimationFlag;    // +0x18
    uint8_t physicsValue;    // +0x19
};

// ██ ConditionInterval ███████████████████████████████████████████████████
// [ORIGINAL] FUN_10086b90 — VERIFIED GREEN
// Layout (from Ghidra analysis):
//   +0x00: vtable
//   +0x0C: negateFlag (bool)
//   +0x14: matchId (int32, 0=match all)
//   +0x1C: buffer (pointer pair {void* begin, void* end})

struct ConditionInterval_Buffer {
    void* begin;
    void* end;
};

struct ConditionInterval {
    void* vtable;                // +0x00
    // [+0x04..+0x0B] — unknown / base class fields
    bool negateFlag;             // +0x0C
    // [+0x0D..+0x13] — unknown / padding
    int32_t matchId;             // +0x14
    // [+0x18..+0x1B] — unknown / padding
    ConditionInterval_Buffer buffer;  // +0x1C
};

// ██ Function declarations ███████████████████████████████████████████████

// CCA::getAnimationNames (FUN_10083b50)
// Returns AnimSlotRange for given type from model:
//   type=1 → model+0x20, type=2 → model+0x2C,
//   type=3 → model+0x38, type=4 → model+0x44
// NOTE: Returns ptr to AnimSlotRange inside model memory (32-bit layout).
//       Caller must use getSlots()/getSlotsEnd() to access.
AnimSlotRange* CCA_getAnimationNames(const ConditionCurrentAnimation* self,
                                      void* model) noexcept;

// CCA::isEqual / virtual_8 (FUN_10083bb0)
// Checks if model's current animation matches the condition.
// Returns low byte = 1 if condition matches, 0 otherwise.
uint32_t CCA_isEqual(const ConditionCurrentAnimation* self,
                      void* model) noexcept;

// ConditionInterval::virtual_8 (FUN_10086b90)
// Iterates context element list and checks condition match.
bool ConditionInterval_virtual_8(const ConditionInterval* self,
                                  void* context) noexcept;

// findMatchingSlotInList (FUN_10083ca0)
// Searches for a slot with matching name in the model's slot list.
// Returns (result & 0xFFFFFF00) | 1 on match, (result & 0xFFFFFF00) on miss.
uint32_t findMatchingSlotInList(const AnimSlot* slotStart,
                                 const AnimSlotRange& listRange) noexcept;

// findNameInModelSlots (FUN_10083d60)
// Searches condition name in model's animation slot list.
// Returns 1 on match, 0 on miss.
uint32_t findNameInModelSlots(const AnimSlotRange& modelSlots,
                               const NameRange& conditionName) noexcept;

// VTable externs (to be defined in conditions.cpp with binary addresses)
extern void* ConditionAnimation_vftable;
extern void* CCA_vftable;
