#include "../engine/reverse/slot_utils.hpp"
#include <cstdio>

static int failed = 0;

static void verify(const char* label, uint32_t result, int expected_match) {
    int match = result & 0xFF;
    if (match != expected_match) {
        std::fprintf(stderr, "FAIL %s: expected_match=%d got=%d (result=0x%08X)\n",
                     label, expected_match, match, result);
        failed = 1;
    } else {
        std::printf("PASS %s: match=%d\n", label, match);
    }
}

int main() {
    const char* name1 = "Hello";
    const char* name2 = "World";
    const char* name3 = "Test!";

    AnimSlot slots[3];
    slots[0] = {name1, name1 + 5, 0};
    slots[1] = {name2, name2 + 5, 0};
    slots[2] = {name3, name3 + 5, 0};
    AnimSlotRange range{slots, slots + 3};

    AnimSlot search1{name1, name1 + 5, 0};
    AnimSlot search2{name2, name2 + 5, 0};
    AnimSlot search_miss{"Miss", "Miss" + 4, 0};

    verify("found first", findMatchingSlotInList(&search1, range), 1);
    verify("found middle", findMatchingSlotInList(&search2, range), 1);
    verify("not found", findMatchingSlotInList(&search_miss, range), 0);

    // Empty range
    AnimSlotRange empty{slots, slots};
    verify("empty range", findMatchingSlotInList(&search1, empty), 0);

    // Partial match by length mismatch
    AnimSlot longer{"LongerName", "LongerName" + 10, 0};
    verify("length mismatch", findMatchingSlotInList(&longer, range), 0);

    std::printf("\n%s\n", failed ? "*** FAILED ***" : "*** ALL PASSED ***");
    return failed;
}
