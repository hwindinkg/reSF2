#include "../engine/reverse/conditions.hpp"
#include "../engine/reverse/name_utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Use VirtualAlloc to get memory in the lower 4GB so 32-bit pointers work
#ifndef _WIN32
#error "This test requires VirtualAlloc (Win32)"
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int failed = 0;

static void test_eq(const char* label, uint32_t result, uint32_t expected_lowbyte) {
    uint32_t lowbyte = result & 0xFF;
    if (lowbyte != expected_lowbyte) {
        std::fprintf(stderr, "FAIL %s: expected_lowbyte=%u got=%u (result=0x%08X)\n",
                     label, expected_lowbyte, lowbyte, result);
        failed = 1;
    } else {
        std::printf("PASS %s: lowbyte=%u\n", label, lowbyte);
    }
}

static void test_bool(const char* label, bool result, bool expected) {
    if (result != expected) {
        std::fprintf(stderr, "FAIL %s: expected=%s got=%s\n",
                     label, expected ? "true" : "false", result ? "true" : "false");
        failed = 1;
    } else {
        std::printf("PASS %s: %s\n", label, result ? "true" : "false");
    }
}

// ── Low-memory allocator for 32-bit pointer compatibility ───────────────
// Returns memory below 4GB so uint32_t pointer fields work on 64-bit hosts.

static void* allocLow(size_t size) {
    // Try specific low address first
    static const uintptr_t LOW_BASE = 0x30000000;
    void* p = VirtualAlloc((LPVOID)LOW_BASE, size,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p) return p;
    // Fall back to any low address
    p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p && reinterpret_cast<uintptr_t>(p) > 0xFFFFFFFFULL) {
        // Free and try one more time with different strategy
        VirtualFree(p, 0, MEM_RELEASE);
        // Use MEM_TOP_DOWN to get high addresses, then... no that's worse.
        // Just use it anyway — if above 4GB, 32-bit truncation breaks.
        // Try specifying a range
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        // Try allocating at the start of the user address space
        uintptr_t base = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        for (int attempt = 0; attempt < 10; attempt++) {
            p = VirtualAlloc((LPVOID)(base + attempt * 0x10000000ULL),
                             size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (p) return p;
        }
        // Last resort: just return what we get and hope
        p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    return p;
}

static void freeLow(void* p) {
    if (p) VirtualFree(p, 0, MEM_RELEASE);
}

struct LowBuf {
    char* data;
    size_t cap;
    size_t used;

    static LowBuf create(size_t cap) {
        LowBuf b;
        b.data = static_cast<char*>(allocLow(cap));
        b.cap = cap;
        b.used = 0;
        if (!b.data) {
            std::fprintf(stderr, "FATAL: allocLow(%zu) failed\n", cap);
            abort();
        }
        return b;
    }

    void destroy() { freeLow(data); data = nullptr; }

    // Copy a string into the buffer and return its 32-bit pointer value
    uint32_t putString(const char* s) {
        size_t len = std::strlen(s) + 1; // include null terminator
        if (used + len > cap) {
            std::fprintf(stderr, "FATAL: LowBuf overflow\n");
            abort();
        }
        uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data + used));
        std::memcpy(data + used, s, len);
        used += len;
        return addr;
    }

    // Return space for an AnimSlot and get its 32-bit address
    uint32_t putSlot(AnimSlot& out, const char* name) {
        uint32_t nameAddr = putString(name);
        out.setName(reinterpret_cast<const char*>(static_cast<uintptr_t>(nameAddr)));
        out.setNameEnd(reinterpret_cast<const char*>(
            static_cast<uintptr_t>(nameAddr + static_cast<uint32_t>(std::strlen(name)))));
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&out));
    }

    uint32_t allocBytes(size_t sz) {
        if (used + sz > cap) {
            std::fprintf(stderr, "FATAL: LowBuf overflow\n");
            abort();
        }
        uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data + used));
        used += sz;
        return addr;
    }

    char* ptr(uint32_t addr) const {
        return reinterpret_cast<char*>(static_cast<uintptr_t>(addr));
    }
};

// ══════════════════════════════════════════════════════════════════════════
// Test: findMatchingSlotInList
// ══════════════════════════════════════════════════════════════════════════

static void test_findMatchingSlotInList() {
    auto buf = LowBuf::create(4096);

    uint32_t aAddr = buf.putString("Attack");
    uint32_t bAddr = buf.putString("Block");
    uint32_t jAddr = buf.putString("Jump");

    // Allocate 3 AnimSlots in low memory
    uint32_t slotBase = buf.allocBytes(3 * sizeof(AnimSlot));
    AnimSlot* slots = reinterpret_cast<AnimSlot*>(buf.ptr(slotBase));
    slots[0].setName(buf.ptr(aAddr));
    slots[0].setNameEnd(buf.ptr(aAddr + 6));
    slots[1].setName(buf.ptr(bAddr));
    slots[1].setNameEnd(buf.ptr(bAddr + 5));
    slots[2].setName(buf.ptr(jAddr));
    slots[2].setNameEnd(buf.ptr(jAddr + 4));

    // Build range using 32-bit compatible pointers
    AnimSlotRange list;
    list.setSlots(slots);
    list.setSlotsEnd(slots + 3);

    // Search for each slot
    AnimSlot searchA;
    uint32_t saAddr = buf.putString("Attack");
    searchA.setName(buf.ptr(saAddr));
    searchA.setNameEnd(buf.ptr(saAddr + 6));
    test_eq("findSlot match first", findMatchingSlotInList(&searchA, list), 1);

    AnimSlot searchB;
    uint32_t sbAddr = buf.putString("Block");
    searchB.setName(buf.ptr(sbAddr));
    searchB.setNameEnd(buf.ptr(sbAddr + 5));
    test_eq("findSlot match middle", findMatchingSlotInList(&searchB, list), 1);

    AnimSlot searchJ;
    uint32_t sjAddr = buf.putString("Jump");
    searchJ.setName(buf.ptr(sjAddr));
    searchJ.setNameEnd(buf.ptr(sjAddr + 4));
    test_eq("findSlot match last", findMatchingSlotInList(&searchJ, list), 1);

    // No match
    uint32_t siAddr = buf.putString("Idle");
    AnimSlot searchI;
    searchI.setName(buf.ptr(siAddr));
    searchI.setNameEnd(buf.ptr(siAddr + 4));
    test_eq("findSlot no match", findMatchingSlotInList(&searchI, list), 0);

    // Empty list
    AnimSlotRange emptyList;
    emptyList.setSlots(nullptr);
    emptyList.setSlotsEnd(nullptr);
    test_eq("findSlot empty list", findMatchingSlotInList(&searchA, emptyList), 0);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Test: findNameInModelSlots
// ══════════════════════════════════════════════════════════════════════════

static void test_findNameInModelSlots() {
    auto buf = LowBuf::create(4096);

    uint32_t pAddr = buf.putString("Punch");
    uint32_t kAddr = buf.putString("Kick");
    uint32_t bAddr = buf.putString("Block");

    uint32_t slotBase = buf.allocBytes(3 * sizeof(AnimSlot));
    AnimSlot* slots = reinterpret_cast<AnimSlot*>(buf.ptr(slotBase));
    slots[0].setName(buf.ptr(pAddr));
    slots[0].setNameEnd(buf.ptr(pAddr + 5));
    slots[1].setName(buf.ptr(kAddr));
    slots[1].setNameEnd(buf.ptr(kAddr + 4));
    slots[2].setName(buf.ptr(bAddr));
    slots[2].setNameEnd(buf.ptr(bAddr + 5));

    AnimSlotRange range;
    range.setSlots(slots);
    range.setSlotsEnd(slots + 3);

    // Match
    NameRange punchRn{buf.ptr(pAddr), buf.ptr(pAddr + 5)};
    test_eq("findName match", findNameInModelSlots(range, punchRn), 1);

    // No match
    uint32_t iAddr = buf.putString("Idle");
    NameRange idleRn{buf.ptr(iAddr), buf.ptr(iAddr + 4)};
    test_eq("findName no match", findNameInModelSlots(range, idleRn), 0);

    // Empty
    AnimSlotRange emptyR;
    emptyR.setSlots(nullptr);
    emptyR.setSlotsEnd(nullptr);
    NameRange kickRn{buf.ptr(kAddr), buf.ptr(kAddr + 4)};
    test_eq("findName empty", findNameInModelSlots(emptyR, kickRn), 0);

    // Last slot
    NameRange blockRn{buf.ptr(bAddr), buf.ptr(bAddr + 5)};
    test_eq("findName last slot", findNameInModelSlots(range, blockRn), 1);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Mock model for CCA tests — placed in low memory for 32-bit compatibility
// ══════════════════════════════════════════════════════════════════════════

enum : size_t {
    OFF_TYPE1      = 0x20,
    OFF_TYPE2      = 0x2C,
    OFF_TYPE3      = 0x38,
    OFF_TYPE4      = 0x44,
    OFF_PHYSICS1   = 0xA0,
    OFF_PHYSICS2   = 0xA1,
    OFF_PHYSICS3   = 0xA2,
    MODEL_SIZE     = 0xA3,
};

static void setModelSlotRange(char* model, size_t off, AnimSlot* slots, int n) {
    AnimSlotRange& r = *reinterpret_cast<AnimSlotRange*>(model + off);
    r.setSlots(slots);
    r.setSlotsEnd(slots + n);
}

// ══════════════════════════════════════════════════════════════════════════
// Test: CCA_getAnimationNames
// ══════════════════════════════════════════════════════════════════════════

static void test_CCA_getAnimationNames() {
    auto buf = LowBuf::create(16384);

    // Model in low memory
    uint32_t modelAddr = buf.allocBytes(MODEL_SIZE + 64);
    char* model = buf.ptr(modelAddr);

    // Slot data in low memory
    uint32_t pAddr = buf.putString("Punch");
    uint32_t kAddr = buf.putString("Kick");
    uint32_t slotBase = buf.allocBytes(2 * sizeof(AnimSlot));
    AnimSlot* animSlots = reinterpret_cast<AnimSlot*>(buf.ptr(slotBase));
    animSlots[0].setName(buf.ptr(pAddr));
    animSlots[0].setNameEnd(buf.ptr(pAddr + 5));
    animSlots[1].setName(buf.ptr(kAddr));
    animSlots[1].setNameEnd(buf.ptr(kAddr + 4));

    setModelSlotRange(model, OFF_TYPE1, animSlots, 2);
    setModelSlotRange(model, OFF_TYPE2, animSlots, 1);
    setModelSlotRange(model, OFF_TYPE3, animSlots + 1, 1);
    // type4: leave as zero

    ConditionCurrentAnimation cca = {};
    cca.type = 1;
    auto* r1 = CCA_getAnimationNames(&cca, model);
    test_bool("getAnimNames type1",
        r1 == reinterpret_cast<AnimSlotRange*>(model + OFF_TYPE1), true);

    cca.type = 2;
    auto* r2 = CCA_getAnimationNames(&cca, model);
    test_bool("getAnimNames type2",
        r2 == reinterpret_cast<AnimSlotRange*>(model + OFF_TYPE2), true);

    cca.type = 3;
    auto* r3 = CCA_getAnimationNames(&cca, model);
    test_bool("getAnimNames type3",
        r3 == reinterpret_cast<AnimSlotRange*>(model + OFF_TYPE3), true);

    cca.type = 4;
    auto* r4 = CCA_getAnimationNames(&cca, model);
    test_bool("getAnimNames type4",
        r4 == reinterpret_cast<AnimSlotRange*>(model + OFF_TYPE4), true);

    cca.type = 99;
    auto* rDefault = CCA_getAnimationNames(&cca, model);
    test_bool("getAnimNames default type1",
        rDefault == reinterpret_cast<AnimSlotRange*>(model + OFF_TYPE1), true);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Test: CCA_isEqual — named animation slots
// ══════════════════════════════════════════════════════════════════════════

static void test_CCA_isEqual_named() {
    auto buf = LowBuf::create(16384);

    uint32_t modelAddr = buf.allocBytes(MODEL_SIZE + 64);
    char* model = buf.ptr(modelAddr);

    uint32_t stAddr = buf.putString("Stance");
    uint32_t slotBase = buf.allocBytes(1 * sizeof(AnimSlot));
    AnimSlot* slot = reinterpret_cast<AnimSlot*>(buf.ptr(slotBase));
    slot->setName(buf.ptr(stAddr));
    slot->setNameEnd(buf.ptr(stAddr + 6));

    setModelSlotRange(model, OFF_TYPE1, slot, 1);

    NameRange nameRn{buf.ptr(stAddr), buf.ptr(stAddr + 6)};

    ConditionCurrentAnimation cca = {};
    cca.type            = 1;
    cca.nameList        = &nameRn;
    cca.noAnimationFlag = false;

    test_eq("CCA_isEqual named match", CCA_isEqual(&cca, model), 1);

    uint32_t blAddr = buf.putString("Block");
    NameRange wrongRn{buf.ptr(blAddr), buf.ptr(blAddr + 5)};
    cca.nameList = &wrongRn;
    test_eq("CCA_isEqual named no match", CCA_isEqual(&cca, model), 0);

    cca.invert = true;
    cca.nameList = &nameRn;
    test_eq("CCA_isEqual named invert (match→0)", CCA_isEqual(&cca, model), 0);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Test: CCA_isEqual — physics comparison (nameList==nullptr)
// ══════════════════════════════════════════════════════════════════════════

static void test_CCA_isEqual_physics() {
    auto buf = LowBuf::create(1024);
    uint32_t modelAddr = buf.allocBytes(MODEL_SIZE);
    char* model = buf.ptr(modelAddr);

    model[OFF_PHYSICS1] = 5;
    model[OFF_PHYSICS2] = 10;
    model[OFF_PHYSICS3] = 15;

    ConditionCurrentAnimation cca = {};
    cca.nameList        = nullptr;
    cca.noAnimationFlag = false;

    cca.type = 1;
    cca.physicsValue = 5;
    test_eq("CCA_isEqual physics1 match", CCA_isEqual(&cca, model), 1);

    cca.physicsValue = 3;
    test_eq("CCA_isEqual physics1 mismatch", CCA_isEqual(&cca, model), 0);

    cca.type = 2;
    cca.physicsValue = 10;
    test_eq("CCA_isEqual physics2 match", CCA_isEqual(&cca, model), 1);

    cca.type = 3;
    cca.physicsValue = 15;
    test_eq("CCA_isEqual physics3 match", CCA_isEqual(&cca, model), 1);

    cca.invert = true;
    cca.type = 2;
    cca.physicsValue = 10;
    test_eq("CCA_isEqual physics2 invert (match→0)", CCA_isEqual(&cca, model), 0);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Test: ConditionInterval_virtual_8
// ══════════════════════════════════════════════════════════════════════════

static void test_ConditionInterval_virtual_8() {
    auto buf = LowBuf::create(4096);

    uint32_t hAddr = buf.putString("Hello");
    uint32_t wAddr = buf.putString("World");

    // Build interval elements in low memory
    uint32_t e1Addr = buf.allocBytes(0x1C);
    char* e1 = buf.ptr(e1Addr);
    *reinterpret_cast<uint32_t*>(e1 + 0x0C) = hAddr;
    *reinterpret_cast<uint32_t*>(e1 + 0x10) = hAddr + 5;
    *reinterpret_cast<int32_t*>(e1 + 0x18) = 100;

    uint32_t e2Addr = buf.allocBytes(0x1C);
    char* e2 = buf.ptr(e2Addr);
    *reinterpret_cast<uint32_t*>(e2 + 0x0C) = wAddr;
    *reinterpret_cast<uint32_t*>(e2 + 0x10) = wAddr + 5;
    *reinterpret_cast<int32_t*>(e2 + 0x18) = 200;

    // Element pointers array
    void* elemPtrs[2] = { e1, e2 };

    // iterPair at context+8: pointer to {begin_ptr, end_ptr}
    // Both elemPtrs and iterPairData must be accessible via the context's
    // void** dereference chain. The context stores a void* that points to
    // iterPairData; iterPairData[0] is the element array start, etc.
    void* iterPairData[2];
    iterPairData[0] = reinterpret_cast<void*>(elemPtrs);
    iterPairData[1] = reinterpret_cast<void*>(elemPtrs + 2);

    // Context in low memory
    uint32_t ctxAddr = buf.allocBytes(0x10);
    char* ctx = buf.ptr(ctxAddr);
    // context+8 holds a void* that points to the iterPair array
    // The function reads it as: void** iterBase = *(void***)(ctx+8)
    // So we store the address of iterPairData (which is void**)
    void** iterPtr = iterPairData;
    *reinterpret_cast<void**>(ctx + 8) = reinterpret_cast<void*>(iterPtr);

    // CI 1: empty buffer → match all
    ConditionInterval interval = {};
    interval.negateFlag = false;
    interval.matchId = 0;
    interval.buffer.begin = nullptr;
    interval.buffer.end   = nullptr;

    test_bool("CI_v8 empty buffer matchAll",
        ConditionInterval_virtual_8(&interval, ctx), true);

    // CI 2: match by typeId=100
    ConditionInterval interval2 = {};
    interval2.negateFlag = false;
    interval2.matchId = 100;
    interval2.buffer.begin = buf.ptr(hAddr);
    interval2.buffer.end   = buf.ptr(hAddr + 5);

    test_bool("CI_v8 matchId+buffer match",
        ConditionInterval_virtual_8(&interval2, ctx), true);

    // CI 3: match typeId=200
    ConditionInterval interval3 = {};
    interval3.negateFlag = false;
    interval3.matchId = 200;
    interval3.buffer.begin = buf.ptr(wAddr);
    interval3.buffer.end   = buf.ptr(wAddr + 5);

    test_bool("CI_v8 matchId+buffer match2",
        ConditionInterval_virtual_8(&interval3, ctx), true);

    // CI 4: no match
    ConditionInterval interval4 = {};
    interval4.negateFlag = false;
    interval4.matchId = 999;
    interval4.buffer.begin = buf.ptr(hAddr);
    interval4.buffer.end   = buf.ptr(hAddr + 5);

    test_bool("CI_v8 no matchId match",
        ConditionInterval_virtual_8(&interval4, ctx), false);

    // CI 5: negate
    ConditionInterval interval5 = {};
    interval5.negateFlag = true;
    interval5.matchId = 999;
    interval5.buffer.begin = buf.ptr(hAddr);
    interval5.buffer.end   = buf.ptr(hAddr + 5);

    test_bool("CI_v8 negate (no match → true)",
        ConditionInterval_virtual_8(&interval5, ctx), true);

    // CI 6: null iterPair
    char emptyCtx[0x10] = {};
    *reinterpret_cast<void**>(emptyCtx + 8) = nullptr;

    ConditionInterval interval6 = {};
    interval6.negateFlag = false;
    interval6.matchId = 0;

    test_bool("CI_v8 null context",
        ConditionInterval_virtual_8(&interval6, emptyCtx), false);

    buf.destroy();
}

// ══════════════════════════════════════════════════════════════════════════
// Main
// ══════════════════════════════════════════════════════════════════════════

int main() {
    std::printf("=== findMatchingSlotInList ===\n");
    test_findMatchingSlotInList();

    std::printf("\n=== findNameInModelSlots ===\n");
    test_findNameInModelSlots();

    std::printf("\n=== CCA_getAnimationNames ===\n");
    test_CCA_getAnimationNames();

    std::printf("\n=== CCA_isEqual (named) ===\n");
    test_CCA_isEqual_named();

    std::printf("\n=== CCA_isEqual (physics) ===\n");
    test_CCA_isEqual_physics();

    std::printf("\n=== ConditionInterval_virtual_8 ===\n");
    test_ConditionInterval_virtual_8();

    std::printf("\n%s\n", failed ? "*** FAILED ***" : "*** ALL PASSED ***");
    return failed;
}
