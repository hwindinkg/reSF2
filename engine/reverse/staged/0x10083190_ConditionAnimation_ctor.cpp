// === STAGED — awaits integration ===
// Address: 0x10083190
// Ghidra: FUN_10083190 -> ConditionAnimation::ConditionAnimation
// Verdict: VERIFIED GREEN (round 1)
//
// Base class constructor for ConditionAnimation hierarchy.
// Writes vtable pointer (0x10382380) to [this].
//
// Called from:
//   - ConditionCurrentAnimation ctor (0x10083ac0) as base init
//   - ~36 other functions including unwind handlers
//
// For ACCEPTED needs:
//   1. Define full Condition class hierarchy in engine/
//   2. Wire into ConditionCurrentAnimation constructor
//   3. Ground-truth test
// ============================================================

#pragma once
#include <cstdint>

// vtable at 0x10382380 — 28 bytes, 7 function pointers
extern void* ConditionAnimation_vftable;

struct ConditionAnimation {
    void* vtable;  // +0x00
};

// __fastcall: ECX = this
inline void __fastcall ConditionAnimation_Ctor(ConditionAnimation* this_ptr)
{
    this_ptr->vtable = &ConditionAnimation_vftable;
}
